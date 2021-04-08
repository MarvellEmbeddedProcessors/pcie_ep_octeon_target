/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* DPI DMA VF driver
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/fs.h>
#include <linux/dmapool.h>

#include "dpi.h"
#include "dpi_cmd.h"
#include "npa_api.h"
#include "dma_api.h"

#define DRV_NAME "octeontx-dpi-vf"
#define DRV_VERSION "2.0"
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX 0xA081

#define DPI_CHUNK_SIZE 1024
#define DPI_DMA_CMD_SIZE 64
#define DPI_MAX_QUEUES 8
#define DPI_NB_CHUNKS 4096
#define FPA_DPI_XAQ_GMID 0x5
#define DPI_NUM_VFS 1
#define DPI_DMA_CMDX_SIZE 64

#define DPI_PF_ID 0

static unsigned int pem_num = 0;
module_param(pem_num, uint, 0644);
MODULE_PARM_DESC(pem_num, "pem number to use");
/* pem 1 and pem 3 do not support dpi */
#define MAX_PEM 4
static int lport[MAX_PEM] = { 0, -1, 1, -1 };

u8 *local_ptr;
u64 local_iova;

extern struct otx2_dpipf_com_s otx2_dpipf_com;
static struct otx2_dpipf_com_s *otx2_dpipf;
static struct pci_dev *otx2_dpi_pfdev = NULL;

struct dma_queue_ctx {
	u16 qid;
	u16 pool_size_m1;
	u16 index;
	u16 reserved;
	u64 dpi_buf;
	void *dpi_buf_ptr;
	spinlock_t queue_lock;
};

struct dpivf_t {
	struct device *dev;
	void __iomem *reg_base;
	void __iomem *reg_base2;
	struct msix_entry *msix_entries;
	struct kobject *kobj;
	struct iommu_domain *iommu_domain;
	struct dma_queue_ctx qctx[DPI_MAX_QUEUES];
	u16 vf_id;
	u16 domain;
	int id;
	struct dma_pool *comp_buf_pool;
	u32 aura;
	u8 *host_writel_ptr;
	u64 host_writel_iova;
};

static int num_vfs = 0;
static struct dpivf_t* vf[DPI_MAX_QUEUES];

DEFINE_PER_CPU(u32*, cpu_lptr);
DEFINE_PER_CPU(u64, cpu_iova);

static const struct pci_device_id dpi_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX) },
	{ 0, }	/* end of table */
};

