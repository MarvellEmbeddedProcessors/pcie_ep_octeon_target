/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
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
#define MBOX_SZ		(size_t)(PEMX_BAR4_INDEX_SIZE / OCTEP_CP_PF_PER_DOM_MAX)

#define PEM_BAR4_INDEX 8
#define PEM_BAR4_INDEX_SIZE 0x400000ULL
#define PEM_BAR4_INDEX_ADDR (PEM_BAR4_INDEX * PEM_BAR4_INDEX_SIZE)


struct cnxk_pf {
	/* pf is valid */
	bool valid;
	/* index of pf */
	unsigned long long idx;
	/* mapped bar4 memory slot address */
	uint64_t bar4_addr;
	/* address of oei_trig register for interrupts */
	void* oei_trig_addr;
	/* offset from mapped address where actual data starts */
	off_t oei_trig_offset;
	/* pf mbox */
	struct octep_ctrl_mbox mbox;
};

struct cnxk_pem {
	/* pem is valid*/
	bool valid;
	/* index of pem */
	unsigned long long idx;
	/* file descriptor for uio interrupt */
	int uio_fd;
	/* array of pf's */
	struct cnxk_pf pfs[OCTEP_CP_PF_PER_DOM_MAX];
};

static struct cnxk_pem pems[OCTEP_CP_DOM_MAX] = { 0 };

static inline void* map_reg(unsigned long long addr, size_t len, int prot,
			    off_t *offset)
{
	off_t pg_addr, pg_offset;
	long pg_sz;
	void* map;
	int fd;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if(fd <= 0)
		return NULL;

	pg_sz = sysconf(_SC_PAGESIZE);
	pg_addr = ((addr / pg_sz) * pg_sz);
	pg_offset = addr % pg_sz;
	map = mmap(0, (pg_offset + len), prot, MAP_SHARED, fd, pg_addr);
	if (map == (void *)MAP_FAILED) {
		CP_LIB_LOG(INFO, CNXK, "mmap[%llx] error (%d)\n",
			   addr, errno);
		close(fd);
		return NULL;
	}
	close(fd);

	if (offset)
		*offset = pg_offset;

	return (map + pg_offset);
}

static inline int unmap_reg(void* addr, off_t offset, size_t len)
{
	return munmap((addr - offset), (len + offset));
}

static int open_oei_trig_csr(struct cnxk_pem *pem, struct cnxk_pf *pf)
{
	pf->oei_trig_addr = map_reg(SDP0_EPFX_OEI_TRIG(pf->idx),
				    8,
				    PROT_READ | PROT_WRITE,
				    &pf->oei_trig_offset);
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

static int init_mbox(struct octep_cp_lib_cfg *cfg, struct cnxk_pem *pem,
		     struct cnxk_pf *pf)
{
	struct octep_ctrl_mbox *mbox;
	char memdev_name[32];
	int err;

	mbox = &pf->mbox;
	snprintf(memdev_name, 32, "/dev/pem%lld_ep_bar4_mem", pem->idx);
	mbox->bar4_fd = open(memdev_name, O_RDWR | O_SYNC);
	if(mbox->bar4_fd <= 0) {
		CP_LIB_LOG(ERR, CNXK,
			   "Error opening pem[%d] pf[%d] mbox file.\n",
			   pem->idx, pf->idx);
		return -ENOMEM;
	}

	mbox->min_version = cfg->min_version;
	mbox->max_version = cfg->max_version;
	mbox->barmem = pf->bar4_addr;
	mbox->barmem_sz = MBOX_SZ;
	err = octep_ctrl_mbox_init(mbox);
	if (err) {
		CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] mbox init failed.\n",
			   pem->idx, pf->idx);
		close(mbox->bar4_fd);
	}
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] control plane versions %x:%x\n",
		   pem->idx, pf->idx, cfg->min_version, cfg->max_version);
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] mbox h2fq sz %u addr %p\n",
		   pem->idx, pf->idx, mbox->h2fq.sz, mbox->h2fq.hw_q);
	CP_LIB_LOG(INFO, CNXK, "pem[%d] pf[%d] mbox f2hq sz %u addr %p\n",
		   pem->idx, pf->idx, mbox->f2hq.sz, mbox->f2hq.hw_q);

	return err;
}

