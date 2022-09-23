/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __CNXK_HW_H__
#define __CNXK_HW_H__

#define PEMX_BASE(a)			(0x8E0000000000ull | a<<36)

/* pemX bar4 index, this is a chunk of 4mb */
#define BAR4_INDEX(i)			(0x700ull | i<<3)

#define PEMX_CFG_WR_OFFSET		0x18
#define PEMX_CFG_WR_DATA		32
#define PEMX_CFG_WR_PF			18
#define PEMX_CFG_WR_REG			0

#define PCIEEP_VSECST_CTL		(0x4d0ull)

/*
 * This register only supported on cn10k.
 * The documentation for this register is not clear, and the current
 * implementation works for 0x418, and should work for all multiple
 * of 8 addresses.  It has not been tested for multiple of 4 addresses,
 * nor for addresses with bit 16 set.
 */
#define PEMX_PFX_CSX_PFCFGX(pem, pf, offset)    ((0x8e0000008000 | \
						  (uint64_t)pem << 36 | \
						  pf << 18 | \
						  ((offset >> 16) & 1) << 16 | \
						  (offset >> 3) << 3) + \
						 (((offset >> 2) & 1) << 2))

#define CN10K_PCIEEP_VSECST_CTL			(0x418ull)


/* oei trig interrupt register */
#define SDP0_EPFX_OEI_TRIG(pf)		(0x86E0C0000000ull | (pf << 25))

enum sdp_epf_oei_trig_bit {
	SDP_EPF_OEI_TRIG_BIT_MBOX,
	SDP_EPF_OEI_TRIG_BIT_HEARTBEAT
};

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

#endif /* __CNXK_HW_H__ */
