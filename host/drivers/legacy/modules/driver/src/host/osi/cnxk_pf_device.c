/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

#include "cavium_sysdep.h"
#include "octeon_hw.h"
#include "cnxk_pf_device.h"
#include "octeon_macros.h"
#include "octeon-pci.h"
#include <linux/log2.h>

#define FW_TO_HOST 0x2
#define HOST_TO_FW 0x1
//int g_app_mode[2] = {CVM_DRV_APP_START, CVM_DRV_APP_START};
extern int g_app_mode[];
static u8 g_vf_srn[2];
enum info_exhg_state {
	/* State where F/W isn't posted anything */
	NO_EXHG,
	/* State where Host posts its ring info */
	RINFO_HOST,
	/* State where F/W acks the host ring info */
	RINFO_FW_ACK
};

struct fw_handshake_wrk {
	octeon_device_t *oct;
	enum info_exhg_state exhg_state;
};
static struct fw_handshake_wrk hs_wrk[2];

extern int octeon_device_init(octeon_device_t *, int);
extern void mv_facility_irq_handler(octeon_device_t *oct, uint64_t event_word);

extern int num_rings_per_pf;
extern int num_rings_per_vf;
static int num_rings_per_pf_pt[2], num_rings_per_vf_pt[2], pf_srn_pt[2];
extern void cnxk_iq_intr_handler(octeon_ioq_vector_t * ioq_vector);

void cnxk_dump_iq_regs(octeon_device_t * oct)
{

}

void cnxk_dump_pf_initialized_regs(octeon_device_t * oct)
{

}

