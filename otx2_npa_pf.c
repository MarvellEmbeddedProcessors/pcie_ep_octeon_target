// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/iommu.h>
#include "otx2_common.h"

#define DRV_NAME "octeontx2-npapf"
#define DRV_VERSION "1.0"
#define DRV_STRING "Marvell OcteonTX2 NPA Physical Function Driver"
#define PCI_DEVID_OCTEONTX2_RVU_NPA_PF 0xA0FB

/* PCI Config offsets */
#define REG_BAR 2
#define MBOX_BAR 4
#define NPA_MEM_REGIONS 5

/* Supported devices */
static const struct pci_device_id otx2_npa_pf_id_table[] = {
        { PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_RVU_NPA_PF) },
        { 0, }  /* end of table */
};

struct npa_dev_t *gnpa_pf_dev = NULL;
typedef struct {
  /** PCI address to which the BAR is mapped. */
        unsigned long start;
  /** Length of this PCI address space. */
        unsigned long len;
  /** Length that has been mapped to phys. address space. */
        unsigned long mapped_len;
  /** The physical address to which the PCI address space is mapped. */
        void *hw_addr;
  /** Flag indicating the mapping was successful. */
        int done;
} octeon_mmio;

enum {
        TYPE_PFAF,
        TYPE_PFVF,
};

struct npa_dev_t {
	u16 pcifunc;
	u16 npa_msixoff;
	u32 hwcap;
	u32 stack_pg_ptrs;  /* No of ptrs per stack page */
	u32 stack_pg_bytes; /* Size of stack page */
	u16 iommu_domain_type;
	void *iommu_domain;
	struct otx2_pool *pool;
	struct workqueue_struct *mbox_wq;
	struct pci_dev *pdev;
	struct mbox mbox;
	octeon_mmio mmio[NPA_MEM_REGIONS];
};

static void otx2_pfaf_mbox_up_handler(struct work_struct *work)
{
	/* TODO: List MBOX uphandler operations */
	printk("Up handler required...!\n");
}

static void npa_mbox_handler_msix_offset(struct npa_dev_t *pfvf,
		struct msix_offset_rsp *rsp)
{
        pfvf->npa_msixoff = rsp->npa_msixoff;
}

static void npa_mbox_handler_lf_alloc(struct npa_dev_t *pfvf,
                               struct npa_lf_alloc_rsp *rsp)
{
        pfvf->stack_pg_ptrs = rsp->stack_pg_ptrs;
        pfvf->stack_pg_bytes = rsp->stack_pg_bytes;
}

static void otx2_process_pfaf_mbox_msg(struct npa_dev_t *npa_pf_dev,
                                       struct mbox_msghdr *msg)
{
        int devid;
	struct device *dev;

	dev = &npa_pf_dev->pdev->dev;
        if (msg->id >= MBOX_MSG_MAX) {
                dev_err(dev,
                        "Mbox msg with unknown ID 0x%x\n", msg->id);
                return;
        }

        if (msg->sig != OTX2_MBOX_RSP_SIG) {
                dev_err(dev,
                        "Mbox msg with wrong signature %x, ID 0x%x\n",
                         msg->sig, msg->id);
                return;
        }

        /* message response heading VF */
        devid = msg->pcifunc & RVU_PFVF_FUNC_MASK;

        switch (msg->id) {
        case MBOX_MSG_READY:
                npa_pf_dev->pcifunc = msg->pcifunc;
                break;
        case MBOX_MSG_MSIX_OFFSET:
                npa_mbox_handler_msix_offset(npa_pf_dev, (struct msix_offset_rsp *)msg);
                break;
        case MBOX_MSG_NPA_LF_ALLOC:
                npa_mbox_handler_lf_alloc(npa_pf_dev, (struct npa_lf_alloc_rsp *)msg);
                break;
        default:
                if (msg->rc)
                        dev_err(&npa_pf_dev->pdev->dev,
                                "Mbox msg response has err %d, ID 0x%x\n",
                                msg->rc, msg->id);
                break;
        }
}

