/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 * Copyright (c) 2020 Marvell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
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
#include <rte_mbuf.h>
#include <rte_bus_pci.h>

#include "otx-drv.h"
static volatile bool force_quit;

static char conf_file[256];

#define RTE_LOGTYPE_L2FWD RTE_LOGTYPE_USER1

#define MAX_PKT_BURST 32
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */
#define MEMPOOL_CACHE_SIZE 256

#define PORT_TYPE_NPU_NET 0
#define PORT_TYPE_NPU_PCI_PF 1
#define PORT_TYPE_NPU_PCI_VF 2


/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 1024
#define RTE_TEST_TX_DESC_DEFAULT 1024
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* ethernet addresses of ports */
static struct rte_ether_addr l2fwd_ports_eth_addr[RTE_MAX_ETHPORTS];
int port_map[RTE_MAX_ETHPORTS];
int port_type[RTE_MAX_ETHPORTS];

static int dump_pkt;
static int reflector_mode;
static int generator_mode;
static int gen_mbuf_jumbo;
static int max_mbuf_size;
static int gen_pkt_size;
static uint32_t gen_port_mask = 0;
static int start_gen;
static struct rte_mempool *gen_pktmbuf_pool = NULL;
static int gen_next_flow[RTE_MAX_LCORE];
#define MBUF_JUMBO_SIZE 9216
#define DEF_GEN_PKT_SIZE 256
#define GEN_SEND_FNAME "/tmp/gen_send"



static unsigned int l2fwd_queues_per_port = 1;

#define MAX_RX_PORT_PER_LCORE 16
#define MAX_QUEUE_PER_PORT 16
struct lcore_queue_conf {
	unsigned queue;
	unsigned n_rx_port;
	unsigned rx_port_list[MAX_RX_PORT_PER_LCORE];
} __rte_cache_aligned;
struct lcore_queue_conf lcore_queue_conf[RTE_MAX_LCORE];

static struct rte_eth_dev_tx_buffer *tx_buffer[RTE_MAX_ETHPORTS][MAX_QUEUE_PER_PORT];

struct rte_mempool * l2fwd_pktmbuf_pool = NULL;

/* Per-port statistics struct */
struct l2fwd_port_statistics {
	uint64_t tx;
	uint64_t rx;
	uint64_t dropped;
} __rte_cache_aligned;
struct l2fwd_port_statistics port_stats[RTE_MAX_ETHPORTS][MAX_QUEUE_PER_PORT];

#define MAX_TIMER_PERIOD 86400 /* 1 day max */
/* A tsc-based timer responsible for triggering statistics printout */
static uint64_t timer_period_hz;
static uint64_t timer_period_sec = 10; /* default period is 10 seconds */

static int
parse_gen_file(const char *fname)
{
	int nb_ports = rte_eth_dev_count_avail();
	FILE *file = fopen(fname, "r");
	unsigned total_ports;
	uint32_t i;

	if (file == NULL)
		return 0;
	gen_port_mask = 0;
	fscanf(file, "%d 0x%x", &gen_pkt_size, &gen_port_mask);
	fclose(file);
	if (gen_pkt_size < 0 || gen_pkt_size > max_mbuf_size)
		gen_pkt_size = DEF_GEN_PKT_SIZE;
	printf("gen_pkt_size %d\n", gen_pkt_size);

	total_ports = 0;
	printf("gen_ports: ");
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		if (i >= (unsigned int)nb_ports)
			break;
		if ((1UL << i) & gen_port_mask) {
			printf("%u ", i);
			total_ports++;
		}
	}
	printf("\n");
	printf("total_ports: %u\n", total_ports);
	rte_smp_wmb();
	return total_ports;
}

