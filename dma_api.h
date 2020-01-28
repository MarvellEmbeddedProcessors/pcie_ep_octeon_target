// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#ifndef _DMA_API_H_
#define _DMA_API_H_

typedef uint64_t host_dma_addr_t;
typedef enum {
	DMA_TO_HOST,
	DMA_FROM_HOST
} host_dma_dir_t;

struct device *get_dpi_dma_dev(void);
void host_writel(host_dma_addr_t host_addr, uint32_t val);
int dpi_vf_init(void);
void dpi_vf_cleanup(void);

int do_dma_sync(host_dma_addr_t local_dma_addr, host_dma_addr_t host_dma_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
int do_dma_sync_dpi(host_dma_addr_t local_dma_addr, host_dma_addr_t host_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
int do_dma_sync_sli(host_dma_addr_t local_dma_addr, host_dma_addr_t host_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
#endif
