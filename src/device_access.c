/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NPU Device Access Driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "barmap.h"
#include "device_access.h"
#include "dma_api.h"

mv_facility_conf_t conf;
mv_facility_conf_t nwa_conf;
extern int irq_list[];
extern struct device *plat_dev;
static bool init_done = false;

int mv_get_facility_handle(char *name)
{
	if (strcmp(name, MV_FACILITY_NAME_RPC) &&
	    strcmp(name, MV_FACILITY_NAME_NETWORK_AGENT))
		return -ENOENT;

	if (!init_done)
		return -EAGAIN;

	if (!strcmp(name, MV_FACILITY_NAME_RPC))
		return conf.type;
	else
		return nwa_conf.type;
}
EXPORT_SYMBOL(mv_get_facility_handle);

int mv_get_bar_mem_map(int handle, mv_bar_map_t *bar_map)
{
	if (handle == MV_FACILITY_RPC) {
		bar_map->addr.target_addr = conf.memmap.target_addr;
		bar_map->memsize = conf.memsize;
	} else if (handle == MV_FACILITY_NW_AGENT) {
		bar_map->addr.target_addr = nwa_conf.memmap.target_addr;
		bar_map->memsize = nwa_conf.memsize;
	} else {
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL(mv_get_bar_mem_map);

int mv_pci_get_dma_dev(int handle, struct device **dev)
{
	if (handle == MV_FACILITY_RPC)
		*dev = get_dpi_dma_dev(handle);
	else
		return -ENOENT;

	return 0;
}
EXPORT_SYMBOL(mv_pci_get_dma_dev);

int mv_get_num_dbell(int handle, enum mv_target target, uint32_t *num_dbells)
{
	if (handle == MV_FACILITY_RPC && target == MV_TARGET_EP)
		*num_dbells = conf.num_h2t_dbells;
	else
		return -ENOENT;

	return 0;
}
EXPORT_SYMBOL(mv_get_num_dbell);

int mv_request_dbell_irq(int handle, u32 dbell, irq_handler_t irq_handler,
			 void *arg)
{
	struct device *dev = plat_dev;
	char irq_name[32];
	int ret = 0, irq;

	if (handle == MV_FACILITY_RPC) {
		if (dbell >= MV_FACILITY_RPC_IRQ_CNT) {
			pr_err("RPC request irq %d is out of range\n", dbell);
			return -ENOENT;
		}

		sprintf(irq_name, "rpc_irq%d", dbell);
		irq = irq_list[NPU_FACILITY_RPC_IRQ_IDX+dbell];
		printk("registering irq %d\n", irq);

		ret = devm_request_irq(dev, irq, irq_handler, 0, irq_name, arg);
		if (ret < 0)
			return ret;
	} else {
		return -ENOENT;
	}

	return ret;
}
EXPORT_SYMBOL(mv_request_dbell_irq);

int mv_dbell_enable(int handle, uint32_t dbell)
{
	return 0;
}
EXPORT_SYMBOL_GPL(mv_dbell_enable);

int mv_dbell_disable(int handle, uint32_t dbell)
{
	return 0;
}
EXPORT_SYMBOL_GPL(mv_dbell_disable);

int mv_dbell_disable_nosync(int handle, uint32_t dbell)
{
	return 0;
}
EXPORT_SYMBOL_GPL(mv_dbell_disable_nosync);

int mv_free_dbell_irq(int handle, uint32_t dbell, void *arg)
{
	struct device *dev = plat_dev;
	int irq;

	if (handle == MV_FACILITY_RPC) {
		if (dbell >= MV_FACILITY_RPC_IRQ_CNT) {
			pr_err("RPC free irq %d is out of range\n", dbell);
			return -ENOENT;
		}

		irq = irq_list[NPU_FACILITY_RPC_IRQ_IDX+dbell];
		devm_free_irq(dev, irq, arg);
	} else {
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL(mv_free_dbell_irq);

int mv_send_dbell(int handle __attribute__((unused)),
		  uint32_t dbell __attribute__((unused)))
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL(mv_send_dbell);

int mv_pci_sync_dma(int handle, host_dma_addr_t host, void *ep_addr,
		    dma_addr_t target, enum mv_dma_dir direction, u32 size)
{
	return do_dma_sync(target, host, ep_addr, size, direction);
}
EXPORT_SYMBOL(mv_pci_sync_dma);

int npu_device_access_init(void)
{
	int ret = 0;

	ret = mv_get_facility_conf(MV_FACILITY_RPC, &conf);
	if (ret < 0) {
		pr_err("Error: getting facility configuration %d failed %d\n",
		       MV_FACILITY_RPC, ret);
		return ret;
	}

	printk("	%s configuration\n"
	       "Type = %d, Host Addr = 0x%llx Memsize = 0x%x\n"
	       "Doorbell count = %d\n",
	       conf.name, conf.type, (u64)conf.memmap.target_addr, conf.memsize,
	       conf.num_h2t_dbells);

	ret = mv_get_facility_conf(MV_FACILITY_NW_AGENT, &nwa_conf);
	if (ret < 0) {
		pr_err("Error: getting facility configuration %d failed %d\n",
		       MV_FACILITY_NW_AGENT, ret);
		return ret;
	}

	printk("	%s configuration\n"
	       "Type = %d, Host Addr = 0x%llx Memsize = 0x%x\n"
	       "Doorbell count = %d\n",
	       nwa_conf.name, nwa_conf.type, (u64)nwa_conf.memmap.target_addr,
	       nwa_conf.memsize, nwa_conf.num_h2t_dbells);
	init_done = true;
	return ret;
}
