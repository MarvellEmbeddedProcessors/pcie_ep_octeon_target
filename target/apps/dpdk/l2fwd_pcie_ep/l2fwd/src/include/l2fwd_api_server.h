/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_API_SERVER_H__
#define __L2FWD_API_SERVER_H__

#define L2FWD_API_SERVER_PORT	8888

/* operations to be used by the api server */
struct l2fwd_api_server_ops {
	/* Start/Stop data plane and any dependencies */
	int (*set_fwd_state)(int state);
	/* Clear data plane table and any dependencies */
	int (*clear_fwd_table)();
	/* Add an entry to forwarding table. */
	int (*add_fwd_table_entry)(struct rte_pci_addr *port1,
				   struct rte_pci_addr *port2,
				   struct rte_ether_addr *mac);
	/* Delete an entry from forwarding table. */
	int (*del_fwd_table_entry)(struct rte_pci_addr *port1,
				   struct rte_pci_addr *port2);
};

/* Initialize api server.
 *
 * Initialize local data for handling api messages.
 * Global configuration can be used for initialization.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_init(struct l2fwd_api_server_ops *ops);

/* Start api server.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_start(void);

/* Stop api server.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_stop(void);

/* Uninitialize api server.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_api_server_uninit(void);

#endif /* __L2FWD_API_SERVER_H__ */