void cnxk_dump_regs(octeon_device_t * oct, int qno)
{
	printk("IQ register dump\n");
	printk("R[%d]_IN_INSTR_DBELL[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_INSTR_DBELL(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_INSTR_DBELL(qno)));
	printk("R[%d]_IN_CONTROL[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_CONTROL(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_CONTROL(qno)));
	printk("R[%d]_IN_ENABLE[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_ENABLE(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_ENABLE(qno)));
	printk("R[%d]_IN_INSTR_BADDR[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_INSTR_BADDR(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_INSTR_BADDR(qno)));
	printk("R[%d]_IN_INSTR_RSIZE[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_INSTR_RSIZE(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_INSTR_RSIZE(qno)));
	printk("R[%d]_IN_CNTS[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_CNTS(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_CNTS(qno)));
	printk("R[%d]_IN_INT_LEVELS[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_INT_LEVELS(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_INT_LEVELS(qno)));
	printk("R[%d]_IN_PKT_CNT[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_PKT_CNT(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_PKT_CNT(qno)));
	printk("R[%d]_IN_BYTE_CNT[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_IN_BYTE_CNT(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_IN_BYTE_CNT(qno)));

	printk("OQ register dump\n");
	printk("R[%d]_OUT_SLIST_DBELL[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_SLIST_DBELL(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_SLIST_DBELL(qno)));
	printk("R[%d]_OUT_CONTROL[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_CONTROL(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_CONTROL(qno)));
	printk("R[%d]_OUT_ENABLE[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_ENABLE(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_ENABLE(qno)));
	printk("R[%d]_OUT_SLIST_BADDR[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_SLIST_BADDR(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_SLIST_BADDR(qno)));
	printk("R[%d]_OUT_SLIST_RSIZE[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_SLIST_RSIZE(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_SLIST_RSIZE(qno)));
	printk("R[%d]_OUT_CNTS[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_CNTS(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_CNTS(qno)));
	printk("R[%d]_OUT_INT_LEVELS[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_INT_LEVELS(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_INT_LEVELS(qno)));
	printk("R[%d]_OUT_PKT_CNT[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_PKT_CNT(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_PKT_CNT(qno)));
	printk("R[%d]_OUT_BYTE_CNT[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_OUT_BYTE_CNT(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_OUT_BYTE_CNT(qno)));

	printk("R[%d]_ERR_TYPE[0x%llx]: 0x%016llx\n", qno,
		CNXK_SDP_R_ERR_TYPE(qno), octeon_read_csr64(oct,
		CNXK_SDP_R_ERR_TYPE(qno)));
}

/* For soft reset of cnxk using core domain reset.
 * TODO: Revisit this code */
static int cnxk_pf_soft_reset(octeon_device_t * oct)
{
	octeon_write_csr64(oct, CNXK_SDP_WIN_WR_MASK_REG, 0xFF);

	cavium_print_msg
	    ("OCTEON[%d]: BIST enabled for CNXK soft reset\n",
	     oct->octeon_id);

	/* Set core domain reset bit */
	OCTEON_PCI_WIN_WRITE(oct, CNXK_RST_CORE_DOMAIN_W1S, 1);
	/* Wait for 100ms as Octeon resets. */
	cavium_mdelay(100);
	/* TBD: Is it required to clear core domain reset bit */
	OCTEON_PCI_WIN_WRITE(oct, CNXK_RST_CORE_DOMAIN_W1C, 1);

	cavium_print_msg("OCTEON[%d]: Reset completed\n", oct->octeon_id);

	/* restore the  reset value */
	octeon_write_csr64(oct, CNXK_SDP_WIN_WR_MASK_REG, 0xFF);
	return 0;
}

void cnxk_enable_error_reporting(octeon_device_t * oct)
{
	uint32_t regval;

	OCTEON_READ_PCI_CONFIG(oct, CNXK_CONFIG_PCIE_DEVCTL, &regval);
	/* clear any old link error bits */
	OCTEON_WRITE_PCI_CONFIG(oct, CNXK_CONFIG_PCIE_DEVCTL, regval);

	/* read again to see if new bits are set */
	msleep(1);
	OCTEON_READ_PCI_CONFIG(oct, CNXK_CONFIG_PCIE_DEVCTL, &regval);
	if (regval & 0x000f0000) {
		cavium_error("PCI-E Link error detected: 0x%08x\n",
			     regval & 0x000f0000);
	}

	regval |= 0xf;		/* Enable Link error reporting */

	cavium_print(PRINT_DEBUG,
		     "OCTEON[%d]: Enabling PCI-E error reporting.\n",
		     oct->octeon_id);
	OCTEON_WRITE_PCI_CONFIG(oct, CNXK_CONFIG_PCIE_DEVCTL, regval);
}

static uint32_t cnxk_coprocessor_clock(octeon_device_t * oct)
{
	/* Bits 29:24 of RST_BOOT[PNR_MUL] holds the ref.clock MULTIPLIER
	 * for SDP. 
	 */

	/* as no handshake */ 
	return CFG_GET_COPROC_TICS_PER_US(CHIP_FIELD(oct, cnxk_pf, conf));
}

uint32_t cnxk_get_oq_ticks(octeon_device_t * oct, uint32_t time_intr_in_us)
{
	/* This gives the SDP clock per microsec */
	uint32_t oqticks_per_us = cnxk_coprocessor_clock(oct);	//0x384; //0x2bc;  

	/* core clock per us / oq ticks will be fractional. TO avoid that
	 * we use the method below. 
	 */

	/* This gives the clock cycles per millisecond */
	oqticks_per_us *= 1000;

	/* This gives the oq ticks (1024 core clock cycles) per millisecond */
	oqticks_per_us /= 1024;

	/* time_intr is in microseconds. The next 2 steps gives the oq ticks
	 *  corressponding to time_intr. 
	 */
	oqticks_per_us *= time_intr_in_us;
	oqticks_per_us /= 1000;

	return oqticks_per_us;
}

int cnxk_reset_iq(octeon_device_t * oct, int q_no)
{
	volatile uint64_t d64 = 0ULL;
	q_no += oct->sriov_info.pf_srn;
	/* There is no RST for a ring. 
	 * Clear all registers one by one after disabling the ring
	 */
	octeon_write_csr64(oct, CNXK_SDP_R_IN_ENABLE(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INSTR_BADDR(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INSTR_RSIZE(q_no), d64);

	d64 = 0xFFFFFFFF;
	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INSTR_DBELL(q_no), d64);

	d64 = 0;
	octeon_write_csr64(oct, CNXK_SDP_R_IN_CNTS(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INT_LEVELS(q_no), d64);

	octeon_write_csr64(oct, CNXK_SDP_R_IN_PKT_CNT(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_BYTE_CNT(q_no), d64);

	return 0;
}

int cnxk_reset_oq(octeon_device_t * oct, int q_no)
{
	volatile uint64_t d64 = 0ULL;
	q_no += oct->sriov_info.pf_srn;

	octeon_write_csr64(oct, CNXK_SDP_R_OUT_ENABLE(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_SLIST_BADDR(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_SLIST_RSIZE(q_no), d64);

	d64 = 0xFFFFFFFF;
	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_SLIST_DBELL(q_no), d64);

	d64 = 0;
	octeon_write_csr64(oct, CNXK_SDP_R_OUT_CNTS(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_INT_LEVELS(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_PKT_CNT(q_no), d64);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_BYTE_CNT(q_no), d64);

	return 0;
}

int cnxk_pf_setup_global_iq_reg(octeon_device_t * oct, int q_no)
{
	volatile uint64_t reg_val = 0ULL;

	q_no += oct->sriov_info.pf_srn;

	/* Select ES, RO, NS, RDSIZE,DPTR Fomat#0 for IQs 
	 * IS_64B is by default enabled.
	 */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_R_IN_CONTROL(q_no));

	reg_val |= CNXK_R_IN_CTL_RDSIZE;
	reg_val |= CNXK_R_IN_CTL_IS_64B | CNXK_R_IN_CTL_ESR;

	octeon_write_csr64(oct, CNXK_SDP_R_IN_CONTROL(q_no), reg_val);
	return 0;
}

int cnxk_pf_setup_global_oq_reg(octeon_device_t * oct, int q_no)
{
	volatile uint64_t reg_val = 0ULL;
	q_no += oct->sriov_info.pf_srn;

	reg_val = octeon_read_csr(oct, CNXK_SDP_R_OUT_CONTROL(q_no));

	reg_val &= ~(CNXK_R_OUT_CTL_IMODE);

	/* ROR: Relaxed ordering
	 * NSR: No SNOOP
	 * _P: for buff/info pairs read operation. 
	 * _I: for info buffer write operations. 
	 * _D: for data buffer write operations. 
	 */
	reg_val &= ~(CNXK_R_OUT_CTL_ROR_P | CNXK_R_OUT_CTL_ES_I | CNXK_R_OUT_CTL_ES_D);
	reg_val &= ~(CNXK_R_OUT_CTL_NSR_P);
	reg_val &= ~(CNXK_R_OUT_CTL_ROR_I);
	reg_val &= ~(CNXK_R_OUT_CTL_NSR_I);
	reg_val &= ~(CNXK_R_OUT_CTL_ROR_D);
	reg_val &= ~(CNXK_R_OUT_CTL_NSR_D);

    /* INFO/DATA ptr swap is required on cn10k  */
	reg_val |= (CNXK_R_OUT_CTL_ES_P);
	/* write all the selected settings */
	octeon_write_csr(oct, CNXK_SDP_R_OUT_CONTROL(q_no), reg_val);

	return 0;
}

int cnxk_reset_input_queues(octeon_device_t * oct)
{
	int q_no = 0;
	cavium_print(PRINT_DEBUG, " %s : OCTEON_CNXK PF\n", __FUNCTION__);

	for (q_no = 0; q_no < oct->sriov_info.rings_per_pf; q_no++) {
		cnxk_reset_iq(oct, q_no);
	}
	return 0;
}

int cnxk_reset_output_queues(octeon_device_t * oct)
{
	uint64_t q_no = 0ULL;
	cavium_print(PRINT_DEBUG, " %s : OCTEON_CNXK PF\n", __FUNCTION__);

	for (q_no = 0; q_no < oct->sriov_info.rings_per_pf; q_no++) {
		cnxk_reset_oq(oct, q_no);
	}
	return 0;
}

int cnxk_pf_setup_global_input_regs(octeon_device_t * oct)
{
	uint64_t q_no = 0ULL;
	int ret = 0;

	ret = cnxk_reset_input_queues(oct);
	cavium_print(PRINT_DEBUG, "Reset IQ Done: %d\n", ret);

	for (q_no = 0; q_no < oct->sriov_info.rings_per_pf; q_no++) {
		cnxk_pf_setup_global_iq_reg(oct, q_no);
	}
	return 0;
}

void cnxk_pf_setup_global_output_regs(octeon_device_t * oct)
{
	uint64_t q_no = 0ULL;
	int ret = 0;

	ret = cnxk_reset_output_queues(oct);
	cavium_print(PRINT_DEBUG, "Reset OQ Done: %d\n", ret);

	for (q_no = 0; q_no < oct->sriov_info.rings_per_pf; q_no++) {
		cnxk_pf_setup_global_oq_reg(oct, q_no);
	}

	/** 
     * NOTE: OUT_WMARK, GBL_CTL, BP_W1S, MAC_CREDIT are not accessible 
     * from Host in cnxk.
     */
}

int cnxk_setup_global_mac_regs(octeon_device_t * oct)
{
	return 0;
}

static int cnxk_setup_pf_device_regs(octeon_device_t * oct)
{

	cnxk_enable_error_reporting(oct);

	cnxk_setup_global_mac_regs(oct);

	cnxk_pf_setup_global_input_regs(oct);

	cnxk_pf_setup_global_output_regs(oct);

	return 0;
}

static void cnxk_setup_iq_regs(octeon_device_t * oct, int iq_no)
{
	volatile uint64_t reg_val = 0ULL;
	octeon_instr_queue_t *iq = oct->instr_queue[iq_no];
	octeon_cnxk_pf_t *cnxk = (octeon_cnxk_pf_t *) oct->chip;

	iq_no += oct->sriov_info.pf_srn;

	reg_val =
	    octeon_read_csr64(oct, CNXK_SDP_R_IN_CONTROL(iq_no));

	/* wait for IDLE to set to 1, as cannot configure BADDR as long as IDLE is 0 */
	if (!(reg_val & CNXK_R_IN_CTL_IDLE)) {
		do {
			reg_val = octeon_read_csr64(oct, CNXK_SDP_R_IN_CONTROL(iq_no));
		}
		while (!(reg_val & CNXK_R_IN_CTL_IDLE));
	}

	/* Write the start of the input queue's ring and its size  */
	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INSTR_BADDR(iq_no),
			   iq->base_addr_dma);
	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INSTR_RSIZE(iq_no),
			   iq->max_count);

	/* Remember the doorbell & instruction count register addr 
	 * for this queue 
	 */
	iq->doorbell_reg = (uint8_t *) oct->mmio[0].hw_addr
	    + CNXK_SDP_R_IN_INSTR_DBELL(iq_no);
	iq->inst_cnt_reg = (uint8_t *) oct->mmio[0].hw_addr
	    + CNXK_SDP_R_IN_CNTS(iq_no);
	iq->intr_lvl_reg = (uint8_t *) oct->mmio[0].hw_addr
	    + CNXK_SDP_R_IN_INT_LEVELS(iq_no);

	cavium_print(PRINT_DEBUG,
		     "InstQ[%d]:dbell reg @ 0x%p instcnt_reg @ 0x%p 0x%p\n", iq_no,
		     iq->doorbell_reg, iq->inst_cnt_reg, iq->intr_lvl_reg);

	/* Store the current instruction counter (used in flush_iq calculation) */
	iq->reset_instr_cnt = OCTEON_READ32(iq->inst_cnt_reg);
	OCTEON_WRITE32(iq->inst_cnt_reg, iq->reset_instr_cnt);
	iq->reset_instr_cnt = OCTEON_READ32(iq->inst_cnt_reg);

	/* IN INTR_THRESHOLD is set to max(FFFFFFFF) to disable the IN INTR */
	reg_val =
	    octeon_read_csr64(oct,
			      CNXK_SDP_R_IN_INT_LEVELS(iq_no));

	reg_val = CFG_GET_IQ_INTR_THRESHOLD(cnxk->conf) & 0xffffffff;

	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INT_LEVELS(iq_no), reg_val);
}

static void cnxk_setup_oq_regs(octeon_device_t * oct, int oq_no)
{
	volatile uint64_t reg_val = 0ULL;
	uint64_t oq_ctl = 0ULL;
	uint32_t time_threshold = 0;
	octeon_droq_t *droq = oct->droq[oq_no];
	octeon_cnxk_pf_t *cnxk = (octeon_cnxk_pf_t *) oct->chip;

	oq_no += oct->sriov_info.pf_srn;

	reg_val =
	    octeon_read_csr64(oct, CNXK_SDP_R_OUT_CONTROL(oq_no));

	/* wait for IDLE to set to 1, as cannot configure BADDR as long as IDLE is 0 */
	if (!(reg_val & CNXK_R_OUT_CTL_IDLE)) {
		do {
			reg_val =
			    octeon_read_csr64(oct,
					      CNXK_SDP_R_OUT_CONTROL(oq_no));
		}
		while (!(reg_val & CNXK_R_OUT_CTL_IDLE));
	}

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_SLIST_BADDR(oq_no),
			   droq->desc_ring_dma);
	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_SLIST_RSIZE(oq_no),
			   droq->max_count);

	oq_ctl =
	    octeon_read_csr(oct,
			    CNXK_SDP_R_OUT_CONTROL(oq_no));
	oq_ctl &= ~0x7fffffULL;	//clear the ISIZE and BSIZE (22-0)
	oq_ctl |= (droq->buffer_size & 0xffff);	//populate the BSIZE (15-0)
#ifndef BUFPTR_ONLY_MODE
	oq_ctl |= ((OCT_RESP_HDR_SIZE << 16) & 0x7fffff);//populate ISIZE(22-16)
#endif
	octeon_write_csr(oct, CNXK_SDP_R_OUT_CONTROL(oq_no), oq_ctl);

	/* Get the mapped address of the pkt_sent and pkts_credit regs */
	droq->pkts_sent_reg = (uint8_t *) oct->mmio[0].hw_addr +
	    CNXK_SDP_R_OUT_CNTS(oq_no);
	droq->pkts_credit_reg = (uint8_t *) oct->mmio[0].hw_addr +
	    CNXK_SDP_R_OUT_SLIST_DBELL(oq_no);

	reg_val =
	    octeon_read_csr64(oct, CNXK_SDP_R_OUT_INT_LEVELS(oq_no));

	/* VSR: TODO: this line does not make sense;
	 * it is overwritten in next line
	 */
#if 0
	time_threshold = cnxk_get_oq_ticks(oct, (uint32_t)
					     CFG_GET_OQ_INTR_TIME
					     (cnxk->conf));
#endif
	time_threshold = CFG_GET_OQ_INTR_TIME(cnxk->conf);

    	reg_val =  ((uint64_t)time_threshold << 32 ) | CFG_GET_OQ_INTR_PKT(cnxk->conf); 

	octeon_write_csr64(oct, CNXK_SDP_R_OUT_INT_LEVELS(oq_no), reg_val);

#ifdef OCT_TX2_ISM_INT	
	octeon_write_csr64(oct, CNXK_SDP_R_OUT_CNTS_ISM(oq_no), (droq->ism.pkt_cnt_dma)|0x1ULL);
#endif
}

/* Mail Box Commminucation is to be verified */ 
static void cnxk_setup_pf_mbox_regs(octeon_device_t * oct, int q_no)
{
	octeon_mbox_t *mbox = oct->mbox[q_no];

	mbox->q_no = q_no;

	/* PF mbox interrupt reg */
	mbox->mbox_int_reg = (uint8_t *) oct->mmio[0].hw_addr +
			      CNXK_SDP_EPF_MBOX_RINT;

	/* PF to VF DATA reg. PF writes into this reg */
	mbox->mbox_write_reg = (uint8_t *) oct->mmio[0].hw_addr +
			     CNXK_SDP_R_MBOX_PF_VF_DATA(q_no);

	/* VF to PF DATA reg. PF reads from this reg */
	mbox->mbox_read_reg = (uint8_t *) oct->mmio[0].hw_addr +
          CNXK_SDP_R_MBOX_VF_PF_DATA(q_no);

}

static void cnxk_enable_input_queue(octeon_device_t * oct, int iq_no)
{
	volatile uint64_t reg_val = 0ULL;
	uint64_t loop = CAVIUM_TICKS_PER_SEC;

	iq_no += oct->sriov_info.pf_srn;

	octeon_write_csr64(oct,
			   CNXK_SDP_R_IN_INSTR_DBELL(iq_no), 0xFFFFFFFF);

	while (((octeon_read_csr64(oct,
				   CNXK_SDP_R_IN_INSTR_DBELL(iq_no))) != 0ULL)
	       && loop--) {
		cavium_sleep_timeout(1);
	}

	reg_val = octeon_read_csr64(oct,  CNXK_SDP_R_IN_INT_LEVELS(iq_no));
	reg_val |= (0x1ULL << 62);
	octeon_write_csr64(oct, CNXK_SDP_R_IN_INT_LEVELS(iq_no), reg_val);

	/* Can directly enable as, waiting for IDLE while configuring BADDR */
	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_R_IN_ENABLE(iq_no));
	reg_val |= 0x1ULL;

	octeon_write_csr64(oct, CNXK_SDP_R_IN_ENABLE(iq_no), reg_val);
}

static void cnxk_enable_output_queue(octeon_device_t * oct, int oq_no)
{
	volatile uint64_t reg_val = 0ULL;

	oq_no += oct->sriov_info.pf_srn;

	reg_val = octeon_read_csr64(oct,  CNXK_SDP_R_OUT_INT_LEVELS(oq_no));
	reg_val |= (0x1ULL << 62);
	octeon_write_csr64(oct, CNXK_SDP_R_OUT_INT_LEVELS(oq_no), reg_val);

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_SLIST_DBELL(oq_no), 0xFFFFFFFF);

	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_R_OUT_ENABLE(oq_no));
	reg_val |= 0x1ULL;

	printk("%s: OCTEON[%d]: oq-%d R_OUT_ENABLED done\n", __func__, oct->octeon_id, oq_no);
	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_ENABLE(oq_no), reg_val);
}

static void cnxk_disable_input_queue(octeon_device_t * oct, int iq_no)
{
	volatile uint64_t reg_val = 0ULL;

	iq_no += oct->sriov_info.pf_srn;

	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_R_IN_ENABLE(iq_no));
	reg_val &= ~0x1ULL;

	octeon_write_csr64(oct, CNXK_SDP_R_IN_ENABLE(iq_no),
			   reg_val);
}

static void cnxk_disable_output_queue(octeon_device_t * oct, int oq_no)
{
	volatile uint64_t reg_val = 0ULL;

	oq_no += oct->sriov_info.pf_srn;
	/* Can directly enable as, waiting for IDLE while configuring BADDR */
	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_R_OUT_ENABLE(oq_no));
	reg_val &= ~0x1ULL;

	octeon_write_csr64(oct,
			   CNXK_SDP_R_OUT_ENABLE(oq_no), reg_val);
}

static void cnxk_enable_io_queues(octeon_device_t * oct)
{
	uint64_t q_no = 0;

	for (q_no = 0; q_no < oct->sriov_info.rings_per_pf; q_no++) {
		cnxk_enable_input_queue(oct, q_no);
		cnxk_enable_output_queue(oct, q_no);
	}
}

static void cnxk_disable_io_queues(octeon_device_t * oct)
{
	int q_no = 0;

	for (q_no = 0; q_no < oct->sriov_info.rings_per_pf; q_no++) {
		cnxk_disable_input_queue(oct, q_no);
		cnxk_disable_output_queue(oct, q_no);
	}
}

void cnxk_handle_pcie_error_intr(octeon_device_t * oct, uint64_t intr64)
{
	cavium_error("OCTEON[%d]: Error Intr: 0x%016llx\n",
		     oct->octeon_id, CVM_CAST64(intr64));
}

void cnxk_force_io_queues_off(octeon_device_t * oct)
{
	uint64_t reg_val = 0ULL, q_no = 0ULL, srn = 0ULL, ern = 0ULL;

	cavium_print_msg(" %s : OCTEON_CNXK PF\n", __FUNCTION__);

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.rings_per_pf;

	for (q_no = srn; q_no < ern; q_no++) {

		reg_val = octeon_read_csr64(oct,
					    CNXK_SDP_R_IN_ENABLE(q_no));
		reg_val &= ~0x1ULL;
		octeon_write_csr64(oct,
				   CNXK_SDP_R_IN_ENABLE(q_no), reg_val);

		reg_val = octeon_read_csr64(oct,
					    CNXK_SDP_R_OUT_ENABLE(q_no));
		reg_val &= ~0x1ULL;
		octeon_write_csr64(oct,
				   CNXK_SDP_R_OUT_ENABLE(q_no), reg_val);
	}
}

/* MailBox Interrupts */
void cnxk_handle_pf_mbox_intr(octeon_device_t * oct)
{
	uint64_t mbox_int_val = 0ULL, val = 0ULL, qno = 0ULL;
	cavium_print_msg("MBOX interrupt received on PF\n");
	mbox_int_val = OCTEON_READ64(oct->mbox[0]->mbox_int_reg);

	for (qno = 0; qno < 64; qno++) {
		val = OCTEON_READ64(oct->mbox[qno]->mbox_read_reg);
		cavium_print_msg("PF MBOX READ: val:%llx from VF:%llx\n", val,
				 qno);
	}

	OCTEON_WRITE64(oct->mbox[0]->mbox_int_reg, mbox_int_val);
}

cvm_intr_return_t cnxk_pf_msix_interrupt_handler(void *dev)
{
	octeon_ioq_vector_t *ioq_vector = (octeon_ioq_vector_t *) dev;
	octeon_droq_t *droq = ioq_vector->droq;

	cavium_print(PRINT_FLOW, " In %s octeon_dev @ %p  \n",
		     __CVM_FUNCTION__, droq->oct_dev);

	droq->ops.napi_fun((void *)droq);
	return CVM_INTR_HANDLED;
}

cvm_intr_return_t cnxk_interrupt_handler(void *dev)
{
	uint64_t reg_val = 0;
    int i =0;
	octeon_device_t *oct = (octeon_device_t *) dev;

	/* Check for IRERR INTR */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_IRERR_RINT);
	if (reg_val) {
		cavium_print_msg("received IRERR_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_IRERR_RINT, reg_val);

        for(i =0 ; i < 64; i++) {
        	reg_val = octeon_read_csr64(oct,
		    		    CNXK_SDP_R_ERR_TYPE(i));
            if(reg_val) {
        		cavium_print_msg("received err type on input ring [%d]: 0x%016llx\n", i, reg_val);
        	    octeon_write_csr64(oct, CNXK_SDP_R_ERR_TYPE(i), reg_val);
            }
        }
		goto irq_handled;
	}

	/* Check for ORERR INTR */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_ORERR_RINT);
	if (reg_val) {
		cavium_print_msg("received ORERR_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_ORERR_RINT, reg_val);
        for (i = 0 ; i < 64; i++) {
               reg_val = octeon_read_csr64(oct, CNXK_SDP_R_ERR_TYPE(i));
            if(reg_val) {
                       cavium_print_msg("received err type on output ring [%d]: 0x%016llx\n", i, reg_val);
                   octeon_write_csr64(oct, CNXK_SDP_R_ERR_TYPE(i), reg_val);
            }
        }

		goto irq_handled;
	}
	
	/* Check for VFIRE INTR */
	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_EPF_VFIRE_RINT);
	if (reg_val) {
		cavium_print_msg("received VFIRE_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_VFIRE_RINT, reg_val);
		goto irq_handled;
	}
	
	/* Check for VFORE INTR */
	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_EPF_VFORE_RINT);
	if (reg_val) {
		cavium_print_msg("received VFORE_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_VFORE_RINT, reg_val);
		goto irq_handled;
	}

	/* Check for MBOX INTR */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_MBOX_RINT);
	if (reg_val) {
		cavium_print_msg("received MBOX_RINT intr: 0x%016llx\n",
				 reg_val);
		cnxk_handle_pf_mbox_intr(oct);
		goto irq_handled;
	}

	/* Check for OEI INTR */
	/*
	 * - there are 16 OEI_RINT per EPF
	 * - for now, using only the first one (to be compatible with existing
	 *   host/npu drivers
	 * - TODO: enhance to use separate interrupt per facility
	 */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_OEI_RINT);
	if (reg_val) {
		octeon_write_csr64(oct, CNXK_SDP_EPF_OEI_RINT, reg_val);
		/* used by facility */
		mv_facility_irq_handler(oct, reg_val);
		goto irq_handled;
	}

	/* Check for DMA INTR */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_DMA_RINT);
	if (reg_val) {
		octeon_write_csr64(oct, CNXK_SDP_EPF_DMA_RINT, reg_val);
		goto irq_handled;
	}
	
	/* Check for DMA VF INTR */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_DMA_VF_RINT);
	if (reg_val) {
		cavium_print_msg("received DMA_VF_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_DMA_VF_RINT,
				   reg_val);
		goto irq_handled;
	}

	/* Check for PPVF INTR */
	reg_val = octeon_read_csr64(oct,
				    CNXK_SDP_EPF_PP_VF_RINT);
	if (reg_val) {
		cavium_print_msg("received PP_VF_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_PP_VF_RINT, reg_val);
		goto irq_handled;
	}
	
	/* Check for MISC INTR */
	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_MISC_RINT);
	if (reg_val) {
		cavium_print_msg("received MISC_RINT intr: 0x%016llx\n",
				 reg_val);
		octeon_write_csr64(oct, CNXK_SDP_EPF_MISC_RINT, reg_val);
		goto irq_handled;
	}
	cavium_print_msg("IGNORE. RSVD INTRS raised\n");
irq_handled:
	return CVM_INTR_HANDLED;
}

static void cnxk_reinit_regs(octeon_device_t * oct)
{
	uint32_t i;

	oct->fn_list.setup_device_regs(oct);

	for (i = 0; i < MAX_OCTEON_INSTR_QUEUES; i++) {
		if (!(oct->io_qmask.iq & (1UL << i)))
			continue;
		oct->fn_list.setup_iq_regs(oct, i);
	}

	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES; i++) {
		if (!(oct->io_qmask.oq & (1UL << i)))
			continue;
		oct->fn_list.setup_oq_regs(oct, i);
	}

	oct->fn_list.enable_interrupt(oct->chip, OCTEON_ALL_INTR);

	oct->fn_list.enable_io_queues(oct);

	/* for (i = 0; i < oct->num_oqs; i++) { */
	for (i = 0; i < MAX_OCTEON_OUTPUT_QUEUES; i++) {
		if (!(oct->io_qmask.oq & (1UL << i)))
			continue;
		OCTEON_WRITE32(oct->droq[i]->pkts_credit_reg,
			       oct->droq[i]->max_count);
	}
}

static void
cnxk_bar1_idx_setup(octeon_device_t * oct,
		      uint64_t core_addr, int idx, int valid)
{
	volatile uint64_t bar1;

	if (valid == 0) {
		bar1 = OCTEON_PCI_WIN_READ(oct,
					   CNXK_PEM_BAR4_INDEX_REG
					   (oct->pcie_port, idx));
		OCTEON_PCI_WIN_WRITE(oct,
				     CNXK_PEM_BAR4_INDEX_REG(oct->pcie_port,
							       idx),
				     (bar1 & 0xFFFFFFFEULL));
		bar1 =
		    OCTEON_PCI_WIN_READ(oct,
					CNXK_PEM_BAR4_INDEX_REG
					(oct->pcie_port, idx));
		return;
	}

	/*  The PEM(0..3)_BAR1_INDEX(0..15)[ADDR_IDX]<23:4> stores 
	 *  bits <41:22> of the Core Addr 
	 */
	OCTEON_PCI_WIN_WRITE(oct,
			     CNXK_PEM_BAR4_INDEX_REG(oct->pcie_port, idx),
			     (((core_addr >> 22) << 4) | PCI_BAR1_MASK));

	bar1 = OCTEON_PCI_WIN_READ(oct,
				   CNXK_PEM_BAR4_INDEX_REG(oct->pcie_port,
							     idx));
}

static void cnxk_bar1_idx_write(octeon_device_t * oct, int idx, uint32_t mask)
{
	OCTEON_PCI_WIN_WRITE(oct,
			     CNXK_PEM_BAR4_INDEX_REG(oct->pcie_port, idx),
			     mask);
}

static uint32_t cnxk_bar1_idx_read(octeon_device_t * oct, int idx)
{
	return OCTEON_PCI_WIN_READ(oct,
				   CNXK_PEM_BAR4_INDEX_REG(oct->pcie_port,
							     idx));
}

static uint32_t cnxk_update_read_index(octeon_instr_queue_t * iq)
{
	u32 new_idx;
	u32 last_done;
	u32 pkt_in_done = OCTEON_READ32(iq->inst_cnt_reg);

	last_done = pkt_in_done - iq->pkt_in_done;
	iq->pkt_in_done = pkt_in_done;

#define OCTEON_PKT_IN_DONE_CNT_MASK (0x00000000FFFFFFFFULL)
	new_idx = (iq->octeon_read_index +
		   (u32)(last_done & OCTEON_PKT_IN_DONE_CNT_MASK)) %
		  iq->max_count;

	return new_idx;
}

static void cnxk_enable_pf_interrupt(void *chip, uint8_t intr_flag)
{
	volatile uint64_t reg_val = 0ULL, pf_ring_ctl = 0ULL;
	uint64_t intr_mask = 0ULL;
	int srn = 0, trs = 0, i;
	octeon_cnxk_pf_t *cnxk = (octeon_cnxk_pf_t *) chip;
	octeon_device_t *oct = (octeon_device_t *) cnxk->oct;

	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_RINFO);

	srn = reg_val & CNXK_SDP_EPF_RINFO_SRN;

	/* Get RPPF from MACX_PF_RING_CTL */
	pf_ring_ctl = octeon_read_csr64(oct,
			CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port));

	if (oct->chip_id == OCTEON_CNXK_PF) {
		trs = (pf_ring_ctl >> CNXK_SDP_MAC_PF_RING_CTL_RPPF_BIT_POS)
			& CNXK_SDP_MAC_PF_RING_CTL_RPPF;
	} else {
		 printk("OCTEON[%d]: Failed to enable PF interrupt; Invalid chip_id 0x%x\n",
			oct->octeon_id, oct->chip_id);
	}

	for (i = 0; i < trs; i++)
		intr_mask |= (0x1ULL << (srn + i));

	octeon_write_csr64(oct,
			   CNXK_SDP_EPF_IRERR_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_ORERR_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct,
			   CNXK_SDP_EPF_VFIRE_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_VFORE_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_OEI_RINT_ENA_W1S, -1ULL);
	octeon_write_csr64(oct, CNXK_SDP_EPF_MISC_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_PP_VF_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct,
			   CNXK_SDP_EPF_DMA_VF_RINT_ENA_W1S,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_DMA_RINT_ENA_W1S,
			   intr_mask);
}

static void cnxk_disable_pf_interrupt(void *chip, uint8_t intr_flag)
{
	volatile uint64_t reg_val = 0ULL, pf_ring_ctl = 0ULL;
	uint64_t intr_mask = 0ULL;
	int srn = 0, trs = 0, i;
	octeon_cnxk_pf_t *cnxk = (octeon_cnxk_pf_t *) chip;
	octeon_device_t *oct = (octeon_device_t *) cnxk->oct;

	reg_val = octeon_read_csr64(oct, CNXK_SDP_EPF_RINFO);

	srn = reg_val & CNXK_SDP_EPF_RINFO_SRN;

	/* Get RPPF from MACX_PF_RING_CTL */
	pf_ring_ctl = octeon_read_csr64(oct,
			CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port));

	if (oct->chip_id == OCTEON_CNXK_PF) {
		trs = (pf_ring_ctl >> CNXK_SDP_MAC_PF_RING_CTL_RPPF_BIT_POS)
			& CNXK_SDP_MAC_PF_RING_CTL_RPPF;
	} else {
		 cavium_print_msg("OCTEON[%d]: Failed to enable PF interrupt; Invalid chip_id 0x%x\n",
				  oct->octeon_id, oct->chip_id);
	}

	for (i = 0; i < trs; i++)
		intr_mask |= (0x1ULL << (srn + i));

	octeon_write_csr64(oct,
			   CNXK_SDP_EPF_IRERR_RINT_ENA_W1C, intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_ORERR_RINT_ENA_W1C,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_OEI_RINT_ENA_W1C, -1ULL);
	octeon_write_csr64(oct, CNXK_SDP_EPF_MISC_RINT_ENA_W1C,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_PP_VF_RINT_ENA_W1C,
			   intr_mask);
	octeon_write_csr64(oct,
			   CNXK_SDP_EPF_DMA_VF_RINT_ENA_W1C,
			   intr_mask);
	octeon_write_csr64(oct, CNXK_SDP_EPF_DMA_RINT_ENA_W1C,
			   intr_mask);
}

static void cnxk_get_pcie_qlmport(octeon_device_t * oct)
{
	uint64_t sdp_mac;

	sdp_mac = octeon_read_csr(oct, CNXK_SDP_MAC_NUMBER);
	oct->pcie_port = sdp_mac & 0xff;

	cavium_print_msg("OCTEON[%d]: CNXK uses PCIE Port %d and PEM %d\n",
			 oct->octeon_id, oct->pcie_port,
			 (uint8_t)((sdp_mac >> 16) & 0xff));
}

static void cnxk_setup_reg_address(octeon_device_t * oct)
{
	uint8_t cavium_iomem *bar0_pciaddr = oct->mmio[0].hw_addr;

	oct->reg_list.pci_win_wr_addr_hi =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_WR_ADDR_HI);
	oct->reg_list.pci_win_wr_addr_lo =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_WR_ADDR_LO);
	oct->reg_list.pci_win_wr_addr =
	    (uint64_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_WR_ADDR64);

	oct->reg_list.pci_win_rd_addr_hi =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_RD_ADDR_HI);
	oct->reg_list.pci_win_rd_addr_lo =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_RD_ADDR_LO);
	oct->reg_list.pci_win_rd_addr =
	    (uint64_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_RD_ADDR64);

	oct->reg_list.pci_win_wr_data_hi =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_WR_DATA_HI);
	oct->reg_list.pci_win_wr_data_lo =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_WR_DATA_LO);
	oct->reg_list.pci_win_wr_data =
	    (uint64_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_WR_DATA64);

	oct->reg_list.pci_win_rd_data_hi =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_RD_DATA_HI);
	oct->reg_list.pci_win_rd_data_lo =
	    (uint32_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_RD_DATA_LO);
	oct->reg_list.pci_win_rd_data =
	    (uint64_t cavium_iomem *) (bar0_pciaddr +
				       CNXK_SDP_WIN_RD_DATA64);
}

