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
#include "otx2.h"
#include "otx2_hw.h"

static off_t octep_drv_barmem_addr = 0;
static void* oei_trig_addr = NULL;

struct octep_ctrl_mbox otx2_mbox =  { 0 };

static int get_octep_drv_bar4_idx8_addr()
{
	uint64_t val;
	void* reg;
	int fd;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if(fd <= 0) {
		CP_LIB_LOG(INFO, OTX2, "Error mapping pem0 bar4 idx8\n");
		return -EINVAL;
	}

	reg = mmap(0, 8, PROT_READ, MAP_SHARED, fd, PEM0_BASE);
	if (reg == (void *)MAP_FAILED) {
		 CP_LIB_LOG(INFO, OTX2,
			    "pem0 base mmap error (%d) p:%llx\n",
			    errno, PEM0_BASE);
		close(fd);
		return -EINVAL;
	}
	close(fd);

	reg += BAR4_IDX8_OFFSET;
	val = cp_read64(reg);
	munmap(reg, 8);

	if (!(val & 1)) {
		CP_LIB_LOG(INFO, OTX2,
			   "Invalid pem0 bar4 idx8 value %lx\n", val);
		return -ENOMEM;
	}

	octep_drv_barmem_addr = ((val << 22) >> 4) & (~1);
	CP_LIB_LOG(INFO, OTX2, "bar4 addr 0x%lx\n", octep_drv_barmem_addr);

	return 0;
}

static int open_oei_trig_csr()
{
	int fd;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if(fd <= 0) {
		CP_LIB_LOG(INFO, OTX2, "Error mapping oei_trig_csr\n");
		return -EINVAL;
	}

	oei_trig_addr = mmap(0,
			     8,
			     PROT_READ | PROT_WRITE,
			     MAP_SHARED,
			     fd,
			     PEM0_OEI_TRIG);
	if (oei_trig_addr == (void *)MAP_FAILED) {
		CP_LIB_LOG(INFO, OTX2,
			   "oei_trig_csr mmap error (%d) p:%llx\n",
			   errno, PEM0_OEI_TRIG);
		close(fd);
		return -EINVAL;
	}
	close(fd);

	CP_LIB_LOG(INFO, OTX2, "oei_trig_addr %p\n", oei_trig_addr);

	return 0;
}

static int init_mbox(fn_otx2_process_req fn, void *mbox_user_ctx)
{
	int fd, err;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if(fd <= 0) {
		CP_LIB_LOG(INFO, OTX2, "Error while opening /dev/mem\n");
		return -EIO;
	}

	otx2_mbox.barmem_sz = OCTEP_DRV_BARMEM_LEN;
	otx2_mbox.barmem = mmap(0,
				OCTEP_DRV_BARMEM_LEN,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				fd,
				octep_drv_barmem_addr);
	if (otx2_mbox.barmem == (void *)MAP_FAILED) {
		CP_LIB_LOG(INFO, OTX2, "mmap error (%d) p:%lx, l:%lx\n",
			   errno, octep_drv_barmem_addr, OCTEP_DRV_BARMEM_LEN);
		close(fd);
		return -EIO;
	}
	close(fd);

	otx2_mbox.h2fq.elem_sz = sizeof(union octep_ctrl_net_h2f_data_sz);
	otx2_mbox.h2fq.elem_cnt = 64;
	otx2_mbox.h2fq.mask = 63;
	otx2_mbox.f2hq.elem_sz = sizeof(union octep_ctrl_net_f2h_data_sz);
	otx2_mbox.f2hq.elem_cnt = 64;
	otx2_mbox.f2hq.mask = 63;
	otx2_mbox.process_req = fn;
	otx2_mbox.user_ctx = mbox_user_ctx;

	err = octep_ctrl_mbox_init(&otx2_mbox);
	if (err) {
		CP_LIB_LOG(INFO, OTX2, "mbox init failed.\n");
		munmap(otx2_mbox.barmem, OCTEP_DRV_BARMEM_LEN);
	}

	return err;
}

int open_perst_uio()
{
	return 0;
}

int otx2_init(fn_otx2_process_req process_mbox, void *mbox_user_ctx)
{
	int err = 0;

	CP_LIB_LOG(INFO, OTX2, "init\n");

	err = get_octep_drv_bar4_idx8_addr();
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

	return 0;

perst_uio_fail:
	munmap(oei_trig_addr, 8);
oei_trig_fail:
	octep_ctrl_mbox_uninit(&otx2_mbox);
	munmap(otx2_mbox.barmem, OCTEP_DRV_BARMEM_LEN);
	return err;
}

int otx2_raise_oei_trig_interrupt()
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

int otx2_uninit()
{
	CP_LIB_LOG(INFO, OTX2, "uninit\n");
	if (otx2_mbox.barmem) {
		octep_ctrl_mbox_uninit(&otx2_mbox);
		munmap(otx2_mbox.barmem, OCTEP_DRV_BARMEM_LEN);
	}

	if (oei_trig_addr)
		munmap(oei_trig_addr, 8);

	return 0;
}