static int dpi_dma_queue_write(struct dpivf_t *dpi_vf, u16 qid, u16 cmd_count,
		u64 *cmds)
{
	struct dma_queue_ctx *qctx;
	unsigned long flags;

	if ((cmd_count < 1) || (cmd_count > 64)) {
		dev_err(dpi_vf->dev, "%s invalid cmdcount %d\n", __func__, cmd_count);
		return -1;
	}

	if (cmds == NULL) {
		dev_err(dpi_vf->dev, "%s cmds buffer NULL\n", __func__);
		return -1;
	}

	qctx = &dpi_vf->qctx[qid];
	//dev_info(dev, "%s index %d cmd_count %d pool_size_m1 %d\n",
	//		__func__, qctx->index, cmd_count, qctx->pool_size_m1);


	/* Normally there is plenty of
	 * room in the current buffer for the command
	 */
	spin_lock_irqsave(&qctx->queue_lock, flags);
	if (qctx->index + cmd_count < qctx->pool_size_m1) {
		u64 *ptr = qctx->dpi_buf_ptr;

		ptr += qctx->index;
		qctx->index += cmd_count;
		while (cmd_count--)
			*ptr++ = *cmds++;
	} else {
		u64 *ptr;
		int count;
		/* New command buffer required.
		 * Fail if there isn't one available.
		 */
		u64 dpi_buf, dpi_buf_phys;
		void *new_buffer;

		dpi_buf = npa_alloc_buf(dpi_vf->aura);
		if (!dpi_buf) {
			spin_unlock_irqrestore(&qctx->queue_lock, flags);
			dev_err(dpi_vf->dev, "Failed to allocate");
			return -ENODEV;
		}

		if (dpi_vf->iommu_domain)
			dpi_buf_phys = iommu_iova_to_phys(dpi_vf->iommu_domain,
					dpi_buf);
		else
			dpi_buf_phys = dpi_buf;

		new_buffer = phys_to_virt(dpi_buf_phys);

		ptr = qctx->dpi_buf_ptr;

		/* Figure out how many command words will fit in this buffer.
		 * One location will be needed for the next buffer pointer.
		 */
		count = qctx->pool_size_m1 - qctx->index;
		ptr += qctx->index;
		cmd_count -= count;
		while (count--)
			*ptr++ = *cmds++;
		/* Chunk next ptr is 2DWORDs on 83xx. Second DWORD is reserved.
		 */
		*ptr++ = dpi_buf;
		*ptr   = 0;
		/* The current buffer is full and has a link to the next buffer.
		 * Time to write the rest of the commands into the new buffer.
		 */
		qctx->dpi_buf = dpi_buf;
		qctx->dpi_buf_ptr = new_buffer;
		qctx->index = cmd_count;
		ptr = new_buffer;
		while (cmd_count--)
			*ptr++ = *cmds++;
		/* queue index may greater than pool size */
		if (qctx->index >= qctx->pool_size_m1) {
			dpi_buf = npa_alloc_buf(dpi_vf->aura);
			if (!dpi_buf) {
				spin_unlock_irqrestore(&qctx->queue_lock, flags);
				dev_err(dpi_vf->dev, "Failed to allocate");
				return -ENODEV;
			}
			if (dpi_vf->iommu_domain)
				dpi_buf_phys =
					iommu_iova_to_phys(dpi_vf->iommu_domain,
							dpi_buf);
			else
				dpi_buf_phys = dpi_buf;

			new_buffer = phys_to_virt(dpi_buf_phys);
			*ptr = dpi_buf;
			qctx->dpi_buf = dpi_buf;
			qctx->dpi_buf_ptr = new_buffer;
			qctx->index = 0;
		}
	}
	spin_unlock_irqrestore(&qctx->queue_lock, flags);

	return 0;
}

/*
 * do_dma_sync() - API to transfer data between host and target
 *
 *  @local_dma_addr: DMA address allocated by target
 *  @host_dma_addr: DMA address allocated by host
 *  @local_virt_addr: Virtual address allocated by target
 *  @len: Size of bytes to be transferred
 *  @dir: Direction of the transfer
 */
int do_dma_sync(struct device *dev, local_dma_addr_t local_dma_addr,
		host_dma_addr_t host_dma_addr, void *local_virt_addr,
		int len, host_dma_dir_t dir)
{
	return do_dma_sync_dpi(dev, local_dma_addr, host_dma_addr, NULL, len, dir);
}
EXPORT_SYMBOL(do_dma_sync);

int get_dpi_dma_dev_count(int handle)
{
	/* rpc gets all available devices, everyone else gets 1 device */
	if (handle == HANDLE_TYPE_RPC)
		return num_vfs;

	return (num_vfs != 0);
}
EXPORT_SYMBOL(get_dpi_dma_dev_count);

struct device *get_dpi_dma_dev(int handle, int index)
{
	(void) handle;

	if (index < 0 || index >= num_vfs)
		return NULL;

	return (vf[index]) ? vf[index]->dev : NULL;
}
EXPORT_SYMBOL(get_dpi_dma_dev);

/* caller needs to provide iova addrs, and completion_word,
 * caller needs to poll completion word we use only the common fields in hdr
 * for 8xxx and 9xxx hence dont differentiate
 */
