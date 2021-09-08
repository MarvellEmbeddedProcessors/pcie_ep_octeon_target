/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

#include "octeon_main.h"
#include "octeon_debug.h"
#include "octeon_macros.h"
#include "octeon_mem_ops.h"

extern int octeon_init_nr_free_list(octeon_instr_queue_t * iq, int count);


#ifdef OCT_NIC_IQ_USE_NAPI
#else
extern oct_poll_fn_status_t check_db_timeout(void *octptr, unsigned long iq_no);
#endif

void octeon_init_iq_intr_moderation(octeon_device_t *oct)
{
	struct iq_intr_wq *iq_intr_wq;

	iq_intr_wq = &oct->iq_intr_wq;
	memset(iq_intr_wq->last_pkt_cnt, 0, sizeof(uint64_t)*64);
	iq_intr_wq->last_ts = jiffies;
	iq_intr_wq->oct = oct;
	iq_intr_wq->wq = alloc_workqueue("octeon_iq_intr_tune",
					 WQ_MEM_RECLAIM, 0);
	if (iq_intr_wq->wq == NULL) {
		cavium_error
		    ("OCTEON: Cannot create wq for IQ interrupt moderation\n");
		return;
	}

	INIT_DELAYED_WORK(&iq_intr_wq->work, octeon_iq_intr_tune);
	queue_delayed_work(iq_intr_wq->wq, &iq_intr_wq->work, msecs_to_jiffies(10));
}

void octeon_cleanup_iq_intr_moderation(octeon_device_t *oct)
{
	struct iq_intr_wq *iq_intr_wq;

	iq_intr_wq = &oct->iq_intr_wq;
	if (iq_intr_wq->wq == NULL)
		return;

	cancel_delayed_work_sync(&iq_intr_wq->work);
	flush_workqueue(iq_intr_wq->wq);
	destroy_workqueue(iq_intr_wq->wq);
	iq_intr_wq->wq = NULL;
}

/* Return 0 on success, 1 on failure */
int octeon_init_instr_queue(octeon_device_t * oct, int iq_no)
{
	octeon_instr_queue_t *iq;
	octeon_iq_config_t *conf = NULL;
	uint32_t q_size;
#ifdef OCT_NIC_IQ_USE_NAPI
#else	
#ifndef APP_CMD_POST
	octeon_poll_ops_t poll_ops;
#endif
#endif	

	if (oct->chip_id == OCTEON_CN83XX_PF)
		conf = &(CFG_GET_IQ_CFG(CHIP_FIELD(oct, cn83xx_pf, conf)));
	else if (oct->chip_id == OCTEON_CN93XX_PF ||
		 oct->chip_id == OCTEON_CN98XX_PF)
		conf = &(CFG_GET_IQ_CFG(CHIP_FIELD(oct, cn93xx_pf, conf)));
	else if (oct->chip_id == OCTEON_CNXK_PF)
		conf = &(CFG_GET_IQ_CFG(CHIP_FIELD(oct, cnxk_pf, conf)));

	if (!conf) {
		cavium_error("OCTEON: Unsupported Chip %x\n", oct->chip_id);
		return 1;
	}

	q_size = conf->instr_type * conf->num_descs;

	iq = oct->instr_queue[iq_no];

	iq->base_addr = octeon_pci_alloc_consistent(oct->pci_dev, q_size,
						    &iq->base_addr_dma,
						    iq->app_ctx);
	if (!iq->base_addr) {
		cavium_error
		    ("OCTEON: Cannot allocate memory for instr queue %d\n",
		     iq_no);
		return 1;
	}

#ifdef OCT_TX2_ISM_INT	
	if (oct->chip_id == OCTEON_CN93XX_PF ||
	    oct->chip_id == OCTEON_CN98XX_PF ||
	    oct->chip_id == OCTEON_CNXK_PF) {
		iq->ism.pkt_cnt_addr =
		    octeon_pci_alloc_consistent(oct->pci_dev, 8,
						&iq->ism.pkt_cnt_dma, iq->app_ctx);

		if (cavium_unlikely(!iq->ism.pkt_cnt_addr)) {
			cavium_error("OCTEON: Output queue %d ism memory alloc failed\n",
				     iq_no);
			return 1;
		}
	
		cavium_print(PRINT_REGS, "iq[%d]: ism addr: virt: 0x%p, dma: %lx",
			     q_no, iq->ism.pkt_cnt_addr, iq->ism.pkt_cnt_dma);
	}
#endif		

	if (conf->num_descs & (conf->num_descs - 1)) {
		cavium_error
		    ("OCTEON: Number of descriptors for instr queue %d not in power of 2.\n",
		     iq_no);
		return 1;
	}

	iq->max_count = conf->num_descs;

	if (octeon_init_nr_free_list(iq, iq->max_count)) {
		octeon_pci_free_consistent(oct->pci_dev, q_size, iq->base_addr,
					   iq->base_addr_dma, iq->app_ctx);
		cavium_error("OCTEON: Alloc failed for IQ[%d] nr free list\n",
			     iq_no);
		return 1;
	}
	/*  Maintaining pending list count more than iq->max_count to handle non-blocking reqs */
	if (octeon_init_iq_pending_list(oct, iq_no, (4 * iq->max_count))) {
		octeon_pci_free_consistent(oct->pci_dev, q_size, iq->base_addr,
					   iq->base_addr_dma, iq->app_ctx);
		cavium_error
		    ("OCTEON: Cannot create pending list for instr queue %d\n",
		     iq_no);
		return 1;
	}

	cavium_print(PRINT_FLOW, "IQ[%d]: base: %p basedma: %lx count: %d\n",
		     iq_no, iq->base_addr, iq->base_addr_dma, iq->max_count);

	iq->iq_no = iq_no;
	iq->fill_threshold = conf->db_min;
	iq->fill_cnt = 0;
	iq->host_write_index = 0;
	iq->octeon_read_index = 0;
	iq->flush_index = 0;
	iq->last_db_time = 0;
	iq->do_auto_flush = 1;
	iq->db_timeout = conf->db_timeout;
	cavium_atomic_set(&iq->instr_pending, 0);
	iq->pkts_processed = 0;
	iq->pkt_in_done = 0;

	oct->io_qmask.iq |= (1ULL << iq_no);

	/* Set the 32B/64B mode for each input queue */
	if (conf->instr_type == 64)
		oct->io_qmask.iq64B |= (1ULL << iq_no);
	iq->iqcmd_64B = (conf->instr_type == 64);

	oct->fn_list.setup_iq_regs(oct, iq_no);


#ifdef OCT_NIC_IQ_USE_NAPI
#else
#ifndef APP_CMD_POST
//this path is taken when IQ NAPI is disabled
	poll_ops.fn = check_db_timeout;
	poll_ops.fn_arg = (unsigned long)iq_no;
	poll_ops.ticks = 1000;
	cavium_strncpy(poll_ops.name, sizeof(poll_ops.name), "Doorbell Timeout",
		       sizeof(poll_ops.name) - 1);
	poll_ops.rsvd = 0xff;
	octeon_register_poll_fn(oct->octeon_id, &poll_ops);
#endif
#endif	

	return 0;
}

