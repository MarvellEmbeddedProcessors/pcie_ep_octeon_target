// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#ifndef _TARGET_ETHDEV_H_
#define _TARGET_ETHDEV_H_

//#define MTU_1500_SETTINGS
#define MTU_9600_SETTINGS

#define DPI_MAX_PTR_1500_MTU 15
#define DPI_MAX_PTR_9600_MTU 4

#define RECV_BUF_SIZE_1500_MTU 2048
#define RECV_BUF_SIZE_9600_MTU 12288

#ifdef MTU_1500_SETTINGS
#define OTXMN_MAX_MTU 1500
#define DPIX_MAX_PTR DPI_MAX_PTR_1500_MTU
#define OTXMN_RECV_BUF_SIZE RECV_BUF_SIZE_1500_MTU
#endif

#ifdef MTU_9600_SETTINGS
#define OTXMN_MAX_MTU 9600
#define DPIX_MAX_PTR DPI_MAX_PTR_9600_MTU
#define OTXMN_RECV_BUF_SIZE RECV_BUF_SIZE_9600_MTU
#endif




#define OTXMN_VERSION  "1.0.0"
#define	OTXMN_SERVICE_TASK_US 1000

/* max number of tx/rx queues we support */
#define OTXMN_MAXQ 1

static inline uint32_t otxmn_circq_add(uint32_t index, uint32_t add,
				       uint32_t mask)
{
	return (index + add) & mask;
}

static inline uint32_t otxmn_circq_inc(uint32_t index, uint32_t mask)
{
	return otxmn_circq_add(index, 1, mask);
}

static inline uint32_t otxmn_circq_depth(uint32_t pi, uint32_t ci,
					 uint32_t mask)
{
	return (pi - ci) & mask;
}

static inline uint32_t otxmn_circq_space(uint32_t pi, uint32_t ci,
					 uint32_t mask)
{
	return mask - otxmn_circq_depth(pi, ci, mask);
}

struct dpi_dma_cmd {
	local_dma_addr_t local_addr[DPIX_MAX_PTR];
	host_dma_addr_t host_addr[DPIX_MAX_PTR];
	int len[DPI_MAX_PTR];
	uint64_t *comp_data;
	local_dma_addr_t comp_iova;
	host_dma_dir_t dir;
	unsigned long start_time;
};

struct dma_compl {
	uint64_t *data;
	uint32_t cmd_idx;
};

struct otxmn_sw_descq {
	struct otxcn_hw_descq *hw_descq;
	void  *priv;
	struct napi_struct napi;
	spinlock_t lock;
	uint32_t local_cons_idx;
	uint32_t refill_prod_idx;
	uint32_t cmd_idx;
	uint32_t pending;
	uint32_t q_num;
	uint32_t __iomem *shadow_cons_idx_ioremap_addr;
	struct sk_buff **skb_list;
	local_dma_addr_t *dma_list;
	struct dpi_dma_cmd *cmd_list;
	struct dma_compl *comp_list;
	uint32_t mask;
	uint32_t num_entries;
	uint64_t pkts;
	uint64_t bytes;
	uint64_t errors;
	uint32_t hw_dma_qidx;
	uint64_t *comp_data;
	local_dma_addr_t comp_iova;
};

struct otxmn_dev {
	struct device *dma_dev;
	struct net_device *ndev;
	struct otxmn_sw_descq txqs[OTXMN_MAXQ];
	struct otxmn_sw_descq rxqs[OTXMN_MAXQ];
	bool  admin_up;
	uint8_t   *bar_map;
	uint32_t bar_map_size;
	uint32_t max_rxq;
	uint32_t num_rxq;
	uint32_t max_txq;
	uint32_t num_txq;
	struct workqueue_struct *mgmt_wq;
	struct delayed_work service_task;
	struct mutex mbox_lock;
	uint32_t send_mbox_id;
	uint32_t recv_mbox_id;
	uint8_t hw_addr[ETH_ALEN];
};
#endif /* _TARGET_ETHDEV_H_ */