static void
print_stats(void)
{
	uint16_t port, i;
	struct  l2fwd_port_statistics cur_stats[RTE_MAX_ETHPORTS] = {0};
	static struct  l2fwd_port_statistics prev_stats[RTE_MAX_ETHPORTS];

	/* print statistics */
	RTE_ETH_FOREACH_DEV(port) {
		for (i = 0; i < l2fwd_queues_per_port; i++) {
			cur_stats[port].tx += port_stats[port][i].tx;
			cur_stats[port].rx += port_stats[port][i].rx;
			cur_stats[port].dropped += port_stats[port][i].dropped;
		}
#define PS(x) ((x) / timer_period_sec)

		printf("port %"PRIu16": tx_pkts: %"PRIu64
		       "/s rx_pkts: %" PRIu64
		       "/s dropped: %" PRIu64 "/s\n",
			port,
			PS((cur_stats[port].tx -
			    prev_stats[port].tx)),
			PS((cur_stats[port].rx -
			    prev_stats[port].rx)),
			PS((cur_stats[port].dropped -
			    prev_stats[port].dropped)));
	}
	memcpy(&prev_stats, &cur_stats, sizeof(cur_stats));
}


static inline void
drop_pkt(uint32_t portid, struct rte_mbuf *m, unsigned queue)
{
	struct rte_eth_dev_tx_buffer *buffer = tx_buffer[portid][queue];

	buffer->error_callback(&m, 1, buffer->error_userdata);
}

static int
prep_mbuf_for_app(uint32_t portid, struct rte_mbuf *mbuf)
{
	if ((port_type[portid] == PORT_TYPE_NPU_PCI_PF) ||
		(port_type[portid] == PORT_TYPE_NPU_PCI_VF)) {
		//printf("packet from PF/VF port-%d\n", portid);
		rte_pktmbuf_adj(mbuf, CVM_RAW_FRONT_SIZE);
		mbuf->l2_len -= CVM_RAW_FRONT_SIZE;
	}
	return 0;
}

