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

#define TX_DESCQ_OFFSET(mdev)	  \
	((uint8_t *)(mdev->bar_map + OTXMN_TX_DESCQ_OFFSET))
#define RX_DESCQ_OFFSET(mdev)     \
	((uint8_t *)(mdev->bar_map + OTXMN_RX_DESCQ_OFFSET))

#define TARGET_STATUS_REG(mdev)      \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_STATUS_REG))
#define TARGET_MBOX_MSG_REG(mdev, i)  \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_MBOX_OFFSET + (i * 8)))
#define TARGET_MBOX_ACK_REG(mdev)     \
	((uint64_t *)(mdev->bar_map + OTXMN_TARGET_MBOX_ACK_REG))

#define HOST_STATUS_REG(mdev)         \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_STATUS_REG))
#define HOST_MBOX_ACK_REG(mdev)       \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_MBOX_ACK_REG))
#define HOST_MBOX_MSG_REG(mdev, i)    \
	((uint64_t *)(mdev->bar_map + OTXMN_HOST_MBOX_OFFSET + (i * 8)))

static struct otxmn_dev *gmdev;
static int  mgmt_tx_bh_cleanup(struct otxmn_dev *mdev, int q_idx);

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

	printk(KERN_DEBUG "prod_idx %u\n", descq->prod_idx);
	printk(KERN_DEBUG "cons_idx %u\n", descq->cons_idx);
	printk(KERN_DEBUG "num_entries %u\n", descq->num_entries);
	printk(KERN_DEBUG "shadow_cons_idx_addr 0x%llx\n",
	       descq->shadow_cons_idx_addr);
	for (i = 0; i < descq->num_entries; i++) {
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

	descq = (struct otxcn_hw_descq *)(TX_DESCQ_OFFSET(mdev));
	for (i = 0; i < hw_txq; i++) {
		descq_tot_size = sizeof(struct otxcn_hw_descq) +
			(descq->num_entries * sizeof(struct otxcn_hw_desc_ptr));
		descq = (struct otxcn_hw_descq *)(TX_DESCQ_OFFSET(mdev) +
						  descq_tot_size);
	}
	/* dump_hw_descq(descq); */
	if ((descq->num_entries == 0) ||
	    (descq->num_entries & (descq->num_entries - 1)))
		return -ENOENT;
	if (descq->cons_idx != 0)
		return -ENOENT;
	if (descq->shadow_cons_idx_addr == 0)
		return -ENOENT;
	mdev->rxqs[rxq].mask = descq->num_entries - 1;
	mdev->rxqs[rxq].num_entries = descq->num_entries;
	mdev->rxqs[rxq].hw_descq = descq;
	mdev->rxqs[rxq].hw_dma_qidx = hw_txq;
	mdev->rxqs[rxq].shadow_cons_idx = descq->shadow_cons_idx_addr;
	printk(KERN_DEBUG "mgmt_net: rxqs[%d]  maps to host txq %d\n", rxq,
	       hw_txq);
	return 0;
}

static int setup_txq(struct otxmn_dev *mdev, int hw_rxq, int txq)
{
	struct otxcn_hw_descq *descq;
	int i, descq_tot_size;

	descq = (struct otxcn_hw_descq *)(RX_DESCQ_OFFSET(mdev));
	for (i = 0; i < hw_rxq; i++) {
		descq_tot_size = sizeof(struct otxcn_hw_descq) +
			(descq->num_entries * sizeof(struct otxcn_hw_desc_ptr));
		descq = (struct otxcn_hw_descq *)(TX_DESCQ_OFFSET(mdev) +
						  descq_tot_size);
	}
	/* printk(KERN_DEBUG " rx dmaq\n"); */
	/* dump_hw_descq(descq); */
	if ((descq->num_entries == 0) ||
	    (descq->num_entries & (descq->num_entries - 1)))
		return -ENOENT;
	if (descq->cons_idx != 0)
		return -ENOENT;
	if (descq->shadow_cons_idx_addr == 0)
		return -ENOENT;
	if (descq->buf_size == 0)
		return -ENOENT;
	mdev->txqs[txq].hw_descq = descq;
	mdev->txqs[txq].local_cons_idx = descq->cons_idx;
	mdev->txqs[txq].local_prod_idx = 0;
	mdev->txqs[txq].mask = descq->num_entries - 1;
	mdev->txqs[txq].num_entries = descq->num_entries;
	mdev->txqs[txq].shadow_cons_idx = descq->shadow_cons_idx_addr;
	mdev->txqs[txq].hw_dma_qidx = hw_rxq;
	mdev->txqs[txq].skb_list = kzalloc(sizeof(struct sk_buff *) *
					   descq->num_entries, GFP_KERNEL);
	if (!mdev->txqs[txq].skb_list)
		return -ENOENT;
	for (i = 0; i < descq->num_entries; i++)
		mdev->txqs[txq].skb_list[i] = NULL;
	printk(KERN_DEBUG " txq[%d] maps to host rxq %d\n", txq, hw_rxq);
	return 0;
}

