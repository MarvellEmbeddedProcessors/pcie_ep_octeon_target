/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __SDP_MDATA_H__
#define __SDP_MDATA_H__

struct sdp_tx_mdata {
	/* rsvd till we support lso and other offloads */
	uint64_t rsvd[3];
};


#define SDP_RX_MDATA_L3_CSUM_VERIFIED 0x1
#define SDP_RX_MDATA_L4_CSUM_VERIFIED 0x2
union sdp_rx_mdata {
	uint64_t u64;
	struct {
#if __BYTE_ORDER == __LITTLE_ENDIAN
		/* reserved */
		uint64_t rsvd1 : 18;
		/* checksum verified . */
		uint64_t csum_verified : 2;
		/* The destination port or driver-specific queue number. */
		uint64_t rsvd2 : 44;
#else
		uint64_t rsvd2 : 44;
		/* checksum verified . */
		uint64_t csum_verified : 2;
		/* reserved2 */
		uint64_t rsvd1 : 18;
#endif /* __BYTE_ORDER */
	} s;
};

#endif /* __SDP_MDATA_H__ */
