/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OTX_DRV_H__
#define __OTX_DRV_H__

#include "otx-app-common.h"

#define  DPI_IPD_PORT_OFFSET     1024

/* Opcodes 0x1200-0x12FF reserved for the NIC module. */
#define OCT_NW_PKT_OP			0x1220
#define OCT_NW_CMD_OP			0x1221
#define HOST_NW_INFO_OP			0x1222
#define HOST_PORT_STATS_OP		0x1223
#define OCT_NIC_MBCAST_COMPLETION_OP	0x1224
#define HOST_NW_STOP_OP			0x1225

/* These two opcodes are used by NIC core application. */
#define CORE_NW_DATA_OP			0x8003
#define CORE_NW_INFO_OP			0x8004

/** Core: The (Packet) Instruction Header appears in the format shown below for 
  * Octeon. Refer to the Octeon HW Manual to read more about the 
  * conversion from PCI instruction header to Packet Instruction Header.
  */
typedef union {
	uint64_t u64;
	struct {
#if __BYTE_ORDER == __LITTLE_ENDIAN
		uint64_t tag : 32; /* 31-00 Tag for the Packet. */
		uint64_t tt : 2; /* 33-32 Tagtype */
		uint64_t rs : 1; /* 34    Is the PCI packet a RAW-SHORT? */
		uint64_t grp : 4; /* 38-35 The group that gets this Packet */
		uint64_t qos : 3; /* 41-39 The QOS set for this Packet. */
		uint64_t rsvd3 : 6; /* 47-42 Reserved */
		uint64_t sl : 7; /* 54-48 Skip Length */
		uint64_t rsvd2 : 1; /* 55    Reserved. */
		uint64_t pm : 2; /* 57-56 The parse mode to use for the packet. */
		uint64_t rsvd1 : 5; /* 62-58 Reserved */
		uint64_t r : 1; /* 63    Is the PCI packet in RAW-mode? */
#else
		uint64_t r : 1; /* 63    Is the PCI packet in RAW-mode? */
		uint64_t rsvd1 : 5; /* 62-58 Reserved */
		uint64_t pm : 2; /* 57-56 The parse mode to use for the packet. */
		uint64_t rsvd2 : 1; /* 55    Reserved. */
		uint64_t sl : 7; /* 54-48 Skip Length */
		uint64_t rsvd3 : 6; /* 47-42 Reserved */
		uint64_t qos : 3; /* 41-39 The QOS set for this Packet. */
		uint64_t grp : 4; /* 38-35 The group that gets this Packet */
		uint64_t rs : 1; /* 34    Is the PCI packet a RAW-SHORT? */
		uint64_t tt : 2; /* 33-32 Tagtype */
		uint64_t tag : 32; /* 31-00 Tag for the Packet. */
#endif
	} s;
} cvm_pci_inst_hdr2_t;

/** Core: Format of the input request header in an instruction. */
typedef union {

	uint64_t u64;
	struct {
#if __BYTE_ORDER == __LITTLE_ENDIAN
		uint64_t rid : 16; /* Request ID  */
		uint64_t pcie_port : 3; /* PCIe port to send response. */
		uint64_t scatter : 1; /* Scatter indicator  1=scatter */
		uint64_t rlenssz : 14; /* Size of Expected result OR no. of entries in scatter list */
		uint64_t dport : 6; /* Desired destination port for result */
		uint64_t param : 8; /* Opcode Specific parameters */
		uint64_t opcode : 16; /* Opcode for the return packet  */
#else
		uint64_t opcode : 16; /* Opcode for the return packet  */
		uint64_t param : 8; /* Opcode Specific parameters */
		uint64_t dport : 6; /* Desired destination port for result */
		uint64_t rlenssz : 14; /* Size of Expected result OR no. of entries in scatter list */
		uint64_t scatter : 1; /* Scatter indicator  1=scatter */
		uint64_t pcie_port : 3;	/* PCIe port to send response. */
		uint64_t rid : 16; /* Request ID  */
#endif
	} s;

} cvmcs_pci_inst_irh_t;

/** Core: Format of the front data for a raw instruction in the first 24 bytes
 *  of the wqe->packet_data or packet ptr when a core gets work from a PCI input
 *  port.
 */
typedef struct {

	/* The instruction header. */
//	cvm_pci_inst_hdr2_t ih;
	/* The host physical address where a response (if any) is expected. */
	uint64_t rptr;
	/* The input request header. */
	cvmcs_pci_inst_irh_t irh;
	uint64_t exhdr;
} cvmcs_raw_inst_front_t;

#define CVM_RAW_FRONT_SIZE   (sizeof(cvmcs_raw_inst_front_t))

/** Core: Format of the response header in the first 8 bytes of response sent
 * to the host. */
typedef union {
	uint64_t u64;
	struct {
#if __BYTE_ORDER == __LITTLE_ENDIAN
		/* The request id of the host instruction. */
		uint64_t request_id : 16;
		/* Reserved. */
		uint64_t reserved : 2;
		/* checksum verified . */
		uint64_t csum_verified : 2;
		/* The destination port or driver-specific queue number. */
		uint64_t destqport : 22;
		/* The response is for a host instruction that arrived at this port. */
		uint64_t sport : 6;
		/* Opcode tells the host the type of response. */
		uint64_t opcode:16;
#else
		/* Opcode tells the host the type of response. */
		uint64_t opcode : 16;
		/* The response is for a host instruction that arrived at this port. */
		uint64_t sport : 6;
		/* The destination port or driver-specific queue number. */
		uint64_t destqport : 22;
		/* checksum verified . */
		uint64_t csum_verified : 2;
		/* Reserved. */
		uint64_t reserved : 2;
		/* The request id of the host instruction. */
		uint64_t request_id : 16;
#endif
	} s;
} cvmcs_resp_hdr_t;

#define  CVMX_RESP_HDR_SIZE   (sizeof(cvmcs_resp_hdr_t))
#endif
