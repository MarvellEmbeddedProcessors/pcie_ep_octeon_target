/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_DATA_H__
#define __L2FWD_DATA_H__

/* Initialize data plane.
 *
 * Initialize local data for handling data packets.
 * Global configuration can be used for initialization.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_init(void);

/* Start data plane.
 *
 * Run packet forwarding on configured interfaces.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_start(void);

/* Stop data plane.
 *
 * Stop packet forwarding on configured interfaces.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_stop(void);

/* Start forwarding on given port.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_start_port(struct rte_pci_addr *dbdf);

/* Stop forwarding on given port.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_stop_port(struct rte_pci_addr *dbdf);

/* Clear forwarding table.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_clear_fwd_table(void);

/* Add an entry to forwarding table.
 *
 * Caller will have to make sure ports are not present in
 * table before adding.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_add_fwd_table_entry(struct rte_pci_addr *port1,
				   struct rte_pci_addr *prot2);

/* Delete an entry from forwarding table.
 *
 * If the entry is active, then it will be stopped before deleting.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_del_fwd_table_entry(struct rte_pci_addr *port1,
				   struct rte_pci_addr *port2);

/* Print statistics.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_print_stats(void);

/* Update offloads for forwarding interfaces.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_update_offloads(int pem_idx, int pf_idx, int vf_idx);

/* UnInitialize data plane.
 *
 * UnInitialize local data.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_data_uninit(void);

#endif /* __L2FWD_DATA_H__ */
