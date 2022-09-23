/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "octep_ctrl_mbox.h"
#include "octep_ctrl_net.h"

#include "octep_cp_lib.h"
#include "cp_compat.h"
#include "cp_log.h"
#include "cp_lib.h"
#include "cnxk.h"
#include "cnxk_hw.h"

/* library defines OCTEP_CP_PF_PER_DOM_MAX pf's per pem,
 * there are 16 4mb slots in bar4, we assign 1 slot per pem,
 * so each pf will get 4mb/OCTEP_CP_PF_PER_DOM_MAX = 32768 bytes for mbox.
 */
#define MBOX_SZ		(size_t)(0x400000 / OCTEP_CP_PF_PER_DOM_MAX)

struct cnxk_pf {
	/* index of pf */
	unsigned long long idx;
	/* mapped bar4 memory slot address */
	uint64_t bar4_addr;
	/* address of oei_trig register for interrupts */
	void* oei_trig_addr;
	/* pf mbox */
	struct octep_ctrl_mbox mbox;
};

struct cnxk_pem {
	/* index of pem */
	unsigned long long idx;
	/* index of 4mb bar4 memory slot */
	int bar4_slot;
	/* number of configured pf's */
	int npfs;
	/* array of pf's */
	struct cnxk_pf *pfs;
};

static int npems;
static struct cnxk_pem pems[OCTEP_CP_DOM_MAX];

static inline void* map_reg(void* addr, size_t len, int prot, int flags,
			    off_t offset)
{
	void* map;
	int fd;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if(fd <= 0)
		return NULL;

	map = mmap(addr, len, prot, flags, fd, offset);
	if (map == (void *)MAP_FAILED) {
		 CP_LIB_LOG(INFO, CNXK, "mmap[%llx:%llx] error (%d)\n",
			    addr, offset, errno);
		close(fd);
		return NULL;
	}
	close(fd);

	return map;
}

static int get_bar4_idx8_addr(struct cnxk_pem *pem, struct cnxk_pf *pf)
{
	uint64_t val;
	void* addr;

	addr = map_reg(0, 8, PROT_READ, MAP_SHARED, PEMX_BASE(pem->idx));
	if (!addr) {
		CP_LIB_LOG(INFO, CNXK, "Error mapping pem[%d] bar4 idx8\n",
			   pem->idx);
		return -EIO;
	}
	val = cp_read64(addr + BAR4_INDEX(8));
	munmap(addr, 8);
	if (!(val & 1)) {
		CP_LIB_LOG(INFO, CNXK,
			   "Invalid pem[%d] pf[%d] bar4 idx8 value %lx\n",
			   pem->idx, pf->idx, val);
		return -ENOMEM;
	}

	val += (pf->idx * MBOX_SZ);
	pf->bar4_addr = (((val & (~1)) >> 4) << 22);
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] bar4 idx8 addr 0x%lx\n",
		   pem->idx, pf->idx, pf->bar4_addr);

	return 0;
}

static int open_oei_trig_csr(struct cnxk_pem *pem, struct cnxk_pf *pf)
{
	pf->oei_trig_addr = map_reg(0,
				    8,
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED,
				    SDP0_EPFX_OEI_TRIG(pf->idx));
	if (!pf->oei_trig_addr) {
		CP_LIB_LOG(INFO, CNXK,
			   "Error mapping pem[%d] pf[%d] oei_trig_addr(%p)\n",
			   pem->idx, pf->idx, SDP0_EPFX_OEI_TRIG(pf->idx));
		return -EIO;
	}
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] oei_trig_addr %p\n",
		   pem->idx, pf->idx, pf->oei_trig_addr);

	return 0;
}

static int init_mbox(struct cnxk_pem *pem, struct cnxk_pf *pf)
{
	struct octep_ctrl_mbox *mbox;
	int err;

	mbox = &pf->mbox;
	mbox->barmem = map_reg(0,
			       MBOX_SZ,
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED,
			       pf->bar4_addr);
	if (!mbox->barmem) {
		CP_LIB_LOG(INFO, CNXK,
			   "Error allocating pem[%d] pf[%d] mbox.\n",
			   pem->idx, pf->idx);
		return -ENOMEM;
	}
	mbox->barmem_sz = MBOX_SZ;

	err = octep_ctrl_mbox_init(mbox);
	if (err) {
		CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] mbox init failed.\n",
			   pem->idx, pf->idx);
		munmap(mbox->barmem, MBOX_SZ);
	}
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] mbox h2fq sz %u addr %p\n",
		   pem->idx, pf->idx, mbox->h2fq.sz, mbox->h2fq.hw_q);
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] mbox f2hq sz %u addr %p\n",
		   pem->idx, pf->idx, mbox->f2hq.sz, mbox->f2hq.hw_q);

	return err;
}

static int open_perst_uio()
{
	return 0;
}

