/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_dmadev.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf_core.h>
#include <rte_mbuf.h>
#include <rte_net.h>

#define MAX_PKT_BURST		32
#define MAX_RX_PORT_PER_LCORE	16
#define MAX_QUEUES_PER_PORT	16

static bool force_quit;
struct rte_mempool *rx_pktmbuf_pool = NULL;
static struct rte_ether_addr eth_addr[RTE_MAX_ETHPORTS];

struct app_port_stats {
	uint64_t tx;
	uint64_t rx;
	uint64_t dropped;
} __rte_cache_aligned;

struct app_port_stats port_stats[RTE_MAX_ETHPORTS][MAX_QUEUES_PER_PORT];

struct lcore_queue_conf {
	unsigned int queue;
	unsigned int n_rx_port;
	unsigned int rx_port_list[MAX_RX_PORT_PER_LCORE];
} __rte_cache_aligned;

static struct rte_eth_dev_tx_buffer
		*tx_buffer[RTE_MAX_ETHPORTS][MAX_QUEUES_PER_PORT];
struct lcore_queue_conf lcore_qconf[RTE_MAX_LCORE];
static unsigned int queues_per_port = 1;

#define RTE_TEST_RX_DESC_DEFAULT 1024
#define RTE_TEST_TX_DESC_DEFAULT 1024
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

static void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
			signum);
		force_quit = true;
	}
}

static inline struct rte_flow *set_rte_flow(uint16_t portid, int qidx)
{
	struct rte_flow_action_queue queue = { .index = qidx };
	struct rte_flow_attr attr = { .ingress = 1 };
	struct rte_flow_item_eth item_eth;
	struct rte_flow_action action[2];
	struct rte_flow *flow = NULL;
	struct rte_flow_error error;
	struct rte_flow_item item;
	int ret;

	memset(&item, 0, sizeof(item));
	memset(&item_eth, 0, sizeof(item_eth));

	item.spec = &item_eth;
	item.mask = &item_eth;
	item.type = RTE_FLOW_ITEM_TYPE_ETH;

	action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
	action[0].conf = &queue;
	action[1].type = RTE_FLOW_ACTION_TYPE_END;

	/* validate and create the flow rule */
	ret = rte_flow_validate(portid, &attr, &item, action, &error);
	if (ret) {
		printf("rte_flow_validate failed returned %d\n", ret);
		return NULL;
	}

	flow = rte_flow_create(portid, &attr, &item, action, &error);
	if (!flow)
		printf("rte_flow_create failed for port %d queue %d\n",
		       portid, qidx);

	return flow;
}

