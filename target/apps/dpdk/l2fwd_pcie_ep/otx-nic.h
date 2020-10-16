/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OTX_NIC_H__
#define __OTX_NIC_H__

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <rte_mbuf.h>

#include "otx-app-common.h"
#include "otx-drv.h"

#define MAX_OCTEON_ETH_PORTS		32
#define MAX_OCTEON_VNICS		64
#define MAX_PKI_CLUSTERS		4

#define OCTNET_MIN_FRM_SIZE		64
#define OCTNET_MAX_FRM_SIZE		16018
#define OCTNET_DEFAULT_FRM_SIZE		1518

/* NIC Command types */
#define OCTNET_CMD_CHANGE_MTU		0x1
#define OCTNET_CMD_CHANGE_MACADDR	0x2

/*
   wqe
   ---------------  0
 |  wqe  word0-3 |
   ---------------  32
 |    PCI IH     |
   ---------------  40
 |     RPTR      |
   ---------------  48
 |    PCI IRH     |
   ---------------  56
 |  OCT_NET_CMD  |
   ---------------  64
 | Addtl 8-BData |
 |               |
   ---------------
 */

typedef union {
	uint64_t u64;
	struct {
#if RTE_BYTE_ORDER == RTE_BIG_ENDIAN
		uint64_t cmd:5;
		uint64_t more:3;
		uint64_t param1:32;
		uint64_t param2:16;
		uint64_t param3:8;
#else
		uint64_t param3:8;
		uint64_t param2:16;
		uint64_t param1:32;
		uint64_t more:3;
		uint64_t cmd:5;

#endif
	} s;

} octnet_cmd_t;

#define   OCTNET_CMD_SIZE     (sizeof(octnet_cmd_t))

/* Store the meta info of the received the pkt.
 * It will get filled by lookup function.
 * It will be used by the process_wqe function 
 */
typedef struct cvmcs_nic_pkt_meta {

	/* set to '1' for mcast/bcast pkts, set to '0' for unicast pkts */
	int8_t is_bitmap;

	/* to_wire is set for mcast/bcast pkts */
	uint8_t to_wire;

	uint8_t offload_csum;

	uint8_t csum_verified;

	/* set to '1' for the pkts received from host */
	uint8_t from_host;

	/* pknd of the port on which these pkt is received. */
	uint8_t pknd;

	/* base queue of the vif to which these pkt is sent. */
	//int16_t      baseq;

	/* indicates the index in vnic_map array. */
	uint8_t map_index;

	/* Pointer to start of pkt data / l2 hdr */
	uint8_t *pkt_start;

	/* Offset from start of pkt to l2 hdr */
	uint16_t l2_offset;

	/* Offset from start of pkt to l3 hdr */
	uint16_t l3_offset;

	/* Offset from start of packet to l4 hdr */
	uint16_t l4_offset;

	/* Offset from start of packet to actual data */
	uint16_t header_len;

	/* Indicatesthe type of packet. IPv4/IPv6/TCP */
	uint32_t flags;

	uint32_t rsvd;

	/* Contains information for tso packets */
	uint64_t exhdr;

	/* for bcast/mcast pkts: indicates bit mask of OQs of vif ports
	 * for uni-casts pkts: indicates the base queue of the vif
	 */
	uint64_t port_map;

	/* In case of bcast or mcast, indicates the pf, the vf belongs to */
	uint64_t pf_map;

} cvmcs_nic_pkt_meta_t;

/* wqe related meta data info - used while sending pkts to Host/Guest
* Total size of this structut must not excced 88 (11 8B words) bytes 
*/
typedef struct {

	/* points the next data buffer. */

	cvmx_pki_buflink_t packet_ptr;

	/* response header for the data pkt. */
	cvmcs_resp_hdr_t resp_hdr;

	cvmcs_nic_pkt_meta_t meta_data;

	/* opcode to check for wqe's with software bit is set */
	uint64_t opcode2;

	uint64_t rsvd0[2];

	/* denotes the count that PKO decements after sending each command. */
	uint64_t outstanding_cmds;

} cvmcs_nic_wqe_data_t;

/* Added this structure to access word6 of wqe to store cvmcs_nic_wqe_data. 
 * Word6 of wqe is defined as a bit field. Compiler wont allow to access its
 * address. Just typecast wqe to this structure pointer and access address.
 * */
typedef struct {
	uint64_t u64[16];
}wqe_resp_hdr_t;

inline uint64_t cvmcs_swap64(uint64_t x)
{
	return ((x >> 56) |
		(((x >> 48) & 0xfful) << 8) |
		(((x >> 40) & 0xfful) << 16) |
		(((x >> 32) & 0xfful) << 24) |
		(((x >> 24) & 0xfful) << 32) |
		(((x >> 16) & 0xfful) << 40) |
		(((x >> 8) & 0xfful) << 48) | (((x >> 0) & 0xfful) << 56));
}

int sdp_bp_set(void);
int cvmcs_nic_process_pkt(struct rte_mbuf * pkt);
#endif
