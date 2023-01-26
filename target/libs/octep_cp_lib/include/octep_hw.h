/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OCTEP_HW_H__
#define __OCTEP_HW_H__

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

	/* Multicast packets received. */
	uint64_t mcast_pkts;

	/* Broadcast packets received. */
	uint64_t bcast_pkts;
};

/* Hardware interface Tx statistics */
struct octep_iface_tx_stats {
	/* Total frames sent on the interface */
	uint64_t pkts;

	/* Total octets sent on the interface */
	uint64_t octs;

	/* Packets sent to a broadcast DMAC */
	uint64_t bcst;

	/* Packets sent to the multicast DMAC */
	uint64_t mcst;

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

	/* Packets sent that experienced a transmit underflow and were
	 * truncated
	 */
	uint64_t undflw;

	/* Control/PAUSE packets sent */
	uint64_t ctl;
};

/* Info from firmware */
struct octep_fw_info {
	/* interface pkind */
	uint16_t pkind;

	/* pf heartbeat interval in milliseconds */
	uint16_t hb_interval;

	/* pf heartbeat miss count */
	uint16_t hb_miss_count;

	/* reserved */
	uint16_t reserved1;

	/* supported offloads */
	uint64_t offloads[2];

	/* supported features */
	uint64_t features[2];

	/* reserved */
	uint64_t reserved2[3];
};

#endif /* __OCTEP_HW_H__ */