static int bb_target_launch_one_lcore(__attribute__((unused)) void *dummy)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	struct lcore_queue_conf *qconf;
	unsigned int lcore_id, portid;
	unsigned int nb_rx, sent;
	struct rte_flow *flow;
	struct rte_mbuf *m;
	unsigned int queue;
	int i, j;

	lcore_id = rte_lcore_id();
	qconf = &lcore_qconf[lcore_id];

	queue = qconf->queue;

	/* setup rte flow */
	for (i = 0; i < qconf->n_rx_port; i++) {
		portid = qconf->rx_port_list[i];
		flow = set_rte_flow(portid, queue);
		if (!flow) {
			printf("set_rte_flow failed\n");
			return -EIO;
		}

		printf(" lcoreid=%u portid=%u queue=%u\n", lcore_id, portid, queue);
	}

	while (!force_quit) {

		for (i = 0; i < qconf->n_rx_port; i++) {
			portid = qconf->rx_port_list[i];
			//buffer = tx_buffer[portid][queue];

			//sent = rte_eth_tx_buffer_flush(portid, queue, buffer);
			//if (sent)
			//	port_stats[portid][queue].tx += sent;

			/*
			 * Read packet from RX queues
			 */
			nb_rx = rte_eth_rx_burst(portid, queue, pkts_burst,
						 MAX_PKT_BURST);
			port_stats[portid][queue].rx += nb_rx;

			for (j = 0; j < nb_rx; j++) {
				m = pkts_burst[j];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));
				//process_bbdev_cmds(m, portid, queue);
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	char if_name[RTE_ETH_NAME_MAX_LEN];
	struct rte_dma_conf dmadev_conf;
	struct rte_dma_info dmadev_info;
	struct lcore_queue_conf *qconf;
	unsigned int nb_lcores = 0;
	uint16_t portid, lcore_id;
	int nports, ncores = 0;
	unsigned int i, j, k;
	int nb_dma_ports;
	int ret = 0;

	uint8_t default_key[] = {
		0xFE, 0xED, 0x0B, 0xAD, 0xFE, 0xED, 0x0B, 0xAD,
		0xAD, 0x0B, 0xED, 0xFE, 0xAD, 0x0B, 0xED, 0xFE,
		0x13, 0x57, 0x9B, 0xEF, 0x24, 0x68, 0xAC, 0x0E,
		0x91, 0x72, 0x53, 0x11, 0x82, 0x64, 0x20, 0x44,
		0x12, 0xEF, 0x34, 0xCD, 0x56, 0xBC, 0x78, 0x9A,
		0x9A, 0x78, 0xBC, 0x56, 0xCD, 0x34, 0xEF, 0x12
	};

	struct rte_eth_conf port_conf = {
		.rxmode = {
			.mq_mode = ETH_MQ_RX_RSS,
			.split_hdr_size = 0,
			.offloads = (DEV_RX_OFFLOAD_TCP_CKSUM |
				     DEV_RX_OFFLOAD_IPV4_CKSUM |
				     DEV_RX_OFFLOAD_UDP_CKSUM |
				     DEV_RX_OFFLOAD_JUMBO_FRAME),
		},
		.rx_adv_conf = {
			.rss_conf = {
				.rss_key = default_key,
				.rss_hf = (ETH_RSS_IP | ETH_RSS_UDP |
					   ETH_RSS_TCP),
			},
		},
		.txmode = {
			.mq_mode = ETH_MQ_TX_NONE,
			.offloads = (DEV_TX_OFFLOAD_TCP_CKSUM |
				     DEV_TX_OFFLOAD_IPV4_CKSUM |
				     DEV_TX_OFFLOAD_UDP_CKSUM),
		},
	};


	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");

	nports = rte_eth_dev_count_avail();
	if (nports == 0)
		rte_exit(EXIT_FAILURE, "No ports found - exiting\n");

	nb_lcores = rte_lcore_count();
	for (i = 0; i < nb_lcores; i++) {
		if (rte_lcore_is_enabled(i))
			ncores++;
	}

	qconf = NULL;
	queues_per_port = ncores;
	if (queues_per_port == 0 || queues_per_port > MAX_QUEUES_PER_PORT)
		rte_exit(EXIT_FAILURE, "Wrong number of queues per port %d - Bye\n",
			 queues_per_port);

	printf("Using queues per port %d\n",  queues_per_port);
	for (i = 0, j = 0; i < nb_lcores; i++) {
		if (rte_lcore_is_enabled(i) == 0)
			continue;
		qconf = &lcore_qconf[i];
		qconf->queue = j;
		for (k = 0; k < nports; k++) {
			qconf->rx_port_list[k] = k;
			qconf->n_rx_port++;
			printf("Lcore %u: RX port %u queue %u\n", i, k, j);
		}
		j++;
	}

	rx_pktmbuf_pool = rte_pktmbuf_pool_create("rx_pktmbuf_pool",
						  4096, 256, 0,
						  RTE_MBUF_DEFAULT_BUF_SIZE,
						  rte_socket_id());
	if (rx_pktmbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

	/* setup eth device */
	RTE_ETH_FOREACH_DEV(portid) {
		struct rte_eth_conf local_port_conf = port_conf;
		struct rte_eth_dev_info dev_info;
		struct rte_eth_rxconf rxq_conf;
		struct rte_eth_txconf txq_conf;

		ret = rte_eth_dev_get_name_by_port(portid, if_name);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "ifname not found for portid %d\n",
				 portid);

		printf("Initializing port %u ifname: %s... ", portid, if_name);
		fflush(stdout);

		ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				"Error during getting device (port %u) info: %s\n",
				portid, strerror(-ret));

		if (!(dev_info.rx_offload_capa & DEV_RX_OFFLOAD_SCATTER))
			printf("SCATTER not supported in driver\n");
		else
			local_port_conf.rxmode.offloads |=
						 DEV_RX_OFFLOAD_SCATTER;

		if (!(dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MULTI_SEGS))
			printf("MULTI_SEG not supported in driver\n");
		else
			local_port_conf.txmode.offloads |=
						 DEV_TX_OFFLOAD_MULTI_SEGS;

		if (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_RSS_HASH) {
			printf("setting rx hash for port id %d\n", portid);
			local_port_conf.rxmode.offloads
				|= DEV_RX_OFFLOAD_RSS_HASH;
		}

		local_port_conf.rx_adv_conf.rss_conf.rss_hf &=
			dev_info.flow_type_rss_offloads;
		if (local_port_conf.rx_adv_conf.rss_conf.rss_hf !=
				port_conf.rx_adv_conf.rss_conf.rss_hf) {
			printf("Port %u modified RSS hash function based on hardware support,"
			       "requested:%#"PRIx64" configured:%#"PRIx64"",
				portid,
				port_conf.rx_adv_conf.rss_conf.rss_hf,
				local_port_conf.rx_adv_conf.rss_conf.rss_hf);
		}

		if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
			local_port_conf.txmode.offloads |=
				DEV_TX_OFFLOAD_MBUF_FAST_FREE;

		/* Configure the number of queues for a port. */
		ret = rte_eth_dev_configure(portid, queues_per_port,
					    queues_per_port, &local_port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
				  ret, portid);

		ret = rte_eth_dev_set_mtu(portid, dev_info.max_mtu);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Can't set mtu to %d\n",
				 dev_info.max_mtu);
		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd,
						       &nb_txd);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, portid);

		ret = rte_eth_macaddr_get(portid, &eth_addr[portid]);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot get MAC address: err=%d, port=%u\n",
				 ret, portid);

		for (i = 0; i < queues_per_port; i++) {
			rxq_conf = dev_info.default_rxconf;
			rxq_conf.offloads = local_port_conf.rxmode.offloads;
			ret = rte_eth_rx_queue_setup(portid, i, nb_rxd,
						     rte_eth_dev_socket_id(portid),
						     &rxq_conf,
						     rx_pktmbuf_pool);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "Rx queue setup err %d, port %d\n",
					 ret, portid);

			txq_conf = dev_info.default_txconf;
			txq_conf.offloads = local_port_conf.txmode.offloads;
			ret = rte_eth_tx_queue_setup(portid, i, nb_txd,
						     rte_eth_dev_socket_id(portid),
						     &txq_conf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "Tx queue setup err %d, port %d\n",
					 ret, portid);

			/* Initialize Tx buffers */
			tx_buffer[portid][i] = rte_zmalloc_socket("tx_buffer",
					RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST),
					0, rte_eth_dev_socket_id(portid));
			if (tx_buffer[portid][i] == NULL)
				rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
					 portid);

			ret = rte_eth_tx_buffer_set_err_callback(
					tx_buffer[portid][i],
					rte_eth_tx_buffer_count_callback,
					&port_stats[portid][i].dropped);
			if (ret < 0)
				rte_exit(EXIT_FAILURE,
					 "Cannot set error callback for tx buffer on port %u\n",
					 portid);
		}

		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				 ret, portid);

		ret = rte_eth_promiscuous_enable(portid);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				 "rte_eth_promiscuous_enable:err=%s, port=%u\n",
				 rte_strerror(-ret), portid);

		printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
		       portid, eth_addr[portid].addr_bytes[0],
		       eth_addr[portid].addr_bytes[1],
		       eth_addr[portid].addr_bytes[2],
		       eth_addr[portid].addr_bytes[3],
		       eth_addr[portid].addr_bytes[4],
		       eth_addr[portid].addr_bytes[5]);
	}

	/* setup dma device */
	nb_dma_ports = rte_dma_count_avail();
	if (nb_dma_ports == 0)
		rte_exit(EXIT_FAILURE, "No dmadev ports found - exiting\n");

	printf("%d dmadev ports detected, using first one\n", nb_dma_ports);

	//FIXME: increase DMA ports support
	rte_dma_info_get(0, &dmadev_info);
	dmadev_conf.nb_vchans = 1;
	dmadev_conf.enable_silent = 0;
	ret = rte_dma_configure(0, &dmadev_conf);
	if (ret)
		rte_exit(EXIT_FAILURE, "Unable to configure dmadev port 0\n");

	printf("dmadev port 0 configured succesfully\n");

	ret = 0;
	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(bb_target_launch_one_lcore, NULL, CALL_MAIN);
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
			ret = -1;
			break;
		}
	}

	RTE_ETH_FOREACH_DEV(portid) {
		printf("Closing port %d...", portid);
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf(" Done\n");
	}

	printf("Bye...\n");

	return ret;
}
