/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/iommu.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>

#include "mmio_api.h"
#include "dpi.h"
#include "dpi_cmd.h"
#include "soc_api.h"
#include "soc_otxx.h"

#define MAX_PEM 		4
#define DPI_DMA_CMD_BUF_SIZE 	64

#define SLI0_M2S_MAC0_CTL	0x874001002100UL
#define SLI0_S2M_REGX_ACC(i)    (0x874001000000UL |  (i << 4))

union sli_s2m_op_s {
	uint64_t u64;
	struct {
		uint64_t addr       :32;
		uint64_t region     :8;
		uint64_t did_hi     :4;
		uint64_t node       :2;
		uint64_t rsvd2      :1;
		uint64_t io         :1;
		uint64_t rsvd1      :16;
	} s;
} __packed;

union sli0_s2m_regx_acc {
	uint64_t u64;
	struct {
		uint64_t ba         :32;
		uint64_t rsvd3      :8;
		uint64_t rtype      :2;
		uint64_t wtype      :2;
		uint64_t rsvd2      :4;
		uint64_t nmerge     :1;
		uint64_t epf        :3;
		uint64_t zero       :1;
		uint64_t ctype      :2;
		uint64_t rsvd1      :9;
	} s;
} __packed;

/* pem 1 and pem 3 do not support dpi */
static int lport[MAX_PEM] = { 0, -1, 1, -1 };

int otxx_init(void)
{
	union sli0_s2m_regx_acc  reg0_acc;
	uint64_t __iomem *reg;
	int i;

	reg = ioremap(SLI0_M2S_MAC0_CTL, 8);
	if (!reg) {
		printk(KERN_DEBUG "ioremap failed for addr 0x%lx\n",
		       SLI0_M2S_MAC0_CTL);
		return -ENOMEM;;
	}
	if (readq(reg) & (1UL << 17))
		writeq(readq(reg), reg);
	iounmap(reg);

	for (i = 0; i < 256; i++) {
		reg0_acc.u64 = 0;
		reg0_acc.s.nmerge = 1;
		reg0_acc.s.ba = i;
		reg = ioremap(SLI0_S2M_REGX_ACC(i), 8);
		if (!reg) {
			printk(KERN_DEBUG "ioremap failed for addr 0x%lx\n",
			       SLI0_S2M_REGX_ACC(i));
			return -ENOMEM;
		}
		writeq(reg0_acc.u64, reg);
		iounmap(reg);
	}

	return 0;
}

int otxx_open(struct dpivf_t *dpi_vf)
{
	struct otxx_data *otxx = OTXX_SOC_PRIV(dpi_vf);
	u64 *dpi_buf_ptr, dpi_buf, dpi_buf_phys;
	char comp_pool_name[32];

	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_EN);
	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_REQQ_CTL);

	otxx->iommu_domain = iommu_get_domain_for_dev(dpi_vf->dev);
	dpi_buf = soc->buf_alloc(dpi_vf);
	if (!dpi_buf) {
		dev_err(dpi_vf->dev, "Failed to allocate dpi buffer\n");
		return -ENOMEM;
	}

	dpi_buf_phys = dpi_buf;
	if (otxx->iommu_domain)
		dpi_buf_phys = iommu_iova_to_phys(otxx->iommu_domain, dpi_buf);
	dpi_buf_ptr = phys_to_virt(dpi_buf_phys);

	writeq_relaxed(((dpi_buf >> 7) << 7),
		       dpi_vf->reg_base + DPI_VDMA_SADDR);
	otxx->dpi_buf_ptr = dpi_buf_ptr;
	otxx->pool_size_m1 = (DPI_CHUNK_SIZE >> 3) - 2;
	otxx->dpi_buf = dpi_buf;
	spin_lock_init(&otxx->queue_lock);

	sprintf(comp_pool_name, "comp_buf_pool_%d_%d",
			dpi_vf->domain, dpi_vf->vf_id);
	otxx->comp_buf_pool = dma_pool_create(comp_pool_name, dpi_vf->dev,
						10 * 128, 128, 0);
	if (!otxx->comp_buf_pool) {
		dev_err(dpi_vf->dev, "Cannot allocate DMA buffer pool\n");
		soc->buf_free(dpi_vf, otxx->dpi_buf);
		otxx->dpi_buf = 0;
		return -ENOMEM;
	}

	/* Enabling DMA Engine */
	writeq_relaxed(0x1, dpi_vf->reg_base + DPI_VDMA_EN);
	return 0;
}