static inline int lowerpow2roundup(int x)
{
	x = x | (x >> 1);
	x = x | (x >> 2);
	x = x | (x >> 4);
	x = x | (x >> 8);
	x = x | (x >> 16);

	return x - (x >> 1);
}

union ring {
        u64 u;
        struct {
                u64 dir:2;
                u64 rpvf:8;
                u64 rppf:8;
                u64 numvf:8;
                u64 pf_srn:8;
                u64 rsvd:2;
                u64 raz:28;
        } s;
};

static int resume_cnxk_setup(octeon_device_t * oct)
{
	uint64_t npfs = 0, rppf = 0, pf_srn = pf_srn_pt[oct->octeon_id];
	uint64_t nvfs = 0, rpvf = 0, vf_srn = 0;
	int i, j, srn;
	int vf_rings = 0;
	uint64_t regval = 0ull;
	octeon_cnxk_pf_t *cnxk = (octeon_cnxk_pf_t *) oct->chip;

	npfs = 1;
	rppf = num_rings_per_pf;
	rpvf = num_rings_per_vf;
	if(oct->sriov_info.num_vfs) {
		/* VF is enabled, Assign ring to VF */
		nvfs = oct->sriov_info.num_vfs;
		vf_srn = g_vf_srn[oct->octeon_id];
	} else {
		/* No VF enabled, Assign ring to PF */
		nvfs = 0;
		vf_srn = 0;
	}

	/* Only PF0 needs to program these */
	//if(!oct->octeon_id) {
	if (1) {
		/* FIXME: Who programs RPPF in SDP_MACX_PF_RING_CTL register? */
		regval = 0;
		if(oct->chip_id == OCTEON_CNXK_PF) {
			regval = (npfs  << CNXK_SDP_MAC_PF_RING_CTL_NPFS_BIT_POS);
			regval |= (pf_srn << CNXK_SDP_MAC_PF_RING_CTL_SRN_BIT_POS);
			regval |= (rppf << CNXK_SDP_MAC_PF_RING_CTL_RPPF_BIT_POS);
		}

		octeon_write_csr64(oct, CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port), 
					regval);
		/* Get RPPF from MACX_PF_RING_CTL */
		regval = octeon_read_csr64(oct,
				CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port));

		cavium_print_msg("SDP_MAC_PF_RING_CTL[%d]:0x%llx\n", oct->pcie_port,
					regval);
		/* Assign rings to PF */
		for (i = 0; i < rppf; i++) {
			regval = octeon_read_csr64(oct, CNXK_SDP_EPVF_RING(pf_srn + i));
			cavium_print_msg("SDP_EPVF_RING[0x%llx]:0x%llx\n",
					CNXK_SDP_EPVF_RING(pf_srn + i), regval);
			regval = 0;
			/* VSR: TODO: is pcie_port==2 means second PEM ? */
			if (oct->pcie_port == 2)
				regval |= (8 << CNXK_SDP_FUNC_SEL_EPF_BIT_POS);
			regval |= (0 << CNXK_SDP_FUNC_SEL_FUNC_BIT_POS);

			octeon_write_csr64(oct, CNXK_SDP_EPVF_RING(pf_srn + i), regval);

			regval = octeon_read_csr64(oct, CNXK_SDP_EPVF_RING(pf_srn + i));
			cavium_print_msg("SDP_EPVF_RING[0x%llx]:0x%llx\n",
					CNXK_SDP_EPVF_RING(pf_srn + i), regval);
		}
	}

	if (!oct->sriov_info.num_vfs) {
		oct->drv_flags |= OCTEON_NON_SRIOV_MODE;

		cavium_print_msg(" num_vfs is zero, SRIOV is not enabled.\n");
		vf_rings = 0;
	} else {

		oct->drv_flags |= OCTEON_SRIOV_MODE;
		oct->drv_flags |= OCTEON_MBOX_CAPABLE;

		/* Program RINFO register */
		regval = octeon_read_csr64(oct, CNXK_SDP_EPF_RINFO);
		cavium_print_msg("SDP_EPF_RINFO[0x%x]:0x%llx\n", CNXK_SDP_EPF_RINFO, regval);

		regval = 0;
		regval |= (vf_srn << CNXK_SDP_EPF_RINFO_SRN_BIT_POS);
		regval |= (rpvf << CNXK_SDP_EPF_RINFO_RPVF_BIT_POS);
		regval |= (nvfs << CNXK_SDP_EPF_RINFO_NVFS_BIT_POS);

		octeon_write_csr64(oct, CNXK_SDP_EPF_RINFO, regval);

		regval = octeon_read_csr64(oct, CNXK_SDP_EPF_RINFO);
		cavium_print_msg("SDP_EPF_RINFO[0x%x]:0x%llx\n", CNXK_SDP_EPF_RINFO, regval);

		/* Assign ring0 to VF */
		for (j = 0; j < nvfs; j++) {
			srn = vf_srn + (j * num_rings_per_vf_pt[oct->octeon_id]);
			for (i = 0; i < rpvf; i++) {
				regval = octeon_read_csr64(oct, CNXK_SDP_EPVF_RING(srn + i));
				cavium_print_msg("SDP_EPVF_RING[0x%llx]:0x%llx\n",
						CNXK_SDP_EPVF_RING(srn + i), regval);
				regval = 0;
				if (oct->pcie_port == 2)
					regval |= (8 << CNXK_SDP_FUNC_SEL_EPF_BIT_POS);
				regval |= ((j+1) << CNXK_SDP_FUNC_SEL_FUNC_BIT_POS);

				octeon_write_csr64(oct, CNXK_SDP_EPVF_RING(srn + i), regval);

				regval = octeon_read_csr64(oct, CNXK_SDP_EPVF_RING(srn + i));
				cavium_print_msg("SDP_EPVF_RING[0x%llx]:0x%llx\n",
						CNXK_SDP_EPVF_RING(srn + i), regval);
			}
		}
	}

	oct->sriov_info.rings_per_vf = rpvf;
	/** All the remaining queues are handled by Physical Function */
	//oct->sriov_info.pf_srn = oct->octeon_id * num_rings_per_pf_pt;
	oct->sriov_info.pf_srn = pf_srn_pt[oct->octeon_id];
	oct->sriov_info.rings_per_pf = rppf;

	oct->sriov_info.sriov_enabled = 0;

	/** Over Write the config values with the calculated ones */
	CFG_GET_NUM_VFS(cnxk->conf, oct->pf_num) = oct->sriov_info.num_vfs;
	CFG_GET_RINGS_PER_VF(cnxk->conf, oct->pf_num) =
	    oct->sriov_info.rings_per_vf;
	CFG_GET_TOTAL_PF_RINGS(cnxk->conf, oct->pf_num) = oct->sriov_info.rings_per_pf;
	CFG_GET_IQ_MAX_BASE_Q(CHIP_FIELD(oct, cnxk_pf, conf)) = oct->sriov_info.rings_per_pf;
	CFG_GET_OQ_MAX_BASE_Q(CHIP_FIELD(oct, cnxk_pf, conf)) = oct->sriov_info.rings_per_pf;

	return 0;
}

