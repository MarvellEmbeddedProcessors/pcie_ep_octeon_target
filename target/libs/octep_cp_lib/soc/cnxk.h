/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CNXK_H__
#define __CNXK_H__

extern struct octep_ctrl_mbox mbox;

/* callback handler for processing mbox messages */
typedef int (*fn_cnxk_process_req)(void *user_ctx,
				   struct octep_ctrl_mbox_msg *msg);

/* Initialize cnxk mbox, csr's etc.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_init(fn_cnxk_process_req process_mbox, void *mbox_user_ctx);

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