static void otx2_pfaf_mbox_handler(struct work_struct *work)
{
        struct otx2_mbox_dev *mdev;
        struct mbox_hdr *rsp_hdr;
        struct mbox_msghdr *msg;
        struct otx2_mbox *mbox;
        struct mbox *af_mbox;
	struct npa_dev_t *npa_pf_dev;
        int offset, id;

        af_mbox = container_of(work, struct mbox, mbox_wrk);
        mbox = &af_mbox->mbox;
        mdev = &mbox->dev[0];
        rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);

        offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);
        npa_pf_dev = (struct npa_dev_t *)af_mbox->pfvf;

        for (id = 0; id < af_mbox->num_msgs; id++) {
                msg = (struct mbox_msghdr *)(mdev->mbase + offset);
                otx2_process_pfaf_mbox_msg(npa_pf_dev, msg);
                offset = mbox->rx_start + msg->next_msgoff;
                mdev->msgs_acked++;
        }

        otx2_mbox_reset(mbox, 0);
}

static int otx2_pfaf_mbox_init(struct npa_dev_t *npa_pf_dev)
{
	struct mbox *mbox;
	struct pci_dev *pdev;
	int err;

	pdev = npa_pf_dev->pdev;
	mbox = &npa_pf_dev->mbox;
	mbox->pfvf = (struct otx2_nic *)npa_pf_dev;
	npa_pf_dev->mbox_wq = alloc_workqueue("otx2_npa_pfaf_mailbox",
					WQ_UNBOUND | WQ_HIGHPRI |
					WQ_MEM_RECLAIM, 1);
	if (!npa_pf_dev->mbox_wq)
		return -ENOMEM;
	
	err = otx2_mbox_init(&mbox->mbox, npa_pf_dev->mmio[MBOX_BAR].hw_addr, pdev,
			npa_pf_dev->mmio[REG_BAR].hw_addr, MBOX_DIR_PFAF, 1);
	if (err) {
		printk("mbox init for pfaf failed\n");
		return -1;
	}
	
	err = otx2_mbox_init(&mbox->mbox_up, npa_pf_dev->mmio[MBOX_BAR].hw_addr, pdev,
			npa_pf_dev->mmio[REG_BAR].hw_addr, MBOX_DIR_PFAF_UP, 1);
	if (err) {
		printk("mbox init for pfaf up failed\n");
		return -1;
	}
	
	/* TODO: Is bounce buffer required? */
	//err = otx2_mbox_bbuf_init(mbox, pf->pdev);
	//if (err) {
	//        goto exit;
	//}
	INIT_WORK(&mbox->mbox_wrk, otx2_pfaf_mbox_handler);
	INIT_WORK(&mbox->mbox_up_wrk, otx2_pfaf_mbox_up_handler);
	otx2_mbox_lock_init(&npa_pf_dev->mbox);
	
	return 0;
}

static void otx2_queue_work(struct mbox *mw, struct workqueue_struct *mbox_wq,
                            int first, int mdevs, u64 intr, int type)
{
        struct otx2_mbox_dev *mdev;
        struct otx2_mbox *mbox;
        struct mbox_hdr *hdr;
        int i;

        for (i = first; i < mdevs; i++) {
                /* start from 0 */
                if (!(intr & BIT_ULL(i - first)))
                        continue;
                mbox = &mw->mbox;
                mdev = &mbox->dev[i];
                if (type == TYPE_PFAF)
                        otx2_sync_mbox_bbuf(mbox, i);
                hdr = mdev->mbase + mbox->rx_start;
                /*The hdr->num_msgs is set to zero immediately in the interrupt
                 * handler to  ensure that it holds a correct value next time
                 * when the interrupt handler is called.
                 * pf->mbox.num_msgs holds the data for use in pfaf_mbox_handler
                 * pf>mbox.up_num_msgs holds the data for use in
                 * pfaf_mbox_up_handler.
                 */
                if (hdr->num_msgs) {
                        mw[i].num_msgs = hdr->num_msgs;
                        hdr->num_msgs = 0;
                        if (type == TYPE_PFAF)
                                memset(mbox->hwbase + mbox->rx_start, 0,
                                       ALIGN(sizeof(struct mbox_hdr),
                                             sizeof(u64)));

                        queue_work(mbox_wq, &mw[i].mbox_wrk);
                }
                mbox = &mw->mbox_up;
                mdev = &mbox->dev[i];
                hdr = mdev->mbase + mbox->rx_start;
                if (hdr->num_msgs) {
                        mw[i].up_num_msgs = hdr->num_msgs;
                        hdr->num_msgs = 0;
                        if (type == TYPE_PFAF)
                                memset(mbox->hwbase + mbox->rx_start, 0,
                                       ALIGN(sizeof(struct mbox_hdr),
                                             sizeof(u64)));

                        queue_work(mbox_wq, &mw[i].mbox_up_wrk);
                }
        }
}

