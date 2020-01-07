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
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/circ_buf.h>

#include <facility.h>

#include <desc_queue.h>
#include <bar_space_mgmt_net.h>
#include <mmio_api.h>
#include <host_ethdev.h>

#define TX_DESCQ_OFFSET(mdev)     (mdev->bar_map + OTXMN_TX_DESCQ_OFFSET)
#define RX_DESCQ_OFFSET(mdev)     (mdev->bar_map + OTXMN_RX_DESCQ_OFFSET)

#define TARGET_STATUS_REG(mdev)        (mdev->bar_map + OTXMN_TARGET_STATUS_REG)
#define TARGET_MBOX_MSG_REG(mdev, i)  \
	(mdev->bar_map + OTXMN_TARGET_MBOX_OFFSET + (i * 8))
#define TARGET_MBOX_ACK_REG(mdev)    \
	(mdev->bar_map + OTXMN_TARGET_MBOX_ACK_REG)

#define HOST_STATUS_REG(mdev)      (mdev->bar_map + OTXMN_HOST_STATUS_REG)
#define HOST_MBOX_ACK_REG(mdev)    (mdev->bar_map + OTXMN_HOST_MBOX_ACK_REG)
#define HOST_MBOX_MSG_REG(mdev, i)    \
	(mdev->bar_map + OTXMN_HOST_MBOX_OFFSET + (i * 8))

static struct otxmn_dev *gmdev;

static uint64_t get_host_status(struct otxmn_dev *mdev)
{
	return readq(HOST_STATUS_REG(mdev));
}

static uint64_t get_target_status(struct otxmn_dev *mdev)
{
	return readq(TARGET_STATUS_REG(mdev));
}

static uint64_t get_target_mbox_ack(struct otxmn_dev *mdev)
{
	return readq(TARGET_MBOX_ACK_REG(mdev));
}

static void set_host_mbox_ack_reg(struct otxmn_dev *mdev, uint32_t id)
{
	writeq(id, HOST_MBOX_ACK_REG(mdev));
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
	for (i = 1; i <= msg->s.hdr.sizew; i++)
		writeq(msg->words[i], HOST_MBOX_MSG_REG(mdev, i));
	/* write header at the end */
	printk(KERN_DEBUG "send mbox msg id:%d opcode:%d sizew: %d\n",
	       msg->s.hdr.id, msg->s.hdr.opcode, msg->s.hdr.sizew);
	writeq(msg->words[0], HOST_MBOX_MSG_REG(mdev, 0));
	/* more than 1 word mbox messages need explicit ack */
	if (msg->s.hdr.req_ack || msg->s.hdr.sizew) {
		printk(KERN_DEBUG "mbox send wait for ack\n");
		expire = jiffies + timeout;
		while (get_target_mbox_ack(mdev) != id) {
			schedule_timeout_interruptible(period);
			if (signal_pending(current))
				break;
			if (time_after(jiffies, expire))
				break;
		}
	}
	mutex_unlock(&mdev->mbox_lock);
}

static int mbox_check_msg_rcvd(struct otxmn_dev *mdev,
			       union otxmn_mbox_msg *msg)
{
	int i, ret;

	mutex_lock(&mdev->mbox_lock);
	msg->words[0] = readq(TARGET_MBOX_MSG_REG(mdev, 0));
	if (mdev->recv_mbox_id != msg->s.hdr.id) {
		/* new msg */
		printk(KERN_DEBUG "new mbox msg id:%d opcode:%d sizew: %d\n",
		       msg->s.hdr.id, msg->s.hdr.opcode, msg->s.hdr.sizew);
		mdev->recv_mbox_id = msg->s.hdr.id;
		for (i = 1; i <= msg->s.hdr.sizew; i++)
			msg->words[i] = readq(TARGET_MBOX_MSG_REG(mdev, i));
		ret = 0;
	} else {
		ret = -ENOENT;
	}
	mutex_unlock(&mdev->mbox_lock);
	return ret;
}

static void change_host_status(struct otxmn_dev *mdev, uint64_t status,
			       bool ack_wait)
{
	union otxmn_mbox_msg msg;

	printk(KERN_DEBUG "change host status from %lu to %llu \n",
	       readq(HOST_STATUS_REG(mdev)), status);
	writeq(status, HOST_STATUS_REG(mdev));
	memset(&msg, 0, sizeof(union otxmn_mbox_msg));
	msg.s.hdr.opcode = OTXMN_MBOX_HOST_STATUS_CHANGE;
	if (ack_wait)
		msg.s.hdr.req_ack = 1;
	mbox_send_msg(mdev, &msg);
}

