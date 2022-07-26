/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "octep_cp_lib.h"
#include "cp_log.h"
#include "cp_lib.h"

/* operating state */
volatile enum cp_lib_state state = CP_LIB_STATE_INVALID;
/* user configuration */
struct octep_cp_lib_cfg user_cfg = {0};
/* library configuration */
struct cp_lib_cfg cfg = {0};
/* soc operations */
static struct cp_lib_soc_ops *sops = NULL;

__attribute__((visibility("default")))
int octep_cp_lib_init(struct octep_cp_lib_cfg *p_cfg)
{
	int err;

	CP_LIB_LOG(INFO, LIB, "init\n");
	if (state >= CP_LIB_STATE_INIT)
		return 0;

	err = soc_get_ops(p_cfg->mode, &sops);
	if (err || !sops)
		return -ENAVAIL;

	err = config_parse_file(p_cfg->cfg_file_path);
	if (err) {
		memset(&cfg, 0, sizeof(struct cp_lib_cfg));
		return -EINVAL;
	}

	user_cfg = *p_cfg;
	state = CP_LIB_STATE_INIT;
	err = sops->init(p_cfg);
	if (err) {
		memset(&user_cfg, 0, sizeof(struct octep_cp_lib_cfg));
		memset(&cfg, 0, sizeof(struct cp_lib_cfg));
		state = CP_LIB_STATE_INVALID;
		return err;
	}
	state = CP_LIB_STATE_READY;

	return 0;
}

__attribute__((visibility("default")))
int octep_cp_lib_poll(int max_events)
{
	//CP_LIB_LOG(INFO, LIB, "poll\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	return sops->poll(max_events);
}

__attribute__((visibility("default")))
int octep_cp_lib_process_sigusr1()
{
	CP_LIB_LOG(INFO, LIB, "sigusr1\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	return sops->process_sigusr1();
}

__attribute__((visibility("default")))
int octep_cp_lib_uninit()
{
	CP_LIB_LOG(INFO, LIB, "uninit\n");

	if (state == CP_LIB_STATE_UNINIT || state == CP_LIB_STATE_INVALID)
		return 0;

	state = CP_LIB_STATE_UNINIT;
	sops->uninit();
	memset(&user_cfg, 0, sizeof(struct octep_cp_lib_cfg));
	memset(&cfg, 0, sizeof(struct cp_lib_cfg));
	sops = NULL;
	state = CP_LIB_STATE_INVALID;

	return 0;
}