static int set_fw_ready(struct cnxk_pem *pem, struct cnxk_pf *pf,
			unsigned long long status)
{
	uint64_t val;
	void* addr;
	off_t reg;

	/* for cn10k we map into pf0 only */
	reg = (IS_SOC_CN10K) ?
	       PEMX_PFX_CSX_PFCFGX(pem->idx, 0, CN10K_PCIEEP_VSECST_CTL) :
	       PEMX_BASE(pem->idx);
	addr = map_reg(0, 8, PROT_READ | PROT_WRITE, MAP_SHARED, reg);
	if (!addr) {
		CP_LIB_LOG(INFO, CNXK,
			   "Error setting pem[%d] [%p] fw ready(%d).\n",
			   pem->idx, reg, status);
		return -EIO;
	}

	if (IS_SOC_CN10K) {
		/* 8 byte mapping needed, both 32 bit addresses used */
		cp_write32(status, addr);
	} else {
		addr += PEMX_CFG_WR_OFFSET;
		val = ((status << PEMX_CFG_WR_DATA) |
		       (1 << 15) |
		       (PCIEEP_VSECST_CTL << PEMX_CFG_WR_REG));

		cp_write64((val | (pf->idx << PEMX_CFG_WR_PF)), addr);
		cp_read64(addr);
		CP_LIB_LOG(INFO, CNXK,
			   "pem[%d] pf[%d] fw ready %lx addr %p\n",
			   pem->idx, pf->idx,
			   val, addr + PEMX_CFG_WR_OFFSET);
	}
	munmap(addr, 8);

	return 0;
}

static int init_pf(struct cnxk_pem *pem, struct cnxk_pf *pf)
{
	int err;

	err = get_bar4_idx8_addr(pem, pf);
	if (err)
		return err;
	err = init_mbox(pem, pf);
	if (err)
		return err;
	err = open_oei_trig_csr(pem, pf);
	if (err)
		return err;

	return 0;
}

static int uninit_pf(struct cnxk_pem *pem, struct cnxk_pf *pf)
{
	if (pf->mbox.barmem) {
		octep_ctrl_mbox_uninit(&pf->mbox);
		munmap(pf->mbox.barmem, MBOX_SZ);
	}

	if (pf->oei_trig_addr)
		munmap(pf->oei_trig_addr, 8);

	return 0;
}

static int raise_oei_trig_int(struct cnxk_pf *pf, enum sdp_epf_oei_trig_bit bit)
{
	union sdp_epf_oei_trig trig = { 0 };

	if (!pf->oei_trig_addr)
		return -EIO;;

	/* As of now we only support sending heartbeat */
	trig.u64 = 0;
	trig.s.set = 1;
	trig.s.bit_num = bit;
	cp_write64(trig.u64, pf->oei_trig_addr);

	return 0;
}

int cnxk_init(struct octep_cp_lib_cfg *cfg)
{
	int err = 0, i, j;
	struct octep_cp_dom_cfg *dom_cfg;
	struct octep_cp_pf_cfg *pf_cfg;
	struct cnxk_pem *pem;
	struct cnxk_pf *pf;

	CP_LIB_LOG(INFO, CNXK, "init\n");

	/* Initialize pf interfaces */
	memset(pems, 0, sizeof(pems));
	npems = cfg->ndoms;
	for (i=0; i<npems; i++) {
		dom_cfg = &cfg->doms[i];
		pem = &pems[i];
		pem->idx = dom_cfg->idx;
		pem->npfs = dom_cfg->npfs;
		pem->pfs = calloc(pem->npfs, sizeof(struct cnxk_pf));
		if (!pem->pfs) {
			err = -ENOMEM;
			goto pf_alloc_fail;
		}
		for (j=0; j<dom_cfg->npfs; j++) {
			pf_cfg = &dom_cfg->pfs[j];
			pf = &pem->pfs[j];
			pf->idx = pf_cfg->idx;
			err = init_pf(pem, pf);
			if (err) {
				err = -ENOLINK;
				goto init_pf_fail;
			}
			pf_cfg->max_msg_sz = pf->mbox.h2fq.sz;
		}
	}

	err = open_perst_uio();
	if (err)
		goto perst_uio_fail;

	return 0;

perst_uio_fail:
	//close uio
init_pf_fail:
	for (i=0; i<npems; i++) {
		if (!pems[i].pfs)
			continue;
		for (j=0; j<dom_cfg->npfs; j++)
			uninit_pf(&pems[i], &(pems[i].pfs[j]));
	}
pf_alloc_fail:
	for (i=0; i<npems; i++) {
		if (!pems[i].pfs)
			continue;
		free(pems[i].pfs);
		pems[i].npfs = 0;
	}
	npems = 0;

	return err;
}

static struct cnxk_pf *get_pf(struct cnxk_pem *pem, int idx)
{
	int i;

	for (i=0; i<pem->npfs; i++) {
		if (pem->pfs[i].idx == idx)
			return &pem->pfs[i];
	}

	return NULL;
}

