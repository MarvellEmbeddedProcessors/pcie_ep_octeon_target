/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

#include "cavium_sysdep.h"
#include "octeon_hw.h"
#include "cn83xx_pf_device.h"
#include "cn83xx_vf_device.h"
#include "octeon_macros.h"

#ifdef IOQ_PERF_MODE_O3
extern int droq_test_size;
#endif

int cn83xx_droq_intr_handler(octeon_ioq_vector_t * ioq_vector)
{
	octeon_droq_t *droq = ioq_vector->droq;

	/* TODO: eliminate poll_mode completely */
	if (droq->ops.poll_mode)
		droq->ops.napi_fun((void *)droq);
	else
		BUG_ON(1);
	return 0;
}

void cn83xx_iq_intr_handler(octeon_ioq_vector_t * ioq_vector)
{
	octeon_instr_queue_t *iq = ioq_vector->iq;

	/** Writing back cnt val to subtract cnt and clear the interrupt */
	OCTEON_WRITE64(iq->inst_cnt_reg, OCTEON_READ64(iq->inst_cnt_reg));
}

/* $Id$ */
