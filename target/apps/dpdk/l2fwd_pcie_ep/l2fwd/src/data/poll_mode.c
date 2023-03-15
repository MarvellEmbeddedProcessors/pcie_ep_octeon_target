/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include "compat.h"
#include "octep_hw.h"
#include "data.h"
#include "poll_mode.h"

#define RTE_LOGTYPE_L2FWD_DATA_PM	RTE_LOGTYPE_USER1

enum {
	PM_STATE_INVALID,
	PM_STATE_INIT,
	PM_STATE_CONFIGURE,
	PM_STATE_START,
	PM_STATE_STOP,
	PM_STATE_UNINIT
};

struct lcore_queue_conf {
	/* queue number to be serviced */
	unsigned int queue;
	/* number of rx ports */
	unsigned int n_rx_port;
	/* rx ports to be serviced */
	unsigned int rx_port_list[RTE_MAX_ETHPORTS];
} __rte_cache_aligned;

/* per core queue configuration */
static struct lcore_queue_conf queue_conf[RTE_MAX_LCORE];

/* poll state */
static unsigned int state = PM_STATE_INVALID;

/* data operations */
static struct data_ops *data_ops;

static int per_core_fn(void *dummy)
{
	/* TX drain every ~100us */
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * 100;
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned int lcore, in_port, out_port, q;
	uint64_t prev_tsc, diff_tsc, cur_tsc;
	struct data_fn_ops *in_ops, *out_ops;
	struct data_port_fwd_info *in, *out;
	struct data_port_statistics *out_ps;
	struct rte_eth_dev_tx_buffer *txb;
	int i, j, nb_rx, sent, n_rx_port;
	struct lcore_queue_conf *conf;
	struct rte_mbuf *m;

	lcore = rte_lcore_id();
	conf = &queue_conf[lcore];
	if (!conf->n_rx_port) {
		RTE_LOG(INFO, L2FWD_DATA_PM,
			"Core[%d] has no assigned queues\n",
			lcore);
		return 0;
	}

	RTE_LOG(INFO, L2FWD_DATA_PM, "Core[%d] polling queue[%d]\n",
		lcore, conf->queue);

	prev_tsc = 0;
	while (true) {
		if (state >= PM_STATE_STOP)
			break;

		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		n_rx_port = conf->n_rx_port;
		q = conf->queue;
		for (i = 0; i < n_rx_port; i++) {
			in_port = conf->rx_port_list[i];
			in = &data_fwd_table[in_port];
			out_port = in->dst;
			out = &data_fwd_table[out_port];

			if (unlikely(!in->running || !out->running))
				continue;

			in_ops = in->fn_ops;
			nb_rx = in_ops->rx_burst(in_port,
						 q,
						 (void **)pkts_burst,
						 MAX_PKT_BURST);
			if (nb_rx <= 0)
				continue;

			port_stats[in_port][q].rx += nb_rx;
			out_ops = out->fn_ops;
			out_ps = &port_stats[out_port][q];
			txb = tx_buffer[out_port][q];
			for (j = 0; j < nb_rx; j++) {
				struct octep_tx_mdata *mdata = NULL;

				m = pkts_burst[j];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));
				in->prepare_rx_pkt(m, &mdata);
				out->prepare_tx_pkt(m, mdata);
				sent = out_ops->tx_buffer(out_port,
							  q,
							  txb,
							  m);
				out_ps->tx += sent;
				if (unlikely(diff_tsc > drain_tsc)) {
					sent = out_ops->tx_buffer_flush(out_port,
									q,
									txb);
					out_ps->tx += sent;
					prev_tsc = cur_tsc;
				}
			}
		}
	}

	return 0;
}

static int pm_start(void)
{
	if (state == PM_STATE_START)
		return 0;

	state = PM_STATE_START;
	rte_eal_mp_remote_launch(per_core_fn, NULL, SKIP_MAIN);

	return 0;
}

static int pm_configure(void)
{
	struct lcore_queue_conf *qconf = NULL;
	unsigned int core_id, port;
	int q_idx, prev_state, n_rx_port;

	/*
	 * Each logical core is assigned a dedicated TX queue on each port.
	 * num rx/tx queues == rte_lcore_count();
	 */
	prev_state = state;
	state = PM_STATE_CONFIGURE;
	q_idx = 0;
	RTE_LCORE_FOREACH_WORKER(core_id) {
		qconf = &queue_conf[core_id];
		qconf->n_rx_port = 0;
		n_rx_port = 0;
		RTE_ETH_FOREACH_DEV(port)
			if (data_port_is_configured(port))
				qconf->rx_port_list[n_rx_port++] = port;

		if (n_rx_port) {
			qconf->n_rx_port = n_rx_port;
			qconf->queue = q_idx++;
		}
	}
	state = prev_state;

	return 0;
}

static int pm_stop(void)
{
	state = PM_STATE_STOP;
	rte_eal_mp_wait_lcore();

	return 0;
}

static struct poll_mode_ops ops = {
	.start = pm_start,
	.configure = pm_configure,
	.stop = pm_stop
};

int poll_mode_init(struct data_ops *d_ops, struct poll_mode_ops **pm_ops)
{
	state = PM_STATE_INIT;
	*pm_ops = &ops;
	data_ops = d_ops;

	return 0;
}

int poll_mode_uninit(void)
{
	state = PM_STATE_UNINIT;
	return 0;
}
