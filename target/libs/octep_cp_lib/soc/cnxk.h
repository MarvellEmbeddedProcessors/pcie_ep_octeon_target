/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CNXK_H__
#define __CNXK_H__

/* Initialize cnxk platform.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_init(struct octep_cp_lib_cfg *cfg);

/* Send response to received message.
 *
 * Total buffer size cannot exceed max_msg_sz in library configuration.
 *
 * @param ctx: [IN] Array of context handles received from a previous
 *             octep_cp_lib_recv_msg call.
 * @param msgs: [IN] Array of non-null pointer to message.
 * @param num: [IN] Size of context and message array's.
 *
 * return value: number of messages sent on success, -errno on failure.
 */
int cnxk_send_msg_resp(uint64_t *ctx, struct octep_cp_msg *msgs, int num);

/* Send a new notification.
 *
 * Reply is not expected for this message.
 * Buffer size cannot exceed max_msg_sz in library configuration.
 *
 * @param msg: [IN] Message buffer.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_send_notification(struct octep_cp_msg* msg);

/* Receive a new message on given pem/pf.
 *
 * ctx received with the message should be used to send a response.
 *
 * @param ctx: [OUT] Array of context handles.
 *             Returned by library. Can be NULL for a notification.
 * @param msgs: [IN/OUT] Array of non-null pointer to message.
 *             Caller should provide msg.sz, msg.sg_list[*].sz,
 *             msg.info.pem_idx and msg.info.pf_idx.
 * @param num: Size of context and message array's.
 *
 * return value: number of messages received on success, -errno on failure.
 */
int cnxk_recv_msg(uint64_t *ctx, struct octep_cp_msg *msgs, int num);

/* Send event to host.
 *
 * Send a new event to host.
 *
 * @param info: [IN] Non-Null pointer to event info structure.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_send_event(struct octep_cp_event_info *info);

/* Receive events.
 *
 * Receive events such as flr, perst etc.
 *
 * @param info: [OUT] Non-Null pointer to event info array.
 * @param num: [IN] Number of event info buffers.
 *
 * return value: number of events received on success, -errno on failure.
 */
int cnxk_recv_event(struct octep_cp_event_info *info, int num);

/* UnInitialize cnxk mbox, csr's etc.
 *
 * return value: 0 on success, -errno on failure.
 */
int cnxk_uninit();

#endif /* __CNXK_H__ */
