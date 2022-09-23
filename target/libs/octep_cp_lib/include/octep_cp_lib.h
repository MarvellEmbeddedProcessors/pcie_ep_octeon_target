/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OCTEP_CP_LIB_H__
#define __OCTEP_CP_LIB_H__

#define OCTEP_CP_DOM_MAX			8
#define OCTEP_CP_PF_PER_DOM_MAX			128
#define OCTEP_CP_MSG_DESC_MAX			4

/* Supported event types */
enum octep_cp_event_type {
	OCTEP_CP_EVENT_TYPE_INVALID,
	OCTEP_CP_EVENT_TYPE_PERST,	/* from host */
	OCTEP_CP_EVENT_TYPE_FLR,	/* from host */
	OCTEP_CP_EVENT_TYPE_FW_READY,	/* from app */
	OCTEP_CP_EVENT_TYPE_HEARTBEAT,	/* from app */
	OCTEP_CP_EVENT_TYPE_MAX
};

struct octep_cp_event_info_perst {
	/* index of pcie mac domain */
	int dom_idx;
};

struct octep_cp_event_info_flr {
	/* index of pcie mac domain */
	int dom_idx;
	/* index of pf in pcie mac domain */
	int pf_idx;
	/* map of vf indices in pf. 1 bit per vf */
	uint64_t vf_mask[2];
};

struct octep_cp_event_info_fw_ready {
	/* index of pcie mac domain */
	int dom_idx;
	/* index of pf in pcie mac domain */
	int pf_idx;
	/* firmware ready true/false */
	int ready;
};

struct octep_cp_event_info_heartbeat {
	/* index of pcie mac domain */
	int dom_idx;
	/* index of pf in pcie mac domain */
	int pf_idx;
};

/* library configuration */
struct octep_cp_event_info {
	enum octep_cp_event_type e;
	union {
		struct octep_cp_event_info_perst perst;
		struct octep_cp_event_info_flr flr;
		struct octep_cp_event_info_fw_ready fw_ready;
		struct octep_cp_event_info_heartbeat hbeat;
	} u;
};

/* Information for sending messages */
union octep_cp_msg_info {
	uint64_t words[2];
	struct {
		uint16_t pem_idx:4;
		/* sender pf index 0-(n-1) */
		uint16_t pf_idx:9;
		uint16_t reserved:2;
		uint16_t is_vf:1;
		/* sender vf index 0-(n-1) */
		uint16_t vf_idx;
		/* message size */
		uint32_t sz;
		/* reserved */
		uint64_t reserved1;
	} s;
};

/* Message buffer */
struct octep_cp_msg_buf {
	uint32_t reserved1;
	uint16_t reserved2;
	/* size of buffer */
	uint16_t sz;
	/* pointer to message buffer */
	void *msg;
};

/* Message */
struct octep_cp_msg {
	/* Message info */
	union octep_cp_msg_info info;
	/* number of sg buffer's */
	int sg_num;
	/* message buffer's */
	struct octep_cp_msg_buf sg_list[OCTEP_CP_MSG_DESC_MAX];
};

/* pcie mac domain pf configuration */
struct octep_cp_pf_cfg {
	/* pcie mac domain pf index */
	int idx;
	/* Maximum supported message size to be filled by library */
	uint16_t max_msg_sz;
};

/* pcie mac domain configuration */
struct octep_cp_dom_cfg {
	/* pcie mac domain index */
	int idx;
	/* pf count */
	uint16_t npfs;
	/* pf indices */
	struct octep_cp_pf_cfg pfs[OCTEP_CP_PF_PER_DOM_MAX];
};

/* library configuration */
struct octep_cp_lib_cfg {
	/* Info to be filled by caller */
	/* number of pcie mac domains */
	uint16_t ndoms;
	/* configuration for pcie mac domains */
	struct octep_cp_dom_cfg doms[OCTEP_CP_DOM_MAX];
};

/* Initialize octep_cp library.
 *
 * Library will fill in information after initialization.
 *
 * @param cfg: [IN/OUT] non-null pointer to struct octep_cp_lib_cfg.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_init(struct octep_cp_lib_cfg *cfg);

/* Send response to received message.
 *
 * Total buffer size cannot exceed max_msg_sz in library configuration.
 *
 * @param ctx: [IN] Array of context handles received from a previous
 *             octep_cp_lib_recv_msg call.
 * @param msg: [IN] Array of non-null pointer to message.
 * @param num: [IN] Size of context and message array's.
 *
 * return value: number of messages sent on success, -errno on failure.
 */
int octep_cp_lib_send_msg_resp(uint64_t *ctx, struct octep_cp_msg *msg,
			       int num);

/* Send a new notification.
 *
 * Reply is not expected for this message.
 * Buffer size cannot exceed max_msg_sz in library configuration.
 *
 * @param msg: [IN] Message buffer.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_send_notification(struct octep_cp_msg* msg);

/* Receive a new message on given pem/pf.
 *
 * ctx received with the message should be used to send a response.
 *
 * @param ctx: [OUT] Array of context handles.
 *             Returned by library. Can be NULL for a notification.
 * @param msg: [IN/OUT] Array of non-null pointer to message.
 *             Caller should provide msg.sz, msg.sg_list[*].sz,
 *             msg.info.pem_idx and msg.info.pf_idx.
 * @param num: Size of context and message array's.
 *
 * return value: number of messages received on success, -errno on failure.
 */
int octep_cp_lib_recv_msg(uint64_t *ctx, struct octep_cp_msg *msg, int num);

/* Send event to host.
 *
 * Send a new event to host.
 *
 * @param info: [IN] Non-Null pointer to event info structure.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_send_event(struct octep_cp_event_info *info);

/* Receive events.
 *
 * Receive events such as flr, perst etc.
 *
 * @param info: [OUT] Non-Null pointer to event info array.
 * @param num: [IN] Number of event info buffers.
 *
 * return value: number of events received on success, -errno on failure.
 */
int octep_cp_lib_recv_event(struct octep_cp_event_info *info, int num);

/* Uninitialize cp library.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_uninit();


#endif /* __OCTEP_CP_LIB_H__ */
