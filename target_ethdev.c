// SPDX-License-Identifier: (GPL-2.0)
/* Mgmt ethernet driver
 *
 * Copyright (C) 2015-2019 Marvell, Inc.
 */

#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include <facility.h>
#include <dma_api.h>

#include <desc_queue.h>
#include <bar_space_mgmt_net.h>
#include <target_ethdev.h>


#define MGMT_IFACE_NAME "mvmgmt%d"

#define TX_DESCQ_OFFSET(mdev)	  \
	((uint8_t *)(mdev->bar_map + OTXMN_TX_DESCQ_OFFSET))
#define RX_DESCQ_OFFSET(mdev)     \
	((uint8_t *)(mdev->bar_map + OTXMN_RX_DESCQ_OFFSET))

#define TARGET_STATUS_REG(mdev)      \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_STATUS_REG))
#define TARGET_INTR_REG(mdev)      \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_INTR_REG))
#define TARGET_MBOX_MSG_REG(mdev, i)  \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_MBOX_OFFSET + (i * 8)))
#define TARGET_MBOX_ACK_REG(mdev)     \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_MBOX_ACK_REG))

#define HOST_STATUS_REG(mdev)         \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_STATUS_REG))
#define HOST_INTR_REG(mdev)         \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_INTR_REG))
#define HOST_MBOX_ACK_REG(mdev)       \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_MBOX_ACK_REG))
#define HOST_MBOX_MSG_REG(mdev, i)    \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_MBOX_OFFSET + (i * 8)))

static struct otxmn_dev *gmdev;
static int rxq_refill(struct otxmn_dev *mdev, int q_idx, int count);
static int cleanup_rx_skb_list(struct otxmn_dev *mdev,  int rxq);
static int cleanup_tx_skb_list(struct otxmn_dev *mdev,  int txq);
static int mgmt_net_napi_poll(struct napi_struct *napi, int budget);
static irqreturn_t mgmt_net_intr_hndlr(int irq, void *arg);

/* the following might be called in loop conditionals avoid compiler optimizations */
static uint64_t get_host_status(struct otxmn_dev *mdev)
{
	return READ_ONCE(*HOST_STATUS_REG(mdev));
}

static uint64_t get_host_mbox_ack(struct otxmn_dev *mdev)
{
	return READ_ONCE(*HOST_MBOX_ACK_REG(mdev));
}
static uint64_t get_target_status(struct otxmn_dev *mdev)
{
	return READ_ONCE(*TARGET_STATUS_REG(mdev));
}

static void set_target_mbox_ack_reg(struct otxmn_dev *mdev, uint32_t id)
{
	WRITE_ONCE(*TARGET_MBOX_ACK_REG(mdev), id);
}

static void mbox_send_msg(struct otxmn_dev *mdev, union otxmn_mbox_msg *msg)
{
	unsigned long timeout = msecs_to_jiffies(OTXMN_MBOX_TIMEOUT_MS);
	unsigned long period = msecs_to_jiffies(OTXMN_MBOX_WAIT_MS);
	unsigned long expire;
	int i, id;

	mutex_lock(&mdev->mbox_lock);
	mdev->send_mbox_id++;
	msg->s.hdr.id = mdev->send_mbox_id;
	id = msg->s.hdr.id;
	for (i = 1; i <= msg->s.hdr.sizew; i++) {
		*TARGET_MBOX_MSG_REG(mdev, i) =  msg->words[i];
	}
	/*  data should go in before header */
	wmb();
	/*printk(KERN_DEBUG "send mbox msg id:%d opcode:%d sizew: %d\n",
	       msg->s.hdr.id, msg->s.hdr.opcode, msg->s.hdr.sizew);
	*/
	*TARGET_MBOX_MSG_REG(mdev, 0) = msg->words[0];
	/* make sure msg is written before wait for ack */
	wmb();
	/* more than 1 word mbox messages need explicit ack */
	if (msg->s.hdr.req_ack || msg->s.hdr.sizew) {
		/* printk(KERN_DEBUG "send mbox msg chk ack\n"); */
		expire = jiffies + timeout;
		while (get_host_mbox_ack(mdev) != id) {
			schedule_timeout_interruptible(period);
			if ((signal_pending(current)) ||
			    (time_after(jiffies, expire))) {
				printk(KERN_ERR "mgmt_net: mbox ack fail\n");
				break;
			}
		}
	}
	mutex_unlock(&mdev->mbox_lock);
}

static int mbox_check_msg_rcvd(struct otxmn_dev *mdev,
			       union otxmn_mbox_msg *msg)
{
	int i, ret;

	mutex_lock(&mdev->mbox_lock);
	msg->words[0] = *HOST_MBOX_MSG_REG(mdev, 0);
	/* make sure read completes before check */
	rmb();
	if (mdev->recv_mbox_id != msg->s.hdr.id) {
		/* new msg */
		/*
		printk(KERN_DEBUG "new mbox msg id:%d opcode:%d sizew: %d\n",
		       msg->s.hdr.id, msg->s.hdr.opcode, msg->s.hdr.sizew);
		*/
		mdev->recv_mbox_id = msg->s.hdr.id;
		for (i = 1; i <= msg->s.hdr.sizew; i++)
			msg->words[i] = *HOST_MBOX_MSG_REG(mdev, i);
		ret = 0;
	} else {
		ret = -ENOENT;
	}
	/* make sure msg is read before proceeding */
	rmb();
	mutex_unlock(&mdev->mbox_lock);
	return ret;
}

static void change_target_status(struct otxmn_dev *mdev, uint64_t status,
				 bool ack_wait)
{
	union otxmn_mbox_msg msg;

	/*
	printk(KERN_DEBUG "change target status from %llu to %llu \n",
	       *TARGET_STATUS_REG(mdev), status);
	*/
	*TARGET_STATUS_REG(mdev) = status;
	/* make sure status updaed beofre sending msg */
	wmb();
	memset(&msg, 0, sizeof(union otxmn_mbox_msg));
	msg.s.hdr.opcode = OTXMN_MBOX_TARGET_STATUS_CHANGE;
	if (ack_wait)
		msg.s.hdr.req_ack = 1;
	mbox_send_msg(mdev, &msg);
}