static void
octeon_wait_fw_info(struct work_struct *work)
{
	struct cavium_wq *fw_hs_wq;
	struct fw_handshake_wrk *fw_hs_wrk;
	union ring rinfo;
	octeon_device_t *oct;
	u64 regval;

	fw_hs_wq = container_of(work,struct cavium_wq, wk.work.work);
	fw_hs_wrk = (struct fw_handshake_wrk *)fw_hs_wq->wk.ctxptr;
	oct = fw_hs_wrk->oct;
	printk("OCTEON id = %d\n", oct->octeon_id);
	rinfo.u = octeon_read_csr64(oct, CNXK_SDP_R_IN_PKT_CNT(0));
	if (rinfo.s.dir == FW_TO_HOST) {
		if (fw_hs_wrk->exhg_state == NO_EXHG) {
			printk("pf_srn %d rpf %d rvf %d nvf %d\n", rinfo.s.pf_srn,
			       rinfo.s.rppf, rinfo.s.rpvf, rinfo.s.numvf);

			/*
			 * For now, rings per PF and rings per VF must match
			 * the firmware DTB, as this is the only way that host
			 * and target agree on the layout of the rings.
			 * The firmware config determines the layout of the rings,
			 * so the host needs to be updated to account for that
			 * layout when not using all the rings configured by
			 * the firmware.
			 * IPBUSW-12515 is the Jira issue tracking this fix.
			 */
#if 0
			pf_srn_pt[oct->octeon_id] = rinfo.s.pf_srn;
			g_vf_srn[oct->octeon_id] = rinfo.s.pf_srn + rinfo.s.rppf;
			num_rings_per_pf_pt[oct->octeon_id] = num_rings_per_pf;
			num_rings_per_vf_pt[oct->octeon_id] = num_rings_per_vf;
			if (num_rings_per_pf_pt[oct->octeon_id] != 1)
				num_rings_per_pf_pt[oct->octeon_id] =
					roundup_pow_of_two(num_rings_per_pf);
			if (num_rings_per_vf_pt[oct->octeon_id] != 1)
				num_rings_per_vf_pt[oct->octeon_id] =
					roundup_pow_of_two(num_rings_per_vf);

			if (num_rings_per_pf_pt[oct->octeon_id] > rinfo.s.rppf)
				num_rings_per_pf_pt[oct->octeon_id] = rinfo.s.rppf;
			if (num_rings_per_vf_pt[oct->octeon_id] > rinfo.s.rpvf)
				num_rings_per_vf_pt[oct->octeon_id] = rinfo.s.rpvf;
#else
			if (oct->sriov_info.num_vfs > rinfo.s.numvf)
				oct->sriov_info.num_vfs = rinfo.s.numvf;
			/* Take and use whatever the firmware provides for Ring counts */
			pf_srn_pt[oct->octeon_id] = rinfo.s.pf_srn;
			g_vf_srn[oct->octeon_id] = rinfo.s.pf_srn + rinfo.s.rppf;
			num_rings_per_pf_pt[oct->octeon_id] = rinfo.s.rppf;
			num_rings_per_vf_pt[oct->octeon_id] = rinfo.s.rpvf;
			if (num_rings_per_pf != rinfo.s.rppf
			    || num_rings_per_vf != rinfo.s.rpvf) {
				printk("NOTICE: rings per PF/VF adjusted to match NIC requirements\n");
			}
			printk("NOTICE: rings per PF: %d, VF: %d\n", rinfo.s.rppf, rinfo.s.rpvf);
			num_rings_per_pf = rinfo.s.rppf;
			num_rings_per_vf = rinfo.s.rpvf;
#endif

			rinfo.s.rppf = num_rings_per_pf;
			rinfo.s.rpvf = num_rings_per_vf;
			rinfo.s.numvf = oct->sriov_info.num_vfs;
			rinfo.s.dir = HOST_TO_FW;
			octeon_write_csr64(oct, CNXK_SDP_R_IN_PKT_CNT(0),
					rinfo.u);
			fw_hs_wrk->exhg_state = RINFO_HOST;
		} else if (fw_hs_wrk->exhg_state == RINFO_HOST) {
			fw_hs_wrk->exhg_state = RINFO_FW_ACK;
			octeon_write_csr64(oct, CNXK_SDP_R_IN_PKT_CNT(0),
					0xFFFFFFFFF);
			/* Relinquish the ring as the exchange is complete */
			regval = 0;

			printk("exchg complete for %d\n", oct->octeon_id);
			if(oct->chip_id == OCTEON_CNXK_PF) {
				regval = (0  << CNXK_SDP_MAC_PF_RING_CTL_NPFS_BIT_POS);
				regval |= (0 << CNXK_SDP_MAC_PF_RING_CTL_SRN_BIT_POS);
				regval |= (1 << CNXK_SDP_MAC_PF_RING_CTL_RPPF_BIT_POS);
			}
			octeon_write_csr64(oct,
				CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port),
				regval);
			resume_cnxk_setup(fw_hs_wrk->oct);
			octeon_device_init(fw_hs_wrk->oct, 1);
			/* Enabling NIC module to continue */
			g_app_mode[oct->octeon_id] = CVM_DRV_NIC_APP;

			return;
		}
	}

        queue_delayed_work(fw_hs_wq->wq, &fw_hs_wq->wk.work, HZ * 1);
}