static void cleanup_rxq(struct otxmn_dev *mdev, int rxq)
{
	memset(&mdev->rxqs[rxq], 0, sizeof(struct otxmn_sw_descq));
}

static void cleanup_txq(struct otxmn_dev *mdev, int txq)
{
	mgmt_tx_bh_cleanup(mdev, txq);
	kfree(mdev->txqs[txq].skb_list);
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
			if (setup_queues(mdev))
				change_target_status(mdev, OTXMN_TARGET_FATAL,
						     false);
			change_target_status(mdev, OTXMN_TARGET_RUNNING, false);
			netif_carrier_on(mdev->ndev);
		}
		break;
	case OTXMN_TARGET_RUNNING:
		host_status = get_host_status(mdev);
		if ((host_status != OTXMN_HOST_READY) &&
		    (host_status != OTXMN_HOST_RUNNING)) {
			/* printk(KERN_DEBUG "host status not ready\n"); */
			netif_carrier_off(mdev->ndev);
			reset_queues(mdev);
			change_target_status(mdev, OTXMN_TARGET_READY, false);
		}
		break;
	default:
		netdev_err(mdev->ndev, "unhandled state transition cur_state:%llu\n", cur_status);
		break;
	}
	return ret;
}

static int  mgmt_tx_bh_cleanup(struct otxmn_dev *mdev, int q_idx)
{
	struct otxmn_sw_descq  *tq;
	int count;
	struct sk_buff *skb;
	int i;

	tq = &mdev->txqs[q_idx];
	count = tq->num_entries;

	for ( i = 0; i < count; i++) {
		skb = tq->skb_list[i];
		if(skb)
			dev_kfree_skb_any(skb);
		tq->skb_list[i] = NULL;
	}
	tq->local_cons_idx = tq->local_prod_idx = 0;
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

static int  mgmt_tx_bh(struct otxmn_dev *mdev, int q_idx)
{
	struct otxcn_hw_desc_ptr *ptr;
	struct otxmn_sw_descq  *tq;
	struct otxcn_hw_descq  *descq;
	host_dma_addr_t host_addr;
	int count;
	uint32_t cons_idx, prod_idx, hw_cons_idx, mask;
	struct sk_buff *skb;
	int i;

	tq = &mdev->txqs[q_idx];
	descq = tq->hw_descq;
	if (!mdev->admin_up)
		return 0;
	if (get_target_status(mdev) != OTXMN_TARGET_RUNNING)
		return 0;

	cons_idx = READ_ONCE(tq->local_cons_idx);
	prod_idx = READ_ONCE(tq->local_prod_idx);
	mask = tq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	if (!count)
		return 0;
	/* printk(KERN_DEBUG "packets to be xmited %d\n", count); */
	for ( i = 0; i < count; i++) {
		ptr = &descq->desc_arr[cons_idx];
		skb = tq->skb_list[cons_idx];
		ptr->hdr.s_mgmt_net.total_len = skb->len;
		ptr->hdr.s_mgmt_net.ptr_len   = skb->len;
		ptr->hdr.s_mgmt_net.is_frag = 0;
		/*printk(KERN_DEBUG "is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
			ptr->hdr.s_mgmt_net.is_frag,
			ptr->hdr.s_mgmt_net.total_len,
			ptr->hdr.s_mgmt_net.ptr_type,
			ptr->hdr.s_mgmt_net.ptr_len,
			ptr->ptr);
		*/
		host_addr = ptr->ptr;
		/* pkt_hex_dump(skb); */
		do_dma_sync(0, host_addr, skb->data, skb->len, DMA_TO_HOST);
		tq->skb_list[cons_idx] = NULL;
		tq->pkts++;
		tq->bytes += skb->len;
		dev_kfree_skb_any(skb);
		cons_idx = otxmn_circq_inc(cons_idx, mask);
	}
	/* dma should complete before index update */
	wmb();
	hw_cons_idx = READ_ONCE(descq->cons_idx);
	hw_cons_idx = otxmn_circq_add(hw_cons_idx, count, mask);
	WRITE_ONCE(tq->local_cons_idx, cons_idx);
	WRITE_ONCE(descq->cons_idx, hw_cons_idx);
	host_writel(descq->shadow_cons_idx_addr, descq->cons_idx);
	wmb();
	
	return count;
}

netdev_tx_t mgmt_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct otxmn_dev *mdev = (struct otxmn_dev *)netdev_priv(dev);
	uint32_t cons_idx, prod_idx, mask;
	struct otxcn_hw_descq  *descq;
	struct otxmn_sw_descq  *tq;
	/* hard code */
	int q_idx = 0;
	int count;

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
	cons_idx = READ_ONCE(tq->local_prod_idx);
	prod_idx =  READ_ONCE(descq->prod_idx);
	mask = tq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	if (!count)
		return NETDEV_TX_BUSY;
	tq->skb_list[cons_idx] = skb;
	cons_idx = otxmn_circq_inc(cons_idx, mask);
	/* make sure list is updated before index update */
	wmb();
	WRITE_ONCE(tq->local_prod_idx, cons_idx);
	wmb();
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

static const struct net_device_ops mgmt_netdev_ops = {
	.ndo_open            = mgmt_open,
	.ndo_stop            = mgmt_close,
	.ndo_start_xmit      = mgmt_tx,
	.ndo_get_stats64     = mgmt_get_stats64,
};

static int handle_rxq(struct otxmn_dev *mdev, int q_idx)
{
	uint32_t cons_idx, prod_idx, mask;
	struct otxcn_hw_descq  *descq;
	struct otxcn_hw_desc_ptr *ptr;
	struct otxmn_sw_descq *rq;
	host_dma_addr_t host_addr;
	struct sk_buff *skb;
	int count, i, len;

	rq = &mdev->rxqs[q_idx];
	descq = rq->hw_descq;
	if (!mdev->admin_up)
		return 0;
	if (get_target_status(mdev) != OTXMN_TARGET_RUNNING)
		return 0;
	cons_idx = READ_ONCE(descq->cons_idx);
	prod_idx = READ_ONCE(descq->prod_idx);
	mask = rq->mask;
	count = otxmn_circq_depth(prod_idx, cons_idx, mask);
	if (!count)
		return 0;
	for (i = 0; i < count; i++) {
		//printk(KERN_DEBUG "new rx count %d\n", count);
		ptr = &descq->desc_arr[cons_idx];
		len = ptr->hdr.s_mgmt_net.total_len;
		/* printk(KERN_DEBUG "idx:%d is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
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
			goto skip;
		}
		skb = alloc_skb(len, GFP_KERNEL);
		if (unlikely(!skb)) {
			printk(KERN_ERR "mgmt_net:unable to alloc pkt\n");
			rq->errors++;
			goto skip;
		}
		skb->dev = mdev->ndev;
		host_addr =  ptr->ptr;
		do_dma_sync(0, host_addr, skb->data, len, DMA_FROM_HOST);
		skb_put(skb, len);
		/* pkt_hex_dump(skb); */
		skb->protocol = eth_type_trans(skb, mdev->ndev);
		rq->pkts += 1;
		rq->bytes += len;
		/* we are in process context */
		netif_rx_ni(skb);
skip:
		cons_idx = otxmn_circq_inc(cons_idx, mask);
	}
	wmb();
	WRITE_ONCE(descq->cons_idx, cons_idx);
	host_writel(descq->shadow_cons_idx_addr, descq->cons_idx);
	wmb();
	return count;
}

static void mdev_rxtx(struct otxmn_dev *mdev)
{
	mgmt_tx_bh(mdev, 0);
	handle_rxq(mdev, 0);
}

static void mgmt_net_task(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct otxmn_dev *mdev =
		(struct otxmn_dev *)container_of(delayed_work,
						 struct otxmn_dev,
						 service_task);
	union otxmn_mbox_msg msg;
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
	mdev_rxtx(mdev);
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
	printk(KERN_DEBUG "mgmt_net map addr %p\n", conf.memmap.target_addr);
	printk(KERN_DEBUG "mgmt_net map size %d\n", conf.memsize);
	/* irrespective of resources, we support single queue only */
	max_txq = num_txq = OTXMN_MAXQ;
	max_rxq = num_rxq = OTXMN_MAXQ;
	/* we support only single queue at this time */
	ndev = alloc_etherdev(sizeof(struct otxmn_dev));
	if (!ndev) {
		printk(KERN_ERR "mgmt_net:alloc etherdev failed\n");
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
	mdev->admin_up = false;
	mdev->bar_map = conf.memmap.target_addr;
	mdev->bar_map_size = conf.memsize;
	mdev->max_txq = max_txq;
	mdev->max_rxq = max_rxq;
	mdev->num_txq = num_txq;
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
	change_target_status(mdev, OTXMN_TARGET_GOING_DOWN, true);
	cancel_delayed_work_sync(&mdev->service_task);
	mgmt_tx_bh_cleanup(mdev, 0);
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