static irqreturn_t otx2_pfaf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct npa_dev_t *npa_pf_dev= (struct npa_dev_t *)pf_irq;
	void __iomem *reg_addr;
	struct mbox *mbox;

	reg_addr = npa_pf_dev->mmio[REG_BAR].hw_addr;
	/* Clear the IRQ */
	writeq(BIT_ULL(0), reg_addr + RVU_PF_INT);

	mbox = &npa_pf_dev->mbox;
	otx2_queue_work(mbox, npa_pf_dev->mbox_wq, 0, 1, 1, TYPE_PFAF);

	return IRQ_HANDLED;
}

static void otx2_disable_mbox_intr(struct npa_dev_t *npa_pf_dev)
{
	int vector = pci_irq_vector(npa_pf_dev->pdev, RVU_PF_INT_VEC_AFPF_MBOX);
	void __iomem *reg_addr = npa_pf_dev->mmio[REG_BAR].hw_addr;
	
	/* Disable AF => PF mailbox IRQ */
	writeq(BIT_ULL(0), reg_addr + RVU_PF_INT_ENA_W1C);
	free_irq(vector, npa_pf_dev);
}

dma_addr_t otx2_alloc_npa_buf(struct npa_dev_t *pfvf, struct otx2_pool *pool,
			gfp_t gfp, struct device *owner)
{
	dma_addr_t iova;
	struct device *dev;
	struct iommu_domain *iommu_domain;

	dev = &pfvf->pdev->dev;
	/* Check if request can be accommodated in previous allocated page */
	if (pool->page &&
	    ((pool->page_offset + pool->rbsize) <= PAGE_SIZE)) {
		pool->pageref++;
		goto ret;
	}

	otx2_get_page(pool);

	/* Allocate a new page */
	pool->page = alloc_pages(gfp | __GFP_COMP | __GFP_NOWARN,
	                         pool->rbpage_order);
	if (unlikely(!pool->page))
		return -ENOMEM;
	
	pool->page_offset = 0;
ret:
	iommu_domain = iommu_get_domain_for_dev(owner);
	if (iommu_domain && (iommu_domain->type == IOMMU_DOMAIN_IDENTITY)) {
		iova = page_to_phys(pool->page) + pool->page_offset;
	} else {
		iova = dma_map_page_attrs(owner, pool->page, pool->page_offset,
				pool->rbsize, DMA_TO_DEVICE,
				DMA_ATTR_SKIP_CPU_SYNC);
		if (unlikely(dma_mapping_error(owner, iova)))
			iova = (dma_addr_t) NULL;
	}

	if (!iova) {
		if (!pool->page_offset)
			__free_pages(pool->page, pool->rbpage_order);
		pool->page = NULL;
		return -ENOMEM;
	}
	pool->page_offset += pool->rbsize;
	return iova;
}