enum setup_stage setup_cnxk_octeon_pf_device(octeon_device_t * oct)
{
	octeon_cnxk_pf_t *cnxk = (octeon_cnxk_pf_t *) oct->chip;
	u64 regval;

	cnxk->oct = oct;

	if (octeon_map_pci_barx(oct, 0, 0))
		return SETUP_FAIL;

	/* TODO: It is not required */
	if (octeon_map_pci_barx(oct, 1, MAX_BAR1_IOREMAP_SIZE)) {
		cavium_error("%s CNXK BAR1 map failed\n", __FUNCTION__);
		octeon_unmap_pci_barx(oct, 0);
		return SETUP_FAIL;
	}

	if (octeon_map_pci_barx(oct, 2, MAX_BAR1_IOREMAP_SIZE)) {
		cavium_error("%s CNXK BAR4 map failed\n", __FUNCTION__);
		octeon_unmap_pci_barx(oct, 0);
		octeon_unmap_pci_barx(oct, 1);
		return SETUP_FAIL;
	}

	cnxk->conf = (cnxk_pf_config_t *) oct_get_config_info(oct);
	if (cnxk->conf == NULL) {
		cavium_error("%s No Config found for CNXK\n", __FUNCTION__);
		goto free_barx;
	}
	oct->fn_list.setup_iq_regs = cnxk_setup_iq_regs;
	oct->fn_list.setup_oq_regs = cnxk_setup_oq_regs;
	oct->fn_list.setup_mbox_regs = cnxk_setup_pf_mbox_regs;

