/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* Octeontx/Octeontx2 Soc abstraction layer common implementation
 */

#ifndef _SOC_OTXX_H_
#define _SOC_OTXX_H_

#include <linux/iommu.h>

#include "dpi_vf.h"
#include "dma_api.h"

#if defined(CONFIG_OCTEONTX2_DPI_PF) || defined(CONFIG_OCTEONTX_DPI)

#define DPI_CHUNK_SIZE 	1024
#define DPI_NB_CHUNKS 	4096

#define OTXX_SOC_PRIV(dpi_vf)	((struct otxx_data *)dpi_vf->soc_priv)

/* This is common data for OCTEONTX and OCTEONTX2 soc's
 * Both soc's should have this as first member of their private data.
 */
struct otxx_data {
	u16 qid;
	u16 pool_size_m1;
	u16 index;
	u16 reserved;
	u64 dpi_buf;
	void *dpi_buf_ptr;
	spinlock_t queue_lock;
	struct iommu_domain *iommu_domain;
	int id;
	struct dma_pool *comp_buf_pool;
};

extern int otxx_init(void);

extern int otxx_open(struct dpivf_t *dpi_vf);

extern int otxx_dma_sync(struct dpivf_t* dpi_vf,
			 local_dma_addr_t local_dma_addr,
			 host_dma_addr_t host_addr, void *local_ptr,
			 int len, host_dma_dir_t dir);

extern int otxx_dma_async_vector(struct dpivf_t* dpi_vf,
				 local_dma_addr_t *local_addr,
				 host_dma_addr_t *host_addr, int *len,
				 int num_ptrs, host_dma_dir_t dir,
				 local_dma_addr_t comp_iova);

extern int otxx_dma_sync_sli(struct dpivf_t* dpi_vf,
			     host_dma_addr_t local_addr,
			     host_dma_addr_t host_addr,
			     void *virt_addr, int len,
			     host_dma_dir_t dir);

static inline void otxx_host_map_writel(struct dpivf_t* dpi_vf,
					host_dma_addr_t host_addr,
					uint32_t val)
{
	uint32_t *addr;

	addr = (uint32_t *)host_ioremap(host_addr);
	if (addr == NULL) {
		printk(KERN_ERR "ioremap failed\n");
		return;
	}
	writel(val, addr);
	host_iounmap(addr);
}

static inline void otxx_host_iounmap(struct dpivf_t* dpi_vf, void __iomem *addr)
{
	iounmap(addr);
}

extern void __iomem *otxx_host_ioremap(struct dpivf_t* dpi_vf,
				       host_dma_addr_t host_addr);

extern int otxx_close(struct dpivf_t *dpi_vf);

#else /* OCTEONTX/OCTEONTX2 are not configured so provide placeholder's */

static inline int otxx_init(void)
{
	return -EAGAIN;
}

static inline int otxx_open(struct dpivf_t *dpi_vf, struct otxx_data *priv)
{
	return -EAGAIN;
}

static inline int otxx_dma_sync(struct dpivf_t* dpi_vf,
				local_dma_addr_t local_dma_addr,
				host_dma_addr_t host_addr, void *local_ptr,
				int len, host_dma_dir_t dir)
{
	return -EAGAIN;
}

static inline int otxx_dma_async_vector(struct dpivf_t* dpi_vf,
					local_dma_addr_t *local_addr,
					host_dma_addr_t *host_addr, int *len,
					int num_ptrs, host_dma_dir_t dir,
					local_dma_addr_t comp_iova)
{
	return -EAGAIN;
}

static inline int otxx_dma_sync_sli(host_dma_addr_t local_addr,
				    host_dma_addr_t host_addr,
				    void *virt_addr, int len,
				    host_dma_dir_t dir)
{
	return -EAGAIN;
}

static inline void otxx_host_map_writel(host_dma_addr_t host_addr, uint32_t val)
{
}

static inline void otxx_host_iounmap(void __iomem *addr)
{
}

static inline int otxx_close(struct dpivf_t *dpi_vf, struct otxx_data)
{
	return -ENOMEM;
}

#endif

#endif /* _SOC_OTXX_H_ */