static int
prep_mbuf_for_port(uint32_t portid, struct rte_mbuf *mbuf)
{
        volatile cvmcs_resp_hdr_t *resp_ptr = NULL;

	if ((port_type[portid] != PORT_TYPE_NPU_PCI_PF) &&
		(port_type[portid] != PORT_TYPE_NPU_PCI_VF)) {
		if (RTE_ETH_IS_IPV4_HDR(mbuf->packet_type))
                        mbuf->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_IPV4;
                if (RTE_ETH_IS_IPV6_HDR(mbuf->packet_type))
                        mbuf->ol_flags |= PKT_TX_IPV6;
                if ((mbuf->packet_type & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP)
                        mbuf->ol_flags |= PKT_TX_TCP_CKSUM;
                if ((mbuf->packet_type & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_UDP)
                        mbuf->ol_flags |= PKT_TX_UDP_CKSUM;
		return 0;
	}

	resp_ptr = (cvmcs_resp_hdr_t *)rte_pktmbuf_prepend(mbuf,
							   CVMX_RESP_HDR_SIZE);
	if (!resp_ptr) {
		printf( "No head room available\n");
		return -1;
	}
	mbuf->l2_len += CVMX_RESP_HDR_SIZE;
	resp_ptr->u64 = 0;
	resp_ptr->s.opcode = CORE_NW_DATA_OP;
	resp_ptr->s.destqport = 0;
	return 0;
}

static uint32_t cfg_ip_src      = RTE_IPV4(10, 254, 0, 0);
static uint32_t cfg_ip_dst      = RTE_IPV4(10, 253, 0, 0);
static uint16_t cfg_udp_src     = 1000;
static uint16_t cfg_udp_dst     = 1001;
static int cfg_n_flows = 65535;
static struct rte_ether_addr cfg_ether_src = {
	{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x00 } };
static struct rte_ether_addr cfg_ether_dst = {
	{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x01 } };

#define IP_DEFTTL  64   /* from RFC 1340. */

static inline uint16_t
ip_sum(const unaligned_uint16_t *hdr, int hdr_len)
{
	uint32_t sum = 0;

	while (hdr_len > 1) {
		sum += *hdr++;
		if (sum & 0x80000000)
			sum = (sum & 0xFFFF) + (sum >> 16);
		hdr_len -= 2;
	}
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ~sum;
}

static struct rte_mbuf*
generate_pkt(int *next_flow)
{
	struct rte_mbuf  *pkt;
	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_udp_hdr *udp_hdr;
	uint64_t ol_flags = 0;

	pkt = rte_pktmbuf_alloc(gen_pktmbuf_pool);
	if (!pkt)
		return NULL;

	pkt->data_len = gen_pkt_size;
	pkt->next = NULL;

	ol_flags = PKT_TX_IP_CKSUM;
	ol_flags |= PKT_TX_UDP_CKSUM;

	/* Initialize Ethernet header. */
	eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
	rte_ether_addr_copy(&cfg_ether_dst, &eth_hdr->d_addr);
	rte_ether_addr_copy(&cfg_ether_src, &eth_hdr->s_addr);
	eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	/* Initialize IP header. */
	ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
	memset(ip_hdr, 0, sizeof(*ip_hdr));
	ip_hdr->version_ihl     = RTE_IPV4_VHL_DEF;
	ip_hdr->type_of_service = 0;
	ip_hdr->fragment_offset = 0;
	ip_hdr->time_to_live    = IP_DEFTTL;
	ip_hdr->next_proto_id   = IPPROTO_UDP;
	ip_hdr->packet_id       = 0;
	ip_hdr->src_addr        = rte_cpu_to_be_32(cfg_ip_src);
	ip_hdr->dst_addr        = rte_cpu_to_be_32(cfg_ip_dst + *next_flow);
	ip_hdr->total_length    = rte_cpu_to_be_16(gen_pkt_size -
						   sizeof(*eth_hdr));
	ip_hdr->hdr_checksum    = ip_sum((unaligned_uint16_t *)ip_hdr,
					 sizeof(*ip_hdr));

	/* Initialize UDP header. */
	udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
	udp_hdr->src_port       = rte_cpu_to_be_16(cfg_udp_src);
	udp_hdr->dst_port       = rte_cpu_to_be_16(cfg_udp_dst);
	udp_hdr->dgram_cksum    = 0; /* No UDP checksum. */
	udp_hdr->dgram_len      = rte_cpu_to_be_16(gen_pkt_size -
						   sizeof(*eth_hdr) -
						   sizeof(*ip_hdr));
	pkt->nb_segs            = 1;
	pkt->pkt_len            = gen_pkt_size;
	pkt->ol_flags           = ol_flags;
	//pkt->vlan_tci           = vlan_tci;
	//pkt->vlan_tci_outer     = vlan_tci_outer;
	pkt->l2_len             = sizeof(struct rte_ether_hdr);
	pkt->l3_len             = sizeof(struct rte_ipv4_hdr);
	*next_flow = (*next_flow + 1) % cfg_n_flows;
	return pkt;
}

static void
l2fwd_simple_send(unsigned dst_port, unsigned queue)
{
	struct rte_mbuf *m;
	unsigned lcore_id;
	int sent;

	if (!start_gen)
		return;
	lcore_id = rte_lcore_id();
	if (unlikely(dst_port == RTE_MAX_ETHPORTS)) {
		RTE_LOG(WARNING, L2FWD, "dst-port not found  %u \n", dst_port);
		return;
	}
	m = generate_pkt(&gen_next_flow[lcore_id]);
	if (m == NULL) {
		//printf("Can't allocate pkt\n");
		return;
	}
	prep_mbuf_for_port(dst_port, m);
	if (unlikely(dump_pkt)) {
		RTE_LOG(INFO, L2FWD, "\nSend pkt to port %u"
			" l2_len %u, l3_len %u, packet_type 0x%x\n",
			dst_port, m->l2_len, m->l3_len,
			m->packet_type);
		rte_pktmbuf_dump(stdout, m, 64);
		printf("\n");
	}
	sent = rte_eth_tx_buffer(dst_port, queue, tx_buffer[dst_port][queue], m);
	if (sent)
		port_stats[dst_port][queue].tx += sent;

	return;
}

static void
l2fwd_simple_forward(struct rte_mbuf *m, unsigned portid, unsigned tx_q)
{
	unsigned dst_port = RTE_MAX_ETHPORTS;
	unsigned lcore_id;
	unsigned src_port;
	int sent;

	if (unlikely(dump_pkt)) {
		RTE_LOG(INFO, L2FWD,
			"\nRcvd from port-%u; l2_len %u, l3_len %u, packet_type 0x%x\n",
			portid, m->l2_len, m->l3_len, m->packet_type);
		rte_pktmbuf_dump(stdout, m, 64);
		printf("\n");
	}
	src_port = portid;
	prep_mbuf_for_app(src_port, m);
	if (unlikely(dump_pkt)) {
		RTE_LOG(INFO, L2FWD,
			"\nafter prep_mbuf_for_app: Rcvd from port-%u; l2_len %u, l3_len %u, packet_type 0x%x\n",
			portid, m->l2_len, m->l3_len, m->packet_type);
		rte_pktmbuf_dump(stdout, m, 64);
		printf("\n");
	}
	dst_port = port_map[src_port];
	if (generator_mode) {
		if (m)
			rte_pktmbuf_free(m);
		if (!start_gen)
			return;
		if (!(gen_port_mask & (1U << portid)))
			return;
		m = NULL;
		lcore_id = rte_lcore_id();
		dst_port = portid;
		m = generate_pkt(&gen_next_flow[lcore_id]);
		if (m == NULL) {
			//printf("Can't allocate pkt\n");
			return;
		}
	} else if (reflector_mode) {
		dst_port =  portid;
	}
	/* process packet */
	if (unlikely(dst_port == RTE_MAX_ETHPORTS)) {
		RTE_LOG(WARNING, L2FWD, "dst-port not found for src-port %u \n", portid);
		goto drop_pkt;
	}
	prep_mbuf_for_port(dst_port, m);
	if (unlikely(dump_pkt)) {
		RTE_LOG(INFO, L2FWD, "\nSend pkt to port %u"
			" l2_len %u, l3_len %u, packet_type 0x%x\n",
			dst_port, m->l2_len, m->l3_len,
			m->packet_type);
		rte_pktmbuf_dump(stdout, m, 64);
		printf("\n");
	}
	sent = rte_eth_tx_buffer(dst_port, tx_q, tx_buffer[dst_port][tx_q], m);
	if (sent)
		port_stats[dst_port][tx_q].tx += sent;

	return;
drop_pkt:
	drop_pkt(portid, m, tx_q);
	return;
}

int cvmcs_nic_process_pkt(struct rte_mbuf * pkt);

/* main processing loop */
static void
l2fwd_main_loop(void)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	struct rte_mbuf *m;
	int sent;
	unsigned lcore_id, queue;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	unsigned i, j, portid, nb_rx;
	struct lcore_queue_conf *qconf;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S *
			BURST_TX_DRAIN_US;
	struct rte_eth_dev_tx_buffer *buffer;

	prev_tsc = 0;
	timer_tsc = 0;

	lcore_id = rte_lcore_id();
	qconf = &lcore_queue_conf[lcore_id];

	if (qconf->n_rx_port == 0) {
		RTE_LOG(INFO, L2FWD, "lcore %u has nothing to do\n", lcore_id);
		return;
	}

	RTE_LOG(INFO, L2FWD, "entering main loop on lcore %u\n", lcore_id);

	queue = qconf->queue;
	for (i = 0; i < qconf->n_rx_port; i++) {

		portid = qconf->rx_port_list[i];
		RTE_LOG(INFO, L2FWD, " -- lcoreid=%u portid=%u queue=%u\n", lcore_id,
			portid, queue);

	}

	while (!force_quit) {

		cur_tsc = rte_rdtsc();

		/*
		 * TX burst queue drain
		 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			for (i = 0; i < qconf->n_rx_port; i++) {

				portid = port_map[qconf->rx_port_list[i]];
				buffer = tx_buffer[portid][queue];

				sent = rte_eth_tx_buffer_flush(portid, queue, buffer);
				if (sent)
					port_stats[portid][queue].tx += sent;

			}

			/* if timer is enabled */
			if (timer_period_hz > 0) {

				/* advance the timer */
				timer_tsc += diff_tsc;

				/* if timer has reached its timeout */
				if (unlikely(timer_tsc >= timer_period_hz)) {

					/* do this only on master core */
					if (lcore_id == rte_get_master_lcore()) {
						print_stats();
						/* reset the timer */
						timer_tsc = 0;
						if (!start_gen &&
						     access(GEN_SEND_FNAME, R_OK) != -1) {
							if (parse_gen_file(GEN_SEND_FNAME))
								start_gen = 1;
						}
						if (start_gen &&
						    access(GEN_SEND_FNAME, R_OK) == -1)
							start_gen = 0;
					}
				}
			}

			prev_tsc = cur_tsc;
		}

		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_port; i++) {

			portid = qconf->rx_port_list[i];
			nb_rx = rte_eth_rx_burst(portid, queue,
						 pkts_burst, MAX_PKT_BURST);


			port_stats[portid][queue].rx += nb_rx;

			for (j = 0; j < nb_rx; j++) {
				m = pkts_burst[j];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));
// 				if (cvmcs_nic_process_pkt(m) != 99)
//                      		continue;
				l2fwd_simple_forward(m, portid, queue);
			}
			if (nb_rx == 0 && generator_mode == 1) {
				if (gen_port_mask & (1U << portid)) {
					for (j = 0; j < 32; j++)
						l2fwd_simple_send(portid, queue);
				}
			}
		}
	}
}

