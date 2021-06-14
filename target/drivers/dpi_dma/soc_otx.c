/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* DPI DMA VF driver ops for octeontx
 */

#include <linux/module.h>

#include "dpi_vf.h"
#include "soc_api.h"

#include "dpi.h"

#include "fpa.h"
#include "octeontx_mbox.h"
#include "octeontx.h"

#define OTX_DPI_NUM_VFS 	1
#define OTX_FPA_NUM_VFS 	1
#define OTX_FPA_PF_ID 		0
#define OTX_DPI_PF_ID 		0

#define OTX_SOC_PRIV(dpi_vf)	((struct otx_data *)dpi_vf->soc_priv)

static struct fpapf_com_s *fpapf;
static struct fpavf_com_s *fpavf;
static struct dpipf_com_s *dpipf;

extern struct dpipf_com_s dpipf_com;

struct otx_data {
	struct otxx_data otxx;
	struct fpavf *fpa;
};

int otx_dpivf_master_send_message(struct mbox_hdr *hdr,
				  union mbox_data *req,
				  union mbox_data *resp,
				  void *master_data,
				  void *add_data)
{
	struct dpivf_t *dpi_vf = master_data;
	int ret;

	if (hdr->coproc == FPA_COPROC) {
		ret = fpapf->receive_message(
			OTX_FPA_PF_ID, dpi_vf->domain,
			hdr, req, resp, add_data);
	} else {
		dev_err(dpi_vf->dev, "SSO message dispatch, wrong VF type\n");
		ret = -1;
	}

	return ret;
}

static struct octeontx_master_com_t dpi_master_com = {
	.send_message = otx_dpivf_master_send_message,
};

static inline int otx_vf_init(struct dpivf_t *dpi_vf)
{
	int err;
	u64 mask;
	struct otx_data *priv;

	priv = devm_kzalloc(dpi_vf->dev, sizeof(struct otx_data), GFP_KERNEL);
	if (!priv) {
		dev_err(dpi_vf->dev, "Unable to allocate DPI VF data\n");
		return -ENOMEM;
	}
	dpi_vf->soc_priv = (void*)priv;

	mask = dpipf->create_domain(OTX_DPI_PF_ID, dpi_vf->domain,
				    OTX_DPI_NUM_VFS, NULL, NULL, dpi_vf->kobj);
	if (!mask) {
		dev_err(dpi_vf->dev, "Failed to create DPI domain\n");
		err = -ENOMEM;
		goto dpi_domain_fail;
	}

	mask = fpapf->create_domain(OTX_FPA_PF_ID, dpi_vf->domain,
				    OTX_FPA_NUM_VFS, dpi_vf->kobj);
	if (!mask) {
		dev_err(dpi_vf->dev, "Failed to create FPA domain\n");
		err = -ENOMEM;
		goto fpa_domain_fail;
	}

	priv->fpa = fpavf->get(dpi_vf->domain, 0, &dpi_master_com, dpi_vf);
	if (priv->fpa == NULL) {
		dev_err(dpi_vf->dev, "Failed to get fpavf\n");
		err = -ENOMEM;
		goto fpavf_fail;
	}

	err = fpavf->setup(priv->fpa, DPI_NB_CHUNKS,
			   DPI_CHUNK_SIZE, dpi_vf->dev);
	if (err) {
		dev_err(dpi_vf->dev, "FPA setup failed\n");
		err = -ENOMEM;
		goto fpavf_setup_fail;
	}

	return 0;

fpavf_setup_fail:
	fpavf->put(priv->fpa);
fpavf_fail:
	fpapf->destroy_domain(OTX_FPA_PF_ID, dpi_vf->domain, dpi_vf->kobj);
fpa_domain_fail:
	dpipf->destroy_domain(OTX_DPI_PF_ID, dpi_vf->domain, dpi_vf->kobj);
dpi_domain_fail:
	devm_kfree(dpi_vf->dev, priv);

	return err;
}

