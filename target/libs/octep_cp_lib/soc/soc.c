/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2022 Marvell.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "octep_cp_lib.h"
#include "cp_log.h"
#include "cp_lib.h"
#include "octep_ctrl_mbox.h"
#include "cnxk.h"
#include "cnxk_loop.h"
#include "cnxk_nic.h"

static struct cp_lib_soc_ops ops[CP_LIB_SOC_MAX][OCTEP_CP_MODE_MAX] = {
	/* cnxk */
	{
		{
			cnxk_loop_init,
			cnxk_loop_poll,
			cnxk_loop_process_sigusr1,
			cnxk_loop_uninit
		},
		{
			cnxk_nic_init,
			cnxk_nic_poll,
			cnxk_nic_process_sigusr1,
			cnxk_nic_uninit
		}
	},
	/* cnxk */
	{
		{
			cnxk_loop_init,
			cnxk_loop_poll,
			cnxk_loop_process_sigusr1,
			cnxk_loop_uninit
		},
		{
			cnxk_nic_init,
			cnxk_nic_poll,
			cnxk_nic_process_sigusr1,
			cnxk_nic_uninit
		}
	}
};

static enum cp_lib_soc detect_soc()
{
	return CP_LIB_SOC_OTX2;
}

int soc_get_ops(enum octep_cp_mode mode, struct cp_lib_soc_ops **p_ops)
{
	cfg.soc = detect_soc();
	if (cfg.soc >= CP_LIB_SOC_MAX || mode >= OCTEP_CP_MODE_MAX || !p_ops) {
		CP_LIB_LOG(INFO, SOC, "Invalid param "
			   "soc:%d mode:%d p_ops:%p\n", cfg.soc, mode, p_ops);
		return -EINVAL;
	}
	*p_ops = &ops[cfg.soc][mode];

	return 0;
}
