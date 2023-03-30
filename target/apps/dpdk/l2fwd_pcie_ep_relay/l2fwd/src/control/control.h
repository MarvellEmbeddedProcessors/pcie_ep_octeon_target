/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CONTROL_H__
#define __CONTROL_H__

/* fn ops */
struct control_fn_ops {
	int (*get_mtu)(int pem, int pf, int vf, uint16_t *mtu);
	int (*set_mtu)(int pem, int pf, int vf, uint16_t mtu);
	int (*get_mac)(int pem, int pf, int vf, uint8_t *addr);
	int (*set_mac)(int pem, int pf, int vf, uint8_t *addr);
	int (*get_link_state)(int pem, int pf, int vf, uint16_t *state);
	int (*set_link_state)(int pem, int pf, int vf, uint16_t state);
	int (*get_rx_state)(int pem, int pf, int vf, uint16_t *state);
	int (*set_rx_state)(int pem, int pf, int vf, uint16_t state);
	int (*get_link_info)(int pem, int pf, int vf, struct octep_ctrl_net_link_info *info);
	int (*set_link_info)(int pem, int pf, int vf, struct octep_ctrl_net_link_info *info);
	int (*reset)(int pem, int pf, int vf);
	int (*set_port)(int pem, int pf, int vf, const struct rte_pci_addr *port);
};

/** Stub/Virtual ctrl_net interface operations */

/* Initialize Stub implementation.
 *
 * Initialize local data for handling control messages.
 * Global configuration and control data can be used for initialization.
 * Fill in the function handlers in ops.
 *
 * @param ops: [IN/OUT] non-null pointer to struct control_fn_ops
 *
 * return value: 0 on success, -errno on failure.
 */
int ctrl_stub_init(struct control_fn_ops **ops);

/* Uninitialize Stub implementation.
 *
 * Uninitialize local data.
 *
 * return value: 0 on success, -errno on failure.
 */
int ctrl_stub_uninit(void);


/** NIC/Real ctrl_net interface operations */

/* Initialize NIC implementation.
 *
 * Initialize local data for handling control messages.
 * Global configuration and control data can be used for initialization.
 * Fill in the function handlers in ops.
 *
 * @param ops: [IN/OUT] non-null pointer to struct control_fn_ops
 *
 * return value: 0 on success, -errno on failure.
 */
int ctrl_nic_init(struct control_fn_ops **ops);

/* Uninitialize NIC implementation.
 *
 * Uninitialize local data.
 *
 * return value: 0 on success, -errno on failure.
 */
int ctrl_nic_uninit(void);

#endif /* __CONTROL_H__ */
