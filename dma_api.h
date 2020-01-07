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
void do_dma_sync(void *virt_addr, host_dma_addr_t host_addr, int len,
		host_dma_dir_t dir);
void host_writel(host_dma_addr_t host_addr, uint32_t val);
#endif