static int otx2_npa_aura_init(struct npa_dev_t *pfvf, int aura_id,
                          int pool_id, int numptrs)
{
	struct npa_aq_enq_req *aq;
	struct otx2_pool *pool;
	struct device *dev;
	int err;

	pool = pfvf->pool;
	dev = &pfvf->pdev->dev;

	/* Allocate memory for HW to update Aura count.
	 * Alloc one cache line, so that it fits all FC_STYPE modes.
	 */
	if (!pool->fc_addr) {
		err = qmem_alloc(dev, &pool->fc_addr, 1, OTX2_ALIGN);
		if (err)
			return err;
	}

	/* Initialize this aura's context via AF */
	aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!aq) {
		/* Shared mbox memory buffer is full, flush it and retry */
		err = otx2_sync_mbox_msg(&pfvf->mbox);
		if (err)
			return err;
		aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
		if (!aq)
			return -ENOMEM;
	}

	aq->aura_id = aura_id;
	/* Will be filled by AF with correct pool context address */
	aq->aura.pool_addr = pool_id;
	aq->aura.pool_caching = 1;
	aq->aura.shift = ilog2(numptrs) - 8;
	aq->aura.count = numptrs;
	aq->aura.limit = numptrs;
	aq->aura.avg_level = 255;
	aq->aura.ena = 1;
	aq->aura.fc_ena = 1;
	aq->aura.fc_addr = pool->fc_addr->iova;
	aq->aura.fc_hyst_bits = 0; /* Store count on all updates */

	/* Fill AQ info */
	aq->ctype = NPA_AQ_CTYPE_AURA;
	aq->op = NPA_AQ_INSTOP_INIT;

	return 0;
}

static int otx2_npa_pool_init(struct npa_dev_t *pfvf, u16 pool_id,
                          int stack_pages, int numptrs, int buf_size)
{
	struct npa_aq_enq_req *aq;
	struct otx2_pool *pool;
	struct device *dev;
	int err;

	dev = &pfvf->pdev->dev;
	pool = pfvf->pool;
	/* Alloc memory for stack which is used to store buffer pointers */
	err = qmem_alloc(dev, &pool->stack,
			stack_pages, pfvf->stack_pg_bytes);
	if (err)
		return err;

	pool->rbsize = buf_size;
	pool->rbpage_order = get_order(buf_size);

	/* Initialize this pool's context via AF */
	aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!aq) {
		/* Shared mbox memory buffer is full, flush it and retry */
		err = otx2_sync_mbox_msg(&pfvf->mbox);
		if (err) {
			qmem_free(dev, pool->stack);
			return err;
		}
		aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
		if (!aq) {
			qmem_free(dev, pool->stack);
			return -ENOMEM;
		}
	}

	aq->aura_id = pool_id;
	aq->pool.stack_base = pool->stack->iova;
	aq->pool.stack_caching = 1;
	aq->pool.ena = 1;
	aq->pool.buf_size = buf_size / 128;
	aq->pool.stack_max_pages = stack_pages;
	aq->pool.shift = ilog2(numptrs) - 8;
	aq->pool.ptr_start = 0;
	aq->pool.ptr_end = ~0ULL;

	/* Fill AQ info */
	aq->ctype = NPA_AQ_CTYPE_POOL;
	aq->op = NPA_AQ_INSTOP_INIT;

	return 0;
}

static inline u64 otx2_npa_blk_offset(u64 offset)
{
	offset &= ~(RVU_FUNC_BLKADDR_MASK << RVU_FUNC_BLKADDR_SHIFT);
	offset |= (BLKADDR_NPA << RVU_FUNC_BLKADDR_SHIFT);
	return offset;
}

/* Alloc pointer from pool/aura */
static inline u64 otx2_npa_aura_allocptr(struct npa_dev_t *pfvf, int aura)
{
	void __iomem *reg_addr = pfvf->mmio[REG_BAR].hw_addr;
	u64 *ptr = (u64 *)(reg_addr + otx2_npa_blk_offset(NPA_LF_AURA_OP_ALLOCX(0)));
	u64 incr = (u64)aura | BIT_ULL(63);

	return otx2_atomic64_add(incr, ptr);
}

u64 npa_alloc_buf(void)
{
	u64 iova;

	iova = otx2_npa_aura_allocptr(gnpa_pf_dev, 0);
	return iova;
}
EXPORT_SYMBOL(npa_alloc_buf);

u16 npa_pf_func(void)
{
	return gnpa_pf_dev->pcifunc;
}
EXPORT_SYMBOL(npa_pf_func);

/* Free pointer to a pool/aura */
static inline void otx2_npa_aura_freeptr(struct npa_dev_t *pfvf,
                                     int aura, s64 buf)
{
	void __iomem *reg_addr = pfvf->mmio[REG_BAR].hw_addr;

	reg_addr = reg_addr + otx2_npa_blk_offset(NPA_LF_AURA_OP_FREE0);
        otx2_write128((u64)buf, (u64)aura | BIT_ULL(63), reg_addr);
}