static int dpi_dma_queue_write(struct dpivf_t *dpi_vf, struct otxx_data *otxx,
			       u16 cmd_count, u64 *cmds)
{
	unsigned long flags;

	/* Normally there is plenty of
	 * room in the current buffer for the command
	 */
	spin_lock_irqsave(&otxx->queue_lock, flags);
	if (otxx->index + cmd_count < otxx->pool_size_m1) {
		u64 *ptr = otxx->dpi_buf_ptr;

		ptr += otxx->index;
		otxx->index += cmd_count;
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

		dpi_buf = soc->buf_alloc(dpi_vf);
		if (!dpi_buf) {
			spin_unlock_irqrestore(&otxx->queue_lock, flags);
			dev_err(dpi_vf->dev, "Failed to allocate");
			return -ENODEV;
		}

		if (otxx->iommu_domain)
			dpi_buf_phys = iommu_iova_to_phys(otxx->iommu_domain,
							  dpi_buf);
		else
			dpi_buf_phys = dpi_buf;

		new_buffer = phys_to_virt(dpi_buf_phys);

		ptr = otxx->dpi_buf_ptr;

		/* Figure out how many command words will fit in this buffer.
		 * One location will be needed for the next buffer pointer.
		 */
		count = otxx->pool_size_m1 - otxx->index;
		ptr += otxx->index;
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
		otxx->dpi_buf = dpi_buf;
		otxx->dpi_buf_ptr = new_buffer;
		otxx->index = cmd_count;
		ptr = new_buffer;
		while (cmd_count--)
			*ptr++ = *cmds++;
		/* queue index may greater than pool size */
		if (otxx->index >= otxx->pool_size_m1) {
			dpi_buf = soc->buf_alloc(dpi_vf);
			if (!dpi_buf) {
				spin_unlock_irqrestore(&otxx->queue_lock,
						       flags);
				dev_err(dpi_vf->dev, "Failed to allocate");
				return -ENODEV;
			}
			if (otxx->iommu_domain)
				dpi_buf_phys =
					iommu_iova_to_phys(otxx->iommu_domain,
							   dpi_buf);
			else
				dpi_buf_phys = dpi_buf;

			new_buffer = phys_to_virt(dpi_buf_phys);
			*ptr = dpi_buf;
			otxx->dpi_buf = dpi_buf;
			otxx->dpi_buf_ptr = new_buffer;
			otxx->index = 0;
		}
	}
	spin_unlock_irqrestore(&otxx->queue_lock, flags);

	return 0;
}

