/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

#include "octep_ctrl_mbox.h"
#include "cp_compat.h"

/* Timeout in msecs for message response */
#define OCTEP_CTRL_MBOX_MSG_TIMEOUT_MS			100
/* Time in msecs to wait for message response */
#define OCTEP_CTRL_MBOX_MSG_WAIT_MS			10

/* Size of mbox info in bytes */
#define OCTEP_CTRL_MBOX_INFO_INFO_SZ			256
/* Size of mbox host to fw queue info in bytes */
#define OCTEP_CTRL_MBOX_H2FQ_INFO_SZ			16
/* Size of mbox fw to host queue info in bytes */
#define OCTEP_CTRL_MBOX_F2HQ_INFO_SZ			16

#define OCTEP_CTRL_MBOX_INFO_MAGIC_NUM_OFFSET(m)	((m) + 0)
#define OCTEP_CTRL_MBOX_INFO_BARMEM_SZ_OFFSET(m)	((m) + 8)
#define OCTEP_CTRL_MBOX_INFO_HOST_VERSION_OFFSET(m)	((m) + 16)
#define OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(m)	((m) + 24)
#define OCTEP_CTRL_MBOX_INFO_FW_VERSION_OFFSET(m)	((m) + 136)
#define OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(m)	((m) + 144)

#define OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)		((m) + OCTEP_CTRL_MBOX_INFO_INFO_SZ)
#define OCTEP_CTRL_MBOX_H2FQ_PROD_OFFSET(m)		(OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m))
#define OCTEP_CTRL_MBOX_H2FQ_CONS_OFFSET(m)		((OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)) + 4)
#define OCTEP_CTRL_MBOX_H2FQ_ELEM_SZ_OFFSET(m)		((OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)) + 8)
#define OCTEP_CTRL_MBOX_H2FQ_ELEM_CNT_OFFSET(m)		((OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)) + 12)

#define OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)		((m) + \
							 OCTEP_CTRL_MBOX_INFO_INFO_SZ + \
							 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ)
#define OCTEP_CTRL_MBOX_F2HQ_PROD_OFFSET(m)		(OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m))
#define OCTEP_CTRL_MBOX_F2HQ_CONS_OFFSET(m)		((OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)) + 4)
#define OCTEP_CTRL_MBOX_F2HQ_ELEM_SZ_OFFSET(m)		((OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)) + 8)
#define OCTEP_CTRL_MBOX_F2HQ_ELEM_CNT_OFFSET(m)		((OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)) + 12)

#define OCTEP_CTRL_MBOX_Q_OFFSET(m, i)			((m) + \
							 (sizeof(struct octep_ctrl_mbox_msg) * i))

static inline uint32_t octep_ctrl_mbox_circq_inc(uint32_t index, uint32_t mask)
{
	return (index + 1) & mask;
}

static inline uint32_t octep_ctrl_mbox_circq_space(uint32_t pi, uint32_t ci, uint32_t mask)
{
	return mask - ((pi - ci) & mask);
}

static inline uint32_t octep_ctrl_mbox_circq_depth(uint32_t pi, uint32_t ci, uint32_t mask)
{
	return ((pi - ci) & mask);
}

