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

/* Control plane version */
#define CP_VERSION_MAJOR		1
#define CP_VERSION_MINOR		0
#define CP_VERSION_VARIANT		0

#define CP_VERSION_CURRENT		(OCTEP_CP_VERSION(CP_VERSION_MAJOR, \
							  CP_VERSION_MINOR, \
							  CP_VERSION_VARIANT))

static volatile int force_quit;
static volatile int perst[APP_CFG_PEM_MAX] = { 0 };
static int hb_interval;
struct octep_cp_lib_cfg cp_lib_cfg = { 0 };

static timer_t tim;
static struct itimerspec itim = { 0 };
static int app_handle_perst(int dom_idx);

static int process_events(void)
{
#define MAX_EVENTS		6

	struct octep_cp_event_info e[MAX_EVENTS];
	int n, i, err;

	n = octep_cp_lib_recv_event(e, MAX_EVENTS);
	if (n < 0)
		return n;

	for (i = 0; i < n; i++) {
		if (e[i].e == OCTEP_CP_EVENT_TYPE_PERST) {
			printf("APP: Event: perst on dom[%d]\n",
			       e[i].u.perst.dom_idx);
			err = app_handle_perst(e[i].u.perst.dom_idx);
			if (err) {
				printf("APP: Unable to handle perst event on PEM %d!\n",
				       e[i].u.perst.dom_idx);
				return err;
			}
		}
	}

	return 0;
}

static int send_heartbeat(void)
{
	struct octep_cp_event_info info;
	int i, j;

	info.e = OCTEP_CP_EVENT_TYPE_HEARTBEAT;
	for (i = 0; i < cp_lib_cfg.ndoms; i++) {
		if (perst[i])
			continue;

		info.u.hbeat.dom_idx = cp_lib_cfg.doms[i].idx;
		for (j = 0; j < cp_lib_cfg.doms[i].npfs; j++) {
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

void sigint_handler(int sig_num)
{
	if (sig_num == SIGINT) {
		printf("APP: Program quitting.\n");
		force_quit = 1;
	} else if (sig_num == SIGALRM) {
		if (force_quit)
			return;

		send_heartbeat();
		trigger_alarm(hb_interval);
	}
}

static int set_fw_ready_for_pem(int dom_idx, int ready)
{
	struct octep_cp_event_info info;
	int j;

	info.e = OCTEP_CP_EVENT_TYPE_FW_READY;
	info.u.fw_ready.ready = ready;
	info.u.fw_ready.dom_idx = dom_idx;
	for (j = 0; j < cp_lib_cfg.doms[dom_idx].npfs; j++) {
		info.u.fw_ready.pf_idx = cp_lib_cfg.doms[dom_idx].pfs[j].idx;
		octep_cp_lib_send_event(&info);
	}

	return 0;
}

static int set_fw_ready(int ready)
{
	int i;

	for (i = 0; i < cp_lib_cfg.ndoms; i++)
		set_fw_ready_for_pem(i, ready);

	return 0;
}

static int app_handle_perst(int dom_idx)
{
	struct pem_cfg *pem;
	int err;

	pem = &cfg.pems[dom_idx];
	if (!pem->valid)
		return -EINVAL;

	perst[dom_idx] = 1;
	set_fw_ready_for_pem(dom_idx, 0);
	octep_cp_lib_uninit_pem(dom_idx);
	loop_uninit_pem(dom_idx);
	printf("APP: Reinitiazing PEM %d\n", dom_idx);

	err = octep_cp_lib_init_pem(&cp_lib_cfg, dom_idx);
	if (err)
		return err;
	app_config_update_pem(dom_idx);
	err = loop_init_pem(dom_idx);
	if (err) {
		octep_cp_lib_uninit_pem(dom_idx);
		return err;
	}
	app_config_print_pem(dom_idx);
	set_fw_ready_for_pem(dom_idx, 1);
	perst[dom_idx] = 0;
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

	hb_interval = 0;
	cp_lib_cfg.min_version = CP_VERSION_CURRENT;
	cp_lib_cfg.max_version = CP_VERSION_CURRENT;
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
	while (!force_quit) {
		loop_process_msgs();
		process_events();
	}
	set_fw_ready(0);

	octep_cp_lib_uninit();
	loop_uninit();

	timer_delete(tim);
	app_config_uninit();

	return 0;
}