int otxx_dma_sync(struct dpivf_t* dpi_vf,
		  local_dma_addr_t local_dma_addr,
		  host_dma_addr_t host_addr, void *local_ptr,
		  int len, host_dma_dir_t dir)
{
	struct otxx_data *otxx = OTXX_SOC_PRIV(dpi_vf);
	u8  *comp_data;
	dpi_dma_buf_ptr_t cmd = {0};
	dpi_dma_ptr_t lptr = {0};
	dpi_dma_ptr_t hptr = {0};
	u64 dpi_cmd[DPI_DMA_CMD_BUF_SIZE] = {0};
	union dpi_dma_instr_hdr_s *header;
	u64 iova, comp_iova;
	unsigned long time_start;
	int i;
	int ret = 0;

	header = (union dpi_dma_instr_hdr_s *)&dpi_cmd[0];
	comp_data = dma_pool_alloc(otxx->comp_buf_pool,
				   GFP_ATOMIC, &comp_iova);
	if (comp_data == NULL) {
		dev_err(dpi_vf->dev, "dma alloc error\n");
		return -ENOMEM;
	}
	WRITE_ONCE(*comp_data,  0xFF);

	if (local_ptr) {
		if (dir == DMA_FROM_HOST)
			iova = dma_map_single(dpi_vf->dev, local_ptr, len,
					      DMA_FROM_DEVICE);
		else
			iova = dma_map_single(dpi_vf->dev, local_ptr, len,
					      DMA_TO_DEVICE);
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

	header->s.nfst = 1;
	header->s.nlst = 1;
	header->s.ptr = comp_iova;
	header->s.lport = lport[pem_num];

	if (header->s.xtype == DPI_HDR_XTYPE_E_INBOUND) {
		dpi_cmd[4] = cmd.wptr[0]->u[0];
		dpi_cmd[5] = cmd.wptr[0]->u[1];
		dpi_cmd[6] = cmd.rptr[0]->u[0];
		dpi_cmd[7] = cmd.rptr[0]->u[1];
	} else {
		header->s.nfst = cmd.rptr_cnt & 0xf;
		header->s.nlst = cmd.wptr_cnt & 0xf;
		dpi_cmd[4] = cmd.rptr[0]->u[0];
		dpi_cmd[5] = cmd.rptr[0]->u[1];
		dpi_cmd[6] = cmd.wptr[0]->u[0];
		dpi_cmd[7] = cmd.wptr[0]->u[1];
	}

	if (dpi_dma_queue_write(dpi_vf, otxx, 8, dpi_cmd)) {
		dev_err(dpi_vf->dev, "DMA queue write fail\n");
		ret = -1;
		goto err;
	}

	wmb();
	writeq_relaxed(8, dpi_vf->reg_base + DPI_VDMA_DBELL);
	/* Wait for the completion */
	time_start = jiffies;
	while (true) {
		if (READ_ONCE(*comp_data) != 0xFF)
			break;
		if (time_after(jiffies, (time_start + (1 * HZ)))) {
			dev_err(dpi_vf->dev, "DMA timed out\n");
			for (i = 0; i < 8; i++) {
				dev_err(dpi_vf->dev,"dpi_cmd[%d]: 0x%016llx\n",
				       i, dpi_cmd[i]);
			}
			ret = -1;
			goto err;
		}
	}
err:
	if (local_ptr) {
		if (dir == DMA_FROM_HOST)
			dma_unmap_single(dpi_vf->dev, iova, len,
					 DMA_FROM_DEVICE);
		else
			dma_unmap_single(dpi_vf->dev, iova, len, DMA_TO_DEVICE);
	}

	if (*comp_data != 0) {
		dev_err(dpi_vf->dev, "DMA failed with err %x\n", *comp_data);
		ret = -1;
	}
	dma_pool_free(otxx->comp_buf_pool, comp_data, comp_iova);
	return ret;
}

int otxx_dma_async_vector(struct dpivf_t* dpi_vf,
			  local_dma_addr_t *local_addr,
			  host_dma_addr_t *host_addr, int *len,
			  int num_ptrs, host_dma_dir_t dir,
			  local_dma_addr_t comp_iova)
{
	struct otxx_data *otxx = OTXX_SOC_PRIV(dpi_vf);
	dpi_dma_buf_ptr_t cmd = {0};
	dpi_dma_ptr_t lptr[DPI_MAX_PTR] = {0};
	dpi_dma_ptr_t hptr[DPI_MAX_PTR] = {0};
	u64 dpi_cmd[DPI_DMA_CMD_BUF_SIZE] = {0};
	union dpi_dma_instr_hdr_s *header;
	u16 index = 4;
	int i;

	header = (union dpi_dma_instr_hdr_s *)&dpi_cmd[0];

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

	header->s.ptr = comp_iova;
	header->s.lport = lport[pem_num];
	if (dir == DMA_FROM_HOST) {
		header->s.xtype = DPI_HDR_XTYPE_E_INBOUND;
		header->s.nfst = cmd.wptr_cnt & 0xf;
		header->s.nlst = cmd.rptr_cnt & 0xf;
		for (i = 0; i < header->s.nfst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
		for (i = 0; i < header->s.nlst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}
	} else {
		header->s.xtype = DPI_HDR_XTYPE_E_OUTBOUND;
		header->s.nfst = cmd.rptr_cnt & 0xf;
		header->s.nlst = cmd.wptr_cnt & 0xf;
		for (i = 0; i < header->s.nfst; i++) {
			dpi_cmd[index++] = cmd.rptr[i]->u[0];
			dpi_cmd[index++] = cmd.rptr[i]->u[1];
		}
		for (i = 0; i < header->s.nlst; i++) {
			dpi_cmd[index++] = cmd.wptr[i]->u[0];
			dpi_cmd[index++] = cmd.wptr[i]->u[1];
		}
	}

	dpi_dma_queue_write(dpi_vf, otxx, index, dpi_cmd);

	wmb();
	writeq_relaxed(index, dpi_vf->reg_base + DPI_VDMA_DBELL);
	return 0;
}

int otxx_dma_sync_sli(struct dpivf_t* dpi_vf,
		      host_dma_addr_t local_addr,
		      host_dma_addr_t host_addr,
		      void *virt_addr, int len,
		      host_dma_dir_t dir)
{
	void  __iomem *raddrp;
	void  __iomem *raddr;
	union sli_s2m_op_s  s2m_op;
	void  *laddr;
	int index;

	index = host_addr >> 32;
	if (index > 255) {
		dev_err(dpi_vf->dev, "phys addr too big 0x%llx\n", host_addr);
		return -1;
	}
	if (len > PAGE_SIZE) {
		dev_err(dpi_vf->dev, "len too big %d\n", len);
		return -1;
	}
	s2m_op.u64 = 0;
	s2m_op.s.region = index;
	s2m_op.s.io = 1;
	s2m_op.s.did_hi = 8;
	s2m_op.s.addr = host_addr & ((1UL << 32) - 1);
	laddr = virt_addr;
	raddrp = ioremap((s2m_op.u64 & (~(PAGE_SIZE - 1))), PAGE_SIZE);
	if (raddrp == NULL) {
		dev_err(dpi_vf->dev, "ioremap failed\n");
		return -1;
	}
	raddr = (uint8_t *)raddrp + (s2m_op.u64 & (PAGE_SIZE - 1));
	if (dir == DMA_TO_HOST)
		mmio_memwrite(raddr, laddr, len);
	if (dir == DMA_FROM_HOST)
		mmio_memread(laddr, raddr, len);
	iounmap(raddrp);

	return 0;
}

void __iomem *otxx_host_ioremap(struct dpivf_t* dpi_vf,
			        host_dma_addr_t host_addr)
{
	union sli_s2m_op_s s2m_op;
	void __iomem *raddrp;
	int index;

	index = host_addr >> 32;
	if (index > 255) {
		dev_err(dpi_vf->dev, "phys addr too big 0x%llx\n", host_addr);
		return NULL;
	}
	s2m_op.u64 = 0;
	s2m_op.s.region = index;
	s2m_op.s.io = 1;
	s2m_op.s.did_hi = 8;
	s2m_op.s.addr = host_addr & ((1UL << 32) - 1);
	raddrp = ioremap((s2m_op.u64 & (~(PAGE_SIZE - 1))), PAGE_SIZE);
	if (raddrp == NULL) {
		dev_err(dpi_vf->dev, "ioremap failed\n");
		return NULL;
	}
	return (raddrp + (s2m_op.u64 & (PAGE_SIZE - 1)));
}

int otxx_close(struct dpivf_t *dpi_vf)
{
	struct otxx_data *otxx = OTXX_SOC_PRIV(dpi_vf);

	/* Disable Engine */
	writeq_relaxed(0x0, dpi_vf->reg_base + DPI_VDMA_EN);

	if (otxx->dpi_buf) {
		soc->buf_free(dpi_vf, otxx->dpi_buf);
		otxx->dpi_buf = 0;
	}

	if (otxx->comp_buf_pool) {
		dma_pool_destroy(otxx->comp_buf_pool);
		otxx->comp_buf_pool = NULL;
	}

	return 0;
}
