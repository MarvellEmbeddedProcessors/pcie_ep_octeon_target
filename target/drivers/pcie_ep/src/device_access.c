/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

/*
 * NPU Device Access Driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "barmap.h"
#include "device_access.h"
#include "ep_base.h"
#include "dma_api.h"

extern struct otx_pcie_ep *g_pcie_ep_dev[];
static bool init_done = false;
static int facility_instance_cnt;

int mv_get_facility_instance_count(char *name)
{
	/* For now return the instance count for any name */
	return facility_instance_cnt;
}
EXPORT_SYMBOL(mv_get_facility_instance_count);

int mv_get_multi_facility_handle(int instance, char *name)
{
	int type, handle;

	if (instance >= facility_instance_cnt)
		return -ENOENT;

	if (strcmp(name, MV_FACILITY_NAME_RPC) &&
	    strcmp(name, MV_FACILITY_NAME_NETWORK_AGENT))
		return -ENOENT;

	if (!init_done)
		return -EAGAIN;

	if (!strcmp(name, MV_FACILITY_NAME_RPC))
		type = MV_FACILITY_RPC;
	else
		type = MV_FACILITY_NW_AGENT;

	handle = (uint8_t)(g_pcie_ep_dev[instance]->facility_conf[type].type) & 0xf;
	handle |= (uint8_t)(g_pcie_ep_dev[instance]->instance) << 4;

	return handle;
}
EXPORT_SYMBOL(mv_get_multi_facility_handle);

int mv_get_facility_handle(char *name)
{
	int type;

	if (strcmp(name, MV_FACILITY_NAME_RPC) &&
	    strcmp(name, MV_FACILITY_NAME_NETWORK_AGENT))
		return -ENOENT;

	if (!init_done)
		return -EAGAIN;

	if (!strcmp(name, MV_FACILITY_NAME_RPC))
		type = MV_FACILITY_RPC;
	else
		type = MV_FACILITY_NW_AGENT;

	return (g_pcie_ep_dev[0]->facility_conf[type].type);
}
EXPORT_SYMBOL(mv_get_facility_handle);

int mv_get_bar_mem_map(int handle, mv_bar_map_t *bar_map)
{
	int instance, type;
	mv_facility_conf_t *conf;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	if (type >= MV_FACILITY_COUNT)
		return -ENOENT;

	conf = &g_pcie_ep_dev[instance]->facility_conf[type];

	bar_map->addr.target_addr = conf->memmap.target_addr;
	bar_map->memsize = conf->memsize;

	return 0;
}
EXPORT_SYMBOL(mv_get_bar_mem_map);

int mv_pci_get_dma_dev_count(int handle)
{
	int instance, type;
	mv_facility_conf_t *conf;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	if (type >= MV_FACILITY_COUNT)
		return -EINVAL;

	conf = &g_pcie_ep_dev[instance]->facility_conf[type];
	return conf->num_dma_dev;
}
EXPORT_SYMBOL(mv_pci_get_dma_dev_count);

int mv_pci_get_dma_dev(int handle, int index, struct device **dev)
{
	int instance, type;
	mv_facility_conf_t *conf;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	if (type >= MV_FACILITY_COUNT)
		return -ENOENT;

	conf = &g_pcie_ep_dev[instance]->facility_conf[type];
	if (index >= conf->num_dma_dev)
		return -EINVAL;

	*dev = get_dpi_dma_dev(type, index);
	return (*dev != NULL) ? 0 : -ENODEV;
}
EXPORT_SYMBOL(mv_pci_get_dma_dev);

int mv_get_num_dbell(int handle, enum mv_target target, uint32_t *num_dbells)
{
	int instance, type;
	mv_facility_conf_t *conf;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	if (type >= MV_FACILITY_COUNT)
		return -ENOENT;

	conf = &g_pcie_ep_dev[instance]->facility_conf[type];
	if (type == MV_FACILITY_RPC && target == MV_TARGET_EP)
		*num_dbells = conf->num_h2t_dbells;
	else
		return -ENOENT;

	return 0;
}
EXPORT_SYMBOL(mv_get_num_dbell);