static inline void otx_vf_uninit(struct dpivf_t *dpi_vf)
{
	struct otx_data *priv = OTX_SOC_PRIV(dpi_vf);
	int err;

	err = fpavf->teardown(priv->fpa);
	if (err)
		dev_err(dpi_vf->dev, "FPA teardown failed\n");

	fpavf->put(priv->fpa);
	fpapf->destroy_domain(OTX_FPA_PF_ID, dpi_vf->domain, dpi_vf->kobj);
	dpipf->destroy_domain(OTX_DPI_PF_ID, dpi_vf->domain, dpi_vf->kobj);
	devm_kfree(dpi_vf->dev, priv);
}

int otx_init(void)
{
	int ret;

	ret = otxx_init();
	if (ret != 0)
		return ret;

	/*TODO: handle -ve cases */
	dpipf = try_then_request_module(symbol_get(dpipf_com), "dpipf");
	if (dpipf == NULL) {
		printk("Load DPI PF module\n");
		return -ENOMEM;
	}

	fpapf = try_then_request_module(symbol_get(fpapf_com), "fpapf");
	if (fpapf == NULL) {
		printk("Load FPA PF module\n");
		symbol_put(dpipf_com);
		return -ENOMEM;
	}

	fpavf = try_then_request_module(symbol_get(fpavf_com), "fpavf");
	if (fpavf == NULL) {
		printk("Load FPA VF module\n");
		symbol_put(fpapf_com);
		symbol_put(dpipf_com);
		return -ENOMEM;
	}

	return 0;
}

int otx_open(struct dpivf_t *dpi_vf)
{
	struct mbox_dpi_cfg cfg;
	union mbox_data resp;
	union mbox_data req;
	struct mbox_hdr hdr;
	u64 val;
	int err;

	err = otx_vf_init(dpi_vf);
	if (err) {
		dev_err(dpi_vf->dev, "Pre-requisites failed");
		return -ENODEV;
	}

	/* override default vf id */
	val = readq_relaxed(dpi_vf->reg_base + DPI_VDMA_SADDR);
	dpi_vf->vf_id = (val >> 24) & 0xffff;

	cfg.buf_size = DPI_CHUNK_SIZE;
	cfg.inst_aura = 0;

	hdr.coproc = DPI_COPROC;
	hdr.msg = DPI_QUEUE_OPEN;
	hdr.vfid = dpi_vf->vf_id;
	/* Opening DPI queue */
	err = dpipf->receive_message(OTX_DPI_PF_ID, dpi_vf->domain, &hdr, &req,
				     &resp, &cfg);
	if (err) {
		dev_err(dpi_vf->dev,
			"%d: Failed to open dpi queue %d:%d:%d",
			err, 0, dpi_vf->domain, hdr.vfid);
		otx_vf_uninit(dpi_vf);
		return err;
	}

	err = otxx_open(dpi_vf);
	if (err != 0) {
		otx_close(dpi_vf);
		return err;
	}

	return 0;
}

u64 otx_buf_alloc(struct dpivf_t *dpi_vf)
{
	struct otx_data *priv = OTX_SOC_PRIV(dpi_vf);

	return fpavf->alloc(priv->fpa, 0);
}

void otx_buf_free(struct dpivf_t *dpi_vf, u64 buf)
{
	struct otx_data *priv = OTX_SOC_PRIV(dpi_vf);
	struct otxx_data *otxx = &priv->otxx;

	fpavf->free(priv->fpa, 0, otxx->dpi_buf, 0);
}

void otx_close(struct dpivf_t *dpi_vf)
{
	union mbox_data req;
	union mbox_data resp;
	struct mbox_hdr hdr;

	hdr.coproc = DPI_COPROC;
	hdr.msg = DPI_QUEUE_CLOSE;
	hdr.vfid = dpi_vf->vf_id;
	resp.data = 0xff;
	/* Closing DPI queue */
	dpipf->receive_message(OTX_DPI_PF_ID, dpi_vf->domain, &hdr,
			       &req, &resp, NULL);

	otxx_close(dpi_vf);
	otx_vf_uninit(dpi_vf);
}

void otx_cleanup(void)
{
	symbol_put(fpavf_com);
	symbol_put(fpapf_com);
	symbol_put(dpipf_com);
}