	oct->fn_list.interrupt_handler = cnxk_interrupt_handler;
	oct->fn_list.msix_interrupt_handler = cnxk_pf_msix_interrupt_handler;

	oct->fn_list.soft_reset = cnxk_pf_soft_reset;
	oct->fn_list.setup_device_regs = cnxk_setup_pf_device_regs;
	oct->fn_list.reinit_regs = cnxk_reinit_regs;
	oct->fn_list.update_iq_read_idx = cnxk_update_read_index;

	oct->fn_list.bar1_idx_setup = cnxk_bar1_idx_setup;
	oct->fn_list.bar1_idx_write = cnxk_bar1_idx_write;
	oct->fn_list.bar1_idx_read = cnxk_bar1_idx_read;

	oct->fn_list.enable_interrupt = cnxk_enable_pf_interrupt;
	oct->fn_list.disable_interrupt = cnxk_disable_pf_interrupt;

	oct->fn_list.enable_io_queues = cnxk_enable_io_queues;
	oct->fn_list.disable_io_queues = cnxk_disable_io_queues;

	oct->fn_list.enable_input_queue = cnxk_enable_input_queue;
	oct->fn_list.enable_output_queue = cnxk_enable_output_queue;

	oct->fn_list.disable_input_queue = cnxk_disable_input_queue;
	oct->fn_list.disable_output_queue = cnxk_disable_output_queue;

