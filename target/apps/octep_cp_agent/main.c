/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "octep_cp_lib.h"
#include "loop.h"
#include "app_config.h"

static volatile int force_quit = 0;
static volatile int perst = 0;

void sigint_handler(int sig_num) {

	if (sig_num == SIGINT) {
		printf("Program quitting.\n");
		force_quit = 1;
	} else if (sig_num == SIGUSR1) {
		printf("Handling sigusr1.\n");
		octep_cp_lib_send_heartbeat();
	}
}

static int octep_cp_lib_msg_handler(enum octep_cp_event e,
				    void *user_ctx,
				    void *msg)
{
	if (e == OCTEP_CP_EVENT_PERST) {
		printf("Handling perst.\n");
		perst = 1;
		return 0;
	}

	return loop_process_msg(user_ctx, msg);
}

int main(int argc, char *argv[])
{
	struct octep_cp_lib_cfg cp_lib_cfg = { 0 };
	int err = 0;

	if (argc < 2) {
		printf("Provide path to config file.\n");
		return -EINVAL;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGUSR1, sigint_handler);

init:
	strncpy(cp_lib_cfg.cfg_file_path, argv[1], 255);
	err = loop_init(cp_lib_cfg.cfg_file_path);


	cp_lib_cfg.msg_handler = octep_cp_lib_msg_handler;
	err = octep_cp_lib_init(&cp_lib_cfg);
	if (err) {
		loop_uninit();
		return err;
	}

	while (!force_quit && !perst) {
		octep_cp_lib_poll(1);
		sleep(1);
	}

	octep_cp_lib_uninit();
	loop_uninit();

	if (perst) {
		perst = 0;
		goto init;
	}

	return 0;
}
