/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

#include "octep_ctrl_mbox.h"
#include "cp_compat.h"

/* Timeout in msecs for message response */
#define OCTEP_CTRL_MBOX_MSG_TIMEOUT_MS			100
/* Time in msecs to wait for message response */
#define OCTEP_CTRL_MBOX_MSG_WAIT_MS			10

/* Size of mbox info in bytes */
#define OCTEP_CTRL_MBOX_INFO_SZ				256
/* Size of mbox host to fw queue info in bytes */
#define OCTEP_CTRL_MBOX_H2FQ_INFO_SZ			16
/* Size of mbox fw to host queue info in bytes */
#define OCTEP_CTRL_MBOX_F2HQ_INFO_SZ			16

#define OCTEP_CTRL_MBOX_TOTAL_INFO_SZ	(OCTEP_CTRL_MBOX_INFO_SZ + \
					 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ + \
					 OCTEP_CTRL_MBOX_F2HQ_INFO_SZ)

#define OCTEP_CTRL_MBOX_INFO_MAGIC_NUM(m)	((m) + 0)
#define OCTEP_CTRL_MBOX_INFO_BARMEM_SZ(m)	((m) + 8)
#define OCTEP_CTRL_MBOX_INFO_HOST_VERSION(m)	((m) + 16)
#define OCTEP_CTRL_MBOX_INFO_HOST_STATUS(m)	((m) + 24)
#define OCTEP_CTRL_MBOX_INFO_FW_VERSION(m)	((m) + 136)
#define OCTEP_CTRL_MBOX_INFO_FW_STATUS(m)	((m) + 144)

#define OCTEP_CTRL_MBOX_H2FQ_INFO(m)	((m) + OCTEP_CTRL_MBOX_INFO_SZ)
#define OCTEP_CTRL_MBOX_H2FQ_PROD(m)	(OCTEP_CTRL_MBOX_H2FQ_INFO(m))
#define OCTEP_CTRL_MBOX_H2FQ_CONS(m)	(OCTEP_CTRL_MBOX_H2FQ_INFO(m) + 4)
#define OCTEP_CTRL_MBOX_H2FQ_SZ(m)	(OCTEP_CTRL_MBOX_H2FQ_INFO(m) + 8)

#define OCTEP_CTRL_MBOX_F2HQ_INFO(m)	((m) + \
					 OCTEP_CTRL_MBOX_INFO_SZ + \
					 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ)
#define OCTEP_CTRL_MBOX_F2HQ_PROD(m)	(OCTEP_CTRL_MBOX_F2HQ_INFO(m))
#define OCTEP_CTRL_MBOX_F2HQ_CONS(m)	((OCTEP_CTRL_MBOX_F2HQ_INFO(m)) + 4)
#define OCTEP_CTRL_MBOX_F2HQ_SZ(m)	((OCTEP_CTRL_MBOX_F2HQ_INFO(m)) + 8)

static const uint32_t mbox_hdr_sz = sizeof(union octep_ctrl_mbox_msg_hdr);

static inline uint32_t octep_ctrl_mbox_circq_inc(uint32_t index, uint32_t inc,
						 uint32_t sz)
{
	return (index + inc) % sz;
}

static inline uint32_t octep_ctrl_mbox_circq_space(uint32_t pi, uint32_t ci,
						   uint32_t sz)
{
	return sz - (abs(pi - ci) % sz);
}

static inline uint32_t octep_ctrl_mbox_circq_depth(uint32_t pi, uint32_t ci,
						   uint32_t sz)
{
	return (abs(pi - ci) % sz);
}

static inline int set_mbox_info(struct octep_ctrl_mbox *mbox)
{
	uint16_t qsz;

	if (mbox->barmem_sz <= OCTEP_CTRL_MBOX_INFO_SZ)
		return -ENOMEM;

	qsz = (mbox->barmem_sz - OCTEP_CTRL_MBOX_INFO_SZ) / 2;
	/* mbox element sz = hdr(2 words) + data(2 words) = 4 words
	 * each queue should have atleast 2 elements
	 */
	if (qsz < 64)
		return -ENOMEM;

	mbox->h2fq.sz = qsz;
	mbox->h2fq.hw_prod = OCTEP_CTRL_MBOX_H2FQ_PROD(mbox->barmem);
	mbox->h2fq.hw_cons = OCTEP_CTRL_MBOX_H2FQ_CONS(mbox->barmem);
	mbox->h2fq.hw_q = mbox->barmem + OCTEP_CTRL_MBOX_TOTAL_INFO_SZ;

	mbox->f2hq.sz = qsz;
	mbox->f2hq.hw_prod = OCTEP_CTRL_MBOX_F2HQ_PROD(mbox->barmem);
	mbox->f2hq.hw_cons = OCTEP_CTRL_MBOX_F2HQ_CONS(mbox->barmem);
	mbox->f2hq.hw_q = mbox->barmem + OCTEP_CTRL_MBOX_TOTAL_INFO_SZ + qsz;

	return 0;
}

