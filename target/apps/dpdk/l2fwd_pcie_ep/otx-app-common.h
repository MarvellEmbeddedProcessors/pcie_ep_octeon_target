/*
 *
 * CNNIC SDK
 *
 * Copyright (c) 2017 Cavium Networks. All rights reserved.
 *
 * This file, which is part of the CNNIC SDK which also includes the
 * CNNIC SDK Package from Cavium Networks, contains proprietary and
 * confidential information of Cavium Networks and in some cases its
 * suppliers. 
 *
 * Any licensed reproduction, distribution, modification, or other use of
 * this file or the confidential information or patented inventions
 * embodied in this file is subject to your license agreement with Cavium
 * Networks. Unless you and Cavium Networks have agreed otherwise in
 * writing, the applicable license terms "OCTEON SDK License Type 5" can be
 * found under the directory: $CNNIC_ROOT/licenses/
 *
 * All other use and disclosure is prohibited.
 *
 * Contact Cavium Networks at info@caviumnetworks.com for more information.
 *
 */
#ifndef __OTX_APP_COMMON_H__
#define __OTX_APP_COMMON_H__

//#define OCTEON_DEBUG_LEVEL 1
/* If OCTEON_DEBUG_LEVEL is defined all 
   application debug messages are enabled. */
#ifdef OCTEON_DEBUG_LEVEL
#define DBG                      printf
#else
#define DBG(format, args...)     do{ }while(0)
#endif

#define SDP_PKND  2
#define BGX_PKND  1
#define SDP_PKI_PORT_OFFSET 0x400
#define BGX_PKI_PORT_OFFSET 0x800

#define SLI0_S2M_REGX_ACC(i)    (0x874001000000UL |  (i << 4))
#define SLI0_M2S_MAC0_CTL	 0x874001002100UL

union sli_s2m_op_s {
	uint64_t u64;
	struct {
		uint64_t addr       :32;
		uint64_t region     :8;
		uint64_t did_hi     :4;
		uint64_t node       :2;
		uint64_t rsvd2      :1;
		uint64_t io         :1;
		uint64_t rsvd1      :16;
	} s;
};

#define CTYPE_PCI_MEMORY 0
#define CTYPE_PCI_CONF   1
#define CTYPE_PCI_IO     2

#define RWTYPE_RELAXED_ORDER 0
#define RWTYPE_NO_SNOOP      1

#define PAGE_SIZE	(64 * 1024)

union sli0_s2m_regx_acc {
	uint64_t u64;
	struct {
		uint64_t ba         :32;
		uint64_t rsvd3      :8;
		uint64_t rtype      :2;
		uint64_t wtype      :2;
		uint64_t rsvd2      :4;
		uint64_t nmerge     :1;
		uint64_t epf        :3;
		uint64_t zero       :1;
		uint64_t ctype      :2;
		uint64_t rsvd1      :9;
	} s;
};

typedef struct {
	union {
		uint64_t u64;
		struct {
			uint64_t size : 16;
			uint64_t rsvd : 15;
			uint64_t i : 1;
			uint64_t sw : 32;
		}v1;
	} word0;
	union {
		uint64_t u64;
		struct {
			uint64_t addr : 64;
		}v2;
	} word1;
} cvm_pki_buflink_t;

/**
 * Structure pki_wqe_s
 *
 * PKI Work-Queue Entry Structure
 * This section is a description of each field in WORD0 - WORD4 of the work-queue entry.
 */
