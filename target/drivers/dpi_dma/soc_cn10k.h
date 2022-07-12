/* Copyright (c) 2021 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* CN10K SoC abstraction layer implementation
 */

#ifndef _SOC_CN10K_H_
#define _SOC_CN10K_H_

#ifdef CONFIG_OCTEONTX2_DPI_PF

extern int cn10k_dma_to_host(struct dpivf_t *dpi_vf, uint32_t val,
			    host_dma_addr_t host_addr);
extern int cn10k_dma_sync(struct dpivf_t* dpi_vf,
			 local_dma_addr_t local_dma_addr,
			 host_dma_addr_t host_addr, void *local_ptr,
			 int len, host_dma_dir_t dir);

extern int cn10k_dma_async_vector(struct dpivf_t* dpi_vf,
				 local_dma_addr_t *local_addr,
				 host_dma_addr_t *host_addr, int *len,
				 int num_ptrs, host_dma_dir_t dir,
				 local_dma_addr_t comp_iova);

static inline void cn10k_fill_header(struct dpivf_t *dpi_vf,
				    union dpi_dma_instr_hdr_s *header,
				    uint8_t nfst, uint8_t nlst, u64 comp_iova,
				    uint8_t lport, host_dma_dir_t dir)
{
	/* member variable offsets are similar for cn96xx and cn98xx */
	header->cn10k.nfst = nfst;
	header->cn10k.nlst = nlst;
	header->cn10k.ptr = comp_iova;
	header->cn10k.lport = lport;
	header->cn10k.xtype = (dir == DMA_FROM_HOST) ?
			   DPI_HDR_XTYPE_E_INBOUND : DPI_HDR_XTYPE_E_OUTBOUND;
}

#else /* OCTEONTX2 is not configured so provide placeholder's */

#define cn10k_fill_header	soc_api_impl_fill_header

#endif
#endif