netdev_tx_t mgmt_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct otxmn_dev *mdev = (struct otxmn_dev *)netdev_priv(dev);
	struct otxcn_hw_desc_ptr ptr;
	struct otxmn_sw_descq  *tq;
	uint32_t cur_cons_idx;
	uint8_t *hw_desc_ptr;
	dma_addr_t dma;
	/* hard code */
	int idx = 0;

	tq = &mdev->txq[idx];
	/* should not get packets without IFF_UP */
	if (!mdev->admin_up)
		goto err;
	if (get_host_status(mdev) != OTXMN_HOST_RUNNING)
		goto err;
	/* dont handle non linear skb, did not set NETIF_F_SG */
	if (skb_is_nonlinear(skb))
		goto err;
	if (skb_put_padto(skb, ETH_ZLEN)) {
		tq->errors++;
		return NETDEV_TX_OK;
	}
	cur_cons_idx = READ_ONCE(*tq->cons_idx_shadow);
	if (!otxmn_circq_space(tq->local_prod_idx, cur_cons_idx, tq->mask)) {
		tq->errors++;
		return NETDEV_TX_BUSY;
	}
	memset(&ptr, 0, sizeof(struct otxcn_hw_desc_ptr));
	ptr.hdr.s_mgmt_net.ptr_type = OTXCN_DESC_PTR_DIRECT;
	ptr.hdr.s_mgmt_net.ptr_len = skb->len;
	ptr.hdr.s_mgmt_net.total_len = skb->len;
	dma = dma_map_single(mdev->dev, skb->data, skb->len,
			     DMA_TO_DEVICE);
	if (dma_mapping_error(mdev->dev, dma)) {
		printk(KERN_DEBUG "dma mapping err in xmit\n");
		goto err;
	}
	ptr.ptr = dma;
	hw_desc_ptr = tq->hw_descq +
		OTXCN_DESC_ARR_ENTRY_OFFSET(tq->local_prod_idx);
	/*printk(KERN_DEBUG "is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
		 ptr.hdr.s_mgmt_net.is_frag,
		 ptr.hdr.s_mgmt_net.total_len,
		 ptr.hdr.s_mgmt_net.ptr_type,
		 ptr.hdr.s_mgmt_net.ptr_len,
		 ptr.ptr);
	*/
	mmio_memwrite(hw_desc_ptr, &ptr, sizeof(struct otxcn_hw_desc_ptr));
	tq->skb_list[tq->local_prod_idx] = skb;
	tq->dma_list[tq->local_prod_idx] = dma;
	/* lists need to be updated before going forward */
	wmb();
	tq->local_prod_idx = otxmn_circq_inc(tq->local_prod_idx, tq->mask);
	writel(tq->local_prod_idx, tq->hw_prod_idx);
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
		s->rx_packets += mdev->rxq[i].pkts;
		s->rx_bytes   += mdev->rxq[i].bytes;
		s->rx_errors  += mdev->rxq[i].errors;
	}
	for (i = 0; i < mdev->num_txq; i++) {
		s->tx_packets += mdev->txq[i].pkts;
		s->tx_bytes   += mdev->txq[i].bytes;
		s->tx_errors  += mdev->txq[i].errors;
	}
}

static const struct net_device_ops mgmt_netdev_ops = {
	.ndo_open            = mgmt_open,
	.ndo_stop            = mgmt_close,
	.ndo_start_xmit      = mgmt_tx,
	.ndo_get_stats64     = mgmt_get_stats64,
};

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

