/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OTX2_H__
#define __OTX2_H__

extern struct octep_ctrl_mbox otx2_mbox;

/* callback handler for processing mbox messages */
typedef int (*fn_otx2_process_req)(void *user_ctx,
				   struct octep_ctrl_mbox_msg *msg);

/* Initialize otx2 mbox, csr's etc.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_init(fn_otx2_process_req process_mbox, void *mbox_user_ctx);

/* Raise oei trig interrupt.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_raise_oei_trig_interrupt();

/* UnInitialize otx2 mbox, csr's etc.
 *
 * return value: 0 on success, -errno on failure.
 */
int otx2_uninit();

#endif /* __OTX2_H__ */
