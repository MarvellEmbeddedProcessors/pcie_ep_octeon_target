/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/* Mgmt ethernet driver
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

enum board_type {
        CN83XX_BOARD,
        CN93XX_BOARD,
        UNKNOWN_BOARD
};

/* This is a copy of enum mv_facility_type
 * and should be kept in sync
 */
enum handle_type {
	HANDLE_TYPE_CONTROL,       /* Control module */
	HANDLE_TYPE_MGMT_NETDEV,   /* Management Netdev */
	HANDLE_TYPE_NW_AGENT,      /* Network Agent */
	HANDLE_TYPE_RPC,           /* RPC */
	HANDLE_TYPE_RAW,           /* RAW; to use in raw mode */
	/* Add new Facilities here */
	HANDLE_TYPE_COUNT          /* Number of facilities */
};

int get_dpi_dma_dev_count(int handle);
struct device *get_dpi_dma_dev(int handle, int index);
void host_writel(uint32_t val,  void __iomem *host_addr);
void host_map_writel(host_dma_addr_t host_addr, uint32_t val);
void __iomem *host_ioremap(host_dma_addr_t host_addr);
void host_iounmap(void __iomem *addr);
int dpi_vf_init(void);
void dpi_vf_cleanup(void);

int do_dma_sync(struct device *dev, host_dma_addr_t local_dma_addr,
		host_dma_addr_t host_dma_addr, void *local_virt_addr,
		int len, host_dma_dir_t dir);
int do_dma_sync_dpi(struct device *dev, host_dma_addr_t local_dma_addr,
		host_dma_addr_t host_addr, void *local_virt_addr,
		int len, host_dma_dir_t dir);
int do_dma_sync_sli(host_dma_addr_t local_dma_addr, host_dma_addr_t host_addr,
		void *local_virt_addr, int len, host_dma_dir_t dir);
int do_dma_async_dpi_vector(struct device* dev, local_dma_addr_t *local_addr,
		host_dma_addr_t *host_addr, int *len, int num_ptrs, host_dma_dir_t dir,
		local_dma_addr_t comp_iova);
int do_dma_to_host(uint32_t val, host_dma_addr_t host_addr);

#endif
