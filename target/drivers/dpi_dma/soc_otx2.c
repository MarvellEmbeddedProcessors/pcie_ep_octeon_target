/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* DPI DMA VF driver ops for octeontx2
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "dpi_vf.h"
#include "soc_api.h"

#include "dpi.h"
#include "npa_api.h"

#define OTX2_SOC_PRIV(dpi_vf)	((struct otx2_data *)dpi_vf->soc_priv)

extern struct otx2_dpipf_com_s otx2_dpipf_com;

static struct otx2_dpipf_com_s *otx2_dpipf;

struct otx2_data {
	struct otxx_data otxx;
	u32 aura;
	u8 *host_writel_ptr;
	u64 host_writel_iova;
};

DEFINE_PER_CPU(u32*, cpu_lptr);
DEFINE_PER_CPU(u64, cpu_iova);

static inline int otx2_vf_init(struct dpivf_t *dpi_vf)
{
	struct otx2_data *priv;
	int err;
	u64 cpu;

	priv = devm_kzalloc(dpi_vf->dev, sizeof(struct otx2_data), GFP_KERNEL);
	if (!priv) {
		dev_err(dpi_vf->dev, "Unable to allocate DPI VF data\n");
		return -ENOMEM;
	}
	dpi_vf->soc_priv = (void*)priv;

	err = npa_aura_pool_init(DPI_NB_CHUNKS, DPI_CHUNK_SIZE,
				 &priv->aura, dpi_vf->dev);
	if (err) {
		dev_err(dpi_vf->dev, "Failed to init aura pool pair");
		devm_kfree(dpi_vf->dev, priv);
		return -ENOMEM;
	}

	if (dpi_vf->vf_id == 0) {
		u8 *local_ptr;
		u64 local_iova;

		/* This vf is shared between multiple cores and is used mainly
		 * by mgmt_net. So this vf will create a dma memory to be used
		 * for host_writel API, This api is executed on multiple cores
		 */
		local_ptr = dma_alloc_coherent(dpi_vf->dev,
					       (num_online_cpus() *
					       sizeof(u64)),
					       &local_iova, GFP_ATOMIC);
		if (!local_ptr) {
			dev_err(dpi_vf->dev, "%d: Failed to alloc dma memory.",
				err);
			npa_aura_pool_fini(priv->aura, dpi_vf->dev);
		}
		for_each_online_cpu(cpu) {
			per_cpu(cpu_lptr, cpu) = (u32*)(local_ptr +
							(cpu * sizeof(u64)));
			per_cpu(cpu_iova, cpu) = local_iova +
						 (cpu * sizeof(u64));
		}
		priv->host_writel_ptr = local_ptr;
		priv->host_writel_iova = local_iova;
	}

	return 0;
}

static inline void otx2_vf_uninit(struct dpivf_t *dpi_vf)
{
	struct otx2_data *priv = OTX2_SOC_PRIV(dpi_vf);

	if (priv->host_writel_ptr && dpi_vf == shared_vf) {
		dma_free_coherent(dpi_vf->dev,
				  (num_online_cpus() * sizeof(u64)),
				  priv->host_writel_ptr,
				  priv->host_writel_iova);
		priv->host_writel_ptr = NULL;
		priv->host_writel_iova = 0;
	}
	npa_aura_pool_fini(priv->aura, dpi_vf->dev);
	devm_kfree(dpi_vf->dev, priv);
}

int otx2_init(void)
{
	int ret;

	ret = otxx_init();
	if (ret != 0)
		return ret;

	otx2_dpipf = try_then_request_module(symbol_get(otx2_dpipf_com),
					     "octeontx2_dpi");
	if (otx2_dpipf == NULL) {
		printk("Load OTX2 DPI PF module\n");
		return -ENOMEM;
	}

	return 0;
}

int otx2_open(struct dpivf_t *dpi_vf)
{
	union dpi_mbox_message_t otx2_mbox_msg;
	struct otx2_data *priv;
	struct pci_dev *pcidev = NULL;
	int err;

	while ((pcidev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
					PCI_DEVID_OCTEONTX2_DPI_PF,
					pcidev)) != NULL) {
		if (pcidev->bus->number == dpi_vf->pdev->bus->number) {
			dpi_vf->pf_pdev = pcidev;
			break;
		}
	}

	err = otx2_vf_init(dpi_vf);
	if (err) {
		dev_err(dpi_vf->dev, "Pre-requisites failed");
		return -ENODEV;
	}

	priv = OTX2_SOC_PRIV(dpi_vf);
	otx2_mbox_msg.s.cmd = DPI_QUEUE_OPEN;
	otx2_mbox_msg.s.vfid = dpi_vf->vf_id;
	otx2_mbox_msg.s.csize = DPI_CHUNK_SIZE;
	otx2_mbox_msg.s.aura = priv->aura;
	otx2_mbox_msg.s.sso_pf_func = 0;
	otx2_mbox_msg.s.npa_pf_func = npa_pf_func(priv->aura);

	/* Opening DPI queue */
	err = otx2_dpipf->queue_config(dpi_vf->pf_pdev, &otx2_mbox_msg);
	if (err) {
		dev_err(dpi_vf->dev, "%d: Failed to open dpi queue.", err);
		otx2_vf_uninit(dpi_vf);
		return err;
	}

	err = otxx_open(dpi_vf);
	if (err != 0) {
		otx2_close(dpi_vf);
		return err;
	}

	return 0;
}

u64 otx2_buf_alloc(struct dpivf_t *dpi_vf)
{
	struct otx2_data *priv = OTX2_SOC_PRIV(dpi_vf);

	return npa_alloc_buf(priv->aura);
}

void otx2_buf_free(struct dpivf_t *dpi_vf, u64 buf)
{
	struct otx2_data *priv = OTX2_SOC_PRIV(dpi_vf);

	npa_free_buf(priv->aura, buf);
}

int otx2_dma_to_host(struct dpivf_t *dpi_vf, uint32_t val,
		     host_dma_addr_t host_addr)
{
	host_dma_addr_t liova = get_cpu_var(cpu_iova);
	u32 *lva = get_cpu_var(cpu_lptr);

	WRITE_ONCE(*lva, val);
	otxx_dma_sync(dpi_vf, liova, host_addr, lva, sizeof(u32), DMA_TO_HOST);
	put_cpu_var(cpu_iova);
	put_cpu_var(cpu_lptr);
	return 0;
}

void otx2_close(struct dpivf_t *dpi_vf)
{
	union dpi_mbox_message_t otx2_mbox_msg = {0};
	u64 val;

	otx2_mbox_msg.s.cmd = DPI_QUEUE_CLOSE;
	otx2_mbox_msg.s.vfid = dpi_vf->vf_id;

	/* Closing DPI queue */
	otx2_dpipf->queue_config(dpi_vf->pf_pdev, &otx2_mbox_msg);
	do {
		val = readq_relaxed(dpi_vf->reg_base + DPI_VDMA_SADDR);
	} while (!(val & (0x1ull << 63)));

	otxx_close(dpi_vf);
	otx2_vf_uninit(dpi_vf);
}

void otx2_cleanup(void)
{
	symbol_put(otx2_dpipf_com);
}
