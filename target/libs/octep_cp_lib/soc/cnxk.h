/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CNXK_H__
#define __CNXK_H__

extern struct octep_ctrl_mbox mbox;

/* callback handler for processing mbox messages */
typedef int (*fn_cnxk_process_req)(void *user_ctx,
				    struct octep_ctrl_mbox_msg *msg);

/* Initialize cnxk platform.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_init(struct octep_cp_lib_cfg *cfg);

/* Poll for a new message.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_msg_poll();

/* Send heartbeat to host.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_send_heartbeat();

/* Raise oei trig interrupt.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_raise_oei_trig_interrupt();

/* UnInitialize cnxk mbox, csr's etc.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_uninit();

#endif /* __CNXK_H__ */