static int mdev_clean_tx_ring(struct otxmn_dev *mdev, int q_idx)
{
	struct otxmn_sw_descq *tq = &mdev->txq[q_idx];
	uint32_t cons_idx, prod_idx;
	struct sk_buff *skb;
	int i, count, start;
	int descq_tot_size;
	dma_addr_t  dma;

	if (tq->status == OTXMN_DESCQ_CLEAN)
		return 0;
	cons_idx = tq->local_cons_idx;
	prod_idx = tq->local_prod_idx;
	count = otxmn_circq_depth(prod_idx, cons_idx, tq->mask);
	descq_tot_size = sizeof(struct otxcn_hw_descq) +
		(tq->element_count * sizeof(struct otxcn_hw_desc_ptr));
	start = cons_idx;
	for (i = 0; i < count; i++) {
		skb = tq->skb_list[start];
		dma = tq->dma_list[start];
		dma_unmap_single(mdev->dev, dma, skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		tq->skb_list[start] = NULL;
		tq->dma_list[start] = 0;
		start = otxmn_circq_inc(start, tq->mask);
	}
	tq->local_cons_idx = tq->local_prod_idx = 0;
	*tq->cons_idx_shadow = 0;
	tq->status = OTXMN_DESCQ_CLEAN;
	kfree(tq->skb_list);
	kfree(tq->dma_list);
	/* tq status need to be updated before memset */
	wmb();
	mmio_memset(tq->hw_descq, 0, descq_tot_size);
	return count;
}

static void mdev_clean_tx_rings(struct otxmn_dev *mdev)
{
	int i;

	for (i = 0; i < mdev->num_txq; i++)
		mdev_clean_tx_ring(mdev, i);
}

/* tx rings are memset to 0. they dont have anything unless
 * there is something to send
 */
static int mdev_setup_tx_ring(struct otxmn_dev *mdev, int q_idx)
{
	int element_count = mdev->element_count;
	struct otxcn_hw_descq *descq;
	struct otxmn_sw_descq *tq;
	int descq_tot_size;

	descq_tot_size = sizeof(struct otxcn_hw_descq) + (element_count *
			      sizeof(struct otxcn_hw_desc_ptr));
	descq = kzalloc(descq_tot_size, GFP_KERNEL);
	if (!descq) {
		printk(KERN_DEBUG "kmalloc failed\n");
		return -ENOMEM;
	}
	tq = &mdev->txq[q_idx];
	tq->local_prod_idx = 0;
	tq->local_cons_idx = 0;
	tq->element_count = element_count;
	tq->mask = element_count - 1;
	descq->num_entries = element_count;
	tq->cons_idx_shadow = mdev->tq_cons_shdw_vaddr + q_idx;
	descq->shadow_cons_idx_addr = mdev->tq_cons_shdw_dma +
		(q_idx * sizeof(*mdev->tq_cons_shdw_vaddr));
	tq->hw_descq = TX_DESCQ_OFFSET(mdev) + (q_idx * descq_tot_size);
	tq->hw_prod_idx = (uint32_t *)(tq->hw_descq +
				       offsetof(struct otxcn_hw_descq,
						prod_idx));
	tq->skb_list = kzalloc(sizeof(struct sk_buff *) * element_count,
			       GFP_KERNEL);
	if (!tq->skb_list) {
		kfree(descq);
		printk(KERN_DEBUG "kmalloc failed\n");
		return -ENOMEM;
	}
	tq->dma_list = kzalloc(sizeof(dma_addr_t) * element_count, GFP_KERNEL);
	if (!tq->dma_list) {
		kfree(descq);
		kfree(tq->skb_list);
		printk(KERN_DEBUG "kmalloc failed\n");
		return -ENOMEM;
	}
	wmb();
	printk(KERN_DEBUG "tx hw descq\n");
	/* dump_hw_descq(descq); */
	tq->status = OTXMN_DESCQ_READY;
	/* tq status needs to be updated before memwrite */
	wmb();
	mmio_memwrite(tq->hw_descq, descq, descq_tot_size);
	kfree(descq);
	return 0;
}

static int mdev_setup_tx_rings(struct otxmn_dev *mdev)
{
	int i, j, ret;

	for  (i = 0; i < mdev->num_txq; i++) {
		ret = mdev_setup_tx_ring(mdev, i);
		if (ret)
			goto error;
	}
	return 0;
error:
	for ( j = 0; j < i; j++)
		mdev_clean_tx_ring(mdev, j);
	return ret;
}

static void mdev_clean_rx_ring(struct otxmn_dev *mdev, int q_idx)
{
	struct otxmn_sw_descq *rq = &mdev->rxq[q_idx];
	int cons_idx, prod_idx;
	struct sk_buff *skb;
	int descq_tot_size;
	int start, count;
	int i;

	if (rq->status == OTXMN_DESCQ_CLEAN)
		return;
	cons_idx = rq->local_cons_idx;
	prod_idx = rq->local_prod_idx;
	count = otxmn_circq_depth(prod_idx, cons_idx, rq->mask);
	descq_tot_size = sizeof(struct otxcn_hw_descq) +
		(rq->element_count * sizeof(struct otxcn_hw_desc_ptr));
	start = cons_idx;
	for (i = 0; i < count; i++) {
		skb = rq->skb_list[start];
		if (skb) {
			dma_unmap_single(mdev->dev, rq->dma_list[start],
					 OTXMN_RX_BUF_SIZE,
					 DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			rq->skb_list[start] = NULL;
			rq->dma_list[start] = 0;
			start = otxmn_circq_inc(start, rq->mask);
		}
	}
	rq->local_prod_idx = rq->local_cons_idx = 0;
	*rq->cons_idx_shadow = 0;
	kfree(rq->skb_list);
	kfree(rq->dma_list);
	rq->status = OTXMN_DESCQ_CLEAN;
	/* rq needs to be updated before memset */
	wmb();
	mmio_memset(rq->hw_descq, 0, descq_tot_size);
}

static void mdev_clean_rx_rings(struct otxmn_dev *mdev)
{
	int i;

	for (i = 0; i < mdev->num_rxq; i++)
		mdev_clean_rx_ring(mdev, i);
}

static int mdev_setup_rx_ring(struct otxmn_dev *mdev, int q_idx)
{
	int element_count = mdev->element_count;
	struct otxcn_hw_desc_ptr *ptr;
	struct otxcn_hw_descq *descq;
	struct otxmn_sw_descq *rq;
	int i, j, ret, count;
	struct sk_buff *skb;
	int descq_tot_size;
	dma_addr_t  dma;


	printk("sizeof hw_desc_ptr %lu sizeof hw_descq %lu\n",
	       sizeof(struct otxcn_hw_desc_ptr), sizeof(struct otxcn_hw_descq));
	rq = &mdev->rxq[q_idx];
	descq_tot_size = sizeof(struct otxcn_hw_descq) + (element_count *
			      sizeof(struct otxcn_hw_desc_ptr));
	descq = kzalloc(descq_tot_size, GFP_KERNEL);
	if (!descq) {
		printk(KERN_DEBUG "kmalloc failed\n");
		return -ENOMEM;
	}
	rq->local_prod_idx = 0;
	rq->local_cons_idx = 0;
	rq->element_count = element_count;
	rq->mask = element_count - 1;
	rq->skb_list = kzalloc(sizeof(struct sk_buff *) * element_count,
			       GFP_KERNEL);
	if (!rq->skb_list) {
		kfree(descq);
		printk(KERN_DEBUG "kmalloc failed\n");
		return -ENOMEM;
	}
	rq->dma_list = kzalloc(sizeof(dma_addr_t) * element_count, GFP_KERNEL);
	if (!rq->dma_list) {
		kfree(descq);
		kfree(rq->skb_list);
		printk(KERN_DEBUG "kmalloc failed\n");
		return -ENOMEM;
	}
	descq->num_entries = element_count;
	descq->buf_size =  OTXMN_RX_BUF_SIZE;
	rq->cons_idx_shadow = mdev->rq_cons_shdw_vaddr + q_idx;
	descq->shadow_cons_idx_addr = mdev->rq_cons_shdw_dma +
					(q_idx * sizeof(*rq->cons_idx_shadow));
	count = otxmn_circq_space(rq->local_prod_idx, rq->local_cons_idx,
				  rq->mask);
	printk("queue space %d\n", count);
	for (i = 0; i < count; i++) {
		skb = alloc_skb(OTXMN_RX_BUF_SIZE, GFP_KERNEL);
		if (!skb) {
			printk(KERN_DEBUG "skb alloc failed\n");
			ret = -ENOMEM;
			goto error;
		}
		skb->dev = mdev->ndev;
		dma = dma_map_single(mdev->dev, skb->data, OTXMN_RX_BUF_SIZE,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(mdev->dev, dma)) {
			printk(KERN_DEBUG "dma mapping failed\n");
			dev_kfree_skb_any(skb);
			ret = -ENOENT;
			goto error;
		}
		ptr = &descq->desc_arr[rq->local_prod_idx];
		memset(ptr, 0, sizeof(struct otxcn_hw_desc_ptr));
		ptr->hdr.s_mgmt_net.ptr_type = OTXCN_DESC_PTR_DIRECT;
		ptr->ptr = dma;
		rq->skb_list[rq->local_prod_idx] = skb;
		rq->dma_list[rq->local_prod_idx] = dma;
		rq->local_prod_idx = otxmn_circq_inc(rq->local_prod_idx,
						     rq->mask);
		descq->prod_idx = otxmn_circq_inc(descq->prod_idx, rq->mask);
	}
	rq->hw_descq = RX_DESCQ_OFFSET(mdev) + (q_idx * descq_tot_size);
	rq->hw_prod_idx = (uint32_t *)(rq->hw_descq +
				       offsetof(struct otxcn_hw_descq,
						prod_idx));
	printk(KERN_DEBUG "rx hw descq\n");
	/* dump_hw_descq(descq); */
	printk(KERN_DEBUG "dma list\n");
	for (i = 0; i < count; i++)
		printk(KERN_DEBUG "skb->data %p  dma 0x%llx\n",
		       rq->skb_list[i]->data, rq->dma_list[i]);
	rq->status = OTXMN_DESCQ_READY;
	/* rq needs to be updated before memwrite */
	wmb();
	mmio_memwrite(rq->hw_descq, descq, descq_tot_size);
	kfree(descq);
	return 0;
error:
	for (j = 0; j < i; j++) {
		skb = rq->skb_list[j];
		dma = rq->dma_list[j];
		if (skb) {
			dev_kfree_skb_any(skb);
			dma_unmap_single(mdev->dev, dma, OTXMN_RX_BUF_SIZE,
					DMA_FROM_DEVICE);
		}
		rq->skb_list[j] = NULL;
		rq->dma_list[j] = 0;
	}
	rq->local_prod_idx = 0;
	rq->local_cons_idx = 0;
	kfree(descq);
	kfree(rq->skb_list);
	kfree(rq->dma_list);
	return ret;
}

static int mdev_setup_rx_rings(struct otxmn_dev *mdev)
{
	int i, j, ret;

	for  (i = 0; i < mdev->num_rxq; i++) {
		ret = mdev_setup_rx_ring(mdev, i);
		if (ret)
			goto error;
	}
	return 0;
error:
	for ( j = 0; j < i; j++)
		mdev_clean_rx_ring(mdev, j);
	return ret;
}

static int mdev_reinit_rings(struct otxmn_dev *mdev)
{
	int ret;

	mdev_clean_tx_rings(mdev);
	mdev_clean_rx_rings(mdev);
	ret = mdev_setup_tx_rings(mdev);
	if (ret)
		return ret;
	ret = mdev_setup_rx_rings(mdev);
	if (ret)
		mdev_clean_tx_rings(mdev);
	return ret;
}

static int handle_target_status(struct otxmn_dev *mdev)
{
	uint64_t target_status;
	uint64_t cur_status;
	int ret = 0;

	cur_status = get_host_status(mdev);
	target_status = get_target_status(mdev);
	printk(KERN_DEBUG "host status %llu\n", cur_status);
	printk(KERN_DEBUG "target status %llu\n", target_status);
	switch (cur_status) {
	case OTXMN_HOST_READY:
		if (target_status == OTXMN_TARGET_RUNNING) {
			printk(KERN_DEBUG "target running after host ready\n");
			change_host_status(mdev, OTXMN_HOST_RUNNING, false);
			netif_carrier_on(mdev->ndev);
		}
		break;
	case OTXMN_HOST_RUNNING:
		target_status = get_target_status(mdev);
		if (target_status != OTXMN_TARGET_RUNNING) {
			printk(KERN_DEBUG "target not running after host running\n");
			netif_carrier_off(mdev->ndev);
			ret = mdev_reinit_rings(mdev);
			if (ret) {
				change_host_status(mdev, OTXMN_HOST_FATAL,
						   false);
				return ret;
			}
			change_host_status(mdev, OTXMN_HOST_READY, false);
		}
		break;
	default:
		printk(KERN_DEBUG "unhandled state transition host_status:%llu target_status %llu\n",
		       cur_status, target_status);
		break;
	}
	return ret;
}

static int handle_txq_completion(struct otxmn_dev *mdev, int q_idx)
{
	struct otxmn_sw_descq *tq = &mdev->txq[q_idx];
	uint32_t prev_cons_idx, cur_cons_idx;
	int bytes = 0, pkts = 0;
	struct sk_buff *skb;
	int count, start, i;
	dma_addr_t dma;

	prev_cons_idx = tq->local_cons_idx;
	cur_cons_idx =  *tq->cons_idx_shadow;
	count = otxmn_circq_depth(cur_cons_idx,  prev_cons_idx, tq->mask);
	start = prev_cons_idx;
	for (i = 0; i < count; i++) {
		skb = tq->skb_list[start];
		dma = tq->dma_list[start];
		pkts += 1;
		bytes += skb->len;
		dma_unmap_single(mdev->dev, dma, skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		tq->skb_list[start] = NULL;
		tq->dma_list[start] = 0;
		start = otxmn_circq_inc(start, tq->mask);
	}
	/* update lists before updating cons idx */
	wmb();
	tq->local_cons_idx = cur_cons_idx;
	tq->pkts  += pkts;
	tq->bytes += bytes;
	return count;
}

static int rxq_refill(struct otxmn_dev *mdev, int q_idx)
{
	struct otxmn_sw_descq *rq = &mdev->rxq[q_idx];
	int cur_prod_idx, cur_cons_idx, count, start;
	struct otxcn_hw_desc_ptr ptr;
	uint8_t *hw_desc_ptr;
	struct sk_buff *skb;
	dma_addr_t dma;
	int i;

	cur_prod_idx = rq->local_prod_idx;
	cur_cons_idx =  *rq->cons_idx_shadow;
	count = otxmn_circq_space(cur_prod_idx,  cur_cons_idx, rq->mask);
	start = cur_prod_idx;
	for (i = 0; i < count; i++) {
		memset(&ptr, 0, sizeof(struct otxcn_hw_desc_ptr));
		skb = alloc_skb(OTXMN_RX_BUF_SIZE, GFP_KERNEL);
		if (!skb) {
			printk(KERN_DEBUG "unable to refill\n");
			break;
		}
		skb->dev = mdev->ndev;
		dma = dma_map_single(mdev->dev, skb->data, OTXMN_RX_BUF_SIZE,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(mdev->dev, dma)) {
			dev_kfree_skb_any(skb);
			printk(KERN_DEBUG "unable to refill\n");
			break;
		}
		ptr.hdr.s_mgmt_net.ptr_type = OTXCN_DESC_PTR_DIRECT;
		ptr.ptr = dma;
		rq->skb_list[start] = skb;
		rq->dma_list[start] = dma;
		hw_desc_ptr = rq->hw_descq +
				OTXCN_DESC_ARR_ENTRY_OFFSET(start);
		mmio_memwrite(hw_desc_ptr, &ptr,
			      sizeof(struct otxcn_hw_desc_ptr));
		rq->local_prod_idx = otxmn_circq_inc(rq->local_prod_idx,
						     rq->mask);
		start = otxmn_circq_inc(start, rq->mask);
	}
	/* the lists need to be updated before updating hwprod idx */
	wmb();
	writel(rq->local_prod_idx, rq->hw_prod_idx);
	return i;
}


/* static void pkt_hex_dump(struct sk_buff *skb)
{
	int i, l, linelen, remaining;
	uint8_t *data, ch;
	int rowsize = 16;
	size_t len;
	int li = 0;

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

static int handle_rxq(struct otxmn_dev *mdev, int q_idx)
{
	struct otxmn_sw_descq *rq = &mdev->rxq[q_idx];
	uint32_t prev_cons_idx, cur_cons_idx;
	struct otxcn_hw_desc_ptr ptr;
	uint8_t *hw_desc_ptr;
	int count, start, i;
	struct sk_buff *skb;

	if (!mdev->admin_up)
		return 0;
	if (get_host_status(mdev) != OTXMN_HOST_RUNNING)
		return 0;
	prev_cons_idx = rq->local_cons_idx;
	cur_cons_idx =  *rq->cons_idx_shadow;
	count = otxmn_circq_depth(cur_cons_idx,  prev_cons_idx, rq->mask);
	start = prev_cons_idx;
	for (i = 0; i < count; i++) {
		skb = rq->skb_list[start];
		dma_unmap_single(mdev->dev, rq->dma_list[start],
				 OTXMN_RX_BUF_SIZE,
				 DMA_FROM_DEVICE);
		hw_desc_ptr = rq->hw_descq +
				OTXCN_DESC_ARR_ENTRY_OFFSET(start);
		/* this is not optimal metatadata should probaly be in the packet */
		mmio_memread(&ptr, hw_desc_ptr,
			     sizeof(struct otxcn_hw_desc_ptr));
		/*printk(KERN_DEBUG "is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
			 ptr.hdr.s_mgmt_net.is_frag,
			 ptr.hdr.s_mgmt_net.total_len,
			 ptr.hdr.s_mgmt_net.ptr_type,
			 ptr.hdr.s_mgmt_net.ptr_len,
			 ptr.ptr);
		*/
		if (unlikely(ptr.hdr.s_mgmt_net.total_len < ETH_ZLEN ||
		    ptr.hdr.s_mgmt_net.is_frag ||
		    ptr.hdr.s_mgmt_net.ptr_len != ptr.hdr.s_mgmt_net.ptr_len)) {
			/* dont handle frags now */
			printk(KERN_DEBUG "bad rx pkt\n");
			printk(KERN_DEBUG "is_frag:%d total_len:%d ptr_type:%d ptr_len:%d ptr:0x%llx\n",
				ptr.hdr.s_mgmt_net.is_frag,
				ptr.hdr.s_mgmt_net.total_len,
				ptr.hdr.s_mgmt_net.ptr_type,
				ptr.hdr.s_mgmt_net.ptr_len,
				ptr.ptr);
			rq->errors++;
			dev_kfree_skb_any(skb);
		} else {
			skb_put(skb, ptr.hdr.s_mgmt_net.total_len);
			/* pkt_hex_dump(skb); */
			skb->protocol = eth_type_trans(skb, mdev->ndev);
			rq->pkts += 1;
			rq->bytes += ptr.hdr.s_mgmt_net.total_len;
			/* we are in process context */
			netif_rx_ni(skb);
		}
		rq->skb_list[start] = NULL;
		rq->dma_list[start] = 0;
		start = otxmn_circq_inc(start, rq->mask);
	}
	/* lists need to be updated before updating cons idx */
	wmb();
	rq->local_cons_idx = cur_cons_idx;
	return count;
}

static void mdev_rxtx(struct otxmn_dev *mdev)
{
	int i;

	for (i = 0; i < mdev->num_txq; i++)
		handle_txq_completion(mdev, i);
	for (i = 0; i < mdev->num_rxq; i++) {
		if (handle_rxq(mdev, i))
			rxq_refill(mdev, i);
	}
}

static void mgmt_net_task(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	union otxmn_mbox_msg msg;
	struct otxmn_dev *mdev;
	int ret;

	mdev = (struct otxmn_dev *)container_of(delayed_work, struct otxmn_dev,
						service_task);
	ret = mbox_check_msg_rcvd(mdev, &msg);
	if (!ret) {
		switch(msg.s.hdr.opcode) {
		case OTXMN_MBOX_TARGET_STATUS_CHANGE:
			handle_target_status(mdev);
			if (msg.s.hdr.req_ack)
				set_host_mbox_ack_reg(mdev, msg.s.hdr.id);
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
	uint32_t *tq_cons_shdw_vaddr, *rq_cons_shdw_vaddr;
	dma_addr_t tq_cons_shdw_dma, rq_cons_shdw_dma;
	int num_txq, num_rxq, max_rxq, max_txq, ret;
	mv_facility_conf_t conf;
	struct net_device *ndev;
	struct otxmn_dev *mdev;

	if (mv_get_facility_conf(MV_FACILITY_MGMT_NETDEV, &conf)) {
		printk(KERN_DEBUG "get_facility_conf failed\n");
		return -ENODEV;
	}
	if (conf.memsize < OTXMN_BAR_SIZE) {
		printk(KERN_DEBUG "bar size less %d\n", conf.memsize);
		return -ENODEV;
	}
	printk(KERN_DEBUG "conf.map_addr.bar_map %p\n", conf.memmap.host_addr);
	printk(KERN_DEBUG "conf.map_size %d\n", conf.memsize);
	/* irrespective of resources, we support single queue only */
	max_txq = num_txq = OTXMN_MAXQ;
	max_rxq = num_rxq = OTXMN_MAXQ;
	tq_cons_shdw_vaddr = dma_alloc_coherent(conf.dma_dev.host_ep_dev,
						(sizeof(uint32_t) * num_txq),
						&tq_cons_shdw_dma,
						GFP_KERNEL);
	if (tq_cons_shdw_vaddr == NULL) {
		printk(KERN_DEBUG "dma_alloc_coherent tq failed\n");
		return  -ENOMEM;
	}
	rq_cons_shdw_vaddr = dma_alloc_coherent(conf.dma_dev.host_ep_dev,
						(sizeof(uint32_t) * num_rxq),
						&rq_cons_shdw_dma,
						GFP_KERNEL);
	if (rq_cons_shdw_vaddr == NULL) {
		ret = -ENOMEM;
		printk(KERN_DEBUG "dma_alloc_coherent rq failed\n");
		goto tq_dma_free;
	}
	/* we support only single queue at this time */
	ndev = alloc_etherdev(sizeof(struct otxmn_dev));
	if (!ndev) {
		ret = -ENOMEM;
		printk(KERN_DEBUG "alloc_etherdev failed\n");
		goto rq_dma_free;
	}
	ndev->netdev_ops = &mgmt_netdev_ops;
	ndev->hw_features = NETIF_F_HIGHDMA;
	ndev->features = ndev->hw_features;
	ndev->mtu = OTXMN_MAX_MTU;
	netif_carrier_off(ndev);
	eth_hw_addr_random(ndev);
	mdev = netdev_priv(ndev);
	memset(mdev, 0, sizeof(struct otxmn_dev));
	mdev->admin_up = false;
	mdev->ndev = ndev;
	mdev->dev = conf.dma_dev.host_ep_dev;
	mdev->bar_map = conf.memmap.host_addr;
	mdev->bar_map_size = conf.memsize;
	mdev->max_txq = max_txq;
	mdev->max_rxq = max_rxq;
	mdev->num_txq = num_txq;
	mdev->num_rxq = num_rxq;
	mdev->element_count = OTXMN_NUM_ELEMENTS;
	mdev->tq_cons_shdw_vaddr = tq_cons_shdw_vaddr;
	mdev->tq_cons_shdw_dma   = tq_cons_shdw_dma;
	mdev->rq_cons_shdw_vaddr = rq_cons_shdw_vaddr;
	mdev->rq_cons_shdw_dma   = rq_cons_shdw_dma;
	printk(KERN_DEBUG "rq_cons_shdw_vaddr %p rq_cons_shdw_dma %llx phys: 0x%llx\n",
	       mdev->rq_cons_shdw_vaddr, mdev->rq_cons_shdw_dma,
	       virt_to_phys(mdev->rq_cons_shdw_vaddr));
	printk(KERN_DEBUG "tq_cons_shdw_vaddr %p tq_cons_shdw_dma %llx phys: 0x%llx\n",
	       mdev->tq_cons_shdw_vaddr, mdev->tq_cons_shdw_dma,
	       virt_to_phys(mdev->tq_cons_shdw_vaddr));
	ret = mdev_setup_tx_rings(mdev);
	if (ret) {
		printk(KERN_DEBUG "setup tx rings failed\n");
		goto free_net;
	}
	ret = mdev_setup_rx_rings(mdev);
	if (ret) {
		printk(KERN_DEBUG "setup rx rings failed\n");
		goto clean_tx_ring;
	}
	mdev->mgmt_wq = alloc_ordered_workqueue("mgmt_net", 0);
	if (!mdev->mgmt_wq) {
		ret = -ENOMEM;
		printk(KERN_DEBUG "alloc_ordered_workqueue failed\n");
		goto clean_rx_ring;
	}
	mdev->send_mbox_id = 0;
	mdev->recv_mbox_id = 0;
	mutex_init(&mdev->mbox_lock);
	ret = register_netdev(ndev);
	if (ret) {
		printk(KERN_DEBUG "register_netdev failed\n");
		goto destroy_mutex;
	}
	change_host_status(mdev, OTXMN_HOST_READY, false);
	INIT_DELAYED_WORK(&mdev->service_task, mgmt_net_task);
	queue_delayed_work(mdev->mgmt_wq, &mdev->service_task,
			   usecs_to_jiffies(OTXMN_SERVICE_TASK_US));
	gmdev = mdev;
	return 0;
destroy_mutex:
	mutex_destroy(&mdev->mbox_lock);
	destroy_workqueue(mdev->mgmt_wq);
clean_rx_ring:
	mdev_clean_rx_rings(mdev);
clean_tx_ring:
	mdev_clean_tx_rings(mdev);
free_net:
	free_netdev(ndev);
rq_dma_free:
	dma_free_coherent(conf.dma_dev.host_ep_dev,
			  (sizeof(uint64_t *) * num_rxq),
			  rq_cons_shdw_vaddr,
			  rq_cons_shdw_dma);
tq_dma_free:
	dma_free_coherent(conf.dma_dev.host_ep_dev,
			  (sizeof(uint64_t *) * num_txq),
			  tq_cons_shdw_vaddr,
			  tq_cons_shdw_dma);
	return ret;
}

static void teardown_mdev_resources(struct otxmn_dev *mdev)
{
	dma_free_coherent(mdev->dev,
			  (sizeof(uint64_t *) * mdev->num_rxq),
			  mdev->rq_cons_shdw_vaddr,
			  mdev->rq_cons_shdw_dma);
	dma_free_coherent(mdev->dev,
			  (sizeof(uint64_t *) * mdev->num_txq),
			  mdev->tq_cons_shdw_vaddr,
			  mdev->tq_cons_shdw_dma);
}

static void __exit mgmt_exit(void)
{
	struct otxmn_dev *mdev;

	if (!gmdev)
		return;
	mdev = gmdev;
	netif_carrier_off(mdev->ndev);
	change_host_status(mdev, OTXMN_HOST_GOING_DOWN, true);
	cancel_delayed_work_sync(&mdev->service_task);
	mutex_destroy(&mdev->mbox_lock);
	destroy_workqueue(mdev->mgmt_wq);
	unregister_netdev(mdev->ndev);
	mdev_clean_rx_rings(mdev);
	mdev_clean_tx_rings(mdev);
	teardown_mdev_resources(mdev);
	free_netdev(mdev->ndev);
	gmdev = NULL;
}

module_init(mgmt_init);
module_exit(mgmt_exit);
MODULE_AUTHOR("Marvell Inc.");
MODULE_DESCRIPTION("OTX Management Net driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(OTXMN_VERSION);
