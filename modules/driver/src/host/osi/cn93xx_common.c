
#include "cavium_sysdep.h"
#include "octeon_hw.h"
#include "cn93xx_pf_device.h"
#include "cn93xx_vf_device.h"
#include "octeon_macros.h"

#ifdef IOQ_PERF_MODE_O3
extern int droq_test_size;
#endif

int cn93xx_droq_intr_handler(octeon_ioq_vector_t * ioq_vector)
{
	octeon_device_t *oct = ioq_vector->oct_dev;
	octeon_droq_t *droq;
	uint32_t pkt_count = 0, schedule = 0;

	droq = ioq_vector->droq;
	//printk("oq_no:%d droq_q_no:%d core:%d\n",oq_no, droq->q_no, smp_processor_id());
	pkt_count = octeon_droq_check_hw_for_pkts(oct, droq);
	if (pkt_count) {
		if (droq->ops.poll_mode) {
			schedule = 0;
			droq->ops.napi_fun((void *)droq);
		} else {
			schedule = 1;

#ifdef IOQ_PERF_MODE_O3
			/* Update the stats and return the credits here itself 
			 * Dont process any pkts */
			OCTEON_WRITE32(droq->pkts_credit_reg, pkt_count);
			droq->stats.bytes_received +=
			    droq_test_size * pkt_count;
			droq->stats.pkts_received += pkt_count;
			droq->stats.dropped_toomany += pkt_count;
#else

#if defined(USE_DROQ_THREADS)
			cavium_wakeup(&droq->wc);
#endif

#endif
		}
	}

#ifndef OCT_NIC_USE_NAPI

#ifdef OCT_TX2_ISM_INT
	/* Write output counts host memory address to 0 */
	*(uint64_t *)(ioq_vector->droq->ism.pkt_cnt_addr) = 0;
#endif

	/* writing back pkt_count and INTR_RESEND bits. */
	OCTEON_WRITE64(ioq_vector->droq->pkts_sent_reg,
		       (CN93XX_INTR_R_RESEND | pkt_count));

#endif

	return 0;
}

void cn93xx_iq_intr_handler(octeon_ioq_vector_t * ioq_vector)
{
	octeon_instr_queue_t *iq = ioq_vector->iq;
	//TODO
	/** Writing back cnt val to subtract cnt and clear the interrupt */
	OCTEON_WRITE64(iq->inst_cnt_reg, OCTEON_READ64(iq->inst_cnt_reg));
}

/* $Id$ */