int do_dma_async_dpi_vector(struct device* dev, local_dma_addr_t *local_addr,
							host_dma_addr_t *host_addr, int *len, int num_ptrs,
							host_dma_dir_t dir, local_dma_addr_t comp_iova)
{
	dpi_dma_buf_ptr_t cmd = {0};
	dpi_dma_ptr_t lptr[DPI_MAX_PTR] = {0};
	dpi_dma_ptr_t hptr[DPI_MAX_PTR] = {0};
	u64 dpi_cmd[DPI_DMA_CMDX_SIZE] = {0};
	union dpi_dma_instr_hdr_s *header = (union dpi_dma_instr_hdr_s *)&dpi_cmd[0];
	u8 nfst, nlst;
	u16 index = 0;
	int i;
	//struct pci_dev* pdev = to_pci_dev(dev);
	struct dpivf_t* dpi_vf = (struct dpivf_t *)dev_get_drvdata(dev);

	/* TODO check Sum(len[i]) < 64K */
	if (num_ptrs > DPI_MAX_PTR || num_ptrs < 1)
		return -EINVAL;

	for (i = 0; i < num_ptrs; i++) {
		lptr[i].s.ptr = local_addr[i];
		lptr[i].s.length = len[i];
		hptr[i].s.ptr = host_addr[i];
		hptr[i].s.length = len[i];
		if (dir == DMA_FROM_HOST) {
			cmd.rptr[i] = &hptr[i];
			cmd.wptr[i] = &lptr[i];
		} else {
			cmd.rptr[i] = &lptr[i];
			cmd.wptr[i] = &hptr[i];
		}
		cmd.rptr_cnt++;
		cmd.wptr_cnt++;
	}
	if (dir == DMA_FROM_HOST)
		header->s.xtype = DPI_HDR_XTYPE_E_INBOUND;
	else
		header->s.xtype = DPI_HDR_XTYPE_E_OUTBOUND;

	header->s.ptr = comp_iova;
	header->s.lport = lport[pem_num];

	index += 4;

	if (header->s.xtype == DPI_HDR_XTYPE_E_INBOUND) {
		header->s.nfst = cmd.wptr_cnt & 0xf;
		header->s.nlst = cmd.rptr_cnt & 0xf;
		nfst = cmd.wptr_cnt & 0xf;
		nlst = cmd.rptr_cnt & 0xf;
		for (i = 0; i < nfst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
		for (i = 0; i < nlst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}
	} else {
		header->s.nfst = cmd.rptr_cnt & 0xf;
		header->s.nlst = cmd.wptr_cnt & 0xf;
		nfst = cmd.rptr_cnt & 0xf;
		nlst = cmd.wptr_cnt & 0xf;

		for (i = 0; i < nfst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}

		for (i = 0; i < nlst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
	}
	/*for (i = 0; i < index; i++) {
		printk("dpi_cmd[%d]: 0x%016llx\n", i, dpi_cmd[i]);
	}*/

	dpi_dma_queue_write(dpi_vf, 0, index, dpi_cmd);

	wmb();
	writeq_relaxed(index, dpi_vf->reg_base + DPI_VDMA_DBELL);
	return 0;
}
EXPORT_SYMBOL(do_dma_async_dpi_vector);

 /* we use only the common fields in hdr for 8xxx and 9xxx hence dont differentiate */
int do_dma_sync_dpi(struct device *dev, local_dma_addr_t local_dma_addr,
					host_dma_addr_t host_addr, void *local_ptr,
					int len, host_dma_dir_t dir)
{
	struct dpivf_t* dpi_vf = dev_get_drvdata(dev);
	u8  *comp_data = NULL;
	dpi_dma_buf_ptr_t cmd = {0};
	dpi_dma_ptr_t lptr = {0};
	dpi_dma_ptr_t hptr = {0};
	u64 dpi_cmd[DPI_DMA_CMD_SIZE] = {0};
	union dpi_dma_instr_hdr_s *header = (union dpi_dma_instr_hdr_s *)&dpi_cmd[0];
	u8 nfst, nlst;
	u16 index = 0;
	u64 iova, comp_iova;
	u64 *dpi_buf_ptr;
	unsigned long time_start;
	int i;
	int  ret = 0;

	comp_data = dma_pool_alloc(dpi_vf->comp_buf_pool, GFP_ATOMIC, &comp_iova);
	if (comp_data == NULL) {
		printk("dpi_dma: dma alloc errr\n");
		return -1;
	}
	WRITE_ONCE(*comp_data,  0xFF);

	if (local_ptr) {
		if (dir == DMA_FROM_HOST)
			iova = dma_map_single(dev, local_ptr, len, DMA_FROM_DEVICE);
		else
			iova = dma_map_single(dev, local_ptr, len, DMA_TO_DEVICE);

	} else {
		iova = local_dma_addr;
	}

	lptr.s.ptr = iova;
	lptr.s.length = len;
	hptr.s.ptr = host_addr;
	hptr.s.length = len;

	if (dir == DMA_FROM_HOST) {
		cmd.rptr[0] = &hptr;
		cmd.wptr[0] = &lptr;
		header->s.xtype = DPI_HDR_XTYPE_E_INBOUND;
	} else {
		cmd.rptr[0] = &lptr;
		cmd.wptr[0] = &hptr;
		header->s.xtype = DPI_HDR_XTYPE_E_OUTBOUND;
	}

	cmd.rptr_cnt = 1;
	cmd.wptr_cnt = 1;
	header->s.ptr = comp_iova;
	header->s.lport = lport[pem_num];

	index += 4;

	if (header->s.xtype == DPI_HDR_XTYPE_E_INBOUND) {
		header->s.nfst = cmd.wptr_cnt & 0xf;
		header->s.nlst = cmd.rptr_cnt & 0xf;
		nfst = cmd.wptr_cnt & 0xf;
		nlst = cmd.rptr_cnt & 0xf;
		for (i = 0; i < nfst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
		for (i = 0; i < nlst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}
	} else {
		header->s.nfst = cmd.rptr_cnt & 0xf;
		header->s.nlst = cmd.wptr_cnt & 0xf;
		nfst = cmd.rptr_cnt & 0xf;
		nlst = cmd.wptr_cnt & 0xf;

		for (i = 0; i < nfst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}

		for (i = 0; i < nlst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
	}

	dpi_buf_ptr = dpi_vf->qctx[0].dpi_buf_ptr;
	if (dpi_dma_queue_write(dpi_vf, 0, index, dpi_cmd)) {
		dev_err(dev, "DMA queue write fail\n");
		ret = -1;
		goto err;
	}

	wmb();
	writeq_relaxed(index, dpi_vf->reg_base + DPI_VDMA_DBELL);
	/* Wait for the completion */
	time_start = jiffies;
	while (true) {
		if (READ_ONCE(*comp_data) != 0xFF)
			break;
		if (time_after(jiffies, (time_start + (1 * HZ)))) {
			dev_err(dev, "DMA timed out\n");
			for (i = 0; i < index; i++) {
				printk("dpi_cmd[%d]: 0x%016llx\n", i, dpi_cmd[i]);
			}
			ret = -1;
			goto err;
		}
	}
err:
	if (local_ptr) {
		if (dir == DMA_FROM_HOST)
			dma_unmap_single(dev, iova, len, DMA_FROM_DEVICE);
		else
			dma_unmap_single(dev, iova, len, DMA_TO_DEVICE);
	}

	if (*comp_data != 0) {
		dev_err(dev, "DMA failed with err %x\n", *comp_data);
		ret = -1;
	}
	dma_pool_free(dpi_vf->comp_buf_pool, comp_data, comp_iova);
	return ret;
}
EXPORT_SYMBOL(do_dma_sync_dpi);

int do_dma_to_host(uint32_t val, host_dma_addr_t host_addr)
{
	host_dma_addr_t liova = get_cpu_var(cpu_iova);
	u32 *lva = get_cpu_var(cpu_lptr);

	WRITE_ONCE(*lva, val);
	do_dma_sync_dpi(vf[0]->dev, liova, host_addr, lva,
		sizeof(u32), DMA_TO_HOST);
	put_cpu_var(cpu_iova);
	put_cpu_var(cpu_lptr);

	return 0;
}

int dpi_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	int err, cpu;
#ifdef TEST_DPI_DMA_API
	struct page *p, *p1;
	u64 iova1;
	u8 *fptr, *fptr1;
#endif
	u64 *dpi_buf_ptr, dpi_buf, dpi_buf_phys, val;
	struct dpivf_t* dpi_vf;
	char comp_pool_name[16];
	static unsigned int domain = FPA_DPI_XAQ_GMID;
	union dpi_mbox_message_t otx2_mbox_msg;

	dpi_vf = devm_kzalloc(dev, sizeof(struct dpivf_t), GFP_KERNEL);
	if (!dpi_vf) {
		dev_err(dev, "Unable to allocate DPI VF device\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, dpi_vf);
	dpi_vf->dev = dev;
	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		return err;
	}

#define PCI_DPI_VF_CFG_BAR0 0
#define PCI_DPI_VF_CFG_BAR2 2
	/* MAP PF's configuration registers */
	dpi_vf->reg_base = pcim_iomap(pdev, PCI_DPI_VF_CFG_BAR0, 0);
	if (!dpi_vf->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		err = -ENOMEM;
		return err;
	}

	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_EN);
	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_REQQ_CTL);
	dpi_vf->domain = domain++;

	if (otx2_dpi_pfdev == NULL) {
		struct pci_dev *pcidev = NULL;
		while ((pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pcidev))) {
			if (pcidev->device == PCI_DEVID_OCTEONTX2_DPI_PF) {
				otx2_dpi_pfdev = pcidev;
				break;
			}
		}
	}
	if (otx2_dpi_pfdev == NULL) {
		dev_err(dev, "OTX2_DPI_PF not found\n");
		return -EINVAL;
	}

	dpi_vf->vf_id = pdev->devfn - 1;
	err = npa_aura_pool_init(DPI_NB_CHUNKS, DPI_CHUNK_SIZE,
				 &dpi_vf->aura, dpi_vf->dev);
	if (err) {
		dev_err(dev, "Failed to init aura pool pair");
		return err;
	}
	dpi_buf = npa_alloc_buf(dpi_vf->aura);
	if (!dpi_buf) {
		dev_err(dev, "Failed to allocate");
		return -ENOMEM;
	}

	otx2_mbox_msg.s.cmd = DPI_QUEUE_OPEN;
	otx2_mbox_msg.s.vfid = dpi_vf->vf_id;
	otx2_mbox_msg.s.csize = DPI_CHUNK_SIZE;
	otx2_mbox_msg.s.aura = dpi_vf->aura;
	otx2_mbox_msg.s.sso_pf_func = 0;
	otx2_mbox_msg.s.npa_pf_func = npa_pf_func(dpi_vf->aura);

	/* Opening DPI queue */
	err = otx2_dpipf->queue_config(otx2_dpi_pfdev, &otx2_mbox_msg);
	if (err) {
		dev_err(dev, "%d: Failed to allocate dpi queue %d:%d:%d", err,
			0, 0, num_vfs);
		return err;
	}

	if (num_vfs == 0) {
		u8 *local_ptr;
		u64 local_iova;

		/* This vf is shared between multiple cores and is used mainly
		 * by mgmt_net. So this vf will create a dma memory to be used
		 * for host_writel API, This api is executed on multiple cores
		 */
		local_ptr = dma_alloc_coherent(dev,
					       (num_online_cpus() * sizeof(u64)),
					       &local_iova, GFP_ATOMIC);
		for_each_online_cpu(cpu) {
			per_cpu(cpu_lptr, cpu) = (u32*)(local_ptr + (cpu * sizeof(u64)));
			per_cpu(cpu_iova, cpu) = local_iova + (cpu * sizeof(u64));
		}
		dpi_vf->host_writel_ptr = local_ptr;
		dpi_vf->host_writel_iova = local_iova;
	}

	dpi_vf->iommu_domain = iommu_get_domain_for_dev(dev);
	if (dpi_vf->iommu_domain)
		dpi_buf_phys = iommu_iova_to_phys(dpi_vf->iommu_domain, dpi_buf);
	else
		dpi_buf_phys = dpi_buf;

	dpi_buf_ptr = phys_to_virt(dpi_buf_phys);

	writeq_relaxed(((dpi_buf >> 7) << 7), dpi_vf->reg_base + DPI_VDMA_SADDR);
	dpi_vf->qctx[0].dpi_buf_ptr = dpi_buf_ptr;
	dpi_vf->qctx[0].pool_size_m1 = (DPI_CHUNK_SIZE >> 3) - 2;
	dpi_vf->qctx[0].dpi_buf = dpi_buf;
	spin_lock_init(&dpi_vf->qctx[0].queue_lock);

	sprintf(comp_pool_name, "comp_buf_pool_%d_%d",
			dpi_vf->domain, dpi_vf->vf_id);
	dpi_vf->comp_buf_pool = dma_pool_create(comp_pool_name, dev,
								10 * 128, 128, 0);
	if (!dpi_vf->comp_buf_pool) {
		pr_err("ERROR: Cannot allocate DMA buffer pool\n");
		return -ENOMEM;
	}

	vf[num_vfs++] = dpi_vf;
	/* Enabling DMA Engine */
	writeq_relaxed(0x1, dpi_vf->reg_base + DPI_VDMA_EN);

#ifdef TEST_DPI_DMA_API
	p = alloc_page(GFP_DMA | __GFP_NOWARN);
	if (!p) {
		pr_err("Unable to allocate internal memory\n");
		return -ENOMEM;
	}
	fptr = page_address(p);

	p1 = alloc_page(GFP_DMA | __GFP_NOWARN);
	if (!p1) {
		pr_err("Unable to allocate internal memory\n");
		return -ENOMEM;
	}

	fptr1 = page_address(p1);
	iova1 = dma_map_single(dev, fptr1, PAGE_SIZE, DMA_BIDIRECTIONAL);

	memset(fptr, 0xaa, 1024);
	memset(fptr1, 0, 1024);
	iova1 = (uint64_t)le64_to_cpu(0x35c57000);
	do_dma_sync(fptr, iova1, 1024, DMA_TO_HOST);
#endif

	return 0;
}

static void dpi_remove(struct pci_dev *pdev)
{
	union dpi_mbox_message_t otx2_mbox_msg = {0};
	struct dpivf_t *dpi_vf;
	u64 val;

	dpi_vf = pci_get_drvdata(pdev);
	/* Disable Engine */
	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_EN);

	otx2_mbox_msg.s.cmd = DPI_QUEUE_CLOSE;
	otx2_mbox_msg.s.vfid = dpi_vf->vf_id;

	/* Closing DPI queue */
	otx2_dpipf->queue_config(otx2_dpi_pfdev, &otx2_mbox_msg);
	do {
		val = readq_relaxed(dpi_vf->reg_base + DPI_VDMA_SADDR);
	} while (!(val & (0x1ull << 63)));

	npa_free_buf(dpi_vf->aura, dpi_vf->qctx[0].dpi_buf);

	if (dpi_vf->host_writel_ptr)
		dma_free_coherent(&pdev->dev, (num_online_cpus() * sizeof(u64)),
				  dpi_vf->host_writel_ptr, dpi_vf->host_writel_iova);

	if (dpi_vf->comp_buf_pool)
		dma_pool_destroy(dpi_vf->comp_buf_pool);
}

static struct pci_driver dpi_vf_driver = {
	.name = DRV_NAME,
	.id_table = dpi_id_table,
	.probe = dpi_vf_probe,
	.remove = dpi_remove,
};


int dpi_vf_init(void)
{
	otx2_dpipf = try_then_request_module(symbol_get(otx2_dpipf_com),
					     "octeontx2_dpi");
	if (otx2_dpipf == NULL) {
		printk("Load OTX2 DPI PF module\n");
		return -ENOMEM;
	}

	return pci_register_driver(&dpi_vf_driver);
}

void dpi_vf_cleanup(void)
{
	symbol_put(otx2_dpipf_com);

	pci_unregister_driver(&dpi_vf_driver);
}

MODULE_AUTHOR("Cavium");
MODULE_DESCRIPTION("Cavium Thunder DPI Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, dpi_id_table);
