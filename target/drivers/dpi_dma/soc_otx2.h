/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* Octeontx2 Soc abstraction layer implementation
 */

#ifndef _SOC_OTX2_H_
#define _SOC_OTX2_H_

#ifdef CONFIG_OCTEONTX2_DPI_PF

extern int otx2_init(void);

extern int otx2_open(struct dpivf_t *dpi_vf);

extern u64 otx2_buf_alloc(struct dpivf_t *dpi_vf);

extern void otx2_buf_free(struct dpivf_t *dpi_vf, u64 buf);

static inline void otx2_fill_header(struct dpivf_t *dpi_vf,
				    union dpi_dma_instr_hdr_s *header,
				    uint8_t nfst, uint8_t nlst, u64 comp_iova,
				    uint8_t lport, host_dma_dir_t dir)
{
	/* member variable offsets are similar for cn96xx and cn98xx */
	header->cn98xx.nfst = nfst;
	header->cn98xx.nlst = nlst;
	header->cn98xx.ptr = comp_iova;
	header->cn98xx.lport = lport;
	header->cn98xx.xtype = (dir == DMA_FROM_HOST) ?
			   DPI_HDR_XTYPE_E_INBOUND : DPI_HDR_XTYPE_E_OUTBOUND;
}

extern int otx2_dma_to_host(struct dpivf_t *dpi_vf, uint32_t val,
			    host_dma_addr_t host_addr);

static inline void otx2_host_writel(struct dpivf_t *dpi_vf, uint32_t val,
				   void __iomem *host_addr)
{
	otx2_dma_to_host(dpi_vf, val, (host_dma_addr_t)host_addr);
}

static inline void __iomem *otx2_host_ioremap(struct dpivf_t* dpi_vf,
					      host_dma_addr_t host_addr)
{
	return (void  __iomem *)host_addr;
}

extern void otx2_close(struct dpivf_t *dpi_vf);

extern void otx2_cleanup(void);

#else /* OCTEONTX2 is not configured so provide placeholder's */

#define otx2_init		soc_api_impl_init
#define otx2_open		soc_api_impl_open
#define otx2_buf_alloc		soc_api_impl_buf_alloc
#define otx2_buf_free		soc_api_impl_buf_free
#define otx2_fill_header	soc_api_impl_fill_header
#define otx2_dma_to_host	soc_api_impl_dma_to_host
#define otx2_host_writel	soc_api_impl_host_writel
#define otx2_host_ioremap	soc_api_impl_host_ioremap
#define otx2_close		soc_api_impl_close
#define otx2_cleanup		soc_api_impl_cleanup

#endif

#endif /* _SOC_OTX2_H_ */