union cvm_pki_wqe_s {
	uint64_t u[16];
	struct cvm_pki_wqe_s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t node:2;
		uint64_t reserved_60_61:2;
		uint64_t aura:12;
		uint64_t reserved_47:1;
		uint64_t apad:3;
		uint64_t chan:12;
		uint64_t bufs:8;
		uint64_t style:8;
		uint64_t reserved_6_15:10;
		uint64_t pknd:6;
#else				/* Word 0 - Little Endian */
		uint64_t pknd:6;
		uint64_t reserved_6_15:10;
		uint64_t style:8;
		uint64_t bufs:8;
		uint64_t chan:12;
		uint64_t apad:3;
		uint64_t reserved_47:1;
		uint64_t aura:12;
		uint64_t reserved_60_61:2;
		uint64_t node:2;
#endif				/* Word 0 - End */
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t len:16;
		uint64_t reserved_108_111:4;
		uint64_t grp:10;
		uint64_t tt:2;
		uint64_t tag:32;
#else				/* Word 1 - Little Endian */
		uint64_t tag:32;
		uint64_t tt:2;
		uint64_t grp:10;
		uint64_t reserved_108_111:4;
		uint64_t len:16;
#endif				/* Word 1 - End */
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t sw:1;
		uint64_t lgty:5;
		uint64_t lfty:5;
		uint64_t lety:5;
		uint64_t ldty:5;
		uint64_t lcty:5;
		uint64_t lbty:5;
		uint64_t lae:1;
		uint64_t reserved_152_159:8;
		uint64_t vv:1;
		uint64_t vs:1;
		uint64_t sh:1;
		uint64_t pf4:1;
		uint64_t pf3:1;
		uint64_t pf2:1;
		uint64_t pf1:1;
		uint64_t l3fr:1;
		uint64_t l3b:1;
		uint64_t l3m:1;
		uint64_t l2b:1;
		uint64_t l2m:1;
		uint64_t raw:1;
		uint64_t errlev:3;
		uint64_t opcode:8;
#else				/* Word 2 - Little Endian */
		uint64_t opcode:8;
		uint64_t errlev:3;
		uint64_t raw:1;
		uint64_t l2m:1;
		uint64_t l2b:1;
		uint64_t l3m:1;
		uint64_t l3b:1;
		uint64_t l3fr:1;
		uint64_t pf1:1;
		uint64_t pf2:1;
		uint64_t pf3:1;
		uint64_t pf4:1;
		uint64_t sh:1;
		uint64_t vs:1;
		uint64_t vv:1;
		uint64_t reserved_152_159:8;
		uint64_t lae:1;
		uint64_t lbty:5;
		uint64_t lcty:5;
		uint64_t ldty:5;
		uint64_t lety:5;
		uint64_t lfty:5;
		uint64_t lgty:5;
		uint64_t sw:1;
#endif				/* Word 2 - End */
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t addr:64;
#else				/* Word 3 - Little Endian */
		uint64_t addr:64;
#endif				/* Word 3 - End */
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t vlptr:8;
		uint64_t lgptr:8;
		uint64_t lfptr:8;
		uint64_t leptr:8;
		uint64_t ldptr:8;
		uint64_t lcptr:8;
		uint64_t lbptr:8;
		uint64_t laptr:8;
#else				/* Word 4 - Little Endian */
		uint64_t laptr:8;
		uint64_t lbptr:8;
		uint64_t lcptr:8;
		uint64_t ldptr:8;
		uint64_t leptr:8;
		uint64_t lfptr:8;
		uint64_t lgptr:8;
		uint64_t vlptr:8;
#endif				/* Word 4 - End */
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t size:16;
		uint64_t dwd:1;
		uint64_t reserved_320_366:47;
#else				/* Word 5 - Little Endian */
		uint64_t reserved_320_366:47;
		uint64_t dwd:1;
		uint64_t size:16;
#endif				/* Word 5 - End */
		uint64_t w6:64;		     /**< [447:384] Packet data may use this word. */
		uint64_t w7:64;		     /**< [511:448] Packet data may use this word. */
		uint64_t w8:64;		     /**< [575:512] Packet data may use this word. */
		uint64_t w9:64;		     /**< [639:576] Packet data may use this word. */
		uint64_t w10:64;	     /**< [703:640] Packet data may use this word. */
		uint64_t w11:64;	     /**< [767:704] Packet data may use this word. */
		uint64_t w12:64;	     /**< [831:768] Packet data may use this word. */
		uint64_t w13:64;	     /**< [895:832] Packet data may use this word. */
		uint64_t w14:64;	     /**< [959:896] Packet data may use this word. */
		uint64_t w15:64;	     /**< [1023:960] Packet data may use this word. */
	} s;
	/* struct cvm_pki_wqe_s_s cn; */
};
typedef union cvm_pki_wqe_s cvm_pki_wqe_t;

inline void *
octeontx_iova2va(uint64_t iova)
{ 
	uint64_t p = iova;

	if (p & (1ul << 48))
		p |= 0xfffful << 48;
	return (void *)p;
}

#define cvmx_phys_to_ptr(phy_addr)	octeontx_iova2va(phy_addr)
#define cvmx_pki_buflink_t		cvm_pki_buflink_t

void host_writel(uint64_t host_addr, uint64_t val);
int dma_api_init_module(void);

#endif
