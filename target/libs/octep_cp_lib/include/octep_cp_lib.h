/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OCTEP_CP_LIB_H__
#define __OCTEP_CP_LIB_H__

/* Supported event types */
enum octep_cp_event {
	OCTEP_CP_EVENT_INVALID,
	OCTEP_CP_EVENT_MBOX,
	OCTEP_CP_EVENT_PERST
};

/* library configuration */
struct octep_cp_lib_cfg {
	/* callback handler for processing mbox request.
	 * return value = size of response data written in words.
	 */
	int (*msg_handler)(enum octep_cp_event e, void *user_ctx, void *msg);
	/* path to library configuration file */
	char cfg_file_path[256];
};

/* Initialize cp library.
 *
 * @param cfg: non-null pointer to struct octep_cp_lib_cfg.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_init(struct octep_cp_lib_cfg *cfg);

/* Poll for messages and events.
 *
 * struct octep_cp_lib_cfg.msg_handler has to be Non-NULL.
 *
 * return value: number of events processed, -errno on failure.
 */
int octep_cp_lib_poll();

/* Send heartbeat to host.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_send_heartbeat();

/* Uninitialize cp library.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_uninit();


#endif /* __OCTEP_CP_LIB_H__ */