void otx2_npa_aura_pool_free(struct npa_dev_t *pfvf)
{
        struct otx2_pool *pool;
	struct device *dev;

        if (!pfvf->pool)
                return;

	dev = &pfvf->pdev->dev;
        pool = pfvf->pool;
        qmem_free(dev, pool->stack);
        qmem_free(dev, pool->fc_addr);
        devm_kfree(dev, pfvf->pool);
}

static int otx2_npa_aura_pool_init(struct npa_dev_t *pfvf, struct device *owner,
				int num_pools, int num_ptrs, int buf_size)
{
        struct otx2_pool *pool;
	struct device *dev;
	struct npa_lf_alloc_req  *npalf;
        int stack_pages, pool_id;
        int aura_cnt, err, ptr;
        s64 bufptr;

	dev = &pfvf->pdev->dev;
	pfvf->pool = devm_kzalloc(dev, sizeof(struct otx2_pool) * 1, GFP_KERNEL);

	npalf = otx2_mbox_alloc_msg_npa_lf_alloc(&pfvf->mbox);
        if (!npalf)
                return -ENOMEM;

        /* Set aura and pool counts */
        npalf->nr_pools = num_pools; /*TODO: should come as an srgument*/
        aura_cnt = ilog2(roundup_pow_of_two(npalf->nr_pools));
        npalf->aura_sz = (aura_cnt >= ilog2(128)) ? (aura_cnt - 6) : 1;

        err = otx2_sync_mbox_msg(&pfvf->mbox);
        if (err)
                return err;

        stack_pages =
                (num_ptrs + pfvf->stack_pg_ptrs - 1) / pfvf->stack_pg_ptrs;

        pool_id = 0;
        /* Initialize aura context */
        err = otx2_npa_aura_init(pfvf, pool_id, pool_id, num_ptrs);
        if (err)
                goto fail;
        err = otx2_npa_pool_init(pfvf, pool_id, stack_pages,
                             num_ptrs, buf_size);
        if (err)
                goto fail;

        /* Flush accumulated messages */
        err = otx2_sync_mbox_msg(&pfvf->mbox);
        if (err)
                goto fail;

        /* Allocate pointers and free them to aura/pool */
        pool = pfvf->pool;
        for (ptr = 0; ptr < num_ptrs; ptr++) {
                bufptr = otx2_alloc_npa_buf(pfvf, pool, GFP_KERNEL, owner);
                if (bufptr <= 0)
                        return bufptr;
                otx2_npa_aura_freeptr(pfvf, pool_id, bufptr);
        }
        otx2_get_page(pool);

        return 0;
fail:
        otx2_mbox_reset(&pfvf->mbox.mbox, 0);
        otx2_npa_aura_pool_free(pfvf);
        return err;
}

int npa_aura_pool_init(int pool_size, int buf_size, struct device *owner)
{
	return otx2_npa_aura_pool_init(gnpa_pf_dev, owner, 1, pool_size, buf_size);
}
EXPORT_SYMBOL(npa_aura_pool_init);