/* static void dump_hw_descq(struct otxcn_hw_descq *descq)
{
	struct  otxcn_hw_desc_ptr *ptr;
	int i;
	int count;

	printk(KERN_DEBUG "prod_idx %u\n", descq->prod_idx);
	printk(KERN_DEBUG "cons_idx %u\n", descq->cons_idx);
	printk(KERN_DEBUG "num_entries %u\n", descq->num_entries);
	printk(KERN_DEBUG "shadow_cons_idx_addr 0x%llx\n",
	       descq->shadow_cons_idx_addr);
	count = otxmn_circq_depth(descq->prod_idx, descq->cons_idx, descq->num_entries - 1);
	for (i = 0; i < count; i++) {
		ptr = &descq->desc_arr[i];
		printk(KERN_DEBUG "idx:%d is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
		       i,
		       ptr->hdr.s_mgmt_net.is_frag,
		       ptr->hdr.s_mgmt_net.total_len,
		       ptr->hdr.s_mgmt_net.ptr_type,
		       ptr->hdr.s_mgmt_net.ptr_len,
		       ptr->ptr);
	}
} */

static int setup_rxq(struct otxmn_dev *mdev, int hw_txq, int rxq)
{
	struct otxcn_hw_descq *descq;
	int i, descq_tot_size;
	struct otxmn_sw_descq *rq;
	int size;
	uint64_t *comp_data;
	local_dma_addr_t comp_iova;
	int count;

	descq = (struct otxcn_hw_descq *)(TX_DESCQ_OFFSET(mdev));
	for (i = 0; i < hw_txq; i++) {
		descq_tot_size = sizeof(struct otxcn_hw_descq) +
			(descq->num_entries * sizeof(struct otxcn_hw_desc_ptr));
		descq = (struct otxcn_hw_descq *)(TX_DESCQ_OFFSET(mdev) +
						  descq_tot_size);
	}
	/* dump_hw_descq(descq); */
	if ((descq->num_entries == 0) ||
	    (descq->num_entries & (descq->num_entries - 1))) {
		printk(KERN_ERR "mgmt_net: tx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}
	if (descq->cons_idx != 0) {
		printk(KERN_ERR "mgmt_net: tx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}
	if (descq->shadow_cons_idx_addr == 0) {
		printk(KERN_ERR "mgmt_net: tx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}
	rq = &mdev->rxqs[rxq];
	rq->mask = descq->num_entries - 1;
	rq->priv = mdev;
	rq->num_entries = descq->num_entries;
	rq->hw_descq = descq;
	rq->hw_dma_qidx = hw_txq;
	rq->shadow_cons_idx_ioremap_addr = (uint32_t __iomem *)host_ioremap(descq->shadow_cons_idx_addr);
	if (rq->shadow_cons_idx_ioremap_addr == NULL) {
		printk(KERN_ERR "mgmt_net: ioremap err line:%d\n", __LINE__);
		return -ENOENT;
	}
	rq->cmd_idx = 0;
	rq->q_num = rxq;
	rq->local_cons_idx = 0;
	rq->refill_prod_idx = 0;
	rq->comp_list = vzalloc(sizeof(struct dma_compl) *
					   descq->num_entries);
	if (!rq->comp_list) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		return -ENOENT;
	}
	rq->skb_list = vzalloc(sizeof(struct sk_buff *) *
					   descq->num_entries);
	if (!rq->skb_list) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		vfree(rq->comp_list);
		return -ENOENT;
	}
	rq->dma_list = vzalloc(sizeof(local_dma_addr_t) * descq->num_entries);
	if (!rq->dma_list) {
		vfree(rq->comp_list);
		vfree(rq->skb_list);
		printk(KERN_ERR "mgmt_net: rq dma_list  alloc failed\n");
		return -ENOMEM;
	}
	count = otxmn_circq_space(0, rq->refill_prod_idx, rq->mask);
	if (rxq_refill(mdev, rxq, count) != count) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		vfree(rq->comp_list);
		vfree(rq->dma_list);
		vfree(rq->skb_list);
		return -ENOENT;
	}
	rq->cmd_list = vzalloc(sizeof(struct dpi_dma_cmd) * descq->num_entries);
	if (!rq->cmd_list) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		vfree(rq->comp_list);
		cleanup_rx_skb_list(mdev, rxq);
		vfree(rq->dma_list);
		vfree(rq->skb_list);
		return -ENOENT;
	}
	size = sizeof(uint64_t) * descq->num_entries;
	comp_data = dma_alloc_coherent(mdev->dma_dev, size,  &comp_iova, GFP_KERNEL);
	if (!comp_data) {
		printk(KERN_ERR "mgmt_net: dma alloc err line:%d\n", __LINE__);
		kfree(comp_data);
		vfree(rq->comp_list);
		cleanup_rx_skb_list(mdev, rxq);
		vfree(rq->skb_list);
		vfree(rq->dma_list);
		vfree(rq->cmd_list);
		return -ENOENT;
	}
	rq->comp_data = comp_data;
	rq->comp_iova = comp_iova;
	for (i = 0; i < descq->num_entries; i++) {
		rq->cmd_list[i].comp_data = comp_data + i;
		rq->cmd_list[i].comp_iova = comp_iova + (i * sizeof(uint64_t));
	}
	spin_lock_init(&rq->lock);
	netif_napi_add(mdev->ndev, &rq->napi, mgmt_net_napi_poll, NAPI_POLL_WEIGHT);
	printk(KERN_DEBUG "mgmt_net: rxqs[%d]  maps to host txq %d\n", rxq,
	       hw_txq);
	return 0;
}

static int setup_txq(struct otxmn_dev *mdev, int hw_rxq, int txq)
{
	struct otxcn_hw_descq *descq;
	int i, descq_tot_size;
	struct otxmn_sw_descq *tq;
	int size;
	uint64_t *comp_data;
	local_dma_addr_t comp_iova;

	descq = (struct otxcn_hw_descq *)(RX_DESCQ_OFFSET(mdev));
	for (i = 0; i < hw_rxq; i++) {
		descq_tot_size = sizeof(struct otxcn_hw_descq) +
			(descq->num_entries * sizeof(struct otxcn_hw_desc_ptr));
		descq = (struct otxcn_hw_descq *)(TX_DESCQ_OFFSET(mdev) +
						  descq_tot_size);
	}
	printk(KERN_DEBUG " rx dmaq\n"); 
	/* dump_hw_descq(descq); */
	if ((descq->num_entries == 0) ||
	    (descq->num_entries & (descq->num_entries - 1))) {
		printk(KERN_ERR "mgmt_net: rx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}
	if (descq->cons_idx != 0) {
		printk(KERN_ERR "mgmt_net: rx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}
	if (descq->shadow_cons_idx_addr == 0) {
		printk(KERN_ERR "mgmt_net: rx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}
	if (descq->buf_size == 0) {
		printk(KERN_ERR "mgmt_net: rx hw descq err line:%d\n", __LINE__);
		return -ENOENT;
	}

	tq = &mdev->txqs[txq];
	tq->priv = mdev;
	tq->hw_descq = descq;
	tq->local_cons_idx = descq->cons_idx;
	tq->mask = descq->num_entries - 1;
	tq->num_entries = descq->num_entries;
	tq->shadow_cons_idx_ioremap_addr = (uint32_t __iomem *)host_ioremap(descq->shadow_cons_idx_addr);
	if (tq->shadow_cons_idx_ioremap_addr == NULL) {
		printk(KERN_ERR "mgmt_net: ioremap err line:%d\n", __LINE__);
		return -ENOENT;
	}
	tq->hw_dma_qidx = hw_rxq;
	tq->cmd_idx = 0;
	tq->q_num = txq;
	tq->comp_list = vzalloc(sizeof(struct dma_compl) *
					   descq->num_entries);
	if (!tq->comp_list) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		return -ENOENT;
	}

	tq->skb_list = vzalloc(sizeof(struct sk_buff *) *
					   descq->num_entries);
	if (!tq->skb_list) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		vfree(tq->comp_list);
		return -ENOENT;
	}
	tq->dma_list = vzalloc(sizeof(local_dma_addr_t) * descq->num_entries);
	if (!tq->dma_list) {
		vfree(tq->comp_list);
		vfree(tq->skb_list);
		printk(KERN_ERR "mgmt_net: tq dma_list malloc failed\n");
		return -ENOMEM;
	}

	tq->cmd_list = vzalloc(sizeof(struct dpi_dma_cmd) * descq->num_entries);
	if (!tq->cmd_list) {
		printk(KERN_ERR "mgmt_net: vmalloc err line:%d\n", __LINE__);
		vfree(tq->dma_list);
		vfree(tq->comp_list);
		vfree(tq->skb_list);
		return -ENOENT;
	}
	size = sizeof(uint64_t) * descq->num_entries;
	comp_data = dma_alloc_coherent(mdev->dma_dev, size,  &comp_iova, GFP_KERNEL);
	if (!comp_data) {
		printk(KERN_ERR "mgmt_net: dma alloc err line:%d\n", __LINE__);
		vfree(tq->dma_list);
		vfree(tq->comp_list);
		vfree(tq->skb_list);
		vfree(tq->cmd_list);
		return -ENOENT;
	}
	tq->comp_data = comp_data;
	tq->comp_iova = comp_iova;
	for (i = 0; i < descq->num_entries; i++) {
		tq->cmd_list[i].comp_data = comp_data + i;
		tq->cmd_list[i].comp_iova = comp_iova + (i * sizeof(uint64_t));
	}
	spin_lock_init(&tq->lock);
	printk(KERN_DEBUG " txq[%d] maps to host rxq %d\n", txq, hw_rxq);
	return 0;
}

static void cleanup_rxq(struct otxmn_dev *mdev, int rxq)
{
	struct otxmn_sw_descq *rq;

	rq = &mdev->rxqs[rxq];
	netif_napi_del(&rq->napi);
	cleanup_rx_skb_list(mdev, rxq);
	host_iounmap(rq->shadow_cons_idx_ioremap_addr);
        dma_free_coherent(mdev->dma_dev,
                          (sizeof(uint64_t) * rq->num_entries),
                          rq->comp_data,
                          rq->comp_iova);
	vfree(rq->comp_list);
	vfree(rq->skb_list);
	vfree(rq->dma_list);
	vfree(rq->cmd_list);
	memset(&mdev->rxqs[rxq], 0, sizeof(struct otxmn_sw_descq));
}

static void cleanup_txq(struct otxmn_dev *mdev, int txq)
{
	struct otxmn_sw_descq *tq;

	tq = &mdev->txqs[txq];
	cleanup_tx_skb_list(mdev, txq);
	host_iounmap(tq->shadow_cons_idx_ioremap_addr);
        dma_free_coherent(mdev->dma_dev,
                          (sizeof(uint64_t) * tq->num_entries),
                          tq->comp_data,
                          tq->comp_iova);
	vfree(tq->skb_list);
	vfree(tq->dma_list);
	vfree(tq->cmd_list);
	vfree(tq->comp_list);
	memset(&mdev->txqs[txq], 0, sizeof(struct otxmn_sw_descq));
}

static int setup_queues(struct otxmn_dev *mdev)
{
	/* hard coded mapping */
	if (setup_rxq(mdev, 0, 0))
		return -ENOMEM;
	if (setup_txq(mdev, 0, 0)) {
		cleanup_rxq(mdev, 0);
		return -ENOMEM;
	}
	return 0;
}

static void reset_queues(struct otxmn_dev *mdev)
{
	/* hard coded mapping */
	cleanup_txq(mdev, 0);
	cleanup_rxq(mdev, 0);
}

static int handle_host_status(struct otxmn_dev *mdev)
{
	uint64_t host_status;
	uint64_t cur_status;
	int ret = 0;

	cur_status  = get_target_status(mdev);
	host_status = get_host_status(mdev);
	/*
	printk(KERN_DEBUG "target status %llu\n", cur_status);
	printk(KERN_DEBUG "host status %llu\n", host_status);
	*/
	switch (cur_status) {
	case OTXMN_TARGET_READY:
		if (host_status == OTXMN_HOST_READY) {
			/* printk(KERN_DEBUG "host status ready\n"); */
			if (setup_queues(mdev)) {
				printk(KERN_ERR "mgmt_net setup_queues failed\n");
				change_target_status(mdev, OTXMN_TARGET_FATAL,
						     false);
				break;
			}
			ret = mv_facility_request_dbell_irq(MV_FACILITY_MGMT_NETDEV, 0, mgmt_net_intr_hndlr, (void *)mdev);
			if (ret) {
				reset_queues(mdev);
				printk(KERN_ERR "mgmt_net:request irq failed\n");
				change_target_status(mdev, OTXMN_TARGET_FATAL,
						     false);
				break;
			}
      			napi_enable(&mdev->rxqs[0].napi);
			WRITE_ONCE(*TARGET_INTR_REG(mdev), 1);
			change_target_status(mdev, OTXMN_TARGET_RUNNING, false);
			netif_carrier_on(mdev->ndev);
		}
		break;
	case OTXMN_TARGET_RUNNING:
		host_status = get_host_status(mdev);
		if ((host_status != OTXMN_HOST_READY) &&
		    (host_status != OTXMN_HOST_RUNNING)) {
			change_target_status(mdev, OTXMN_TARGET_READY, false);
			napi_synchronize(&mdev->rxqs[0].napi);
			/* printk(KERN_DEBUG "host status not ready\n"); */
			netif_carrier_off(mdev->ndev);
      			napi_disable(&mdev->rxqs[0].napi);
			printk("freeing dbell irq %d\n", __LINE__);
			mv_facility_free_dbell_irq(MV_FACILITY_MGMT_NETDEV, 0, (void *)mdev);
			reset_queues(mdev);
		}
		break;
	default:
		netdev_err(mdev->ndev, "unhandled state transition cur_state:%llu\n", cur_status);
		break;
	}
	return ret;
}

static int cleanup_rx_skb_list(struct otxmn_dev *mdev,  int rxq)
{
	struct otxmn_sw_descq *rq;
	struct sk_buff *skb;
	int i;
	int count;

	rq = &mdev->rxqs[rxq];
	count = rq->num_entries;
	for ( i = 0; i < count; i++) {
		skb = rq->skb_list[i];
		if(skb) {
			dma_unmap_single(mdev->dma_dev, rq->dma_list[i], OTXMN_RECV_BUF_SIZE,  DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			rq->skb_list[i] = NULL;
			rq->dma_list[i] = 0;
		}
	}
	return count;
}

static int cleanup_tx_skb_list(struct otxmn_dev *mdev,  int txq)
{
	struct otxmn_sw_descq *tq;
	struct sk_buff *skb;
	int i;
	int count;

	tq = &mdev->txqs[txq];
	count = tq->num_entries;
	for ( i = 0; i < count; i++) {
		skb = tq->skb_list[i];
		if(skb) {
			dma_unmap_single(mdev->dma_dev, tq->dma_list[i], skb->len,  DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			tq->skb_list[i] = NULL;
			tq->dma_list[i] = 0;
		}
	}
	return count;
}

/* static void pkt_hex_dump(struct sk_buff *skb)
{
	size_t len;
	int rowsize = 16;
	int i, l, linelen, remaining;
	int li = 0;
	uint8_t *data, ch;

	printk("Packet hex dump:\n");
	data = (uint8_t *) skb->data;
	len = skb->len;
	remaining = len;
	for (i = 0; i < len; i += rowsize) {
		printk("%06d\t", li);
		linelen = min(remaining, rowsize);
		remaining -= rowsize;
		for (l = 0; l < linelen; l++) {
			ch = data[l];
			printk(KERN_CONT "%02X ", (uint32_t) ch);
		}
		data += linelen;
		li += 10;
		printk(KERN_CONT "\n");
	}
} */

static bool __mgmt_txq_completion(struct otxmn_dev *mdev, int q_idx, int budget)
{
	struct otxmn_sw_descq  *tq;
	struct otxcn_hw_descq  *descq;
	int count;
	uint32_t cons_idx, prod_idx, mask;
	struct sk_buff *skb;
	int i;
	bool resched = false;
	uint64_t *data, comp_val;
	unsigned long start_time;
	int cmd_idx;

	tq = &mdev->txqs[q_idx];
	descq = tq->hw_descq;
	if (!mdev->admin_up)
		return false;
	if (get_target_status(mdev) != OTXMN_TARGET_RUNNING)
		return false;

	cons_idx = READ_ONCE(descq->cons_idx);
	prod_idx = READ_ONCE(tq->local_cons_idx);
	mask = tq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	if (!count)
		return 0;
	if (budget && count > budget) {
		count = budget;
		resched = true;
	}

	//printk(KERN_DEBUG "packets to be cleaned %d\n", count);
	for ( i = 0; i < count; i++) {
		data =  tq->comp_list[cons_idx].data;
		cmd_idx =  tq->comp_list[cons_idx].cmd_idx;
		comp_val = READ_ONCE(*data);
		if (comp_val != 0xFF) {
			if (comp_val != 0) {
				printk("mgmt_net: tx dma err idx:%d cmd_idx%d comp_val:0x%llx\n", cons_idx, cmd_idx, comp_val);
				return false;
			}
			skb = tq->skb_list[cons_idx];
			dma_unmap_single(mdev->dma_dev, tq->dma_list[cons_idx], skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			tq->skb_list[cons_idx] = NULL;
			tq->dma_list[cons_idx] = 0;
			cons_idx = otxmn_circq_inc(cons_idx, mask);
		} else {
			start_time = tq->cmd_list[cmd_idx].start_time;
			if (time_after(jiffies, (start_time + msecs_to_jiffies(5000)))) {
				printk("mgmt_net: tx dma timeout idx:%d cmd_idx%d\n", cons_idx, cmd_idx);
				return false;
			}
			break;
		}
	}
	if (i) {
		wmb();
		WRITE_ONCE(descq->cons_idx, cons_idx);
		//printk("tx comp host writel params host_addr 0x%p cons_idx %d\n", tq->shadow_cons_idx_ioremap_addr, descq->cons_idx);
		writel(descq->cons_idx, tq->shadow_cons_idx_ioremap_addr);
		wmb();
		/* send rx intr event only if host has not disabled intrs
		 * if host enables intr after we have read this
		 * host has also periodic task to wake up rx/tx
		 */
		if (READ_ONCE(*HOST_INTR_REG(mdev)) == 1)
			mv_send_facility_event(MV_FACILITY_MGMT_NETDEV);
	}
	return resched;
}

netdev_tx_t mgmt_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct otxmn_dev *mdev = (struct otxmn_dev *)netdev_priv(dev);
	struct  otxcn_hw_desc_ptr *ptr;
	uint32_t cons_idx, prod_idx, mask;
	struct otxcn_hw_descq  *descq;
	struct otxmn_sw_descq  *tq;
	/* hard code */
	int q_idx = 0;
	int count;
	int cmd_idx;
	local_dma_addr_t local_addr;
	int xmit_more;

	tq = &mdev->txqs[q_idx];
	descq = tq->hw_descq;
	/* should not get packets without IFF_UP */
	if (!mdev->admin_up)
		goto err;
	if (get_target_status(mdev) != OTXMN_TARGET_RUNNING)
		goto err;
	/* dont handle non linear skb, did not set NETIF_F_SG */
	if (skb_is_nonlinear(skb))
		goto err;
	if (skb_put_padto(skb, ETH_ZLEN)) {
		tq->errors++;
		return NETDEV_TX_OK;
	}
	if (skb->len > descq->buf_size)
		goto err;
	xmit_more = skb->xmit_more;
	//printk("send skb:%p skb->data:%p skb->len %d\n", skb, skb->data, skb->len);
	cons_idx = READ_ONCE(tq->local_cons_idx);
	prod_idx = READ_ONCE(descq->prod_idx);
	cmd_idx = READ_ONCE(tq->cmd_idx);
	mask = tq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	//printk("available count %d\n", count);
	if (!count) {
		/* send any pending skbs */
		if (tq->pending) {
			do_dma_async_dpi_vector(tq->cmd_list[cmd_idx].local_addr, 
						tq->cmd_list[cmd_idx].host_addr, 
						tq->cmd_list[cmd_idx].len, 
						tq->pending, 
						DMA_TO_HOST, 
						tq->cmd_list[cmd_idx].comp_iova); 	
			tq->cmd_list[cmd_idx].start_time = jiffies;
			tq->pending = 0;
			tq->cmd_idx = otxmn_circq_inc(tq->cmd_idx, mask);
			local_bh_disable();
			napi_schedule(&mdev->rxqs[q_idx].napi);
			local_bh_enable();
			wmb();
		}
		return NETDEV_TX_BUSY;
	}
	ptr = &descq->desc_arr[cons_idx];
	ptr->hdr.s_mgmt_net.total_len = skb->len;
	ptr->hdr.s_mgmt_net.ptr_len   = skb->len;
	ptr->hdr.s_mgmt_net.is_frag = 0;
	/* printk(KERN_DEBUG "is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
		ptr->hdr.s_mgmt_net.is_frag,
		ptr->hdr.s_mgmt_net.total_len,
		ptr->hdr.s_mgmt_net.ptr_type,
		ptr->hdr.s_mgmt_net.ptr_len,
		ptr->ptr);
	*/
	tq->cmd_list[cmd_idx].host_addr[tq->pending] = ptr->ptr;
	local_addr = dma_map_single(mdev->dma_dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(mdev->dma_dev, local_addr))
		goto err;
	tq->cmd_list[cmd_idx].local_addr[tq->pending] = local_addr;
	tq->cmd_list[cmd_idx].len[tq->pending] =  skb->len;
	tq->cmd_list[cmd_idx].dir = DMA_TO_HOST;
	WRITE_ONCE(*tq->cmd_list[cmd_idx].comp_data, 0xFF);
	tq->skb_list[cons_idx] = skb;
	tq->dma_list[cons_idx] = local_addr;
	tq->comp_list[cons_idx].data  = tq->cmd_list[cmd_idx].comp_data;
	tq->comp_list[cons_idx].cmd_idx  = cmd_idx;
	/* this will be overwritten when the dma is actually scheduled 
	 * but add it here so that the completion routine is 
	 * can check it before xmit_more stops
	 * this helps in catching xmit_more issues 
	 */
	tq->cmd_list[cmd_idx].start_time = jiffies;
	/* printk("tq cons_idx:%d cmd_idx:%d\n", cons_idx, cmd_idx);
	printk("dump cmd\n");
	printk("tq->cmd_list[cmd_idx].local_addr[tq->pending]: 0x%llx\n", tq->cmd_list[cmd_idx].local_addr[tq->pending]);
	printk("tq->cmd_list[cmd_idx].tq->cmd_list[cmd_idx].host_addr[tq->pending]: 0x%llx\n", tq->cmd_list[cmd_idx].host_addr[tq->pending]);
	printk("tq->cmd_list[cmd_idx].len[tq->pending]: %d\n", tq->cmd_list[cmd_idx].len[tq->pending]);
	printk("tq->cmd_list[cmd_idx].dir: %d\n", tq->cmd_list[cmd_idx].dir);
	printk("tq->cmd_list[cmd_idx].comp_data: %p\n", tq->cmd_list[cmd_idx].comp_data);
	printk("tq->cmd_list[cmd_idx].comp_iova: 0x%llx\n", tq->cmd_list[cmd_idx].comp_iova);
	*/
	tq->pending++;
	cons_idx = otxmn_circq_inc(cons_idx, mask);
	/* make sure list is updated before index update */
	wmb();
	WRITE_ONCE(tq->local_cons_idx, cons_idx);
	wmb();
	tq->pkts++;
	tq->bytes += skb->len;
	if (xmit_more && tq->pending < DPIX_MAX_PTR)
		return NETDEV_TX_OK;
	
	do_dma_async_dpi_vector(tq->cmd_list[cmd_idx].local_addr, 
				tq->cmd_list[cmd_idx].host_addr, 
				tq->cmd_list[cmd_idx].len, 
				tq->pending, 
				DMA_TO_HOST, 
				tq->cmd_list[cmd_idx].comp_iova); 	
	tq->cmd_list[cmd_idx].start_time = jiffies;
	tq->pending = 0;
	tq->cmd_idx = otxmn_circq_inc(tq->cmd_idx, mask);
	wmb();
	local_bh_disable();
	napi_schedule(&mdev->rxqs[q_idx].napi);
	local_bh_enable();
	return NETDEV_TX_OK;
err:
	tq->errors++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int mgmt_open(struct net_device *dev)
{
	struct otxmn_dev *mdev = netdev_priv(dev);

	mdev->admin_up = true;
	__module_get(THIS_MODULE);
	return 0;
}

static int mgmt_close(struct net_device *dev)
{
	struct otxmn_dev *mdev = netdev_priv(dev);

	mdev->admin_up = false;
	module_put(THIS_MODULE);
	return 0;
}

static void mgmt_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *s)
{
	struct otxmn_dev *mdev = netdev_priv(dev);
	int i;

	for (i = 0; i < mdev->num_rxq; i++) {
		s->rx_packets += mdev->rxqs[i].pkts;
		s->rx_bytes   += mdev->rxqs[i].bytes;
		s->rx_errors  += mdev->rxqs[i].errors;
	}
	for (i = 0; i < mdev->num_txq; i++) {
		s->tx_packets += mdev->txqs[i].pkts;
		s->tx_bytes   += mdev->txqs[i].bytes;
		s->tx_errors  += mdev->txqs[i].errors;
	}
}

static int mgmt_set_mac(struct net_device *netdev, void *p)
{
	struct otxmn_dev *mdev = netdev_priv(netdev);
        struct sockaddr *addr = (struct sockaddr *)p;

        if (!is_valid_ether_addr(addr->sa_data))
                return -EADDRNOTAVAIL;

        memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
        memcpy(mdev->hw_addr, addr->sa_data, ETH_ALEN);

        return 0;
}

static const struct net_device_ops mgmt_netdev_ops = {
	.ndo_open            = mgmt_open,
	.ndo_stop            = mgmt_close,
	.ndo_start_xmit      = mgmt_tx,
	.ndo_get_stats64     = mgmt_get_stats64,
	.ndo_set_mac_address = mgmt_set_mac,
};

static bool __handle_rxq(struct otxmn_dev *mdev, int q_idx, int budget)
{
	uint32_t cons_idx, prod_idx, mask;
	struct otxcn_hw_descq  *descq;
	struct otxcn_hw_desc_ptr *ptr;
	struct otxmn_sw_descq *rq;
	struct sk_buff *skb;
	int count, i, len, cmd_idx;
	bool resched = false;
	local_dma_addr_t comp_iova;

	rq = &mdev->rxqs[q_idx];
	descq = rq->hw_descq;
	if (!mdev->admin_up)
		return false;
	if (get_target_status(mdev) != OTXMN_TARGET_RUNNING)
		return false;
	cons_idx = READ_ONCE(rq->local_cons_idx);
	prod_idx = READ_ONCE(descq->prod_idx);
	mask = rq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	if (!count) {
		return false;
	}
	if (budget && count > budget) {
		count = budget;
		resched = true;
	}
	/*if (count)
		printk(KERN_DEBUG "new rx count %d\n", count);
	*/
	for (i = 0; i < count; ) {
		cmd_idx = rq->cmd_idx;
		ptr = &descq->desc_arr[cons_idx];
		len = ptr->hdr.s_mgmt_net.total_len;
		/*printk(KERN_DEBUG "rx cmd_idx %d idx:%d is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
			  cmd_idx,
			  i,
			  ptr->hdr.s_mgmt_net.is_frag,
			  ptr->hdr.s_mgmt_net.total_len,
			  ptr->hdr.s_mgmt_net.ptr_type,
			  ptr->hdr.s_mgmt_net.ptr_len,
			  ptr->ptr);
		*/
		if (unlikely(len < ETH_ZLEN || ptr->hdr.s_mgmt_net.is_frag)) {
			/* dont handle frags now */
			printk(KERN_ERR "mgmt_net:bad rx pkt\n");
			printk(KERN_ERR "mgmt_net:idx:%d is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
			       i,
			       ptr->hdr.s_mgmt_net.is_frag,
			       ptr->hdr.s_mgmt_net.total_len,
			       ptr->hdr.s_mgmt_net.ptr_type,
			       ptr->hdr.s_mgmt_net.ptr_len,
			       ptr->ptr);
			rq->errors++;
			return false;
		}
		skb = rq->skb_list[cons_idx];
		rq->cmd_list[cmd_idx].host_addr[i] =  ptr->ptr;
		rq->cmd_list[cmd_idx].local_addr[i] = rq->dma_list[cons_idx];
		rq->comp_list[cons_idx].data = rq->cmd_list[cmd_idx].comp_data;
		rq->comp_list[cons_idx].cmd_idx = cmd_idx;
		rq->cmd_list[cmd_idx].len[i] = len;
		skb->len = len;
		WRITE_ONCE(*rq->cmd_list[cmd_idx].comp_data, 0xFF);
		/*printk("rq cons_idx:%d cmd_idx:%d\n", cons_idx, cmd_idx);
		printk("dump cmd\n");
		printk("rq->cmd_list[cmd_idx].local_addr[i]: 0x%llx\n", rq->cmd_list[cmd_idx].local_addr[i]);
		printk("rq->cmd_list[cmd_idx].rq->cmd_list[cmd_idx].host_addr[i]: 0x%llx\n", rq->cmd_list[cmd_idx].host_addr[i]);
		printk("rq->cmd_list[cmd_idx].len[i]: %d\n", rq->cmd_list[cmd_idx].len[i]);
		printk("rq->cmd_list[cmd_idx].dir: %d\n", rq->cmd_list[cmd_idx].dir);
		printk("rq->cmd_list[cmd_idx].comp_data: %p\n", rq->cmd_list[cmd_idx].comp_data);
		printk("rq->cmd_list[cmd_idx].comp_iova: 0x%llx\n", rq->cmd_list[cmd_idx].comp_iova);
		*/
		if (i == (DPIX_MAX_PTR - 1) || i == (count - 1)) {
			comp_iova = rq->cmd_list[cmd_idx].comp_iova;
			wmb();
			do_dma_async_dpi_vector(rq->cmd_list[cmd_idx].local_addr, rq->cmd_list[cmd_idx].host_addr, rq->cmd_list[cmd_idx].len, i + 1, DMA_FROM_HOST, comp_iova);
			rq->cmd_list[cmd_idx].start_time = jiffies;
			rq->cmd_idx = otxmn_circq_inc(rq->cmd_idx, mask);
		}
		cons_idx = otxmn_circq_inc(cons_idx, mask);
		i = (i + 1) % DPIX_MAX_PTR;
		if (i == 0)
			count -= DPIX_MAX_PTR;
	}
	wmb();
	WRITE_ONCE(rq->local_cons_idx, cons_idx);
	wmb();	
	return resched;
}
	

static int rxq_refill(struct otxmn_dev *mdev, int q_idx, int count)
{
	uint32_t mask;
	struct otxmn_sw_descq *rq;
	struct sk_buff *skb;
	int i;
	int prod_idx;

	rq = &mdev->rxqs[q_idx];
	mask = rq->mask;
	if (!count) {
		return 0;
	}
	prod_idx = READ_ONCE(rq->refill_prod_idx);
	for (i = 0; i < count; i++) {
		skb = dev_alloc_skb(OTXMN_RECV_BUF_SIZE);
		if (!skb) {
			printk(KERN_ERR "mgmt net unable to alloc skb\n");
			break;
		}
		if (rq->skb_list[prod_idx] != NULL) {
			printk(KERN_ERR "mgmt net refill entry not null\n");
			break;
		}
		rq->skb_list[prod_idx] = skb;
		rq->dma_list[prod_idx] = dma_map_single(mdev->dma_dev, skb->data, 
							  OTXMN_RECV_BUF_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(mdev->dma_dev, rq->dma_list[prod_idx])) {
			dev_kfree_skb_any(skb);
			rq->skb_list[prod_idx] = NULL;
			printk(KERN_ERR "mgmt_net: dma mapping error\n");
			break;
		}
		prod_idx = otxmn_circq_inc(prod_idx, mask);
	}
	wmb();
	WRITE_ONCE(rq->refill_prod_idx, prod_idx);
	return count;
}

static bool __handle_rxq_completion(struct otxmn_dev *mdev, int q_idx, int budget, int from_wq)
{
	uint32_t cons_idx, prod_idx, mask;
	struct otxcn_hw_descq  *descq;
	struct otxmn_sw_descq *rq;
	struct sk_buff *skb;
	int count, i;
	bool resched = false;
	uint32_t refill_idx;
	uint64_t *data, comp_val;
	unsigned long start_time;
	int cmd_idx;
	

	rq = &mdev->rxqs[q_idx];
	descq = rq->hw_descq;
	if (!mdev->admin_up)
		return false;
	if (get_target_status(mdev) != OTXMN_TARGET_RUNNING)
		return false;
	cons_idx = READ_ONCE(descq->cons_idx);
	prod_idx = READ_ONCE(rq->local_cons_idx);
	mask = rq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	if (!count)
		return false;
	if (budget && count > budget) {
		count = budget;
		resched = true;
	}
	//printk("new may be rx complete count %d\n", count);
	//printk("comes here %s %d\n", __FILE__, __LINE__);
	refill_idx = cons_idx;
	for (i = 0; i < count; i++) {
		data =  rq->comp_list[cons_idx].data;
		cmd_idx =  rq->comp_list[cons_idx].cmd_idx;
		comp_val = READ_ONCE(*data);
		if (comp_val != 0xFF) {
			if (comp_val != 0) {
				printk("mgmt_net:rx dma err for idx %d cmd_idx %d comp_val:0x%llx\n", cons_idx, cmd_idx, comp_val); 
				return false;
			}
			skb =  rq->skb_list[cons_idx];
			dma_unmap_single(mdev->dma_dev, rq->dma_list[cons_idx], skb->len, DMA_FROM_DEVICE);
			skb_put(skb, skb->len);
			/* pkt_hex_dump(skb); */
			skb->protocol = eth_type_trans(skb, mdev->ndev);
			rq->pkts += 1;
			rq->bytes += skb->len;
			if (from_wq)
				netif_receive_skb(skb);
			else
				napi_gro_receive(&rq->napi, skb);
			rq->skb_list[cons_idx] = NULL;
			rq->dma_list[cons_idx] = 0;
			cons_idx = otxmn_circq_inc(cons_idx, mask);
		} else {
			start_time = rq->cmd_list[cmd_idx].start_time;
			if (time_after(jiffies, (start_time + msecs_to_jiffies(1000)))) {
				printk("mgmt_net:rx dma timeout for idx %d cmd_idx %d\n", cons_idx, cmd_idx);
				return false;
			}
			break;
		}
	}
	if (i) {
		rxq_refill(mdev, q_idx, i);
		wmb();
		WRITE_ONCE(descq->cons_idx, cons_idx);
		//printk("rx comp host writel params host_addr 0x%p cons_idx %d\n", rq->shadow_cons_idx_ioremap_addr, descq->cons_idx);
		writel(descq->cons_idx, rq->shadow_cons_idx_ioremap_addr);
		wmb();
		/* send tx comp intr event only if host has not disabled intrs
		 * if host enables intr after we have read this
		 * host has also periodic task to wake up rx/tx
		 */
		if (READ_ONCE(*HOST_INTR_REG(mdev)) == 1)
			mv_send_facility_event(MV_FACILITY_MGMT_NETDEV);
	}
	return resched;
}

static int mgmt_net_napi_poll(struct napi_struct *napi, int budget)
{
	struct otxmn_sw_descq *rq;
	struct otxmn_sw_descq *tq;
	struct otxmn_dev *mdev;
	int q_num;
	bool need_resched = false;

	rq = container_of(napi, struct otxmn_sw_descq, napi);
	q_num = rq->q_num;
	mdev = (struct otxmn_dev *)rq->priv;
	tq = &mdev->txqs[q_num];

	spin_lock_bh(&tq->lock);
	__mgmt_txq_completion(mdev, q_num, budget);
	spin_unlock_bh(&tq->lock);

	spin_lock_bh(&rq->lock);
	need_resched |= __handle_rxq(mdev, q_num, budget);
	need_resched |= __handle_rxq_completion(mdev, q_num, budget, 0);
	spin_unlock_bh(&rq->lock);

	if (need_resched)
	        return budget;
	napi_complete(napi);
	WRITE_ONCE(*TARGET_INTR_REG(mdev), 1);
	wmb();
	return 0;
}

static irqreturn_t mgmt_net_intr_hndlr(int irq, void *arg)
{
        struct otxmn_dev *mdev = arg;
	struct otxmn_sw_descq *rq;

	if (mdev->admin_up == false)
		goto skip;
        WRITE_ONCE(*TARGET_INTR_REG(mdev), 0);
	wmb();

	rq = &mdev->rxqs[0];
        /* hard coded queue num */
        napi_schedule_irqoff(&rq->napi);
skip:
        return IRQ_HANDLED;
}

static void mgmt_net_task(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct otxmn_dev *mdev =
		(struct otxmn_dev *)container_of(delayed_work,
						 struct otxmn_dev,
						 service_task);
	union otxmn_mbox_msg msg;
	struct otxmn_sw_descq *rq;
	struct otxmn_sw_descq *tq;
	int q_num = 0;
	int ret;

	ret = mbox_check_msg_rcvd(mdev, &msg);
	if (!ret) {
		switch(msg.s.hdr.opcode) {
		case OTXMN_MBOX_HOST_STATUS_CHANGE:
			/* printk(KERN_DEBUG "host status channge\n"); */
			handle_host_status(mdev);
			if (msg.s.hdr.req_ack)
				set_target_mbox_ack_reg(mdev, msg.s.hdr.id);
			break;
		default:
			break;
		}
	}
	tq = &mdev->txqs[q_num];
	rq = &mdev->rxqs[q_num];
	if (spin_trylock_bh(&tq->lock)) {
		__mgmt_txq_completion(mdev, q_num, 0);
		spin_unlock_bh(&tq->lock);
	}
	if (spin_trylock_bh(&rq->lock)) {
		__handle_rxq(mdev, q_num, 0);
		__handle_rxq_completion(mdev, q_num, 0, 1);
		spin_unlock_bh(&rq->lock);
	}
	queue_delayed_work(mdev->mgmt_wq, &mdev->service_task,
			   usecs_to_jiffies(OTXMN_SERVICE_TASK_US));
}

static int __init mgmt_init(void)
{
	int num_txq, num_rxq, max_rxq, max_txq, ret;
	mv_facility_conf_t  conf;
	struct net_device *ndev;
	struct otxmn_dev *mdev;

	/* printk(KERN_DEBUG "mgmt_net init\n"); */
	if (mv_get_facility_conf(MV_FACILITY_MGMT_NETDEV, &conf)) {
		printk(KERN_ERR "mgmt_net:get_facility_conf failed\n");
		return -ENODEV;
	}
	if (conf.memsize < OTXMN_BAR_SIZE) {
		printk(KERN_ERR "mgmt_net:too small bar\n");
		return -ENODEV;
	}
	if (conf.num_h2t_dbells < 1) {
		printk(KERN_ERR "need at least 1 h2t dbell\n");
		return -ENODEV;
	}
	printk(KERN_DEBUG "mgmt_net map addr %p\n", conf.memmap.target_addr);
	printk(KERN_DEBUG "mgmt_net map size %d\n", conf.memsize);
	/* irrespective of resources, we support single queue only */
	max_txq = num_txq = OTXMN_MAXQ;
	max_rxq = num_rxq = OTXMN_MAXQ;
	/* we support only single queue at this time */
	ndev = alloc_netdev(sizeof(struct otxmn_dev), MGMT_IFACE_NAME,
			    NET_NAME_UNKNOWN, ether_setup);
	if (!ndev) {
		printk(KERN_ERR "mgmt_net:alloc netdev failed\n");
		return -ENOMEM;
	}
	ndev->netdev_ops = &mgmt_netdev_ops;
	ndev->hw_features = NETIF_F_HIGHDMA;
	ndev->features = ndev->hw_features;
	ndev->mtu = OTXMN_MAX_MTU;
	netif_carrier_off(ndev);
	eth_hw_addr_random(ndev);
	mdev = netdev_priv(ndev);
	mdev->ndev = ndev;
	mdev->dma_dev = get_dpi_dma_dev();
	if (mdev->dma_dev == NULL) {
		printk(KERN_ERR "no dma device\n");
		return -ENODEV;	
		goto free_net;
	}
	mdev->admin_up = false;
	mdev->bar_map = conf.memmap.target_addr;
	mdev->bar_map_size = conf.memsize;
	mdev->max_txq = max_txq;
	mdev->num_txq = num_txq;
	mdev->max_rxq = max_rxq;
	mdev->num_rxq = num_rxq;
	mdev->mgmt_wq = alloc_ordered_workqueue("mgmt_net", 0);
	if (!mdev->mgmt_wq) {
		printk(KERN_ERR "mgmt_net:alloc wq failed\n");
		ret = -ENOMEM;
		goto free_net;
	}
	mdev->send_mbox_id = 0;
	mdev->recv_mbox_id = 0;
	mutex_init(&mdev->mbox_lock);
	ret = register_netdev(ndev);
	if (ret) {
		printk(KERN_ERR "mgmt_net:register_netdev failed\n");
		goto destroy_mutex;
	}
	change_target_status(mdev, OTXMN_TARGET_READY, false);
	INIT_DELAYED_WORK(&mdev->service_task, mgmt_net_task);
	queue_delayed_work(mdev->mgmt_wq, &mdev->service_task,
			   usecs_to_jiffies(OTXMN_SERVICE_TASK_US));
	gmdev = mdev;
	return 0;
destroy_mutex:
	mutex_destroy(&mdev->mbox_lock);
	destroy_workqueue(mdev->mgmt_wq);
free_net:
	free_netdev(ndev);
	return ret;
}

static void __exit mgmt_exit(void)
{
	struct otxmn_dev *mdev;

	if (!gmdev)
		return;
	mdev = gmdev;
	netif_carrier_off(mdev->ndev);
	cancel_delayed_work_sync(&mdev->service_task);
	change_target_status(mdev, OTXMN_TARGET_GOING_DOWN, true);
	if (get_target_status(mdev) == OTXMN_TARGET_RUNNING) {
		napi_synchronize(&mdev->rxqs[0].napi);
      		napi_disable(&mdev->rxqs[0].napi);
		printk("freeing dbell irq %d\n", __LINE__);
		mv_facility_free_dbell_irq(MV_FACILITY_MGMT_NETDEV, 0, (void *)mdev);
		reset_queues(mdev);
	}
	mutex_destroy(&mdev->mbox_lock);
	destroy_workqueue(mdev->mgmt_wq);
	unregister_netdev(mdev->ndev);
	free_netdev(mdev->ndev);
	gmdev = NULL;
	printk(KERN_DEBUG "mgmt_net:mgmt_net exit\n");
}

module_init(mgmt_init);
module_exit(mgmt_exit);
MODULE_AUTHOR("Marvell Inc.");
MODULE_DESCRIPTION("OTX Management Net driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(OTXMN_VERSION);