int mv_request_dbell_irq(int handle, u32 dbell, irq_handler_t irq_handler,
			 void *arg, const struct cpumask *cpumask)
{
	struct device *dev;
	char irq_name[32], msg[128];
	int ret = 0, cpu, n;
	int instance, type;
	struct npu_irq_info *irq_info;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	if (type >= MV_FACILITY_COUNT)
		return -ENOENT;

	dev = g_pcie_ep_dev[instance]->plat_dev;
	if (type == MV_FACILITY_RPC) {
		irq_info = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_RPC_IRQ_IDX + dbell];
		if (dbell >= MV_FACILITY_RPC_IRQ_CNT) {
			pr_err("RPC request irq %d is out of range\n", dbell);
			return -ENOENT;
		}

		sprintf(irq_name, "rpc_irq%d", dbell);
		ret = devm_request_irq(dev, irq_info->irq, irq_handler, 0, irq_name, arg);
		if (ret < 0)
			return ret;

		n = snprintf(msg, 128, "%s : registered irq %d on core ",
				irq_name, irq_info->irq);
		irq_info->cpumask = cpumask;
		if (cpumask != NULL) {
			irq_set_affinity_hint(irq_info->irq, cpumask);
			for_each_cpu(cpu, cpumask)
				n += snprintf((msg + n), 128, "%d ", cpu);
		} else {
			n += snprintf((msg + n), 128, "0");
		}
		snprintf((msg + n), 128, "\n");
		pr_info("%s", msg);
		disable_irq(irq_info->irq);
	} else {
		return -ENOENT;
	}

	return ret;
}
EXPORT_SYMBOL(mv_request_dbell_irq);

int mv_dbell_enable(int handle, uint32_t dbell)
{
	int instance, type;
	struct npu_irq_info *irq_info;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	irq_info = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_RPC_IRQ_IDX + dbell];
	enable_irq(irq_info->irq);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_dbell_enable);

int mv_dbell_disable(int handle, uint32_t dbell)
{
	int instance, type;
	struct npu_irq_info *irq_info;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	irq_info = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_RPC_IRQ_IDX + dbell];
	disable_irq(irq_info->irq);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_dbell_disable);

int mv_dbell_disable_nosync(int handle, uint32_t dbell)
{
	int instance, type;
	struct npu_irq_info *irq_info;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	irq_info = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_RPC_IRQ_IDX + dbell];
	disable_irq_nosync(irq_info->irq);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_dbell_disable_nosync);

int mv_free_dbell_irq(int handle, uint32_t dbell, void *arg)
{
	struct device *dev;
	struct npu_irq_info *ii;
	int instance, type;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);
	ii = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_RPC_IRQ_IDX + dbell];
	dev = g_pcie_ep_dev[instance]->plat_dev;

	if (type == MV_FACILITY_RPC) {
		if (dbell >= MV_FACILITY_RPC_IRQ_CNT) {
			pr_err("RPC free irq %d is out of range\n", dbell);
			return -ENOENT;
		}

		irq_set_affinity_hint(ii->irq, NULL);
		devm_free_irq(dev, ii->irq, arg);
		ii->cpumask = NULL;
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

int mv_pci_sync_dma(int handle, struct device *dev, host_dma_addr_t host,
					void *ep_addr, dma_addr_t target,
					enum mv_dma_dir direction, u32 size)
{
	return do_dma_sync(dev, target, host, ep_addr, size, direction);
}
EXPORT_SYMBOL(mv_pci_sync_dma);

void npu_device_access_init(int instance)
{
	mv_facility_conf_t *conf;

	conf = &g_pcie_ep_dev[instance]->facility_conf[MV_FACILITY_RPC];
	printk("	%s configuration\n"
	       "Type = %d, Host Addr = 0x%llx Memsize = 0x%x\n"
	       "Doorbell count = %d\n",
	       conf->name, conf->type, (u64)conf->memmap.target_addr, conf->memsize,
	       conf->num_h2t_dbells);

	conf = &g_pcie_ep_dev[instance]->facility_conf[MV_FACILITY_NW_AGENT];
	printk("	%s configuration\n"
	       "Type = %d, Host Addr = 0x%llx Memsize = 0x%x\n"
	       "Doorbell count = %d\n",
	       conf->name, conf->type, (u64)conf->memmap.target_addr,
	       conf->memsize, conf->num_h2t_dbells);

	init_done = true;
	facility_instance_cnt++;
}