int octep_ctrl_mbox_init(struct octep_ctrl_mbox *mbox)
{
	if (!mbox)
		return -EINVAL;

	if (!mbox->barmem ||
	    !mbox->h2fq.elem_sz || !mbox->f2hq.elem_sz ||
	    !mbox->h2fq.elem_cnt || !mbox->f2hq.elem_cnt)
		return -EINVAL;

	/* element count has to be power of 2, mask has to be cnt - 1 */
	if ((mbox->h2fq.elem_cnt & (mbox->h2fq.elem_cnt - 1)) ||
	    (mbox->f2hq.elem_cnt & (mbox->f2hq.elem_cnt - 1)) ||
	    (mbox->h2fq.mask != (mbox->h2fq.elem_cnt - 1)) ||
	    (mbox->f2hq.mask != (mbox->f2hq.elem_cnt - 1)))
		return -EINVAL;

	cp_write64(OCTEP_CTRL_MBOX_MAGIC_NUMBER,
		   OCTEP_CTRL_MBOX_INFO_MAGIC_NUM_OFFSET(mbox->barmem));
	cp_write32(mbox->barmem_sz,
		   OCTEP_CTRL_MBOX_INFO_BARMEM_SZ_OFFSET(mbox->barmem));
//	cp_write64(OCTEP_DRV_VERSION,
//		   OCTEP_CTRL_MBOX_INFO_FW_VERSION_OFFSET(mbox->barmem));
	cp_write64(OCTEP_CTRL_MBOX_STATUS_INIT,
		   OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(mbox->barmem));

	/* Align element size to word size */
	mbox->h2fq.elem_sz += (mbox->h2fq.elem_sz % (sizeof(uint64_t)));
	mbox->h2fq.hw_prod = OCTEP_CTRL_MBOX_H2FQ_PROD_OFFSET(mbox->barmem);
	mbox->h2fq.hw_cons = OCTEP_CTRL_MBOX_H2FQ_CONS_OFFSET(mbox->barmem);
	mbox->h2fq.hw_q = mbox->barmem +
			  OCTEP_CTRL_MBOX_INFO_INFO_SZ +
			  OCTEP_CTRL_MBOX_H2FQ_INFO_SZ +
			  OCTEP_CTRL_MBOX_F2HQ_INFO_SZ;
	cp_write32(0, OCTEP_CTRL_MBOX_H2FQ_PROD_OFFSET(mbox->barmem));
	cp_write32(0, OCTEP_CTRL_MBOX_H2FQ_CONS_OFFSET(mbox->barmem));
	cp_write32(mbox->h2fq.elem_sz,
		   OCTEP_CTRL_MBOX_H2FQ_ELEM_SZ_OFFSET(mbox->barmem));
	cp_write32(mbox->h2fq.elem_cnt,
		   OCTEP_CTRL_MBOX_H2FQ_ELEM_CNT_OFFSET(mbox->barmem));
	pthread_mutex_init(&mbox->h2fq_lock, NULL);

	/* Align element size to word size */
	mbox->f2hq.elem_sz += (mbox->f2hq.elem_sz % (sizeof(uint64_t)));
	mbox->f2hq.hw_prod = OCTEP_CTRL_MBOX_F2HQ_PROD_OFFSET(mbox->barmem);
	mbox->f2hq.hw_cons = OCTEP_CTRL_MBOX_F2HQ_CONS_OFFSET(mbox->barmem);
	mbox->f2hq.hw_q = mbox->h2fq.hw_q +
			  ((mbox->h2fq.elem_sz +
			    sizeof(union octep_ctrl_mbox_msg_hdr)) *
			   mbox->h2fq.elem_cnt);
	cp_write32(0, OCTEP_CTRL_MBOX_F2HQ_PROD_OFFSET(mbox->barmem));
	cp_write32(0, OCTEP_CTRL_MBOX_F2HQ_CONS_OFFSET(mbox->barmem));
	cp_write32(mbox->f2hq.elem_sz,
		   OCTEP_CTRL_MBOX_F2HQ_ELEM_SZ_OFFSET(mbox->barmem));
	cp_write32(mbox->f2hq.elem_cnt,
		   OCTEP_CTRL_MBOX_F2HQ_ELEM_CNT_OFFSET(mbox->barmem));
	pthread_mutex_init(&mbox->f2hq_lock, NULL);

	cp_write64(OCTEP_CTRL_MBOX_STATUS_READY,
		    OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(mbox->barmem));

	return 0;
}

int octep_ctrl_mbox_is_host_ready(struct octep_ctrl_mbox *mbox)
{
	uint64_t status;

	if (!mbox)
		return -EINVAL;
	if (!mbox->barmem)
		return -EINVAL;

	status = cp_read64(OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(mbox->barmem));
	if (status != OCTEP_CTRL_MBOX_STATUS_READY)
		return -EAGAIN;

	return 0;
}

