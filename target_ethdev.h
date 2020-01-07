// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#ifndef _TARGET_ETHDEV_H_
#define _TARGET_ETHDEV_H_

#define OTXMN_MAX_MTU 9600
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

struct otxmn_sw_descq {
	struct otxcn_hw_descq *hw_descq;
	uint32_t local_prod_idx;
	uint32_t local_cons_idx;
	host_dma_addr_t shadow_cons_idx;
	struct sk_buff **skb_list;
	uint32_t mask;
	uint32_t num_entries;
	uint64_t pkts;
	uint64_t bytes;
	uint64_t errors;
	uint32_t hw_dma_qidx;
};

struct otxmn_dev {
	struct device *pdev;
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
};
#endif /* _TARGET_ETHDEV_H_ */