int octep_ctrl_mbox_init(struct octep_ctrl_mbox *mbox)
{
	int err;

	if (!mbox)
		return -EINVAL;

	if (!mbox->barmem || !mbox->barmem_sz)
		return -EINVAL;

	err = set_mbox_info(mbox);
	if (err)
		return err;

	cp_write64(OCTEP_CTRL_MBOX_MAGIC_NUMBER,
		   OCTEP_CTRL_MBOX_INFO_MAGIC_NUM(mbox->barmem));
	cp_write32(mbox->barmem_sz,
		   OCTEP_CTRL_MBOX_INFO_BARMEM_SZ(mbox->barmem));

	cp_write64(OCTEP_CTRL_MBOX_STATUS_INIT,
		   OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem));

	cp_write32(0, OCTEP_CTRL_MBOX_H2FQ_PROD(mbox->barmem));
	cp_write32(0, OCTEP_CTRL_MBOX_H2FQ_CONS(mbox->barmem));
	cp_write32(mbox->h2fq.sz, OCTEP_CTRL_MBOX_H2FQ_SZ(mbox->barmem));

	cp_write32(0, OCTEP_CTRL_MBOX_F2HQ_PROD(mbox->barmem));
	cp_write32(0, OCTEP_CTRL_MBOX_F2HQ_CONS(mbox->barmem));
	cp_write32(mbox->f2hq.sz, OCTEP_CTRL_MBOX_F2HQ_SZ(mbox->barmem));

	cp_write64(OCTEP_CTRL_MBOX_STATUS_READY,
		   OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem));

	return 0;
}

static int write_mbox_data(struct octep_ctrl_mbox_q *q, uint32_t *pi,
			   uint32_t ci, void *buf, uint32_t w_sz)
{
	uint32_t cp_sz;
	void *qbuf;

	/* Assumption: Caller has ensured enough write space */
	qbuf = (q->hw_q + *pi);
	if (*pi < ci) {
		/* copy entire w_sz */
		memcpy(qbuf, buf, w_sz);
		*pi = octep_ctrl_mbox_circq_inc(*pi, w_sz, q->sz);
	} else {
		/* copy upto end of queue */
		cp_sz = cp_min((q->sz - *pi), w_sz);
		memcpy(qbuf, buf, cp_sz);
		w_sz -= cp_sz;
		*pi = octep_ctrl_mbox_circq_inc(*pi, cp_sz, q->sz);
		if (w_sz) {
			/* roll over and copy remaining w_sz */
			buf += cp_sz;
			qbuf = (q->hw_q + *pi);
			memcpy(qbuf, buf, w_sz);
			*pi = octep_ctrl_mbox_circq_inc(*pi, w_sz, q->sz);
		}
	}

	return 0;
}

int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox,
			 struct octep_ctrl_mbox_msg *msgs,
			 int num)
{
	struct octep_ctrl_mbox_msg_buf *sg;
	struct octep_ctrl_mbox_msg *msg;
	struct octep_ctrl_mbox_q *q;
	uint32_t pi, ci, prev_pi, buf_sz, w_sz;
	int m, s;

	if (!mbox || !msgs)
		return -EINVAL;

	if (cp_read64(OCTEP_CTRL_MBOX_INFO_HOST_STATUS(mbox->barmem)) !=
	    OCTEP_CTRL_MBOX_STATUS_READY)
		return -EIO;

	q = &mbox->f2hq;
	pi = cp_read32(q->hw_prod);
	ci = cp_read32(q->hw_cons);
	for (m = 0; m < num; m++) {
		msg = &msgs[m];
		if (!msg)
			break;

		/* not enough space for next message */
		if (octep_ctrl_mbox_circq_space(pi, ci, q->sz) <
		    (msg->hdr.s.sz + mbox_hdr_sz))
			break;

		prev_pi = pi;
		write_mbox_data(q, &pi, ci, (void*)&msg->hdr, mbox_hdr_sz);
		buf_sz = msg->hdr.s.sz;
		for (s = 0; ((s < msg->sg_num) && (buf_sz > 0)); s++) {
			sg = &msg->sg_list[s];
			w_sz = (sg->sz <= buf_sz) ? sg->sz : buf_sz;
			write_mbox_data(q, &pi, ci, sg->msg, w_sz);
			buf_sz -= w_sz;
		}
		if (buf_sz) {
			/* we did not write entire message */
			pi = prev_pi;
			break;
		}
	}
	cp_write32(pi, q->hw_prod);

	return (m) ? m : -EAGAIN;
}