	oct->fn_list.force_io_queues_off = cnxk_force_io_queues_off;

	oct->fn_list.dump_registers = cnxk_dump_pf_initialized_regs;

	cnxk_setup_reg_address(oct);

	/* Update pcie port number in the device structure */
	cnxk_get_pcie_qlmport(oct);

/* TODO: Hardcoding ring configuration for validation on emulator.
 *       Remove following code after validation on emulator */
#ifdef BUILD_FOR_EMULATOR

#define NPFS	8
#define PFS_SRN 0
#define RPPF	2

	/* No rings for VF's of this PF */
	octeon_write_csr64(oct, CNXK_SDP_EPF_RINFO, 0ULL);
	/* Program MACX_PF_RING_CTL from PF0 only. 
	 * TODO: Condition check should done on the PF number not on octeon_id */
	if(1) {
		/* Hardcode NPFS = 8, RPPF = 2 and SRN = 0 */
		regval = 0;
		regval = (NPFS  << CNXK_SDP_MAC_PF_RING_CTL_NPFS_BIT_POS);
		regval |= (PFS_SRN << CNXK_SDP_MAC_PF_RING_CTL_SRN_BIT_POS);
		regval |= (RPPF << CNXK_SDP_MAC_PF_RING_CTL_RPPF_BIT_POS);

		octeon_write_csr64(oct, CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port), 
				regval);
	}

	oct->drv_flags |= OCTEON_NON_SRIOV_MODE;
	oct->sriov_info.rings_per_vf = 0;
	/* Starting ring number for this PF */
	oct->sriov_info.pf_srn = oct->octeon_id * RPPF;
	oct->sriov_info.rings_per_pf = RPPF;
	CFG_GET_TOTAL_PF_RINGS(cnxk->conf, oct->pf_num) = RPPF;
	CFG_GET_IQ_MAX_BASE_Q(CHIP_FIELD(oct, cnxk_pf, conf)) = PFS_SRN;
	CFG_GET_OQ_MAX_BASE_Q(CHIP_FIELD(oct, cnxk_pf, conf)) = PFS_SRN;

	cavium_print_msg(" OCTEON PF[%d] IOQ CONFIGURATION \n", oct->pf_num);
	cavium_print_msg(" PF[%d] TOTAL NUMBER OF RINGS:%u \n", oct->pf_num,
			 CFG_GET_TOTAL_PF_RINGS(cnxk->conf, oct->pf_num));
	cavium_print_msg(" PF[%d] RINGS PER PF:%u \n", oct->pf_num,
			 oct->sriov_info.rings_per_pf);
	cavium_print_msg(" PF[%d] STARTING RING NUMBER:%u \n", oct->pf_num,
			 oct->sriov_info.pf_srn);
	cavium_print_msg(" PF[%d] TOTAL NUMBER OF VFs:%u \n", oct->pf_num,
			 oct->sriov_info.num_vfs);
	cavium_print_msg(" PF[%d] RINGS PER VF:%u \n", oct->pf_num,
			 oct->sriov_info.rings_per_vf);
	return SETUP_SUCCESS;