int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox,
			 struct octep_ctrl_mbox_msg *msg)
{
	unsigned long expire;
	struct octep_ctrl_mbox_q *q;
	uint16_t pi, ci;
	uint64_t *mbuf, *qidx;
	int i;

	if (!mbox || !msg)
		return -EINVAL;
	if (!mbox->barmem)
		return -EINVAL;

	q = &mbox->f2hq;
	pi = cp_read32(q->hw_prod);
	ci = cp_read32(q->hw_cons);

	if (!octep_ctrl_mbox_circq_space(pi, ci, q->mask))
		return -ENOMEM;

	qidx = OCTEP_CTRL_MBOX_Q_OFFSET(q->hw_q, pi);
	mbuf = (uint64_t*)msg->msg;

	pthread_mutex_lock(&mbox->f2hq_lock);
	for (i=1; i<=msg->hdr.sizew; i++)
		cp_write64(*mbuf++, (qidx + i));

	cp_write64(msg->hdr.word0, qidx);

	pi = octep_ctrl_mbox_circq_inc(pi, q->mask);
	cp_write32(pi, q->hw_prod);
	pthread_mutex_unlock(&mbox->f2hq_lock);

	/* don't check for notification response */
	if (msg->hdr.flags & OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY)
		return 0;

	expire = 0;
	while(1) {
		msg->hdr.word0 = cp_read64(qidx);
		if (msg->hdr.flags == OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP)
			break;
		usleep(OCTEP_CTRL_MBOX_MSG_WAIT_MS * 1000);
		if (expire >= OCTEP_CTRL_MBOX_MSG_TIMEOUT_MS)
			return -EBUSY;
		expire += OCTEP_CTRL_MBOX_MSG_WAIT_MS;
	}
	mbuf = (uint64_t*)msg->msg;
	for (i=1; i<=msg->hdr.sizew; i++)
		*mbuf++ = cp_read64(qidx + i);
	
	return 0;
}

int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox,
			 struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_mbox_q *q;
	uint32_t count, pi, ci;
	uint64_t *qidx, *mbuf;
	int i;

	if (!mbox || !msg)
		return -EINVAL;
	if (!mbox->barmem)
		return -EINVAL;

	q = &mbox->h2fq;
	pi = cp_read32(q->hw_prod);
	ci = cp_read32(q->hw_cons);
	count = octep_ctrl_mbox_circq_depth(pi, ci, q->mask);
	if (!count)
		return -EAGAIN;

	qidx = OCTEP_CTRL_MBOX_Q_OFFSET(q->hw_q, ci);
	mbuf = (uint64_t*)msg->msg;

	pthread_mutex_lock(&mbox->h2fq_lock);

	msg->hdr.word0 = cp_read64(qidx);
	for (i=1; i<=msg->hdr.sizew; i++)
		*mbuf++ = cp_read64(qidx + i);

	ci = octep_ctrl_mbox_circq_inc(ci, q->mask);
	cp_write32(ci, q->hw_cons);

	pthread_mutex_unlock(&mbox->h2fq_lock);

	if ((msg->hdr.flags != OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ) ||
	    (!mbox->process_req))
		return 0;

	mbox->process_req(mbox->user_ctx, msg);
	mbuf = (uint64_t*)msg->msg;
	for (i=1; i<=msg->hdr.sizew; i++) {
		cp_write64(*mbuf++, (qidx + i));
	}

	cp_write64(msg->hdr.word0, qidx);

	return 0;
}

int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox)
{
	if (!mbox)
		return -EINVAL;
	if (!mbox->barmem)
		return -EINVAL;

	cp_write64(OCTEP_CTRL_MBOX_STATUS_UNINIT,
		   OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(mbox->barmem));
	cp_write64(0, OCTEP_CTRL_MBOX_INFO_MAGIC_NUM_OFFSET(mbox->barmem));
	cp_write64(0, OCTEP_CTRL_MBOX_INFO_FW_VERSION_OFFSET(mbox->barmem));

	pthread_mutex_destroy(&mbox->h2fq_lock);
	pthread_mutex_destroy(&mbox->f2hq_lock);

	cp_write64(0, OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(mbox->barmem));

	return 0;
}