static int read_mbox_data(struct octep_ctrl_mbox_q *q, uint32_t pi,
			  uint32_t *ci, void *buf, uint32_t r_sz)
{
	uint32_t cp_sz;
	void *qbuf;

	/* Assumption: Caller has ensured enough read space */
	qbuf = (q->hw_q + *ci);
	if (*ci < pi) {
		/* copy entire r_sz */
		memcpy(buf, qbuf, r_sz);
		*ci = octep_ctrl_mbox_circq_inc(*ci, r_sz, q->sz);
	} else {
		/* copy upto end of queue */
		cp_sz = cp_min((q->sz - *ci), r_sz);
		memcpy(buf, qbuf, cp_sz);
		r_sz -= cp_sz;
		*ci = octep_ctrl_mbox_circq_inc(*ci, cp_sz, q->sz);
		if (r_sz) {
			/* roll over and copy remaining r_sz */
			buf += cp_sz;
			qbuf = (q->hw_q + *ci);
			memcpy(buf, qbuf, r_sz);
			*ci = octep_ctrl_mbox_circq_inc(*ci, r_sz, q->sz);
		}
	}

	return 0;
}

int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox,
			 struct octep_ctrl_mbox_msg *msgs,
			 int num)
{
	struct octep_ctrl_mbox_msg_buf *sg;
	struct octep_ctrl_mbox_msg *msg;
	struct octep_ctrl_mbox_q *q;
	uint32_t pi, ci, q_depth, r_sz, buf_sz, prev_ci;
	int s, m;

	if (!mbox || !msgs)
		return -EINVAL;

	if (cp_read64(OCTEP_CTRL_MBOX_INFO_HOST_STATUS(mbox->barmem)) !=
	    OCTEP_CTRL_MBOX_STATUS_READY)
		return -EIO;

	q = &mbox->h2fq;
	pi = cp_read32(q->hw_prod);
	ci = cp_read32(q->hw_cons);
	for (m = 0; m < num; m++) {
		q_depth = octep_ctrl_mbox_circq_depth(pi, ci, q->sz);
		if (q_depth < mbox_hdr_sz)
			break;

		msg = &msgs[m];
		if (!msg)
			break;

		prev_ci = ci;
		read_mbox_data(q, pi, &ci, (void*)&msg->hdr, mbox_hdr_sz);
		buf_sz = msg->hdr.s.sz;
		for (s = 0; ((s < msg->sg_num) && (buf_sz > 0)); s++) {
			sg = &msg->sg_list[s];
			r_sz = (sg->sz <= buf_sz) ? sg->sz : buf_sz;
			read_mbox_data(q, pi, &ci, sg->msg, r_sz);
			buf_sz -= r_sz;
		}
		if (buf_sz) {
			/* we did not read entire message */
			ci = prev_ci;
			break;
		}
	}
	cp_write32(ci, q->hw_cons);

	return (m) ? m : -EAGAIN;
}

int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox)
{
	if (!mbox)
		return -EINVAL;
	if (!mbox->barmem)
		return -EINVAL;

	cp_write64(OCTEP_CTRL_MBOX_STATUS_UNINIT,
		   OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem));
	cp_write64(0, OCTEP_CTRL_MBOX_INFO_MAGIC_NUM(mbox->barmem));
	cp_write64(0, OCTEP_CTRL_MBOX_INFO_FW_VERSION(mbox->barmem));
	cp_write64(0, OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem));

	return 0;
}
