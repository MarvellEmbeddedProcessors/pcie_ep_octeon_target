/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "octep_cp_lib.h"
#include "loop.h"
#include "app_config.h"

static volatile int force_quit = 0;
static volatile int perst = 0;
static int hb_interval = 0;
struct octep_cp_lib_cfg cp_lib_cfg = { 0 };

static timer_t tim;
static struct itimerspec itim = { 0 };

static int process_events()
{
#define MAX_EVENTS		6

	struct octep_cp_event_info e[MAX_EVENTS];
	int n, i;

	n = octep_cp_lib_recv_event(e, MAX_EVENTS);
	if (n < 0)
		return n;

	for (i = 0; i < n; i++) {
		if (e[i].e == OCTEP_CP_EVENT_TYPE_PERST) {
			printf("APP: Event: perst on dom[%d]\n",
			       e[i].u.perst.dom_idx);
			perst = 1;
		}
	}

	return 0;
}

static int send_heartbeat()
{
	struct octep_cp_event_info info;
	int i, j;

	info.e = OCTEP_CP_EVENT_TYPE_HEARTBEAT;
	for (i=0; i<cp_lib_cfg.ndoms; i++) {
		info.u.hbeat.dom_idx = cp_lib_cfg.doms[i].idx;
		for (j=0; j<cp_lib_cfg.doms[i].npfs; j++) {
			info.u.hbeat.pf_idx = cp_lib_cfg.doms[i].pfs[j].idx;
			octep_cp_lib_send_event(&info);
		}
	}

	return 0;
}

static void trigger_alarm(int hb_interval)
{
	itim.it_value.tv_sec = (hb_interval / 1000);
	itim.it_value.tv_nsec = (hb_interval % 1000) * 1000000;

	timer_settime(tim, 0, &itim, NULL);
}

void sigint_handler(int sig_num) {

	if (sig_num == SIGINT) {
		printf("APP: Program quitting.\n");
		force_quit = 1;
	} else if (sig_num == SIGALRM) {
		if (force_quit || perst)
			return;

		send_heartbeat();
		trigger_alarm(hb_interval);
	}
}

static int set_fw_ready(int ready)
{
	struct octep_cp_event_info info;
	int i, j;

	info.e = OCTEP_CP_EVENT_TYPE_FW_READY;
	info.u.fw_ready.ready = ready;
	for (i=0; i<cp_lib_cfg.ndoms; i++) {
		info.u.fw_ready.dom_idx = cp_lib_cfg.doms[i].idx;
		for (j=0; j<cp_lib_cfg.doms[i].npfs; j++) {
			info.u.fw_ready.pf_idx = cp_lib_cfg.doms[i].pfs[j].idx;
			octep_cp_lib_send_event(&info);
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int err = 0, src_i, src_j, dst_i, dst_j;
	struct pem_cfg *pem;
	struct pf_cfg *pf;

	if (argc < 2) {
		printf("APP: Provide path to config file.\n");
		return -EINVAL;
	}
	err = app_config_init(argv[1]);
	if (err)
		return err;

	signal(SIGINT, sigint_handler);
	signal(SIGALRM, sigint_handler);

	timer_create(CLOCK_REALTIME, NULL, &tim);
init:
	hb_interval = 0;
	cp_lib_cfg.ndoms = cfg.npem;
	dst_i = 0;
	for (src_i = 0; src_i < APP_CFG_PEM_MAX; src_i++) {
		pem = &cfg.pems[src_i];
		if (!pem->valid)
			continue;

		cp_lib_cfg.doms[dst_i].idx = src_i;
		cp_lib_cfg.doms[dst_i].npfs = pem->npf;
		dst_j = 0;
		for (src_j = 0; src_j < APP_CFG_PF_PER_PEM_MAX; src_j++) {
			pf = &pem->pfs[src_j];
			if (!pf->valid)
				continue;

			cp_lib_cfg.doms[dst_i].pfs[dst_j].idx = src_j;
			if (hb_interval == 0 ||
			    pf->fn.info.hb_interval < hb_interval)
				hb_interval = pf->fn.info.hb_interval;

			dst_j++;
		}
		dst_i++;
	}
	err = octep_cp_lib_init(&cp_lib_cfg);
	if (err)
		return err;

	app_config_update();
	err = loop_init();
	if (err) {
		octep_cp_lib_uninit();
		return err;
	}

	app_config_print();
	printf("APP: Heartbeat interval : %u msecs\n", hb_interval);

	set_fw_ready(1);
	trigger_alarm(hb_interval);
	while (!force_quit && !perst) {
		loop_process_msgs();
		process_events();
	}
	set_fw_ready(0);

	octep_cp_lib_uninit();
	loop_uninit();

	if (perst) {
		perst = 0;
		printf("\nAPP: Reinitializing...\n");
		goto init;
	}

	timer_delete(tim);
	app_config_uninit();

	return 0;
}
