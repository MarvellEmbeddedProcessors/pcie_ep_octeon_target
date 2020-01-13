#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/iommu.h>

#include "dpi.h"
#include "fpa.h"
#include "dpi_cmd.h"
#include "dma_api.h"

#define DRV_NAME "octeontx-dpi-vf"
#define DRV_VERSION "1.0"
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_OCTEONTX_DPI_VF 0xA058

#define DPI_CHUNK_SIZE 1024
#define DPI_MAX_QUEUES 8
#define DPI_NB_CHUNKS 4096
#define DPI_AURA 0
#define DPI_NUM_VFS 1
#define FPA_NUM_VFS 1

static struct fpapf_com_s *fpapf;
static struct fpavf_com_s *fpavf;

//struct dpipf_com_s *dpipf_com_ptr;
struct dpipf *dpi_pf;
struct dpivf_l *dpi_vf;
struct dpipf_com_s *dpipf;
static struct fpavf *fpa;

struct dma_queue_ctx {
	u16 qid;
	u16 pool_size_m1;
	u16 index;
	u16 reserved;
	u64 dpi_buf;
	void *dpi_buf_ptr;
};

struct dpivf_l {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	void __iomem *reg_base2;
	struct msix_entry *msix_entries;
	struct dma_queue_ctx qctx[DPI_MAX_QUEUES];
	u16 vf_id;
	u16 domain;
	int id;
};

static const struct pci_device_id dpi_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_OCTEONTX_DPI_VF) },
	{ 0, }	/* end of table */
};

