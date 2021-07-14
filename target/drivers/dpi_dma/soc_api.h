/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* Soc abstraction layer
 */

#ifndef _SOC_API_H_
#define _SOC_API_H_

#include "dpi_vf.h"
#include "dma_api.h"
#include "dpi_cmd.h"

#include "soc_otx.h"
#include "soc_otx2.h"
#include "soc_cn10k.h"
#include "soc_otxx.h"

/* Supported soc's. Add entry for new soc
 */
enum soc_e {
	SOC_OCTEONTX,	/* 83xx */
	SOC_OCTEONTX2,  /* 98xx, 96xx, 95xx */
	SOC_CN10K,	/* cn10k family */
	SOC_MAX
};

/* These function handlers need to be implemented by each supported soc
 * Function pointer cannot be NULL, Atleast a default handler has to be
 * provided.
 */
struct vf_soc_ops {
	/* Initialize any global data/functionality */
	int (*init)(void);
	/* Open a dpi vf device and set it up */
	int (*open)(struct dpivf_t *dpi_vf);
	/* Allocate a buffer from internal buffer pool */
	u64 (*buf_alloc)(struct dpivf_t *dpi_vf);
	/* Free buffer back to buffer pool */
	void (*buf_free)(struct dpivf_t *dpi_vf, u64 buf);
	/* Fill dpi instruction header */
	void (*fill_header)(struct dpivf_t *dpi_vf,
			    union dpi_dma_instr_hdr_s *header,
			    uint8_t nfst, uint8_t nlst, u64 comp_iova,
			    uint8_t lport, host_dma_dir_t dir);
	/* Copy the data to host using dma */
	int (*dma_to_host)(struct dpivf_t *dpi_vf, uint32_t val,
			   host_dma_addr_t host_addr);
	/* Do a synchronous dma transfer */
	int (*dma_sync)(struct dpivf_t* dpi_vf,
			local_dma_addr_t local_dma_addr,
			host_dma_addr_t host_addr, void *local_ptr,
			int len, host_dma_dir_t dir);
	/* Do asynchronous dma transfer. Update comp_iova when
	 * transfer is done
	 */
	int (*dma_async_vector)(struct dpivf_t* dpi_vf,
				local_dma_addr_t *local_addr,
				host_dma_addr_t *host_addr, int *len,
				int num_ptrs, host_dma_dir_t dir,
				local_dma_addr_t comp_iova);
	/* Do dma transfer using sli */
	int (*dma_sync_sli)(struct dpivf_t* dpi_vf,
			    host_dma_addr_t local_addr,
			    host_dma_addr_t host_addr,
			    void *virt_addr, int len,
			    host_dma_dir_t dir);
	/* Copy data to host */
	void (*host_writel)(struct dpivf_t *dpi_vf, uint32_t val,
			    void __iomem *host_addr);
	/* Copy data to host using iomapped address */
	void (*host_map_writel)(struct dpivf_t* dpi_vf,
				host_dma_addr_t host_addr,
				uint32_t val);
	/* Unmap host iomem address */
	void (*host_iounmap)(struct dpivf_t* dpi_vf, void __iomem *addr);
	/* Remap host address to iomem address */
	void __iomem *(*host_ioremap)(struct dpivf_t* dpi_vf,
				      host_dma_addr_t host_addr);
	/* Close dpi vf and release resources */
	void (*close)(struct dpivf_t *dpi_vf);
	/* Release all global data and cleanup */
	void (*cleanup)(void);
};

extern struct vf_soc_ops *soc; /* Global list of supported soc's */
extern void* soc_priv; /* private data for active soc */

/* Following empty implementation of soc api can be used by soc's
 * as placeholders when they are not being configured in the kernel.
 */
static inline int soc_api_impl_init(void)
{
	return -ENOMEM;
}

static inline int soc_api_impl_open(struct dpivf_t *dpi_vf)
{
	return -ENOMEM;
}

static inline u64 soc_api_impl_buf_alloc(struct dpivf_t *dpi_vf)
{
	return 0;
}

static inline void soc_api_impl_buf_free(struct dpivf_t *dpi_vf, u64 buf)
{
}

static inline void soc_api_impl_fill_header(struct dpivf_t *dpi_vf,
					    union dpi_dma_instr_hdr_s *header,
					    uint8_t nfst, uint8_t nlst,
					    u64 comp_iova, uint8_t lport,
					    host_dma_dir_t dir)
{
}

static inline int soc_api_impl_dma_to_host(struct dpivf_t *dpi_vf,
					   uint32_t val,
					   host_dma_addr_t host_addr)
{
	return 0;
}

static inline void soc_api_impl_host_writel(struct dpivf_t *dpi_vf,
					    uint32_t val,
					    void __iomem *host_addr)
{
}

static inline void __iomem *soc_api_impl_host_ioremap(struct dpivf_t* dpi_vf,
						      host_dma_addr_t host_addr)
{
	return NULL;
}

static inline void soc_api_impl_close(struct dpivf_t *dpi_vf)
{
}

static inline void soc_api_impl_cleanup(void)
{
}

#endif /* _SOC_API_H_ */