static int
l2fwd_launch_one_lcore(__attribute__((unused)) void *dummy)
{
	l2fwd_main_loop();
	return 0;
}

static void dump_port_mapping(void)
{
	int i = 0;

	printf("\nPort mapping:\n");
	printf("%-12s%-12s\n", "Src-Port", "Dst-Port");
	while (port_map[i] != RTE_MAX_ETHPORTS) {
		printf("%-12d%-12d\n",
		       i,
		       port_map[i]);
		i++;
	}
	printf("\n");
}

/* display usage */
static void
l2fwd_usage(const char *prgname)
{
	printf("%s [EAL options] -- -p PORTMASK [-q NQ]\n"
	       "  -d: enable debug prints (default disable)\n"
	       "  -g: enable generator mode in this mode file /tmp/gen_send\n"
	       "      will provide pkt size and dpdk port mask for traffic generation\n"
	       "      for ex\n"
	       "      256 0x4\n"
	       "      (packet size 256 and send on port 2)\n"
	       "      received packets are dropped in this mode\n"
	       "  -j: enable jumbo mbufs(for generator)\n"
	       "  -f: configuration map file for sdp to cgx ports\n"
	       "  -r: reflector mode, sends back packets  received on the same port\n"
	       "  -T PERIOD: statistics will be refreshed each PERIOD seconds (0 to disable, 10 default, 86400 maximum)\n",
	       prgname);
}