#define FPA_DPI_XAQ_GMID 0x4
int dpivf_master_send_message(struct mbox_hdr *hdr,
			      union mbox_data *req,
			      union mbox_data *resp,
			      void *master_data,
			      void *add_data)
{
	struct dpivf_l *dpi_vf = master_data;
	int ret;

	if (hdr->coproc == FPA_COPROC) {
		ret = fpapf->receive_message(
			dpi_vf->vf_id, FPA_DPI_XAQ_GMID,
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

static int dpivf_pre_setup(struct dpivf_l *dpi_vf)
{
	struct device *dev = &dpi_vf->pdev->dev;
	int err = -ENODEV;

	/*TODO: handle -ve cases */
	dpipf = try_then_request_module(symbol_get(dpipf_com), "dpipf");
	if (dpipf == NULL) {
		dev_err(dev, "Load DPI PF module\n");
		return err;
	}

	fpapf = try_then_request_module(symbol_get(fpapf_com), "fpapf");
	if (fpapf == NULL) {
		dev_err(dev, "Load FPA PF module\n");
		goto put_1_dep;
	}

	fpavf = try_then_request_module(symbol_get(fpavf_com), "fpavf");
	if (fpavf == NULL) {
		dev_err(dev, "Load FPA VF module\n");
		goto put_2_deps;
	}

	err = dpipf->create_domain(dpi_vf->vf_id, dpi_vf->domain, DPI_NUM_VFS,
			NULL, NULL, NULL);
	if (!err) {
		dev_err(dev, "Failed to create DPI domain\n");
		err = -ENODEV;
		goto put_3_deps;
	}

	err = fpapf->create_domain(dpi_vf->vf_id, FPA_DPI_XAQ_GMID,
			FPA_NUM_VFS, NULL);
	if (!err) {
		dev_err(dev, "Failed to create DPI_XAQ_DOMAIN\n");
		err = -ENODEV;
		goto put_3_deps;
	}

	fpa = fpavf->get(FPA_DPI_XAQ_GMID, 0, &dpi_master_com, dpi_vf);
	if (fpa == NULL) {
		dev_err(dev, "Failed to get fpavf\n");
		err = -ENODEV;
		goto put_3_deps;
	}

	err = fpavf->setup(fpa, DPI_NB_CHUNKS, DPI_CHUNK_SIZE,
			FPA_VF_FLAG_CONT_MEM);
	if (err) {
		dev_err(dev, "FPA setup failed\n");
		err = -ENODEV;
		goto put_3_deps;
	}

put_3_deps:
	symbol_put(fpavf_com);
put_2_deps:
	symbol_put(fpapf_com);
put_1_dep:
	symbol_put(dpipf_com);

	return err;
}

static int dpi_dma_queue_write(struct dpivf_l *dpi_vf, u16 qid, u16 cmd_count,
		u64 *cmds)
{
	struct device *dev = &dpi_vf->pdev->dev;
	struct dma_queue_ctx *qctx;

	if ((cmd_count < 1) || (cmd_count > 64)) {
		dev_err(dev, "%s invalid cmdcount %d\n", __func__, cmd_count);
		return -1;
	}

	if (cmds == NULL) {
		dev_err(dev, "%s cmds buffer NULL\n", __func__);
		return -1;
	}

	qctx = &dpi_vf->qctx[qid];
	dev_info(dev, "%s index %d cmd_count %d pool_size_m1 %d\n",
			__func__, qctx->index, cmd_count, qctx->pool_size_m1);


	/* Normally there is plenty of
	 * room in the current buffer for the command
	 */
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

		dev_info(dev, "%s:%d Allocating new command buffer\n",
				__func__, __LINE__);

		dpi_buf = fpavf->alloc(fpa, DPI_AURA);
		if (!dpi_buf) {
			dev_err(dev, "Failed to allocate");
			return -ENODEV;
		}

		if (fpa->iommu_domain)
			dpi_buf_phys = iommu_iova_to_phys(fpa->iommu_domain,
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
			dpi_buf = fpavf->alloc(fpa, DPI_AURA);
			if (!dpi_buf) {
				dev_err(dev, "Failed to allocate");
				return -ENODEV;
			}

			if (fpa->iommu_domain)
				dpi_buf_phys =
					iommu_iova_to_phys(fpa->iommu_domain,
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

	return 0;
}

void do_dma_sync(void *local_ptr, host_dma_addr_t host_addr, int len, host_dma_dir_t dir)
{
	struct device *dev = &dpi_vf->pdev->dev;
	void *buf, *bufp[1];
	volatile u64 *comp_ptr;
	dpi_dma_req_compl_t *comp_data;
	dpi_dma_buf_ptr_t cmd = {0};
	dpi_dma_ptr_t lptr = {0};
	dpi_dma_ptr_t hptr = {0};
	u64 dpi_cmd[DPI_DMA_CMD_SIZE] = {0};
	dpi_dma_instr_hdr_s *header = (dpi_dma_instr_hdr_s *)&dpi_cmd[0];
	dpi_dma_queue_ctx_t ctx = {0};
	u8 nfst, nlst;
	u16 index = 0;
	u64 iova, comp_iova;
	u64 *dpi_buf_ptr;
	int i;

	comp_data = devm_kzalloc(dev, len, GFP_DMA);
	memset(comp_data, 0, len);
	iova = dma_map_single(dev, local_ptr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	lptr.s.ptr = iova;
	lptr.s.length = len;
	hptr.s.ptr = host_addr;
	hptr.s.length = len;

	buf = (void *)&cmd;
	bufp[0] = &buf;

	if (dir == DMA_FROM_HOST) {
		cmd.rptr[0] = &hptr;
		cmd.wptr[0] = &lptr;
		ctx.xtype = DPI_XTYPE_INBOUND;
	} else {
		cmd.rptr[0] = &lptr;
		cmd.wptr[0] = &hptr;
		ctx.xtype = DPI_XTYPE_OUTBOUND;
	}

	cmd.rptr_cnt = 1;
	cmd.wptr_cnt = 1;

	ctx.pt = 0;
	cmd.comp_ptr = comp_data;
	cmd.comp_ptr->cdata = 0xFF;
	comp_iova = dma_map_single(dev, comp_data, 1024, DMA_BIDIRECTIONAL);
	header->s.ptr = comp_iova;
	header->s.xtype = ctx.xtype & 0x3;
	header->s.pt = ctx.pt & 0x3;
	//header->s.ptr = 1;
	header->s.deallocv = ctx.deallocv;
	header->s.tt = ctx.tt & 0x3;
	header->s.grp = ctx.grp & 0x3ff;
	header->s.csel = 0;
	header->s.ca = 1;
	//header->s.fi = 1;

	if (header->s.deallocv)
		header->s.pvfe = 1;

	/* TODO: define macros for 0x2 and 0x3. */
	if (header->s.pt == 0x2)
		header->s.ptr = header->s.ptr | 0x3;

	index += 4;

	if (ctx.xtype ==  DPI_XTYPE_INBOUND) {
		header->s.nfst = cmd.wptr_cnt & 0xf;
		header->s.nlst = cmd.rptr_cnt & 0xf;
		header->s.fport = 0;
		header->s.lport = 0;
		for (i = 0; i < header->s.nfst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
		for (i = 0; i < header->s.nlst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}
	} else {
		header->s.fport = 0;
		header->s.lport = 0;
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
	dpi_dma_queue_write(dpi_vf, 0, index, dpi_cmd);

	wmb();
	writeq_relaxed(index, dpi_vf->reg_base + DPI_VDMA_DBELL);

	comp_ptr = &cmd.comp_ptr->cdata;
	/* Wait for the completion */
	while (true) {
		if (*comp_ptr != 0xFF)
			break;
	}
	devm_kfree(dev, comp_data);
	if (*comp_ptr != 0)
		dev_err(dev, "DMA failed with err %llx\n", *comp_ptr);
}
EXPORT_SYMBOL(do_dma_sync);

int dpi_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	u64 val, *dpi_buf_ptr, dpi_buf, dpi_buf_phys;
	union mbox_data req;
	union mbox_data resp;
	struct mbox_hdr hdr;
	struct mbox_dpi_cfg cfg;
	int err;
#ifdef TEST_DPI_DMA_API
	struct page *p, *p1;
	u64 iova1;
	u8 *fptr, *fptr1;
#endif

	dpi_vf = devm_kzalloc(dev, sizeof(struct dpivf_l), GFP_KERNEL);
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
	val = readq_relaxed(dpi_vf->reg_base + DPI_VDMA_SADDR);
	dpi_vf->vf_id = (val >> 24) & 0xffff;
	dpi_vf->domain = (val >> 8) & 0xffff;

	err = dpivf_pre_setup(dpi_vf);
	if (err) {
		dev_err(dev, "Pre-requisites failed");
		return -ENODEV;
	}

	dpi_buf = fpavf->alloc(fpa, DPI_AURA);
	if (!dpi_buf) {
		symbol_put(fpapf_com);
		symbol_put(fpavf_com);
		dev_err(dev, "Failed to allocate");
		return -ENODEV;
	}

	if (fpa->iommu_domain)
		dpi_buf_phys = iommu_iova_to_phys(fpa->iommu_domain, dpi_buf);
	else
		dpi_buf_phys = dpi_buf;

	dpi_buf_ptr = phys_to_virt(dpi_buf_phys);
	writeq_relaxed(((dpi_buf >> 7) << 7),
			dpi_vf->reg_base + DPI_VDMA_SADDR);
	dpi_vf->qctx[0].dpi_buf_ptr = dpi_buf_ptr;
	dpi_vf->qctx[0].pool_size_m1 = (DPI_CHUNK_SIZE >> 3) - 2;
	dpi_vf->qctx[0].dpi_buf = dpi_buf;

	cfg.buf_size = DPI_CHUNK_SIZE;
	cfg.inst_aura = DPI_AURA;

	hdr.coproc = DPI_COPROC;
	hdr.msg = DPI_QUEUE_OPEN;
	hdr.vfid = dpi_vf->vf_id;
	/* Opening DPI queue */
	dpipf->receive_message(dpi_vf->vf_id, dpi_vf->domain, &hdr, &req,
			&resp, &cfg);

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
	union mbox_data req;
	union mbox_data resp;
	struct mbox_hdr hdr;
	struct device *dev = &pdev->dev;
	int err;

	/* Disable Engine */
	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_EN);

	hdr.coproc = DPI_COPROC;
	hdr.msg = DPI_QUEUE_CLOSE;
	hdr.vfid = dpi_vf->vf_id;
	resp.data = 0xff;
	/* Closing DPI queue */
	dpipf->receive_message(dpi_vf->vf_id, dpi_vf->domain, &hdr, &req,
			&resp, NULL);

	/* TODO: Need to check the response */
	fpavf->free(fpa, DPI_AURA, dpi_vf->qctx[0].dpi_buf, 0);
	err = fpavf->teardown(fpa);
	if (err)
		dev_err(dev, "FPA teardown failed\n");

	err = fpapf->destroy_domain(dpi_vf->vf_id, FPA_DPI_XAQ_GMID, NULL);
	if (err)
		dev_err(dev, "FPA PF destroy domain failed\n");

	err = dpipf->destroy_domain(dpi_vf->vf_id, dpi_vf->domain, NULL);
	if (err)
		dev_err(dev, "DPI PF destroy domain failed\n");

	symbol_put(fpavf_com);
	symbol_put(fpapf_com);
	symbol_put(dpipf_com);
}

static struct pci_driver dpi_vf_driver = {
	.name = DRV_NAME,
	.id_table = dpi_id_table,
	.probe = dpi_vf_probe,
	.remove = dpi_remove,
};


static int __init dpi_init_module(void)
{
	return pci_register_driver(&dpi_vf_driver);
}

static void __exit dpi_cleanup_module(void)
{
	pci_unregister_driver(&dpi_vf_driver);
}

module_init(dpi_init_module);
module_exit(dpi_cleanup_module);

MODULE_AUTHOR("Cavium");
MODULE_DESCRIPTION("Cavium Thunder DPI Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, dpi_id_table);