static int set_fw_ready(struct cnxk_pem *pem, struct cnxk_pf *pf,
			unsigned long long status)
{
	off_t offset = 0;
	uint64_t val;
	void* addr;

	if (IS_SOC_CN10K) {
		/* for cn10k we map into pf0 only
		 *
		 * This register only supported on cn10k.
		 * The documentation for this register is not clear, and the current
		 * implementation works for 0x418, and should work for all multiple
		 * of 8 addresses.  It has not been tested for multiple of 4 addresses,
		 * nor for addresses with bit 16 set.
		 */
		addr = map_reg((PEMX_BASE(pem->idx) +
				(0x8000 | CN10K_PCIEEP_VSECST_CTL)),
			       8,
			       PROT_READ | PROT_WRITE,
			       &offset);
		if (!addr) {
			CP_LIB_LOG(INFO, CNXK,
				   "Error setting pem[%d] pf[%d] fw ready(%d).\n",
				   pem->idx, pf->idx, status);
			return -EIO;
		}
		cp_write32(status, addr);
		CP_LIB_LOG(INFO, CNXK,
			   "pem[%d] pf[%d] fw ready %lx addr %p\n",
			   pem->idx, pf->idx,
			   status, addr);
	} else {
		addr = map_reg((PEMX_BASE(pem->idx) + PEMX_CFG_WR_OFFSET),
			       8,
			       PROT_READ | PROT_WRITE,
			       &offset);
		if (!addr) {
			CP_LIB_LOG(INFO, CNXK,
				   "Error setting pem[%d] pf[%d] fw ready(%d).\n",
				   pem->idx, pf->idx, status);
			return -EIO;
		}
		val = ((status << PEMX_CFG_WR_DATA) |
		       (1 << 15) |
		       (PCIEEP_VSECST_CTL << PEMX_CFG_WR_REG) |
		       (pf->idx << PEMX_CFG_WR_PF));
		cp_write64(val, addr);
		cp_read64(addr);
		CP_LIB_LOG(INFO, CNXK,
			   "pem[%d] pf[%d] fw ready %lx addr %p\n",
			   pem->idx, pf->idx,
			   val, addr);
	}
	unmap_reg(addr, offset, 8);

	return 0;
}

static int init_pf(struct octep_cp_lib_cfg *cfg, struct cnxk_pem *pem,
		   struct cnxk_pf *pf)
{
	int err;

	pf->bar4_addr = PEMX_BAR4_INDEX_ADDR + (pf->idx * MBOX_SZ);
	err = init_mbox(cfg, pem, pf);
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
		close(pf->mbox.bar4_fd);
	}

	if (pf->oei_trig_addr)
		unmap_reg(pf->oei_trig_addr, pf->oei_trig_offset, 8);

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

static int check_pem_status(struct cnxk_pem *pem)
{
	int wait, ret = -EAGAIN;
	off_t offset = 0;
	uint64_t val;
	void* addr;

	addr = map_reg((PEMX_BASE(pem->idx) + PEMX_ON_OFFSET),
		       8,
		       PROT_READ,
		       &offset);
	if (!addr) {
		CP_LIB_LOG(ERR, CNXK, "Error mapping pem[%d] status\n",
			   pem->idx);
		return -EIO;
	}
	wait = 0;
	do {
		val = cp_read64(addr);
		if (val & PEMX_ON_PEMOOR) {
			ret = 0;
			break;
		}

		sleep(1);
		wait++;
	} while (wait < 10);

	unmap_reg(addr, offset, 8);

	if (ret < 0)
		CP_LIB_LOG(ERR, CNXK, "pem[%d] unavailable\n", pem->idx);

	return ret;
}

