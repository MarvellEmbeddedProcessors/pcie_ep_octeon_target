#include "compat.h"
#include "data.h"
#include "poll_mode.h"

#define RTE_LOGTYPE_L2FWD_DATA_PM0	RTE_LOGTYPE_USER1

enum {
	PM0_STATE_INVALID,
	PM0_STATE_INIT,
	PM0_STATE_START,
	PM0_STATE_STOP,
	PM0_STATE_UNINIT
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
static unsigned int state = PM0_STATE_INVALID;

/* data operations */
static struct data_ops *data_ops;

static int forward_pkt(struct rte_mbuf *m, unsigned int src_port,
		       unsigned int tx_q, unsigned int dst_port)
{
	/*
	 * unsigned cksum_offload = 0;
	 */
	int sent;

	if (!data_fwd_table[dst_port].running)
		return -EAGAIN;

	/*
	 * prep_mbuf_rx(src_port, m, &cksum_offload);
	 */
	/* process packet */
	/*
	 * prep_mbuf_tx(dst_port, m, cksum_offload);
	 */

	sent = data_fwd_table[dst_port].fn_ops->tx_buffer(dst_port,
							  tx_q,
							  tx_buffer[dst_port][tx_q],
							  m);
	if (sent)
		port_stats[dst_port][tx_q].tx += sent;
	else if (sent < 0)
		data_ops->drop_pkt(src_port, m, tx_q);

	return (sent >= 0) ? 0 : -EAGAIN;
}

static int per_core_fn(void *dummy)
{
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */

	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	uint64_t prev_tsc, diff_tsc, cur_tsc;
	unsigned int lcore, port, queue;
	struct data_port_fwd_info *fwd;
	struct lcore_queue_conf *conf;
	struct rte_mbuf *m;
	int i, j, nb_rx, sent, n_rx_port;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;

	lcore = rte_lcore_id();
	conf = &queue_conf[lcore];
	if (!conf->n_rx_port) {
		RTE_LOG(INFO, L2FWD_DATA_PM0,
			"Core[%d] has no assigned queues\n",
			lcore);
		return 0;
	}

	RTE_LOG(INFO, L2FWD_DATA_PM0, "Core[%d] polling queue[%d]\n",
		lcore, conf->queue);

	prev_tsc = 0;
	queue = conf->queue;
	n_rx_port = conf->n_rx_port;
	while (state == PM0_STATE_START) {
		cur_tsc = rte_rdtsc();

		/*
		 * TX burst queue drain
		 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			for (i = 0; i < n_rx_port; i++) {
				port = conf->rx_port_list[i];
				sent = rte_eth_tx_buffer_flush(port,
							       queue,
							       tx_buffer[port][queue]);
				if (sent)
					port_stats[port][queue].tx += sent;
			}
			prev_tsc = cur_tsc;
		}

		for (i = 0; i < n_rx_port; i++) {
			port = conf->rx_port_list[i];
			fwd = &data_fwd_table[port];
			if (unlikely(!fwd->running))
				continue;

			nb_rx = fwd->fn_ops->rx_burst(port,
						      queue,
						      (void **)pkts_burst,
						      MAX_PKT_BURST);
			if (nb_rx <= 0)
				continue;

			port_stats[port][queue].rx += nb_rx;
			for (j = 0; j < nb_rx; j++) {
				m = pkts_burst[j];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));
				forward_pkt(m, port, queue, fwd->dst);
			}
		}
	}

	return 0;
}

static int pm0_start(void)
{
	struct lcore_queue_conf *qconf = NULL;
	unsigned int core_id, port;
	int q_idx;

	if (state == PM0_STATE_START)
		return 0;

	/*
	 * Each logical core is assigned a dedicated TX queue on each port.
	 * num rx/tx queues == rte_lcore_count();
	 */
	q_idx = 0;
	RTE_LCORE_FOREACH_WORKER(core_id) {
		qconf = &queue_conf[core_id];
		RTE_ETH_FOREACH_DEV(port)
			if (data_port_is_configured(port))
				qconf->rx_port_list[qconf->n_rx_port++] = port;

		if (qconf->n_rx_port)
			qconf->queue = q_idx++;
	}

	state = PM0_STATE_START;
	if (q_idx)
		rte_eal_mp_remote_launch(per_core_fn, NULL, SKIP_MAIN);

	return 0;
}

static int pm0_pause(unsigned int port)
{
	return 0;
}

static int pm0_resume(unsigned int port)
{
	return 0;
}

static int pm0_stop(void)
{
	if (state != PM0_STATE_START)
		return 0;

	state = PM0_STATE_STOP;
	rte_eal_mp_wait_lcore();
	state = PM0_STATE_INIT;

	return 0;
}

static struct poll_mode_ops pm0_ops = {
	.start = pm0_start,
	.pause = pm0_pause,
	.resume = pm0_resume,
	.stop = pm0_stop
};

static int init_queue_conf(void)
{
	return 0;
}

int poll_mode_0_init(struct data_ops *ops, struct poll_mode_ops **pm_ops)
{
	int err;

	err = init_queue_conf();
	if (err < 0)
		return err;

	state = PM0_STATE_INIT;
	*pm_ops = &pm0_ops;
	data_ops = ops;

	return 0;
}

int poll_mode_0_uninit(void)
{
	state = PM0_STATE_UNINIT;
	return 0;
}