int octeon_delete_instr_queue(octeon_device_t * oct, int iq_no)
{
	uint64_t desc_size = 0, q_size;
	octeon_instr_queue_t *iq = oct->instr_queue[iq_no];

#ifdef OCT_NIC_IQ_USE_NAPI
#else	
	octeon_unregister_poll_fn(oct->octeon_id, check_db_timeout, iq_no);
#endif	

	if (oct->chip_id == OCTEON_CN83XX_PF)
		desc_size =
		    CFG_GET_IQ_INSTR_TYPE(CHIP_FIELD(oct, cn83xx_pf, conf));
	else if (oct->chip_id == OCTEON_CN93XX_PF ||
		 oct->chip_id == OCTEON_CN98XX_PF)
		desc_size =
		    CFG_GET_IQ_INSTR_TYPE(CHIP_FIELD(oct, cn93xx_pf, conf));
	else if (oct->chip_id == OCTEON_CNXK_PF)
		desc_size =
		    CFG_GET_IQ_INSTR_TYPE(CHIP_FIELD(oct, cnxk_pf, conf));

#ifdef OCT_TX2_ISM_INT
	if (oct->chip_id == OCTEON_CN93XX_PF ||
	    oct->chip_id == OCTEON_CN98XX_PF ||
	    oct->chip_id == OCTEON_CNXK_PF) {
		if (iq->ism.pkt_cnt_addr)
			octeon_pci_free_consistent(oct->pci_dev, 8,
						   iq->ism.pkt_cnt_addr, iq->ism.pkt_cnt_dma,
						   iq->app_ctx);
	}
#endif

	if (iq->plist)
		octeon_delete_iq_pending_list(oct, iq->plist);

	if (iq->nr_free.q)
		cavium_free_virt(iq->nr_free.q);

	if (iq->nrlist)
		cavium_free_virt(iq->nrlist);

	if (iq->base_addr) {
		q_size = iq->max_count * desc_size;
		octeon_pci_free_consistent(oct->pci_dev, q_size, iq->base_addr,
					   iq->base_addr_dma,
					   (void *)(long)iq->iq_no);

		oct->io_qmask.iq &= ~(1ULL << iq_no);

		cavium_memset(iq, 0, sizeof(octeon_instr_queue_t));

		cavium_free_virt(oct->instr_queue[iq_no]);

		oct->instr_queue[iq_no] = NULL;

		return 0;
	}
	return 1;
}

/* $Id: request_manager.c 118468 2015-05-27 11:26:18Z vattunuru $ */
