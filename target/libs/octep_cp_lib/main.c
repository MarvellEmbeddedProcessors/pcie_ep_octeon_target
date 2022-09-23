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
/* soc operations */
static struct cp_lib_soc_ops *sops = NULL;

__attribute__((visibility("default")))
int octep_cp_lib_init(struct octep_cp_lib_cfg *cfg)
{
	int err;

	CP_LIB_LOG(INFO, LIB, "init\n");
	if (state >= CP_LIB_STATE_INIT)
		return 0;

	err = soc_get_ops(&sops);
	if (err || !sops)
		return -ENAVAIL;

	memset(&user_cfg, 0, sizeof(struct octep_cp_lib_cfg));
	state = CP_LIB_STATE_INIT;
	err = sops->init(cfg);
	if (err) {
		state = CP_LIB_STATE_INVALID;
		return err;
	}
	state = CP_LIB_STATE_READY;
	user_cfg = *cfg;

	return 0;
}

__attribute__((visibility("default")))
int octep_cp_lib_send_msg_resp(uint64_t *ctx, struct octep_cp_msg *msgs,
			       int num)
{
	//CP_LIB_LOG(INFO, LIB, "send message response\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	if (!ctx || !msgs || num <= 0)
		return -EINVAL;

	return sops->send_msg_resp(ctx, msgs, num);
}

__attribute__((visibility("default")))
int octep_cp_lib_send_notification(struct octep_cp_msg* msg)
{
	CP_LIB_LOG(INFO, LIB, "send notification\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	if (!msg)
		return -EINVAL;

	return sops->send_notification(msg);
}

__attribute__((visibility("default")))
int octep_cp_lib_recv_msg(uint64_t *ctx, struct octep_cp_msg *msgs, int num)
{
	//CP_LIB_LOG(INFO, LIB, "receive message\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	if (!ctx || !msgs || num <= 0)
		return -EINVAL;

	return sops->recv_msg(ctx, msgs, num);
}

__attribute__((visibility("default")))
int octep_cp_lib_send_event(struct octep_cp_event_info *info)
{
	CP_LIB_LOG(INFO, LIB, "send event\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	if (!info)
		return -EINVAL;

	return sops->send_event(info);
}

__attribute__((visibility("default")))
int octep_cp_lib_recv_event(struct octep_cp_event_info *info, int num)
{
	CP_LIB_LOG(INFO, LIB, "receive event\n");

	if (state != CP_LIB_STATE_READY)
		return -EAGAIN;

	if (!info || num <= 0)
		return -EINVAL;

	return sops->recv_event(info, num);
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
	sops = NULL;
	state = CP_LIB_STATE_INVALID;

	return 0;
}