static int uninit_pem(struct cnxk_pem *pem)
{
	int j;

	if (pem->uio_fd)
		close(pem->uio_fd);

	for (j = 0; j < OCTEP_CP_PF_PER_DOM_MAX; j++) {
		if (pem->pfs[j].valid)
			uninit_pf(pem, &(pem->pfs[j]));
	}
	pem->valid = false;

	return 0;
}

static int init_pem(struct octep_cp_lib_cfg *cfg, struct cnxk_pem *pem,
		    struct octep_cp_dom_cfg *dom_cfg)
{
	struct octep_cp_pf_cfg *pf_cfg;
	struct cnxk_pf *pf;
	char uio_path[256];
	int err, j, fd;

	pem->idx = dom_cfg->idx;
	err = check_pem_status(pem);
	if (err < 0)
		return err;

	/* TODO: this is temporary mapping, currently there is no way to
	 * map a /dev/uioX entry to pem->idx, this needs to be fixed
	 * in the pem_ep driver. This code will have to iterate all /dev/uioX
	 * devices and identify correct device for the pem potentially
	 * based on some identifier in uio->name
	 */
	sprintf(uio_path, "/dev/uio%lld", pem->idx);
	fd = open(uio_path, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	pem->uio_fd = fd;
	for (j = 0; j < dom_cfg->npfs; j++) {
		pf_cfg = &dom_cfg->pfs[j];
		if (pf_cfg->idx >= OCTEP_CP_PF_PER_DOM_MAX) {
			CP_LIB_LOG(ERR, CNXK,
				   "Invalid pf[%d][%d] config index.\n",
				   dom_cfg->idx, pf_cfg->idx);
			err = -EINVAL;
			goto init_fail;
		}

		pf = &pem->pfs[j];
		pf->idx = pf_cfg->idx;
		err = init_pf(cfg, pem, pf);
		if (err) {
			err = -ENOLINK;
			goto init_fail;
		}
		pf->valid = true;
		pf_cfg->max_msg_sz = pf->mbox.h2fq.sz;
	}
	pem->valid = true;

	return 0;

init_fail:
	uninit_pem(pem);
	return -ENOLINK;
}

int cnxk_init(struct octep_cp_lib_cfg *cfg)
{
	struct octep_cp_dom_cfg *dom_cfg;
	int err = 0, i;

	CP_LIB_LOG(INFO, CNXK, "init\n");

	/* Initialize pf interfaces */
	memset(pems, 0, sizeof(pems[0]) * OCTEP_CP_DOM_MAX);
	for (i = 0; i < cfg->ndoms; i++) {
		dom_cfg = &cfg->doms[i];
		if (dom_cfg->idx >= OCTEP_CP_DOM_MAX) {
			CP_LIB_LOG(ERR, CNXK,
				   "Invalid pem[%d] config index.\n",
				   dom_cfg->idx);
			err = -EINVAL;
			goto init_fail;
		}

		err = init_pem(cfg, &pems[dom_cfg->idx], dom_cfg);
		if (err)
			goto init_fail;
	}

	return 0;

init_fail:
	for (i = 0; i < OCTEP_CP_DOM_MAX; i++)
		if (pems[i].valid)
			uninit_pem(&pems[i]);

	return err;
}

int cnxk_get_info(struct octep_cp_lib_info *info)
{
	struct octep_cp_dom_info *dom_info;
	struct octep_cp_pf_info *pf_info;
	struct cnxk_pem *pem;
	struct cnxk_pf *pf;
	int i, j;

	info->ndoms = 0;
	for (i = 0; i < OCTEP_CP_DOM_MAX; i++) {
		pem = &pems[i];
		if (!pem->valid)
			continue;

		dom_info = &info->doms[i];
		dom_info->idx = i;
		dom_info->npfs = 0;
		for (j = 0; j < OCTEP_CP_PF_PER_DOM_MAX; j++) {
			pf = &pem->pfs[j];
			if (!pf->valid)
				continue;

			pf_info = &dom_info->pfs[j];
			pf_info->idx = j;
			pf_info->max_msg_sz = pf->mbox.h2fq.sz;
			dom_info->npfs++;
		}
		info->ndoms++;
	}

	return 0;
}

static inline struct cnxk_pf* get_pf(int pem_idx, int pf_idx)
{
	if (pem_idx >= OCTEP_CP_DOM_MAX || pf_idx >= OCTEP_CP_PF_PER_DOM_MAX)
		return NULL;

	if (!pems[pem_idx].valid)
		return NULL;

	if (!pems[pem_idx].pfs[pf_idx].valid)
		return NULL;

	return &pems[pem_idx].pfs[pf_idx];
}

int cnxk_send_msg_resp(union octep_cp_msg_info *ctx,
		       struct octep_cp_msg *msgs,
		       int num)
{
	union octep_ctrl_mbox_msg_hdr *hdr;
	struct octep_cp_msg *msg;
	struct cnxk_pf *pf;
	int i, ret;

	pf = get_pf(ctx->s.pem_idx, ctx->s.pf_idx);
	if (!pf)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		msg = &msgs[i];
		hdr = (union octep_ctrl_mbox_msg_hdr *)&msg->info;
		hdr->s.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP;
		ret = octep_ctrl_mbox_send(&pf->mbox,
					   (struct octep_ctrl_mbox_msg *)msg,
					   1);
		if (ret < 0) {
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

int cnxk_send_notification(union octep_cp_msg_info *ctx,
			   struct octep_cp_msg* msg)
{
	union octep_ctrl_mbox_msg_hdr *hdr;
	struct cnxk_pf *pf;
	int ret;

	pf = get_pf(ctx->s.pem_idx, ctx->s.pf_idx);
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

int cnxk_recv_msg(union octep_cp_msg_info *ctx,
		  struct octep_cp_msg *msgs,
		  int num)
{
	struct cnxk_pf *pf;

	pf = get_pf(ctx->s.pem_idx, ctx->s.pf_idx);
	if (!pf)
		return -EINVAL;

	return octep_ctrl_mbox_recv(&pf->mbox,
				    (struct octep_ctrl_mbox_msg *)msgs,
				    num);
}

int cnxk_send_event(struct octep_cp_event_info *info)
{
	struct cnxk_pf *pf;

	if (info->e == OCTEP_CP_EVENT_TYPE_FW_READY) {
		pf = get_pf(info->u.fw_ready.dom_idx, info->u.fw_ready.pf_idx);
		if (!pf)
			return -EINVAL;

		return set_fw_ready(&pems[info->u.fw_ready.dom_idx],
				    pf,
				    (info->u.fw_ready.ready != 0));
	} else if (info->e == OCTEP_CP_EVENT_TYPE_HEARTBEAT) {
		pf = get_pf(info->u.hbeat.dom_idx, info->u.hbeat.pf_idx);
		if (!pf)
			return -EINVAL;

		return raise_oei_trig_int(pf, SDP_EPF_OEI_TRIG_BIT_HEARTBEAT);
	}

	return -EINVAL;
}

int cnxk_recv_event(struct octep_cp_event_info *info, int num)
{
	int i, n_ev, data, n;
	struct cnxk_pem *pem;

	for (i = 0, n_ev = 0; i < OCTEP_CP_DOM_MAX; i++) {
		pem = &pems[i];
		if (!pem->valid)
			continue;

		n = read(pem->uio_fd, &data, sizeof(int));
		if (n <= 0)
			continue;

		info[n_ev].e = OCTEP_CP_EVENT_TYPE_PERST;
		info[n_ev].u.perst.dom_idx = pem->idx;
		if (++n_ev >= num)
			break;
	}

	return n_ev;
}

int cnxk_uninit()
{
	int i;

	CP_LIB_LOG(INFO, CNXK, "uninit\n");

	for (i = 0; i < OCTEP_CP_DOM_MAX; i++)
		if (pems[i].valid)
			uninit_pem(&pems[i]);

	return 0;
}
