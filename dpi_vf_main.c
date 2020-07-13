// SPDX-License-Identifier: (GPL-2.0)
/* DPI DMA VF driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/fs.h>
#include <linux/dmapool.h>

#include "dpi.h"
#include "fpa.h"
#include "dpi_cmd.h"
#include "dma_api.h"
#include "octeontx_mbox.h"
#include "octeontx.h"
#include "otx2_npa_pf.h"

#define DRV_NAME "octeontx-dpi-vf"
#define DRV_VERSION "1.0"
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_OCTEONTX_DPI_VF_83XX 0xA058
#define PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX 0xA081

#define DPI_CHUNK_SIZE 1024
#define DPI_DMA_CMD_SIZE 64
#define DPI_MAX_QUEUES 8
#define DPI_NB_CHUNKS 4096
#define FPA_DPI_XAQ_GMID 0x5
#define DPI_NUM_VFS 1
#define FPA_NUM_VFS 1
#define DPI_DMA_CMDX_SIZE 64

#define FPA_PF_ID 0
#define DPI_PF_ID 0


static unsigned int pem_num = 0;
module_param(pem_num, uint, 0644);
MODULE_PARM_DESC(pem_num, "pem number to use");
/* pem 1 and pem 3 do not support dpi */
#define MAX_PEM 4
static int lport[MAX_PEM] = { 0, -1, 1, -1 };

static struct fpapf_com_s *fpapf;
static struct fpavf_com_s *fpavf;

u8 *local_ptr;
u64 local_iova;
unsigned long part_num;

static struct dpipf_com_s *dpipf;

extern struct dpipf_com_s dpipf_com;

struct dpipf_com_s {
        u64 (*create_domain)(u32 id, u16 domain_id, u32 num_vfs,
                             void *master, void *master_data,
                             struct kobject *kobj);
        int (*destroy_domain)(u32 id, u16 domain_id, struct kobject *kobj);
        int (*reset_domain)(u32, u16);
        int (*receive_message)(u32, u16 domain_id,
                               struct mbox_hdr *hdr, union mbox_data *req,
                               union mbox_data *resp, void *add_data);
        int (*get_vf_count)(u32 id);
};

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
	struct pci_dev *pdev;
	void __iomem *reg_base;
	void __iomem *reg_base2;
	struct msix_entry *msix_entries;
	struct kobject *kobj;
	struct iommu_domain *iommu_domain;
	struct dma_queue_ctx qctx[DPI_MAX_QUEUES];
	u16 vf_id;
	u16 domain;
	int id;
	struct fpavf *fpa;
	struct dma_pool *comp_buf_pool;
	u16 aura;
	u8 *host_writel_ptr;
	u64 host_writel_iova;
};

DEFINE_PER_CPU(struct dpivf_t*, cpu_dpi_vf);
DEFINE_PER_CPU(u32*, cpu_lptr);
DEFINE_PER_CPU(u64, cpu_iova);

static const struct pci_device_id dpi_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX_DPI_VF_83XX) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX_DPI_VF_93XX) },
	{ 0, }	/* end of table */
};

int dpivf_master_send_message(struct mbox_hdr *hdr,
			      union mbox_data *req,
			      union mbox_data *resp,
			      void *master_data,
			      void *add_data)
{
	struct dpivf_t *dpi_vf = master_data;
	int ret;

	if (hdr->coproc == FPA_COPROC) {
		ret = fpapf->receive_message(
			FPA_PF_ID, dpi_vf->domain,
			hdr, req, resp, add_data);
	} else {
		dev_err(&dpi_vf->pdev->dev, "SSO message dispatch, wrong VF type\n");
		ret = -1;
	}

	return ret;
}

static struct octeontx_master_com_t dpi_master_com = {
	.send_message = dpivf_master_send_message,
};

static int dpivf_pre_setup(struct dpivf_t *dpi_vf)
{
	int err;
	struct device *dev = &dpi_vf->pdev->dev;
	char kobj_name[16];
	u64 mask;

	sprintf(kobj_name, "dpi_kobj_%d", dpi_vf->domain);
	dpi_vf->kobj = kobject_create_and_add(kobj_name, NULL);
	if (!dpi_vf->kobj) {
		printk("Failed to create Kobject\n");
		goto kobj_fail;
	}

	mask = dpipf->create_domain(DPI_PF_ID, dpi_vf->domain, DPI_NUM_VFS,
								NULL, NULL, dpi_vf->kobj);
	if (!mask) {
		printk("Failed to create DPI domain\n");
		goto dpi_domain_fail;
	}

	mask = fpapf->create_domain(FPA_PF_ID, dpi_vf->domain, FPA_NUM_VFS, dpi_vf->kobj);
	if (!mask) {
		printk("Failed to create FPA domain\n");
		goto fpa_domain_fail;
	}

	dpi_vf->fpa = fpavf->get(dpi_vf->domain, 0, &dpi_master_com, dpi_vf);
	if (dpi_vf->fpa == NULL) {
		dev_err(dev, "Failed to get fpavf\n");
		goto fpavf_fail;
	}

	err = fpavf->setup(dpi_vf->fpa, DPI_NB_CHUNKS, DPI_CHUNK_SIZE, dev);
	if (err) {
		dev_err(dev, "FPA setup failed\n");
		goto fpavf_setup_fail;
	}

	return 0;

fpavf_setup_fail:
	fpavf->put(dpi_vf->fpa);
fpavf_fail:
	fpapf->destroy_domain(FPA_PF_ID, dpi_vf->domain, dpi_vf->kobj);
fpa_domain_fail:
	dpipf->destroy_domain(DPI_PF_ID, dpi_vf->domain, dpi_vf->kobj);
dpi_domain_fail:
	kobject_del(dpi_vf->kobj);
kobj_fail:

	return -ENODEV;
}

