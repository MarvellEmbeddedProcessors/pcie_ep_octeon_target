/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CNXK_HW_H__
#define __CNXK_HW_H__

/* pem0 bar4 index8 address, this is a chunk of 4mb */
#define PEMX_BASE(a)			(0x8E0000000000ull | \
					 (unsigned long long)a<<36)
#define PEMX_CFG_WR_OFFSET		0x18
#define PEMX_CFG_WR_DATA		32
#define PEMX_CFG_WR_PF			18
#define PEMX_CFG_WR_REG			0
/*
 * This register only supported on cn10k.
 * The documentation for this register is not clear, and the current
 * implementation works for 0x418, and should work for all multiple
 * of 8 addresses.  It has not been tested for multiple of 4 addresses,
 * nor for addresses with bit 16 set.
 */
#define PEMX_PFX_CSX_PFCFGX(pem, pf, offset)      ((0x8e0000008000 | \
						   (uint64_t)pem << 36 \
						| pf << 18 \
						| ((offset >> 16) & 1) << 16 \
						| (offset >> 3) << 3) \
						+ (((offset >> 2) & 1) << 2))

#define BAR4_IDX_OFFSET(i)		(0x700ull | i<<3)

#define PCIEEP_VSECST_CTL		({		\
			uint64_t offset;		\
			offset = 0x4d0;			\
			if (is_cn10k())			\
				offset = 0x418;		\
			offset; })

/* oei trig interrupt register contents */
union sdp_epf_oei_trig {
	uint64_t u64;
	struct {
		uint64_t bit_num:6;
		uint64_t rsvd2:12;
		uint64_t clr:1;
		uint64_t set:1;
		uint64_t rsvd:44;
	} s;
};

/* Hardware interface Rx statistics */
struct octep_iface_rx_stats {
	/* Received packets */
	uint64_t pkts;

	/* Octets of received packets */
	uint64_t octets;

	/* Received PAUSE and Control packets */
	uint64_t pause_pkts;

	/* Received PAUSE and Control octets */
	uint64_t pause_octets;

	/* Filtered DMAC0 packets */
	uint64_t dmac0_pkts;

	/* Filtered DMAC0 octets */
	uint64_t dmac0_octets;

	/* Packets dropped due to RX FIFO full */
	uint64_t dropped_pkts_fifo_full;

	/* Octets dropped due to RX FIFO full */
	uint64_t dropped_octets_fifo_full;

	/* Error packets */
	uint64_t err_pkts;

	/* Filtered DMAC1 packets */
	uint64_t dmac1_pkts;

	/* Filtered DMAC1 octets */
	uint64_t dmac1_octets;

	/* NCSI-bound packets dropped */
	uint64_t ncsi_dropped_pkts;

	/* NCSI-bound octets dropped */
	uint64_t ncsi_dropped_octets;
};

/* Hardware interface Tx statistics */
struct octep_iface_tx_stats {
	/* Packets dropped due to excessive collisions */
	uint64_t xscol;

	/* Packets dropped due to excessive deferral */
	uint64_t xsdef;

	/* Packets sent that experienced multiple collisions before successful
	 * transmission
	 */
	uint64_t mcol;

	/* Packets sent that experienced a single collision before successful
	 * transmission
	 */
	uint64_t scol;

	/* Total octets sent on the interface */
	uint64_t octs;

	/* Total frames sent on the interface */
	uint64_t pkts;

	/* Packets sent with an octet count < 64 */
	uint64_t hist_lt64;

	/* Packets sent with an octet count == 64 */
	uint64_t hist_eq64;

	/* Packets sent with an octet count of 65–127 */
	uint64_t hist_65to127;

	/* Packets sent with an octet count of 128–255 */
	uint64_t hist_128to255;

	/* Packets sent with an octet count of 256–511 */
	uint64_t hist_256to511;

	/* Packets sent with an octet count of 512–1023 */
	uint64_t hist_512to1023;

	/* Packets sent with an octet count of 1024-1518 */
	uint64_t hist_1024to1518;

	/* Packets sent with an octet count of > 1518 */
	uint64_t hist_gt1518;

	/* Packets sent to a broadcast DMAC */
	uint64_t bcst;

	/* Packets sent to the multicast DMAC */
	uint64_t mcst;

	/* Packets sent that experienced a transmit underflow and were
	 * truncated
	 */
	uint64_t undflw;

	/* Control/PAUSE packets sent */
	uint64_t ctl;
};

#endif /* __CNXK_HW_H__ */
