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

#define PEM0_BASE			PEMX_BASE(0)
/* oei trig interrupt register */
#define PEM0_OEI_TRIG			0x86E0C0000000ull
#define PEM0_PF0_CSX_PFCFGX		PEMX_PFX_CSX_PFCFGX(0, 0, \
							    PCIEEP_VSECST_CTL)

#define FW_STATUS_READY			0x1ul
#define FW_STATUS_INVALID		0x0ul

#define MBOX_SZ				(size_t)(4 * 1024 * 1024)
#define MBOX_H2FQ_ELEM_CNT		64
#define MBOX_H2FQ_MASK			63
#define MBOX_F2HQ_ELEM_CNT		64
#define MBOX_F2HQ_MASK			63

static off_t mbox_bar4_addr = 0;
static void* oei_trig_addr = NULL;

struct octep_ctrl_mbox mbox =  { 0 };

static inline int is_cn10k()
{
	/* Support only otx2 until we add soc detection */
	//if (sdp->pdev->subsystem_device >= PCI_SUBSYS_DEVID_CN10K_A)
	//	return 1;

	return 0;
}

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

static int get_bar4_idx8_addr()
{
	uint64_t val;
	void* addr;

	addr = map_reg(0, 8, PROT_READ, MAP_SHARED, PEM0_BASE);
	if (!addr) {
		CP_LIB_LOG(INFO, CNXK, "Error mapping pem0 bar4 idx8\n");
		return -EIO;
	}
	val = cp_read64(addr + BAR4_IDX_OFFSET(8));
	munmap(addr, 8);
	if (!(val & 1)) {
		CP_LIB_LOG(INFO, CNXK,
			   "Invalid pem0 bar4 idx8 value %lx\n", val);
		return -ENOMEM;
	}

	mbox_bar4_addr = ((val << 22) >> 4) & (~1);
	CP_LIB_LOG(INFO, CNXK, "pem0 bar4 addr 0x%lx\n", mbox_bar4_addr);

	return 0;
}

static int open_oei_trig_csr()
{
	oei_trig_addr = map_reg(0,
				8,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				PEM0_OEI_TRIG);
	if (!oei_trig_addr) {
		CP_LIB_LOG(INFO, CNXK, "Error mapping pem0 oei_trig_csr\n");
		return -EINVAL;
	}
	CP_LIB_LOG(INFO, CNXK, "pem0 oei_trig_addr %p\n", oei_trig_addr);

	return 0;
}

static int init_mbox(fn_cnxk_process_req fn, void *mbox_user_ctx)
{
	int err;

	mbox.barmem = map_reg(0,
				   MBOX_SZ,
				   PROT_READ | PROT_WRITE,
				   MAP_SHARED,
				   mbox_bar4_addr);
	if (!mbox.barmem) {
		CP_LIB_LOG(INFO, CNXK, "Error allocating pem0 pf0 mbox.\n");
		return -ENOMEM;
	}
	mbox.barmem_sz = MBOX_SZ;
	mbox.h2fq.elem_sz = sizeof(union octep_ctrl_net_h2f_data_sz);
	mbox.h2fq.elem_cnt = MBOX_H2FQ_ELEM_CNT;
	mbox.h2fq.mask = MBOX_H2FQ_MASK;
	mbox.f2hq.elem_sz = sizeof(union octep_ctrl_net_f2h_data_sz);
	mbox.f2hq.elem_cnt = MBOX_F2HQ_ELEM_CNT;
	mbox.f2hq.mask = MBOX_F2HQ_MASK;
	mbox.process_req = fn;
	mbox.user_ctx = mbox_user_ctx;

	err = octep_ctrl_mbox_init(&mbox);
	if (err) {
		CP_LIB_LOG(INFO, CNXK, "pem0 pf0 mbox init failed.\n");
		munmap(mbox.barmem, MBOX_SZ);
	}

	return err;
}

static int open_perst_uio()
{
	return 0;
}

static int set_fw_ready(int status)
{
	uint64_t val;
	void* addr;
	off_t reg;

	reg = (is_cn10k()) ? PEM0_PF0_CSX_PFCFGX : PEMX_BASE(0);
	addr = map_reg(0, 8, PROT_READ | PROT_WRITE, MAP_SHARED, reg);
	if (!addr) {
		CP_LIB_LOG(INFO, CNXK,
			   "Error setting pem0 pf0 fw ready(%d).\n",
			   status);
		return -EIO;
	}
	/* Config space access different between otx2 and cn10k */
	if (is_cn10k()) {
		/* 8 byte mapping needed, both 32 bit addresses used */
		cp_write32(status, addr);
	} else {
		val = (((unsigned long long)status << PEMX_CFG_WR_DATA) |
		       (0 << PEMX_CFG_WR_PF) |
		       (1 << 15) |
		       (PCIEEP_VSECST_CTL << PEMX_CFG_WR_REG));
		cp_write64(val, addr + PEMX_CFG_WR_OFFSET);

		val = cp_read64(addr + PEMX_CFG_WR_OFFSET);
		CP_LIB_LOG(INFO, CNXK, "fw_ready %p = %lx\n",
			   addr + PEMX_CFG_WR_OFFSET, val);
		munmap(addr, 8);
	}
	munmap(addr, 8);

	return 0;
}

int cnxk_init(fn_cnxk_process_req process_mbox, void *mbox_user_ctx)
{
	int err = 0;

	CP_LIB_LOG(INFO, CNXK, "init\n");

	err = get_bar4_idx8_addr();
	if (err)
		return err;
	err = init_mbox(process_mbox, mbox_user_ctx);
	if (err)
		return err;
	err = open_oei_trig_csr();
	if (err)
		goto oei_trig_fail;
	err = open_perst_uio();
	if (err)
		goto perst_uio_fail;
	err = set_fw_ready(FW_STATUS_READY);
	if (err)
		goto fw_ready_fail;

	return err;

fw_ready_fail:
	//close uio
perst_uio_fail:
	munmap(oei_trig_addr, 8);
oei_trig_fail:
	octep_ctrl_mbox_uninit(&mbox);
	munmap(mbox.barmem, MBOX_SZ);
	return err;
}

int cnxk_raise_oei_trig_interrupt()
{
	union sdp_epf_oei_trig trig = { 0 };

	if (!oei_trig_addr)
		return -EAGAIN;

	trig.u64 = 0;
	trig.s.set = 1;
	trig.s.bit_num = 1;
	cp_write64(trig.u64, oei_trig_addr);

	return 0;
}

int cnxk_uninit()
{
	CP_LIB_LOG(INFO, CNXK, "uninit\n");

	set_fw_ready(FW_STATUS_INVALID);
	if (mbox.barmem) {
		octep_ctrl_mbox_uninit(&mbox);
		munmap(mbox.barmem, MBOX_SZ);
	}

	if (oei_trig_addr)
		munmap(oei_trig_addr, 8);

	//close uio

	return 0;
}
