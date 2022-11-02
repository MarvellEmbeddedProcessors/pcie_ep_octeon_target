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

#include "l2fwd.h"
#include "l2fwd_control.h"
#include "l2fwd_data.h"
#include "l2fwd_config.h"
#include "l2fwd_api_server.h"

#define RTE_LOGTYPE_L2FWD_PCIE_EP	RTE_LOGTYPE_USER1

static volatile bool force_quit;
static uint32_t debug_level = RTE_LOG_INFO;
static char cfg_file_path[256] = { 0 };

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

		l2fwd_control_handle_alarm();
	}
}

/* display usage */
static void l2fwd_pcie_ep_usage(const char *prgname)
{
	printf("%s [EAL options] --\n"
	       "  -d LEVEL<1-8>: debug level 1-8 (default 7)\n"
	       "  -f FILE: configuration file path\n",
	       prgname);
}

static const char short_options[] =
	"d:"  /* debug */
	"f:"  /* configuration file */
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
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, short_options,
				  lgopts, &option_index)) != EOF) {
		switch (opt) {
		case 'd':
			debug_level = atoi(optarg);
			if (debug_level < RTE_LOG_EMERG ||
			    debug_level > RTE_LOG_DEBUG)
				debug_level = RTE_LOG_INFO;
			break;
		case 'f':
			strncpy(cfg_file_path, optarg, 255);
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

/* Control plane calls this before resetting pem */
void l2fwd_on_before_control_pem_reset(int pem)
{
}

/* Control plane calls this before resetting pf */
void l2fwd_on_before_control_pf_reset(int pem, int pf)
{
}

/* Control plane calls this before resetting vf */
void l2fwd_on_before_control_vf_reset(int pem, int pf, int vf)
{
}

/* Control plane calls this after resetting vf */
void l2fwd_on_after_control_vf_reset(int pem, int pf, int vf)
{
}

/* Control plane calls this after resetting pf */
void l2fwd_on_after_control_pf_reset(int pem, int pf)
{
}

/* Control plane calls this after resetting pem */
void l2fwd_on_after_control_pem_reset(int pem)
{
}

int main(int argc, char **argv)
{
	uint64_t prev_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period_hz;
	int ret;
	struct l2fwd_data_cfg data_cfg = {
		.poll_mode = L2FWD_DATA_POLL_MODE_0
	};

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

	if (!strlen(cfg_file_path))
		rte_exit(EXIT_FAILURE, "L2FWD no config file\n");

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGALRM, signal_handler);

	rte_log_set_level(RTE_LOGTYPE_USER1, debug_level);

	ret = l2fwd_config_init(cfg_file_path);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD config init failed\n");

	ret = l2fwd_control_init();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD control init failed\n");

	ret = l2fwd_data_init(&data_cfg);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD data init failed\n");

	ret = l2fwd_api_server_init();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD api server init failed\n");

	ret = l2fwd_data_start();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD data start failed\n");

	ret = l2fwd_api_server_start();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "L2FWD api server start failed\n");

	force_quit = false;
	timer_period_hz = 10 * rte_get_timer_hz();
	prev_tsc = 0;
	timer_tsc = 0;
	while (!force_quit) {
		l2fwd_control_poll();

		cur_tsc = rte_rdtsc();
		timer_tsc += (cur_tsc - prev_tsc);
		if (unlikely(timer_tsc >= timer_period_hz)) {
			l2fwd_data_print_stats();
			timer_tsc = 0;
		}
		prev_tsc = cur_tsc;
	}

	l2fwd_api_server_stop();
	l2fwd_data_stop();

	l2fwd_api_server_uninit();
	l2fwd_data_uninit();
	l2fwd_control_uninit();
	l2fwd_config_uninit();

	return ret;
}