#else
	/* 
	 * Take control of the first ring, as the ring info 
	 * from F/W is exchanged through its register.
	 */
	regval = 0;
	regval = (0ULL  << CNXK_SDP_MAC_PF_RING_CTL_NPFS_BIT_POS);
	regval |= (0ULL << CNXK_SDP_MAC_PF_RING_CTL_SRN_BIT_POS);
	regval |= (1ULL << CNXK_SDP_MAC_PF_RING_CTL_RPPF_BIT_POS);
	
	octeon_write_csr64(oct, CNXK_SDP_MAC_PF_RING_CTL(oct->pcie_port),
			regval);

 	oct->sdp_wq.wq = alloc_workqueue("sdp_epmode_fw_hs", WQ_MEM_RECLAIM, 0);

	hs_wrk[oct->octeon_id].oct = oct;
	hs_wrk[oct->octeon_id].exhg_state = NO_EXHG;
	oct->sdp_wq.wk.ctxptr = &hs_wrk[oct->octeon_id];
        INIT_DELAYED_WORK(&oct->sdp_wq.wk.work, octeon_wait_fw_info);
        queue_delayed_work(oct->sdp_wq.wq, &oct->sdp_wq.wk.work, 0);

	return SETUP_IN_PROGRESS;
#endif

free_barx:
	octeon_unmap_pci_barx(oct, 0);
	octeon_unmap_pci_barx(oct, 1);
	octeon_unmap_pci_barx(oct, 2);
	return SETUP_FAIL;

}

int validate_cnxk_pf_config_info(cnxk_pf_config_t * conf_cnxk)
{
	uint64_t total_instrs = 0ULL;

	if (CFG_GET_IQ_MAX_Q(conf_cnxk) > CNXK_MAX_INPUT_QUEUES) {
		cavium_error("%s: Num IQ (%d) exceeds Max (%d)\n",
			     __CVM_FUNCTION__, CFG_GET_IQ_MAX_Q(conf_cnxk),
			     CNXK_MAX_INPUT_QUEUES);
		return 1;
	}

	if (CFG_GET_OQ_MAX_Q(conf_cnxk) > CNXK_MAX_OUTPUT_QUEUES) {
		cavium_error("%s: Num OQ (%d) exceeds Max (%d)\n",
			     __CVM_FUNCTION__, CFG_GET_OQ_MAX_Q(conf_cnxk),
			     CNXK_MAX_OUTPUT_QUEUES);
		return 1;
	}

	if (CFG_GET_IQ_INSTR_TYPE(conf_cnxk) != OCTEON_32BYTE_INSTR &&
	    CFG_GET_IQ_INSTR_TYPE(conf_cnxk) != OCTEON_64BYTE_INSTR) {
		cavium_error("%s: Invalid instr type for IQ\n",
			     __CVM_FUNCTION__);
		return 1;
	}

	if (!(CFG_GET_IQ_NUM_DESC(conf_cnxk)) || !(CFG_GET_IQ_DB_MIN(conf_cnxk))
	    || !(CFG_GET_IQ_DB_TIMEOUT(conf_cnxk))) {
		cavium_error("%s: Invalid parameter for IQ\n",
			     __CVM_FUNCTION__);
		return 1;
	}

	total_instrs =
	    CFG_GET_IQ_NUM_DESC(conf_cnxk) * CFG_GET_IQ_MAX_Q(conf_cnxk);

	if (CFG_GET_IQ_PENDING_LIST_SIZE(conf_cnxk) < total_instrs) {
		cavium_error
		    ("%s Pending list size (%d) should be >= total instructions queue size (%d)\n",
		     __CVM_FUNCTION__, CFG_GET_IQ_PENDING_LIST_SIZE(conf_cnxk),
		     (int)total_instrs);
		return 1;
	}

	if (!(CFG_GET_OQ_INFO_PTR(conf_cnxk)) ||
	    !(CFG_GET_OQ_PKTS_PER_INTR(conf_cnxk)) ||
	    !(CFG_GET_OQ_NUM_DESC(conf_cnxk)) ||
	    !(CFG_GET_OQ_REFILL_THRESHOLD(conf_cnxk)) ||
	    !(CFG_GET_OQ_BUF_SIZE(conf_cnxk))) {
		cavium_error("%s: Invalid parameter for OQ\n",
			     __CVM_FUNCTION__);
		return 1;
	}

	if (!(CFG_GET_OQ_INTR_TIME(conf_cnxk))) {
		cavium_error("%s: Invalid parameter for OQ\n",
			     __CVM_FUNCTION__);
		return 1;
	}

	return 0;
}

/* $Id$ */