static int dpi_dma_queue_write(struct dpivf_t *dpi_vf, u16 qid, u16 cmd_count,
		u64 *cmds)
{
	struct device *dev = &dpi_vf->pdev->dev;
	struct dma_queue_ctx *qctx;
	unsigned long flags;

	if ((cmd_count < 1) || (cmd_count > 64)) {
		dev_err(dev, "%s invalid cmdcount %d\n", __func__, cmd_count);
		return -1;
	}

	if (cmds == NULL) {
		dev_err(dev, "%s cmds buffer NULL\n", __func__);
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

		//dev_info(dev, "%s:%d Allocating new command buffer\n",
		//		__func__, __LINE__);

		if (part_num == CAVIUM_CPU_PART_T83)
			dpi_buf = fpavf->alloc(dpi_vf->fpa, dpi_vf->aura);
		else
			dpi_buf = npa_alloc_buf(dpi_vf->aura);

		if (!dpi_buf) {
			spin_unlock_irqrestore(&qctx->queue_lock, flags);
			dev_err(dev, "Failed to allocate");
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
			if (part_num == CAVIUM_CPU_PART_T83)
				dpi_buf = fpavf->alloc(dpi_vf->fpa, dpi_vf->aura);
			else
				dpi_buf = npa_alloc_buf(dpi_vf->aura);

			if (!dpi_buf) {
				spin_unlock_irqrestore(&qctx->queue_lock, flags);
				dev_err(dev, "Failed to allocate");
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
int do_dma_sync(local_dma_addr_t local_dma_addr, host_dma_addr_t host_dma_addr,
		void *local_virt_addr, int len,	host_dma_dir_t dir)
{
	return do_dma_sync_dpi(local_dma_addr, host_dma_addr, NULL, len, dir);
}
EXPORT_SYMBOL(do_dma_sync);

struct device *get_dpi_dma_dev(int handle)
{
	struct dpivf_t* dpi_vf = NULL;

	if (handle == HANDLE_TYPE_RPC) {
		/* return high priority core specific device */
		dpi_vf = __this_cpu_read(cpu_dpi_vf);
	} else {
		/* return common device shared between multiple cores */
		dpi_vf = per_cpu(cpu_dpi_vf, 0);
	}

	return (dpi_vf) ? &dpi_vf->pdev->dev : NULL;
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
	struct pci_dev* pdev = to_pci_dev(dev);
	struct dpivf_t* dpi_vf = (struct dpivf_t *)pci_get_drvdata(pdev);

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
int do_dma_sync_dpi(local_dma_addr_t local_dma_addr, host_dma_addr_t host_addr,
		    void *local_ptr, int len, host_dma_dir_t dir)
{
	struct dpivf_t* dpi_vf = __this_cpu_read(cpu_dpi_vf);
	struct device *dev = &dpi_vf->pdev->dev;
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
	do_dma_sync_dpi(liova, host_addr, lva, sizeof(u32), DMA_TO_HOST);
	put_cpu_var(cpu_iova);
	put_cpu_var(cpu_lptr);

	return 0;
}

int dpi_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	union mbox_data req;
	union mbox_data resp;
	struct mbox_hdr hdr;
	struct mbox_dpi_cfg cfg;
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
	static unsigned int num_vfs = 0;
	union dpi_mbox_message_t otx2_mbox_msg;

	part_num = read_cpuid_part_number();
	if ((part_num != CAVIUM_CPU_PART_T83) &&
	    (part_num != MRVL_CPU_PART_OCTEONTX2_96XX)) {
		printk("Unsupported CPU type\n");
		return -EINVAL;
	}

	dpi_vf = devm_kzalloc(dev, sizeof(struct dpivf_t), GFP_KERNEL);
	if (!dpi_vf) {
		dev_err(dev, "Unable to allocate DPI VF device\n");
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, dpi_vf);
	dpi_vf->pdev = pdev;
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
	if (part_num == CAVIUM_CPU_PART_T83) {
		err = dpivf_pre_setup(dpi_vf);
		if (err) {
			dev_err(dev, "Pre-requisites failed");
			return -ENODEV;
		}
		val = readq_relaxed(dpi_vf->reg_base + DPI_VDMA_SADDR);
		dpi_vf->vf_id = (val >> 24) & 0xffff;
		dpi_vf->aura = 0;
		dpi_buf = fpavf->alloc(dpi_vf->fpa, dpi_vf->aura);
		if (!dpi_buf) {
			dev_err(dev, "Failed to allocate");
			return -ENODEV;
		}

		cfg.buf_size = DPI_CHUNK_SIZE;
		cfg.inst_aura = dpi_vf->aura;

		hdr.coproc = DPI_COPROC;
		hdr.msg = DPI_QUEUE_OPEN;
		hdr.vfid = dpi_vf->vf_id;
		/* Opening DPI queue */
		err = dpipf->receive_message(DPI_PF_ID, dpi_vf->domain, &hdr, &req,
								&resp, &cfg);
		if (err) {
			dev_err(dev, "%d: Failed to allocate dpi queue %d:%d:%d", err,
					0, dpi_vf->domain, hdr.vfid);
			return err;
		}
	} else {
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
		dpi_vf->aura = npa_aura_pool_init(DPI_NB_CHUNKS, DPI_CHUNK_SIZE,
								&dpi_vf->pdev->dev);
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
		otx2_mbox_msg.s.npa_pf_func = npa_pf_func();

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

	if (num_vfs == 0) {
		/* core0 and cores which cannot get dedicated vf share vf[0] */
		for_each_online_cpu(cpu)
			per_cpu(cpu_dpi_vf, cpu) = dpi_vf;
	} else {
		/* cores 1 onwards get dedicated vf's until we run out of vf's */
		int ncpu = num_vfs;
		for_each_online_cpu(cpu) {
			if (ncpu == 0)
				per_cpu(cpu_dpi_vf, cpu) = dpi_vf;

			ncpu--;
		}
	}
	num_vfs++;
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

static void dpivf_pre_setup_undo(struct dpivf_t *dpi_vf)
{
	struct device *dev = &dpi_vf->pdev->dev;
	int err;

	/* TODO: Need to check the response */
	fpavf->free(dpi_vf->fpa, dpi_vf->aura, dpi_vf->qctx[0].dpi_buf, 0);
	err = fpavf->teardown(dpi_vf->fpa);
	if (err)
		dev_err(dev, "FPA teardown failed\n");

	fpavf->put(dpi_vf->fpa);

	fpapf->destroy_domain(FPA_PF_ID, dpi_vf->domain, dpi_vf->kobj);

	dpipf->destroy_domain(DPI_PF_ID, dpi_vf->domain, dpi_vf->kobj);

	kobject_del(dpi_vf->kobj);
}

static void dpi_remove(struct pci_dev *pdev)
{
	struct dpivf_t *dpi_vf;
	u64 val;

	dpi_vf = pci_get_drvdata(pdev);
	/* Disable Engine */
	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_EN);

	if (part_num == CAVIUM_CPU_PART_T83) {
		union mbox_data req;
		union mbox_data resp;
		struct mbox_hdr hdr;

		hdr.coproc = DPI_COPROC;
		hdr.msg = DPI_QUEUE_CLOSE;
		hdr.vfid = dpi_vf->vf_id;
		resp.data = 0xff;
		/* Closing DPI queue */
		dpipf->receive_message(DPI_PF_ID, dpi_vf->domain, &hdr, &req, &resp, NULL);

		dpivf_pre_setup_undo(dpi_vf);
	} else {
		union dpi_mbox_message_t otx2_mbox_msg = {0};

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
	}

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
	/*TODO: handle -ve cases */
	dpipf = try_then_request_module(symbol_get(dpipf_com), "dpipf");
	if (dpipf == NULL) {
		printk("Load DPI PF module\n");
		return -ENOMEM;
	}

	fpapf = try_then_request_module(symbol_get(fpapf_com), "fpapf");
	if (fpapf == NULL) {
		printk("Load FPA PF module\n");
		goto fpapf_fail;
	}

	fpavf = try_then_request_module(symbol_get(fpavf_com), "fpavf");
	if (fpavf == NULL) {
		printk("Load FPA VF module\n");
		goto fpavf_fail;
	}

	otx2_dpipf = try_then_request_module(symbol_get(otx2_dpipf_com),
											"octeontx2_dpi");
	if (otx2_dpipf == NULL) {
		printk("Load OTX2 DPI PF module\n");
		goto otx2_dpipf_fail;
	}

	return pci_register_driver(&dpi_vf_driver);

otx2_dpipf_fail:
	symbol_put(fpavf_com);
fpavf_fail:
	symbol_put(fpapf_com);
fpapf_fail:
	symbol_put(dpipf_com);

	return -ENOMEM;
}

void dpi_vf_cleanup(void)
{
	symbol_put(fpavf_com);
	symbol_put(fpapf_com);
	symbol_put(dpipf_com);

	symbol_put(otx2_dpipf_com);

	pci_unregister_driver(&dpi_vf_driver);
}

MODULE_AUTHOR("Cavium");
MODULE_DESCRIPTION("Cavium Thunder DPI Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, dpi_id_table);
