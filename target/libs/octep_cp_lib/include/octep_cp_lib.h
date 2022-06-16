/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OCTEP_CP_LIB_H__
#define __OCTEP_CP_LIB_H__

/* Supported run modes */
enum octep_cp_mode {
	OCTEP_CP_MODE_LOOP,
	OCTEP_CP_MODE_NIC,
	OCTEP_CP_MODE_MAX
};

/* Supported event types */
enum octep_cp_event {
	OCTEP_CP_EVENT_INVALID,
	OCTEP_CP_EVENT_MBOX,
	OCTEP_CP_EVENT_PERST
};

/* library configuration */
struct octep_cp_lib_cfg {
	/* run mode */
	enum octep_cp_mode mode;
	/* callback handler for processing mbox request.
	 * library will continue to process request if return value != 0.
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
 * Handler will be called for each message/event.
 *
 * @param max_events: Max events to poll in one call.
 *
 * return value: number of events processed, -errno on failure.
 */
int octep_cp_lib_poll(int max_events);

/* Process user interrupt signal.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_process_sigusr1();

/* Uninitialize cp library.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_cp_lib_uninit();


#endif /* __OCTEP_CP_LIB_H__ */
