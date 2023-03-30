/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_CONTROL_H__
#define __L2FWD_CONTROL_H__

/* operations to be used by the api server */
struct l2fwd_control_ops {
	/* Called by control before resetting pem */
	void (*on_before_pem_reset)(int pem);
	/* Called by control after resetting pem */
	void (*on_after_pem_reset)(int pem);
	/* Called by control before resetting pf */
	void (*on_before_pf_reset)(int pem, int pf);
	/* Called by control after resetting vf */
	void (*on_after_pf_reset)(int pem, int pf);
	/* Called by control before resetting vf */
	void (*on_before_vf_reset)(int pem, int pf, int vf);
	/* Called by control after resetting vf */
	void (*on_after_vf_reset)(int pem, int pf, int vf);
};

/* Initialize control plane.
 *
 * Initialize local data for handling control messages.
 * Global configuration can be used for initialization.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_init(struct l2fwd_control_ops *ops);

/* Process SIGALRM.
 *
 * Do processing in SIGALRM signal handler.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_handle_alarm(void);

/* Poll for control messages and events.
 *
 * Poll and process control messages and events.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_poll(void);

/* Set all to_wire interfaces to stub
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_clear_port_mapping(void);

/* Update to_wire interface for given to_host interface.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_set_port_mapping(const struct rte_pci_addr *port1,
				   const struct rte_pci_addr *port2);

/* UnInitialize control plane.
 *
 * UnInitialize local data for handling control messages.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_uninit(void);

#endif /* __L2FWD_CONTROL_H__ */
