/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include "compat.h"
#include "l2fwd_main.h"
#include "l2fwd_config.h"
#include "data.h"

#define RTE_LOGTYPE_L2FWD_DATA_NIC	RTE_LOGTYPE_USER1

/* data operations */
static struct data_ops *data_ops;

static uint16_t nic_tx_buffer_flush(uint16_t port, uint16_t queue, void *buffer)
{
	return rte_eth_tx_buffer_flush(port,
				       queue,
				       (struct rte_eth_dev_tx_buffer *)buffer);
}

static uint16_t nic_tx_buffer(uint16_t port, uint16_t queue, void *buffer,
			      void *tx_pkt)
{
	return rte_eth_tx_buffer(port,
				 queue,
				 (struct rte_eth_dev_tx_buffer *)buffer,
				 (struct rte_mbuf *)tx_pkt);
}

static int nic_rx_burst(uint16_t port, uint16_t queue, void **rx_pkts,
			const uint16_t nb_pkts)
{
	return rte_eth_rx_burst(port,
				queue,
				(struct rte_mbuf **)rx_pkts,
				nb_pkts);
}

static struct data_fn_ops nic_data_ops = {
	.rx_burst = nic_rx_burst,
	.tx_buffer = nic_tx_buffer,
	.tx_buffer_flush = nic_tx_buffer_flush,
};

int data_nic_init(struct data_ops *ops, struct data_fn_ops **fn_ops)
{
	*fn_ops = &nic_data_ops;
	data_ops = ops;

	return 0;
}

int data_nic_uninit(void)
{
	return 0;
}
