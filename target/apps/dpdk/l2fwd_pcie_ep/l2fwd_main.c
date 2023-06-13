/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <stdlib.h>

#include <rte_version.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_pci.h>
#include <rte_ethdev.h>
#include <rte_bus_pci.h>

#include "compat.h"
#include "l2fwd.h"

#define RTE_LOGTYPE_L2FWD_PCIE_EP	RTE_LOGTYPE_USER1

static volatile bool force_quit;

static uint32_t debug_level = RTE_LOG_INFO;

static int print_stats = 1;

static struct l2fwd_user_config l2fwd_cfg = {
	.features = L2FWD_FEATURE_CTRL_PLANE | L2FWD_FEATURE_DATA_PLANE
};

static void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		RTE_LOG(ERR, L2FWD_PCIE_EP,
			"\n\nSignal %d received, preparing to exit...\n",
			signum);
		force_quit = true;
	} else if (signum == SIGALRM) {
		if (force_quit)
			return;

		l2fwd_sigalrm();
	}
}

/* display usage */
static void l2fwd_pcie_ep_usage(const char *prgname)
{
	printf("%s [EAL options] --\n"
	       "  -a <1/0> Toggle api server feature (default: 0)\n"
	       "     0: Disabled\n"
	       "     1: Run on tcp port 8888\n"
	       "  -c <1/0> Toggle control plane feature (default: 1)\n"
	       "     0: Run in virtual mode for configured sdp interfaces\n"
	       "        Real interface paired with sdp will not be managed\n"
	       "     1: Run in real mode for configured sdp interfaces\n"
	       "        Real interface paired with sdp will be managed\n"
	       "  -d <1/0> Toggle data plane feature (default: 1)\n"
	       "     0: Disabled\n"
	       "     1: Run with configured forwarding interface pairs\n"
	       "  -f FILE: configuration file path\n"
	       "  -s <1/0> Toggle periodic statistics printing (default: 1)\n"
	       "  -v <1-8>: verbosity 1-8 (default 7)\n"
	       "  -x <1/0> Toggle packet capture feature (default: 0)\n"
	       "     0: Disabled\n"
	       "     1: enable packet capture\n",
	       prgname);
}

static const char short_options[] =
	"a:"  /* api server feature */
	"c:"  /* control plane feature */
	"d:"  /* data plane feature */
	"f:"  /* configuration file */
	"s:"  /* statistics printing */
	"v:"  /* verbosity */
	"x:"  /* enable packet capture */
	;

enum {
	/* long options mapped to a short option */

	/* first long only option value must be >= 256, so that we won't
	 * conflict with short options
	 */
	CMD_LINE_OPT_MIN_NUM = 256,
};

static const struct option lgopts[] = {
	{NULL, 0, 0, 0}
};

/* Parse the argument given in the command line of the application */
static int l2fwd_pcie_ep_parse_args(int argc, char **argv)
{
	int opt, ret, val;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, short_options,
				  lgopts, &option_index)) != EOF) {
		switch (opt) {
		case 'a':
			val = atoi(optarg);
			if (val)
				l2fwd_cfg.features |= L2FWD_FEATURE_API_SERVER;
			else
				l2fwd_cfg.features &= ~L2FWD_FEATURE_API_SERVER;
			break;
		case 'c':
			val = atoi(optarg);
			if (val)
				l2fwd_cfg.features |= L2FWD_FEATURE_CTRL_PLANE;
			else
				l2fwd_cfg.features &= ~L2FWD_FEATURE_CTRL_PLANE;
			break;
		case 'd':
			val = atoi(optarg);
			if (val)
				l2fwd_cfg.features |= L2FWD_FEATURE_DATA_PLANE;
			else
				l2fwd_cfg.features &= ~L2FWD_FEATURE_DATA_PLANE;
			break;
		case 'f':
			strncpy(l2fwd_cfg.cfg_file_path, optarg, 255);
			break;
		case 's':
			print_stats = !!atoi(optarg);
			break;
		case 'v':
			debug_level = atoi(optarg);
			if (debug_level < RTE_LOG_EMERG ||
			    debug_level > RTE_LOG_DEBUG)
				debug_level = RTE_LOG_INFO;
			break;
		case 'x':
			val = strtoul(optarg, NULL, 10);
			if (val)
				l2fwd_cfg.features |= L2FWD_FEATURE_PKT_CAPTURE;
			else
				l2fwd_cfg.features &= ~L2FWD_FEATURE_PKT_CAPTURE;
			break;
		default:
			l2fwd_pcie_ep_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 1; /* reset getopt lib */
	return ret;
}

int main(int argc, char **argv)
{
	uint64_t prev_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period_hz;
	int ret;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");

	argc -= ret;
	argv += ret;

	/* parse application arguments (after the EAL ones) */
	ret = l2fwd_pcie_ep_parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD invalid arguments\n");

	if (!strlen(l2fwd_cfg.cfg_file_path))
		rte_exit(EXIT_FAILURE, "L2FWD no config file\n");

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGALRM, signal_handler);

	rte_log_set_level(RTE_LOGTYPE_USER1, debug_level);

	ret = l2fwd_init(&l2fwd_cfg);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD init failed\n");

	ret = l2fwd_start();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD start failed\n");

	force_quit = false;
	timer_period_hz = 10 * rte_get_timer_hz();
	prev_tsc = 0;
	timer_tsc = 0;
	while (!force_quit) {
		l2fwd_poll();

		if (print_stats) {
			cur_tsc = rte_rdtsc();
			timer_tsc += (cur_tsc - prev_tsc);
			if (unlikely(timer_tsc >= timer_period_hz)) {
				l2fwd_print_stats();
				timer_tsc = 0;
			}
			prev_tsc = cur_tsc;
		}
	}

	l2fwd_stop();

	l2fwd_uninit();

	return ret;
}
