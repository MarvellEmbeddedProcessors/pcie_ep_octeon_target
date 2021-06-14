/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* Octeontx Soc abstraction layer implementation
 */

#ifndef _SOC_OTX_H_
#define _SOC_OTX_H_

#ifdef CONFIG_OCTEONTX_DPI

#include <linux/iommu.h>

extern int otx_init(void);

extern int otx_open(struct dpivf_t *dpi_vf);

extern u64 otx_buf_alloc(struct dpivf_t *dpi_vf);

extern void otx_buf_free(struct dpivf_t *dpi_vf, u64 buf);

static inline int otx_dma_to_host(struct dpivf_t *dpi_vf, uint32_t val,
				  host_dma_addr_t host_addr)
{
	return 0;
}

static inline void otx_host_writel(struct dpivf_t *dpi_vf, uint32_t val,
				   void __iomem *host_addr)
{
	writel(val, host_addr);
}

extern void otx_close(struct dpivf_t *dpi_vf);

extern void otx_cleanup(void);

#else /* OCTEONTX is not configured so provide placeholder's */

#define otx_init		soc_api_impl_init
#define otx_open		soc_api_impl_open
#define otx_buf_alloc		soc_api_impl_buf_alloc
#define otx_buf_free		soc_api_impl_buf_free
#define otx_dma_to_host		soc_api_impl_dma_to_host
#define otx_host_writel		soc_api_impl_host_writel
#define otx_host_ioremap	soc_api_impl_host_ioremap
#define otx_close		soc_api_impl_close
#define otx_cleanup		soc_api_impl_cleanup

#endif

#endif /* _SOC_OTX_H_ */
