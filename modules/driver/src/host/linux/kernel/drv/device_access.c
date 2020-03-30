/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host Device Access Driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "facility.h"
#include "device_access.h"

mv_facility_conf_t rpc_facility_conf;
mv_facility_conf_t nwa_facility_conf;

int mv_get_facility_handle(char *name)
{
	if (!strcmp(name, MV_FACILITY_NAME_RPC))
		return rpc_facility_conf.type;
	else if (!strcmp(name, MV_FACILITY_NAME_NETWORK_AGENT))
		return nwa_facility_conf.type;
	else
		return -ENOENT;
}
EXPORT_SYMBOL(mv_get_facility_handle);

int mv_get_bar_mem_map(int handle, mv_bar_map_t *bar_map)
{
	if (handle == MV_FACILITY_RPC) {
		bar_map->addr.host_addr = rpc_facility_conf.memmap.host_addr;
		bar_map->memsize = rpc_facility_conf.memsize;
	} else if (handle == MV_FACILITY_NW_AGENT) {
		bar_map->addr.host_addr = nwa_facility_conf.memmap.host_addr;
		bar_map->memsize = nwa_facility_conf.memsize;
	} else {
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL(mv_get_bar_mem_map);

int mv_pci_get_dma_dev(int handle, struct device **dev)
{
	if (handle == MV_FACILITY_RPC)
		*dev = rpc_facility_conf.dma_dev.host_ep_dev;
	else
		return -ENOENT;

	return 0;
}
EXPORT_SYMBOL(mv_pci_get_dma_dev);

int mv_get_num_dbell(int handle, enum mv_target target, uint32_t *num_dbells)
{
	if (handle == MV_FACILITY_RPC && target == TARGET_EP) {
		*num_dbells = rpc_facility_conf.num_h2t_dbells;
	} else {
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL(mv_get_num_dbell);

int mv_send_dbell(int handle, uint32_t dbell)
{
	int ret = 0;

	if (handle == MV_FACILITY_RPC) {
		ret = mv_send_facility_dbell(handle, dbell);
	} else {
		return -ENOENT;
	}

	return ret;
}
EXPORT_SYMBOL(mv_send_dbell);

int host_device_access_init(void)
{
	int ret = 0;

	ret = mv_get_facility_conf(MV_FACILITY_RPC, &rpc_facility_conf);
	if (ret < 0) {
		pr_err("Error: getting facility configuration %d failed %d\n",
		       MV_FACILITY_RPC, ret);
		return ret;
	}

	printk("	%s configuration\n"
		"Type = %d, Host Addr = 0x%llx Memsize = 0x%x\n"
		 "Doorbell_count = %d\n",
		 rpc_facility_conf.name,
		 rpc_facility_conf.type,
		 (u64)rpc_facility_conf.memmap.host_addr,
		 rpc_facility_conf.memsize,
		 rpc_facility_conf.num_h2t_dbells);

	ret = mv_get_facility_conf(MV_FACILITY_NW_AGENT, &nwa_facility_conf);
	if (ret < 0) {
		pr_err("Error: getting facility configuration %d failed %d\n",
		       MV_FACILITY_RPC, ret);
		return ret;
	}

	printk("	%s configuration\n"
		"Type = %d, Host Addr = 0x%llx Memsize = 0x%x\n"
		 "Doorbell_count = %d\n",
		 nwa_facility_conf.name,
		 nwa_facility_conf.type,
		 (u64)nwa_facility_conf.memmap.host_addr,
		 nwa_facility_conf.memsize,
		 nwa_facility_conf.num_h2t_dbells);
	return ret;
}
