/* Copyright (c) 2022 Marvell.
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

#define DPI_DMA_CMD_BUF_SIZE 	64

static int lport = 0;

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

int cn10k_dma_sync(struct dpivf_t* dpi_vf,
		  local_dma_addr_t local_dma_addr,
		  host_dma_addr_t host_addr, void *local_ptr,
		  int len, host_dma_dir_t dir)
{
	struct otxx_data *otxx = OTXX_SOC_PRIV(dpi_vf);
	u8  *comp_data;
	dpi_dma_ptr_t sptr = {0};
	dpi_dma_ptr_t dptr = {0};
	u64 dpi_cmd[DPI_DMA_CMD_BUF_SIZE] = {0};
	union dpi_dma_instr_hdr_s *header;
	u64 iova, comp_iova;
	unsigned long time_start;
	int i;
	int ret = 0;

	comp_data = dma_pool_alloc(otxx->comp_buf_pool, GFP_ATOMIC, &comp_iova);
	if (comp_data == NULL) {
		dev_err(dpi_vf->dev, "dma alloc error\n");
		return -ENOMEM;
	}
	WRITE_ONCE(*comp_data,  0xFF);

	header = (union dpi_dma_instr_hdr_s *)&dpi_cmd[0];
	soc->fill_header(dpi_vf, header, 1, 1, comp_iova, lport, dir);

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

	if (dir == DMA_FROM_HOST) {
		sptr.s.ptr = host_addr;
		dptr.s.ptr = iova;
	} else {
		sptr.s.ptr = iova;
		dptr.s.ptr = host_addr;
	}

	sptr.s.length = len;
	dptr.s.length = len;
	dpi_cmd[4] = sptr.u[0];
	dpi_cmd[5] = sptr.u[1];
	dpi_cmd[6] = dptr.u[0];
	dpi_cmd[7] = dptr.u[1];

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

int cn10k_dma_async_vector(struct dpivf_t* dpi_vf,
			  local_dma_addr_t *local_addr,
			  host_dma_addr_t *host_addr, int *len,
			  int num_ptrs, host_dma_dir_t dir,
			  local_dma_addr_t comp_iova)
{
	struct otxx_data *otxx = OTXX_SOC_PRIV(dpi_vf);
	dpi_dma_ptr_t sptr[DPI_MAX_PTR] = {0};
	dpi_dma_ptr_t dptr[DPI_MAX_PTR] = {0};
	u64 dpi_cmd[DPI_DMA_CMD_BUF_SIZE] = {0};
	union dpi_dma_instr_hdr_s *header;
	u16 index = 4;
	int i;

	/* TODO check Sum(len[i]) < 64K */
	if (num_ptrs > DPI_MAX_PTR || num_ptrs < 1)
		return -EINVAL;

	header = (union dpi_dma_instr_hdr_s *)&dpi_cmd[0];
	soc->fill_header(dpi_vf, header, num_ptrs, num_ptrs, comp_iova,
			 lport, dir);

	for (i = 0; i < num_ptrs; i++) {
		if (dir == DMA_TO_HOST) {
			sptr[i].s.ptr = local_addr[i];
			dptr[i].s.ptr = host_addr[i];
		} else {
			sptr[i].s.ptr = host_addr[i];
			dptr[i].s.ptr = local_addr[i];
		}
		sptr[i].s.length = len[i];
		dptr[i].s.length = len[i];
	}

	/* inbound and outbound modes need local pointers followed by
	 * host pointers.
	 */
	for (i = 0; i < num_ptrs; i++) {
		dpi_cmd[index++] = sptr[i].u[0];
		dpi_cmd[index++] = sptr[i].u[1];
	}
	for (i = 0; i < num_ptrs; i++) {
		dpi_cmd[index++] = dptr[i].u[0];
		dpi_cmd[index++] = dptr[i].u[1];
	}

	dpi_dma_queue_write(dpi_vf, otxx, index, dpi_cmd);

	wmb();
	writeq_relaxed(index, dpi_vf->reg_base + DPI_VDMA_DBELL);
	return 0;
}

int cn10k_dma_to_host(struct dpivf_t *dpi_vf, uint32_t val,
		     host_dma_addr_t host_addr)
{
	/* TBD */
	return 0;
}
