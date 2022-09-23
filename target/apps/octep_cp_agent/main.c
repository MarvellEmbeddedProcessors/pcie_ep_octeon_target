/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "octep_cp_lib.h"
#include "loop.h"
#include "app_config.h"

static volatile int force_quit = 0;
static volatile int perst = 0;

struct octep_cp_lib_cfg cp_lib_cfg = { 0 };

void sigint_handler(int sig_num) {

	if (sig_num == SIGINT) {
		printf("Program quitting.\n");
		force_quit = 1;
	} else if (sig_num == SIGUSR1) {
		struct octep_cp_event_info info;
		printf("Handling sigusr1.\n");
		octep_cp_lib_send_event(&info);
	}
}

int main(int argc, char *argv[])
{
	int err = 0, i, j;
	struct pem_cfg *pem;
	struct pf_cfg *pf;

	if (argc < 2) {
		printf("Provide path to config file.\n");
		return -EINVAL;
	}
	err = app_config_init(argv[1]);
	if (err)
		return err;

	signal(SIGINT, sigint_handler);
	signal(SIGUSR1, sigint_handler);

init:
	cp_lib_cfg.ndoms = cfg.npem;
	pem = cfg.pems;
	i = 0;
	while (pem) {
		cp_lib_cfg.doms[i].idx = pem->idx;
		cp_lib_cfg.doms[i].npfs = pem->npf;
		pf = pem->pfs;
		j = 0;
		while (pf) {
			cp_lib_cfg.doms[i].pfs[j++].idx = pf->idx;
			pf = pf->next;
		}
		pem = pem->next;
		i++;
	}
	err = octep_cp_lib_init(&cp_lib_cfg);
	if (err)
		return err;

	err = loop_init();
	if (err) {
		octep_cp_lib_uninit();
		return err;
	}

	while (!force_quit && !perst) {
		loop_process_msgs();
		sleep(1);
	}

	octep_cp_lib_uninit();
	loop_uninit();

	if (perst) {
		perst = 0;
		goto init;
	}

	app_config_uninit();

	return 0;
}
