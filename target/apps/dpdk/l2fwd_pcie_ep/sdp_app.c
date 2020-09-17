#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include "otx-drv.h"
#include "otx-nic.h"
#include "otx-app-common.h"

uint64_t cpu_freq = 0;
uint64_t coproc_freq = 0;


uint16_t out_port_arr[2] = { 1, 0 };


static inline int
packet_push_head(struct rte_mbuf *pkt, uint32_t len)
{
        volatile cvmcs_resp_hdr_t *resp_ptr = NULL;

	resp_ptr = (cvmcs_resp_hdr_t *)rte_pktmbuf_prepend(pkt, len);
	if (!resp_ptr) {
		printf("%s No head room available\n", __func__);
		return 1;
	}
	resp_ptr->u64 = 0;
	resp_ptr->s.opcode = CORE_NW_DATA_OP;
	resp_ptr->s.destqport = 0;
	/* checksum notification to Host */
	resp_ptr->s.csum_verified = 1;
	return 0;
}

static inline void
packet_pull_head(struct rte_mbuf *pkt, uint32_t len)
{

	rte_pktmbuf_adj(pkt, len);
}

/* Forwards any packets that need to go to Hosts,
 * Handles unicast, multicast and broadcast pkts
 */
static inline int
cvmcs_nic_send_to_vnic(struct rte_mbuf * pkt)
{
	int port;
	/* Add resp header */ 
	if (packet_push_head(pkt, CVMX_RESP_HDR_SIZE))
		return 0;

//	rte_pktmbuf_dump(stdout, pkt, rte_pktmbuf_pkt_len(pkt));
	port = pkt->port;
	pkt->port = out_port_arr[port];
	//printf("Incomig Port %d outgoing port %d\n", port, pkt->port);
	//pkt->port = 0;
	return 99;

}

static inline int
cvmcs_nic_add_vif_to_nic(struct rte_mbuf * pkt __attribute__((unused)))
{
	return 0;
}


/* Process wqe for O3 targets */
int cvmcs_nic_process_pkt(struct rte_mbuf * pkt)
{
	cvmcs_raw_inst_front_t *front;
	uint32_t port = pkt->port;
		
	//rte_pktmbuf_dump(stdout, pkt, rte_pktmbuf_pkt_len(pkt));
	front = (cvmcs_raw_inst_front_t *) rte_pktmbuf_mtod(pkt, void *);

	/* if pkt is bcast/mcast or uincast pkt. */
	if (!port) {
//		printf("%s From wire, sending to Host\n", __func__);
		return cvmcs_nic_send_to_vnic(pkt);
	}
	
	/* Control will reach here, when pkt from host and it is uincast or ctrl cmd. 
	 * In both the cases, fsz will not be stripped down by the lookup.  
	 */
	switch (front->irh.s.opcode) {
	case OCT_NW_PKT_OP:
//		printf("%s network packet\n", __func__);
		/* Remove FSZ */
		packet_pull_head(pkt, CVM_RAW_FRONT_SIZE);	
		pkt->port = out_port_arr[port];
		//printf("Incomig Port %d outgoing port %d\n", port, pkt->port);
		//pkt->port = 1;
		return 99;

	default:
		printf("%s Add handler to support opcode %d\n",
			__func__, front->irh.s.opcode);
		rte_pktmbuf_free(pkt);
	}

	return 0;
}
