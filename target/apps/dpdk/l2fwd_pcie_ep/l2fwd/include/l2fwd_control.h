/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __L2FWD_CONTROL_H__
#define __L2FWD_CONTROL_H__

/* Initialize control plane.
 *
 * Initialize local data for handling control messages.
 * Global configuration can be used for initialization.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_init(void);

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

/* UnInitialize control plane.
 *
 * UnInitialize local data for handling control messages.
 *
 * return value: 0 on success, -errno on failure.
 */
int l2fwd_control_uninit(void);

#endif /* __L2FWD_CONTROL_H__ */