static int otx2_register_mbox_intr(struct npa_dev_t *npa_pf_dev, bool probe_af)
{
        struct msg_req *req;
	void __iomem *reg_addr = npa_pf_dev->mmio[REG_BAR].hw_addr;
	struct device *dev;
	struct rsrc_attach *attach;
	struct msg_req *msix;
        int err;

        /* Register mailbox interrupt handler */
        //irq_name = &hw->irq_name[RVU_PF_INT_VEC_AFPF_MBOX * NAME_SIZE];
        //snprintf(irq_name, NAME_SIZE, "RVUPFAF Mbox");
	dev = &npa_pf_dev->pdev->dev;
        err = request_irq(pci_irq_vector(npa_pf_dev->pdev, RVU_PF_INT_VEC_AFPF_MBOX),
                          otx2_pfaf_mbox_intr_handler, 0, "RVUPFAF Mbox", npa_pf_dev);
        if (err) {
                dev_err(dev,
                        "RVUPF: IRQ registration failed for PFAF mbox irq\n");
                return err;
        }

        /* Enable mailbox interrupt for msgs coming from AF.
         * First clear to avoid spurious interrupts, if any.
         */
	writeq(BIT_ULL(0), reg_addr + RVU_PF_INT);
	writeq(BIT_ULL(0), reg_addr + RVU_PF_INT_ENA_W1S);

        if (!probe_af)
                return 0;

        /* Check mailbox communication with AF */
        req = otx2_mbox_alloc_msg_ready(&npa_pf_dev->mbox);
        if (!req) {
                otx2_disable_mbox_intr(npa_pf_dev);
                return -ENOMEM;
        }

        err = otx2_sync_mbox_msg(&npa_pf_dev->mbox);
        if (err) {
                dev_warn(dev,
                         "AF not responding to mailbox, deferring probe\n");
                otx2_disable_mbox_intr(npa_pf_dev);
                return -EPROBE_DEFER;
        }

	otx2_mbox_lock(&npa_pf_dev->mbox);
        /* Get memory to put this msg */
        attach = otx2_mbox_alloc_msg_attach_resources(&npa_pf_dev->mbox);
        if (!attach) {
                otx2_mbox_unlock(&npa_pf_dev->mbox);
                return -ENOMEM;
        }

        attach->npalf = true;
        /* Send attach request to AF */
        err = otx2_sync_mbox_msg(&npa_pf_dev->mbox);
        if (err) {
                otx2_mbox_unlock(&npa_pf_dev->mbox);
                return err;
        }

	 /* Get NPA MSIX vector offsets */
        msix = otx2_mbox_alloc_msg_msix_offset(&npa_pf_dev->mbox);
        if (!msix) {
                otx2_mbox_unlock(&npa_pf_dev->mbox);
                return -ENOMEM;
        }

        err = otx2_sync_mbox_msg(&npa_pf_dev->mbox);
        if (err) {
                otx2_mbox_unlock(&npa_pf_dev->mbox);
                return err;
        }

	otx2_mbox_unlock(&npa_pf_dev->mbox);

        return 0;
}

static void otx2_pfaf_mbox_destroy(struct npa_dev_t *npa_pf_dev)
{
        struct mbox *mbox = &npa_pf_dev->mbox;

        if (npa_pf_dev->mbox_wq) {
                flush_workqueue(npa_pf_dev->mbox_wq);
                destroy_workqueue(npa_pf_dev->mbox_wq);
                npa_pf_dev->mbox_wq = NULL;
        }

        otx2_mbox_destroy(&mbox->mbox);
        otx2_mbox_destroy(&mbox->mbox_up);
}

