// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#ifndef _DMA_API_H_
#define _DMA_API_H_

typedef uint64_t host_dma_addr_t;
typedef uint64_t local_dma_addr_t;
#define DPI_MAX_PTR 15
typedef enum {
	DMA_TO_HOST,
	DMA_FROM_HOST
} host_dma_dir_t;

struct device *get_dpi_dma_dev(void);
void host_writel(host_dma_addr_t host_addr, uint32_t val);
uint32_t __iomem *host_ioremapl(host_dma_addr_t host_addr);
void host_iounmap(void __iomem *addr);
int dpi_vf_init(void);
void dpi_vf_cleanup(void);

int do_dma_sync(host_dma_addr_t local_dma_addr, host_dma_addr_t host_dma_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
int do_dma_sync_dpi(host_dma_addr_t local_dma_addr, host_dma_addr_t host_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
int do_dma_sync_sli(host_dma_addr_t local_dma_addr, host_dma_addr_t host_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
int do_dma_async_dpi_vector(local_dma_addr_t *local_addr, host_dma_addr_t *host_addr,
		            int *len, int num_ptrs, host_dma_dir_t dir, local_dma_addr_t comp_iova);



#endif
