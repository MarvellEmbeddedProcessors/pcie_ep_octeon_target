/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __DATA_H__
#define __DATA_H__

#define MAX_PKT_BURST		32

/* operations to be used by the function */
struct data_ops {
	void (*drop_pkt)(uint16_t port, void *m, uint16_t queue);
};

/* operations to be implemented by the function */
struct data_fn_ops {
	/* Receive up to nb_pkts from queue on port
	 * return number of packets received or -errno
	 */
	int (*rx_burst)(uint16_t port, uint16_t queue, void **rx_pkts,
			const uint16_t nb_pkts);
	/* enqueue tx_pkt to buffer for pqueue on port
	 * return 0 if packet was buffered,
	 *        > 0 if packet was buffered and queue was flushed
	 */
	uint16_t (*tx_buffer)(uint16_t port, uint16_t queue,
			      void *buffer, void *tx_pkt);
	/* Send any packets queued up for transmission on
	 * a port and HW queue
	 */
	uint16_t (*tx_buffer_flush)(uint16_t port, uint16_t queue,
				    void *buffer);
};

/* Per-port statistics struct */
struct data_port_statistics {
	uint64_t tx;
	uint64_t rx;
	uint64_t dropped;
} __rte_cache_aligned;

/* forwarding table entry */
struct data_port_fwd_info {
	/* port fn ops */
	struct data_fn_ops *fn_ops;
	/* forwarding port id */
	unsigned int dst;
	/* port supports offloads */
	bool offload;
	/* this entry is active */
	bool running;
	/* The link state of the external port */
	bool wire_link_state;
};

/* tx buffers */
extern struct rte_eth_dev_tx_buffer *tx_buffer[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];

/* port statistics */
extern struct data_port_statistics port_stats[RTE_MAX_ETHPORTS][RTE_MAX_LCORE];

/* port forwarding table, last entry is stub */
extern struct data_port_fwd_info data_fwd_table[RTE_MAX_ETHPORTS + 1];

/* number of tx queues per port, to be decided by polling method */
extern uint16_t num_tx_queues;

/* number of rx queues per port, to be decided by polling method */
extern uint16_t num_rx_queues;

/** Stub/Virtual data interface operations */

/* Initialize Stub implementation.
 *
 * Initialize local data for handling data packets.
 * Global configuration can be used for initialization.
 * Fill in the function handlers in ops.
 *
 * @param ops: [IN] non-null pointer to struct data_ops
 * @param fn_ops: [IN/OUT] non-null pointer to struct data_fn_ops
 *
 * return value: 0 on success, -errno on failure.
 */
int data_stub_init(struct data_ops *ops, struct data_fn_ops **fn_ops);

/* Uninitialize Stub implementation.
 *
 * Uninitialize local data.
 *
 * return value: 0 on success, -errno on failure.
 */
int data_stub_uninit(void);


/** NIC/Real ctrl_net interface operations */

/* Initialize NIC implementation.
 *
 * Initialize local data for handling data packets.
 * Global configuration can be used for initialization.
 * Fill in the function handlers in ops.
 *
 * @param ops: [IN] non-null pointer to struct data_ops
 * @param ops: [IN/OUT] non-null pointer to struct data_fn_ops
 *
 * return value: 0 on success, -errno on failure.
 */
int data_nic_init(struct data_ops *ops, struct data_fn_ops **fn_ops);

/* Uninitialize NIC implementation.
 *
 * Uninitialize local data.
 *
 * return value: 0 on success, -errno on failure.
 */
int data_nic_uninit(void);

/* Check if port is configured for forwarding.
 *
 * @param port: [IN] port
 *
 * return value: 1 on success, 0 on failure.
 */
int data_port_is_configured(unsigned int port);

#endif /* __DATA_H__ */
