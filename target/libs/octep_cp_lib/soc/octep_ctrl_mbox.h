/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */
#ifndef __OCTEP_CTRL_MBOX_H__
#define __OCTEP_CTRL_MBOX_H__

/*              barmem structure
 * |===========================================|
 * |Info (16 + 120 + 120 = 256 bytes)          |
 * |-------------------------------------------|
 * |magic number (8 bytes)                     |
 * |bar memory size (4 bytes)                  |
 * |reserved (4 bytes)                         |
 * |-------------------------------------------|
 * |host version (8 bytes)                     |
 * |host status (8 bytes)                      |
 * |host reserved (104 bytes)                  |
 * |-------------------------------------------|
 * |fw version (8 bytes)                       |
 * |fw status (8 bytes)                        |
 * |fw reserved (104 bytes)                    |
 * |===========================================|
 * |Host to Fw Queue info (16 bytes)           |
 * |-------------------------------------------|
 * |producer index (4 bytes)                   |
 * |consumer index (4 bytes)                   |
 * |element size (4 bytes)                     |
 * |element count (4 bytes)                    |
 * |===========================================|
 * |Fw to Host Queue info (16 bytes)           |
 * |-------------------------------------------|
 * |producer index (4 bytes)                   |
 * |consumer index (4 bytes)                   |
 * |element size (4 bytes)                     |
 * |element count (4 bytes)                    |
 * |===========================================|
 * |Host to Fw Queue                           |
 * |-------------------------------------------|
 * |((elem_sz + hdr(8 bytes)) * elem_cnt) bytes|
 * |===========================================|
 * |===========================================|
 * |Fw to Host Queue                           |
 * |-------------------------------------------|
 * |((elem_sz + hdr(8 bytes)) * elem_cnt) bytes|
 * |===========================================|
 */
#include <pthread.h>

#ifndef BIT
#define BIT(a)	(1ULL << (a))
#endif

#define OCTEP_CTRL_MBOX_MAGIC_NUMBER			0xdeaddeadbeefbeefull

/* Valid request message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ		BIT(0)
/* Valid response message */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP		BIT(1)
/* Valid notification, no response required */
#define OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY		BIT(2)

enum octep_ctrl_mbox_status {
	OCTEP_CTRL_MBOX_STATUS_INVALID = 0,
	OCTEP_CTRL_MBOX_STATUS_INIT,
	OCTEP_CTRL_MBOX_STATUS_READY,
	OCTEP_CTRL_MBOX_STATUS_UNINIT
};

/* mbox message */
union octep_ctrl_mbox_msg_hdr {
	uint64_t word0;
	struct {
		/* OCTEP_CTRL_MBOX_MSG_HDR_FLAG_* */
		uint32_t flags;
		/* size of message in words excluding header */
		uint32_t sizew;
	};
};

/* mbox message */
struct octep_ctrl_mbox_msg {
	/* mbox transaction header */
	union octep_ctrl_mbox_msg_hdr hdr;
	/* pointer to message buffer */
	void *msg;
};

/* Mbox queue */
struct octep_ctrl_mbox_q {
	/* q element size, should be aligned to unsigned long */
	uint16_t elem_sz;
	/* q element count, should be power of 2 */
	uint16_t elem_cnt;
	/* q mask */
	uint16_t mask;
	/* producer address in bar mem */
	void *hw_prod;
	/* consumer address in bar mem */
	void *hw_cons;
	/* q base adddress in bar mem */ 
	void *hw_q;
};

struct octep_ctrl_mbox {
	/* host driver version */
	uint64_t version;
	/* size of bar memory */
	uint32_t barmem_sz;
	/* pointer to BAR memory */
	void *barmem;
	/* user context for callback, can be null */
	void *user_ctx;
	/* callback handler for processing request, called from octep_ctrl_mbox_recv */
	int (*process_req)(void *user_ctx, struct octep_ctrl_mbox_msg *msg);
	/* host-to-fw queue */
	struct octep_ctrl_mbox_q h2fq;
	/* fw-to-host queue */
	struct octep_ctrl_mbox_q f2hq;
	/* lock for h2fq */
	pthread_mutex_t h2fq_lock;
	/* lock for f2hq */
	pthread_mutex_t f2hq_lock;
};

/* Initialize control mbox.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_init(struct octep_ctrl_mbox *mbox);

/* Check if host driver is initialized and ready.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_is_host_ready(struct octep_ctrl_mbox *mbox);

/* Send mbox message.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg);

/* Retrieve mbox message.
 *
 * @param mbox: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg);

/* Uninitialize control mbox.
 *
 * @param ep: non-null pointer to struct octep_ctrl_mbox.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox);

#endif /* __OCTEP_CTRL_MBOX_H__ */