int cnxk_send_msg_resp(uint64_t *ctx, struct octep_cp_msg *msgs, int num)
{
	union octep_ctrl_mbox_msg_hdr *hdr;
	union octep_cp_msg_info *info;
	struct octep_cp_msg *msg;
	struct cnxk_pem *pem;
	struct cnxk_pf *pf;
	int i, ret;

	for (i=0; i<num; i++) {
		msg = &msgs[i];
		info = &msg->info;
		if (info->s.pem_idx >= npems) {
			/* this is first msg */
			if (i == 0)
				return -EINVAL;

			/* we have sent some msgs successfully so break */
			break;
		}

		pem = &pems[info->s.pem_idx];
		pf = get_pf(pem, info->s.pf_idx);
		if (!pf) {
			/* this is first msg */
			if (i == 0)
				return -EINVAL;

			/* we have sent some msgs successfully so break */
			break;
		}

		hdr = (union octep_ctrl_mbox_msg_hdr *)info;
		hdr->s.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
		ret = octep_ctrl_mbox_send(&pf->mbox,
					   (struct octep_ctrl_mbox_msg *)msg,
					   1);
		if (ret == 1)
			continue;
		else if (ret < 0) {
			/* error while sending first msg */
			if (i == 0)
				return ret;

			/* we have sent some msgs successfully so break */
			break;
		}
	}
	if (i)
		raise_oei_trig_int(pf, SDP_EPF_OEI_TRIG_BIT_MBOX);

	return i;
}

int cnxk_send_notification(struct octep_cp_msg* msg)
{
	union octep_ctrl_mbox_msg_hdr *hdr;
	struct cnxk_pem *pem;
	struct cnxk_pf *pf;
	int ret;

	/* Assume all messages are going to same pf/vf */
	if (msg->info.s.pem_idx >= npems)
		return -EINVAL;

	pem = &pems[msg->info.s.pem_idx];
	pf = get_pf(pem, msg->info.s.pf_idx);
	if (!pf)
		return -EINVAL;

	hdr = (union octep_ctrl_mbox_msg_hdr *)&msg->info;
	hdr->s.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY;
	ret = octep_ctrl_mbox_send(&pf->mbox,
				   (struct octep_ctrl_mbox_msg *)msg,
				   1);
	if (ret < 0)
		return ret;

	raise_oei_trig_int(pf, SDP_EPF_OEI_TRIG_BIT_MBOX);

	return 0;
}

int cnxk_recv_msg(uint64_t *ctx, struct octep_cp_msg *msgs, int num)
{
	union octep_cp_msg_info *info;
	struct octep_cp_msg *msg;
	struct cnxk_pem *pem;
	struct cnxk_pf *pf;
	int i, ret;

	for (i=0; i<num; i++) {
		msg = &msgs[i];
		info = &msg->info;
		if (info->s.pem_idx >= npems) {
			/* this is first msg */
			if (i == 0)
				return -EINVAL;

			/* we have received some msgs successfully so break */
			break;
		}

		pem = &pems[info->s.pem_idx];
		pf = get_pf(pem, info->s.pf_idx);
		if (!pf) {
			/* this is first msg */
			if (i == 0)
				return -EINVAL;

			/* we have received some msgs successfully so break */
			break;
		}

		ret = octep_ctrl_mbox_recv(&pf->mbox,
					   (struct octep_ctrl_mbox_msg *)msg,
					   1);
		if (ret < 0) {
			/* error while sending first msg */
			if (i == 0)
				return ret;

			/* we have received some msgs successfully so break */
			break;
		}
		if (ret == 0)
			break;

		ctx[i] = info->words[1];
	}

	return i;
}

int cnxk_send_event(struct octep_cp_event_info *info)
{
	struct cnxk_pem *pem;
	struct cnxk_pf *pf;

	if (info->e == OCTEP_CP_EVENT_TYPE_FW_READY) {
		if (info->u.fw_ready.dom_idx >= npems)
			return -EINVAL;

		pem = &pems[info->u.fw_ready.dom_idx];
		pf = get_pf(pem, info->u.fw_ready.pf_idx);
		if (!pf)
			return -EINVAL;

		return set_fw_ready(pem, pf, (info->u.fw_ready.ready != 0));
	} else if (info->e == OCTEP_CP_EVENT_TYPE_HEARTBEAT) {
		if (info->u.hbeat.dom_idx >= npems)
			return -EINVAL;

		pem = &pems[info->u.hbeat.dom_idx];
		pf = get_pf(pem, info->u.hbeat.pf_idx);
		if (!pf)
			return -EINVAL;

		return raise_oei_trig_int(pf, SDP_EPF_OEI_TRIG_BIT_HEARTBEAT);
	}

	return -EINVAL;
}

int cnxk_recv_event(struct octep_cp_event_info *info, int num)
{
	//poll uio
	return 0;
}

int cnxk_uninit()
{
	int i, j;

	CP_LIB_LOG(INFO, CNXK, "uninit\n");

	for (i=0; i<npems; i++) {
		if (pems[i].pfs) {
			for (j=0; j<pems[i].npfs; j++) {
				uninit_pf(&pems[i], &(pems[i].pfs[j]));
			}
			free(pems[i].pfs);
		}
		pems[i].npfs = 0;
	}
	npems = 0;

	//close uio

	return 0;
}