static int
l2fwd_parse_timer_period(const char *q_arg)
{
	char *end = NULL;
	int n;

	/* parse number string */
	n = strtol(q_arg, &end, 10);
	if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;
	if (n >= MAX_TIMER_PERIOD)
		return -1;

	return n;
}

static const char short_options[] =
	"dgjr"  /* debug */
	"f:"  /* config file */
	"T:"  /* timer period */
	;


enum {
	/* long options mapped to a short option */

	/* first long only option value must be >= 256, so that we won't
	 * conflict with short options */
	CMD_LINE_OPT_MIN_NUM = 256,
};

static const struct option lgopts[] = {
	{NULL, 0, 0, 0}
};

/* Parse the argument given in the command line of the application */
static int
l2fwd_parse_args(int argc, char **argv)
{
	int opt, ret, timer_secs;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, short_options,
				  lgopts, &option_index)) != EOF) {

		switch (opt) {
		case 'r':
			reflector_mode = 1;
			printf("reflector mode enabled\n");
			break;

		/* debug */
		case 'd':
			dump_pkt = 1;
			break;
		case 'g':
			generator_mode = 1;
			printf("generator mode enabled\n");
			break;
		case 'j':
			gen_mbuf_jumbo = 1;
			printf("jumbo mbufs enabled\n");
			break;

		/* timer period */
		case 'T':
			timer_secs = l2fwd_parse_timer_period(optarg);
			if (timer_secs < 0) {
				printf("invalid timer period\n");
				l2fwd_usage(prgname);
				return -1;
			}
			timer_period_sec = timer_secs;
			break;
		case 'f':
			strcpy(conf_file, optarg);
			printf("Conf file %s Selected\n", conf_file);
			break;
		/* long options */

		case 0:
			break;

		default:
			l2fwd_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 1; /* reset getopt lib */
	return ret;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(void)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint16_t portid;
	uint8_t count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;
	int ret;

	printf("\nChecking link status");
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		all_ports_up = 1;
		RTE_ETH_FOREACH_DEV(portid) {
			if (force_quit)
				return;
			memset(&link, 0, sizeof(link));
			ret = rte_eth_link_get_nowait(portid, &link);
			if (ret < 0) {
				all_ports_up = 0;
				if (print_flag == 1)
					printf("Port %u link get failed: %s\n",
						portid, rte_strerror(-ret));
				continue;
			}
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf(
					"Port%d Link Up. Speed %u Mbps - %s\n",
						portid, link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n", portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;
	}
}

static void
prepare_port_mapping(const char *filename)
{
	int row_count = 0, j, field_count = 0, numLines = 0;
	int nb_ports = rte_eth_dev_count_avail();
	char buf[1024], *field;
	FILE *fp;
	unsigned sdp_port = 0;
	unsigned cgx_port = 0;

	for (j = 0; j < nb_ports; j++) {
		port_map[j] = j;
	}
	port_map[j] = RTE_MAX_ETHPORTS;

	fp = fopen(filename, "r");
	if (!fp) {
		printf("Cannot open mapping file %s fall into default mode\n",
			filename);
		goto print_exit;
	}

	while (fgets(buf, 1024, fp))
		numLines++;

	printf("total lines in file: %d\n", numLines);
	if ((numLines < 2) || (numLines > RTE_MAX_ETHPORTS)) {
		printf("not enough maps specified, fall into default mode\n");
		goto print_exit;
	}
	rewind(fp);

	printf("Input Port Map Table\n");
	printf("====================\n");
	while (fgets(buf, 1024, fp)) {
		field_count = 0;
		row_count++;
		cgx_port = 0;
		sdp_port = 0;

		if (row_count == 1)
			continue;

		field = strtok(buf, ",");
		while (field) {
			if (field_count == 0)
				cgx_port =
					 (uint32_t)strtol(field, NULL, 16);
			if (field_count == 1)
				sdp_port =
					 (uint32_t)strtol(field, NULL, 16);
			field = strtok(NULL, ",");
			field_count++;
		}
		port_map[cgx_port] = sdp_port;
		port_map[sdp_port] = cgx_port;
		printf("Index %d ", row_count-1);
		printf("Sdp Port: %d\t", sdp_port);
		printf("cgx Port: %d\n", port_map[sdp_port]);
	}
	fclose(fp);
print_exit:
	printf("====================\n\n");
	printf("Final Port Map Table\n");
	printf("====================\n");
	for (j = 0; j < nb_ports; j++) {
		printf("Source Port:%d dst port %d\n", j, port_map[j]);
	}
	printf("====================\n");
}

static void
init_port_type(void)
{
	struct rte_pci_device *pci_dev;
	uint16_t port;

	RTE_ETH_FOREACH_DEV(port) {
		struct rte_eth_dev *eth_dev = &rte_eth_devices[port];

		pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);
		printf("port %d: device (%x:%x) %u:%u:%u:%u\n",
			port, pci_dev->id.vendor_id, pci_dev->id.device_id,
			pci_dev->addr.domain, pci_dev->addr.bus,
			pci_dev->addr.devid, pci_dev->addr.function);
		if (pci_dev->id.device_id == 0xa063) {
			port_type[port] = PORT_TYPE_NPU_NET;
		} else if (pci_dev->id.device_id == 0xa0f7) {
			if (pci_dev->addr.function == 1)
				port_type[port] = PORT_TYPE_NPU_PCI_PF;
			else
				port_type[port] = PORT_TYPE_NPU_PCI_VF;
		} else {
			printf("Unknown port %d; device (%x:%x) %u:%u:%u:%u\n",
				port,
				pci_dev->id.vendor_id, pci_dev->id.device_id,
				pci_dev->addr.domain, pci_dev->addr.bus,
				pci_dev->addr.devid, pci_dev->addr.function);
		}
	}
}

int
main(int argc, char **argv)
{
	struct lcore_queue_conf *qconf;
	int ret;
	uint16_t nb_ports, core_cnt = 0;
	uint16_t portid;
	unsigned lcore_id;
	unsigned int nb_lcores = 0;
	unsigned int nb_mbufs;
	unsigned i, j, k;
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
			.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
			.split_hdr_size = 0,
			.offloads = (DEV_RX_OFFLOAD_TCP_CKSUM |
					DEV_RX_OFFLOAD_IPV4_CKSUM |
					DEV_RX_OFFLOAD_UDP_CKSUM),
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

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
	argv += ret;

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* parse application arguments (after the EAL ones) */
	ret = l2fwd_parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid L2FWD arguments\n");

	if (generator_mode) {
		if (gen_mbuf_jumbo)
			max_mbuf_size = MBUF_JUMBO_SIZE;
		else
			max_mbuf_size = RTE_MBUF_DEFAULT_BUF_SIZE;
		gen_pktmbuf_pool = rte_pktmbuf_pool_create("gen_mbuf_pool",
				8192U, 0, 0,
				max_mbuf_size, rte_socket_id());
		if (gen_pktmbuf_pool == NULL)
			rte_panic("Cannot init mbuf pool\n");
	}
	/* convert to number of cycles */
	timer_period_hz = timer_period_sec * rte_get_timer_hz();

	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No  ports - bye\n");
	if (nb_ports % 2)
		rte_exit(EXIT_FAILURE, "Odd no. ports  - bye\n");

	/*
	 * Each logical core is assigned a dedicated TX queue on each port.
	 */

	qconf = NULL;
	nb_lcores = rte_lcore_count();
	for (i = 0; i < nb_lcores; i++) {
		if (!rte_lcore_is_enabled(i) == 0)
			core_cnt++;
	}
	l2fwd_queues_per_port = core_cnt;
	if (l2fwd_queues_per_port == 0 || l2fwd_queues_per_port > MAX_QUEUE_PER_PORT)
		rte_exit(EXIT_FAILURE, "wrong queue per port %u - bye\n", l2fwd_queues_per_port);
	printf("queues per port %u\n",  l2fwd_queues_per_port);
	for (i = 0, j = 0; i < nb_lcores; i++) {
		if (rte_lcore_is_enabled(i) == 0)
			continue;
		qconf = &lcore_queue_conf[i];
		qconf->queue = j;
		for (k = 0; k < nb_ports; k++) {
			qconf->rx_port_list[k] = k;
			qconf->n_rx_port++;
			printf("Lcore %u: RX port %u queue %u\n", i, k, j);
		}
		j++;
	}

	nb_mbufs = RTE_MAX(nb_ports * (nb_rxd + nb_txd + MAX_PKT_BURST +
		nb_lcores * MEMPOOL_CACHE_SIZE), 8192U);

	/* create the mbuf pool */
	l2fwd_pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", nb_mbufs,
		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());
	if (l2fwd_pktmbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");


	init_port_type();
	prepare_port_mapping(conf_file);
	dump_port_mapping();

	/* Initialise each port */
	RTE_ETH_FOREACH_DEV(portid) {
		struct rte_eth_rxconf rxq_conf;
		struct rte_eth_txconf txq_conf;
		struct rte_eth_conf local_port_conf = port_conf;
		struct rte_eth_dev_info dev_info;
		char if_name[RTE_ETH_NAME_MAX_LEN];

		/* init port */
		ret = rte_eth_dev_get_name_by_port(portid, if_name);
		if (ret < 0) {
			printf("Error: ifname not found for portid-%d\n", portid);
			continue;
		}
		printf("Initializing port %u; ifname = %s... ", portid, if_name);

		ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret != 0)
			rte_panic("Error during getting device (port %u) info: %s\n",
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

		/* local_port_conf.link_speeds =
			rte_eth_devices[portid].data->dev_conf.link_speeds;
		*/
		ret = rte_eth_dev_configure(portid, l2fwd_queues_per_port, l2fwd_queues_per_port, &local_port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
				  ret, portid);


		if ((port_type[portid] == PORT_TYPE_NPU_PCI_PF) ||
		    (port_type[portid] == PORT_TYPE_NPU_PCI_VF)) {
			/* fix MTU for jumbo frames */
			ret = rte_eth_dev_set_mtu(portid, dev_info.max_mtu);
			if (ret < 0)
				 rte_exit(EXIT_FAILURE, "Cannot set mtu to 1500\n");
		}

		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd,
						       &nb_txd);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, portid);

		/* Update Hash with new RSS Key */
		local_port_conf.rx_adv_conf.rss_conf.rss_key_len =
							dev_info.hash_key_size;
		rte_eth_dev_rss_hash_update(portid,
					&local_port_conf.rx_adv_conf.rss_conf);

		ret = rte_eth_macaddr_get(portid,
					  &l2fwd_ports_eth_addr[portid]);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot get MAC address: err=%d, port=%u\n",
				 ret, portid);

		for (i = 0; i < l2fwd_queues_per_port; i++) {
			rxq_conf = dev_info.default_rxconf;
			rxq_conf.offloads = local_port_conf.rxmode.offloads;
			ret = rte_eth_rx_queue_setup(portid, i, nb_rxd,
						     rte_eth_dev_socket_id(portid),
						     &rxq_conf,
						     l2fwd_pktmbuf_pool);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
					  ret, portid);

			/* init one TX queue on each port */
			txq_conf = dev_info.default_txconf;
			txq_conf.offloads = local_port_conf.txmode.offloads;
			ret = rte_eth_tx_queue_setup(portid, i, nb_txd,
					rte_eth_dev_socket_id(portid),
					&txq_conf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
					ret, portid);
			/* Initialize TX buffers */
			tx_buffer[portid][i] = rte_zmalloc_socket("tx_buffer",
					RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST), 0,
					rte_eth_dev_socket_id(portid));
			if (tx_buffer[portid][i] == NULL)
				rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
						portid);

			ret = rte_eth_tx_buffer_set_err_callback(tx_buffer[portid][i],
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
		printf("done: \n");

		ret = rte_eth_promiscuous_enable(portid);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				 "rte_eth_promiscuous_enable:err=%s, port=%u\n",
				 rte_strerror(-ret), portid);

		printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				portid,
				l2fwd_ports_eth_addr[portid].addr_bytes[0],
				l2fwd_ports_eth_addr[portid].addr_bytes[1],
				l2fwd_ports_eth_addr[portid].addr_bytes[2],
				l2fwd_ports_eth_addr[portid].addr_bytes[3],
				l2fwd_ports_eth_addr[portid].addr_bytes[4],
				l2fwd_ports_eth_addr[portid].addr_bytes[5]);

		/* initialize port stats */
		memset(&port_stats, 0, sizeof(port_stats));
	}

	check_all_ports_link_status();

	ret = 0;
	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(l2fwd_launch_one_lcore, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
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