static int otx2_npa_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct npa_dev_t *npa_pf_dev;
	struct device *dev;
	int err, num_vec;
	u32 val;

	dev = &pdev->dev;
	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return err;
	}

	if (pci_request_regions(pdev, DRV_NAME)) {
		printk("pci_request_regions failed\n");
		return -1;
	}

	pci_set_master(pdev);

	npa_pf_dev = vmalloc(sizeof(struct npa_dev_t));
	if (npa_pf_dev == NULL) {
		printk("Device allocation failed\n");
		goto err_release_regions;
	}
	gnpa_pf_dev = npa_pf_dev;

	memset(npa_pf_dev, 0, sizeof(struct npa_dev_t));
	pci_set_drvdata(pdev, npa_pf_dev);
	npa_pf_dev->pdev = pdev;

	num_vec = pci_msix_vec_count(pdev);
	//err = otx2_check_pf_usable(pf);
        //if (err) {
	//	 dev_err(&pdev->dev, "PF unusable\n");
        //        goto err_free_privdev;
	//}

	err = pci_alloc_irq_vectors(pdev, RVU_PF_INT_VEC_CNT,
                                    RVU_PF_INT_VEC_CNT, PCI_IRQ_MSIX);
        if (err < 0) {
                dev_err(&pdev->dev, "%s: Failed to alloc %d IRQ vectors\n",
                        __func__, num_vec);
                goto err_free_privdev;
        }

	npa_pf_dev->mmio[REG_BAR].start = pci_resource_start(pdev, REG_BAR);
        npa_pf_dev->mmio[REG_BAR].len = pci_resource_len(pdev, REG_BAR);
	npa_pf_dev->mmio[REG_BAR].hw_addr =
            ioremap_wc(npa_pf_dev->mmio[REG_BAR].start, npa_pf_dev->mmio[REG_BAR].len);
        npa_pf_dev->mmio[REG_BAR].mapped_len = npa_pf_dev->mmio[REG_BAR].len;
	printk("REG BAR %p\n", npa_pf_dev->mmio[REG_BAR].hw_addr);

	npa_pf_dev->mmio[MBOX_BAR].start = pci_resource_start(pdev, MBOX_BAR);
        npa_pf_dev->mmio[MBOX_BAR].len = pci_resource_len(pdev, MBOX_BAR);
	npa_pf_dev->mmio[MBOX_BAR].hw_addr =
            ioremap_wc(npa_pf_dev->mmio[MBOX_BAR].start, npa_pf_dev->mmio[MBOX_BAR].len);
        npa_pf_dev->mmio[MBOX_BAR].mapped_len = npa_pf_dev->mmio[MBOX_BAR].len;
	printk("MBOX BAR %p\n", npa_pf_dev->mmio[MBOX_BAR].hw_addr);

	err = otx2_pfaf_mbox_init(npa_pf_dev);
	if (err) {
		dev_err(&pdev->dev, "Mbox init failed\n");
                goto err_free_irq_vectors;
	}

	/* Register mailbox interrupt */
        err = otx2_register_mbox_intr(npa_pf_dev, true);
        if (err) {
		dev_err(&pdev->dev, "Registering MBOX interrupt failed\n");
                goto err_mbox_destroy;
	}

	npa_pf_dev->iommu_domain = iommu_get_domain_for_dev(dev);
        if (npa_pf_dev->iommu_domain)
                npa_pf_dev->iommu_domain_type =
                        ((struct iommu_domain *)npa_pf_dev->iommu_domain)->type;

	pci_read_config_dword(pdev, PCI_DEVID_OCTEONTX2_RVU_NPA_PF, &val);
	npa_pf_dev->hwcap = val;

	return 0;
err_mbox_destroy:
        otx2_pfaf_mbox_destroy(npa_pf_dev);
err_free_irq_vectors:
	pci_free_irq_vectors(npa_pf_dev->pdev);
err_free_privdev:
	iounmap(npa_pf_dev->mmio[REG_BAR].hw_addr);
	iounmap(npa_pf_dev->mmio[MBOX_BAR].hw_addr);
	pci_set_drvdata(pdev, NULL);
	vfree(npa_pf_dev);
err_release_regions:
	pci_release_regions(pdev);
	return err;
}

static int otx2_npa_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	return 0;
}

static void otx2_npa_remove(struct pci_dev *pdev)
{
	struct npa_dev_t *npa_pf_dev;

	npa_pf_dev = pci_get_drvdata(pdev);
	pci_set_drvdata(pdev, NULL);
	otx2_detach_resources(&npa_pf_dev->mbox);
	otx2_disable_mbox_intr(npa_pf_dev);
	otx2_pfaf_mbox_destroy(npa_pf_dev);
	pci_free_irq_vectors(pdev);
	vfree(npa_pf_dev);

	pci_release_regions(pdev);
}

static struct pci_driver otx2_pf_driver = {
	.name = DRV_NAME,
	.id_table = otx2_npa_pf_id_table,
	.probe = otx2_npa_probe,
	.shutdown = otx2_npa_remove,
	.remove = otx2_npa_remove,
	.sriov_configure = otx2_npa_sriov_configure
};

static int __init otx2_npa_rvupf_init_module(void)
{
	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	return pci_register_driver(&otx2_pf_driver);
}

static void __exit otx2_npa_rvupf_cleanup_module(void)
{
	pci_unregister_driver(&otx2_pf_driver);
}
module_init(otx2_npa_rvupf_init_module);
module_exit(otx2_npa_rvupf_cleanup_module);

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, otx2_npa_pf_id_table);
