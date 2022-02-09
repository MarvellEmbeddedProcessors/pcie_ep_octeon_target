/* Copyright (c) 2020 Marvell.
 * SPDX-License-Identifier: GPL-2.0
 */

//#include <string.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "barmap.h"
#include "facility.h"
#include "ep_base.h"
#include "dma_api.h"

extern void *oei_trig_remap_addr;
extern struct otx_pcie_ep *g_pcie_ep_dev[];

void mv_dump_facility_conf(mv_facility_conf_t *conf)
{
	printk("##### Target facility \"%s\" configuration #####\n", conf->name);
	printk("BAR memory virt addr= %p; size=%u\n",
	       conf->memmap.target_addr, conf->memsize);
	printk("h2t_dbells = %u; t2h_dbells = %u; dma_devs = %u\n",
	       conf->num_h2t_dbells, conf->num_t2h_dbells, conf->num_dma_dev);
}

//void mv_facility_conf_init(void *mapaddr, struct npu_bar_map *bar_map)
void mv_facility_conf_init(struct otx_pcie_ep *pcie_ep)
{
	struct facility_bar_map *facility_map;
	void *mapaddr = pcie_ep->npu_barmap_mem;
	int handle;

	memset(&pcie_ep->facility_conf, 0,
	       sizeof(mv_facility_conf_t) * MV_FACILITY_COUNT);

	/* TODO: set name for all facility names */
	facility_map = &pcie_ep->bar_map.facility_map[MV_FACILITY_CONTROL];
	handle = (pcie_ep->instance << 4) | HANDLE_TYPE_CONTROL;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].type = MV_FACILITY_CONTROL;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].dma_dev.target_dma_dev =
			get_dpi_dma_dev(handle, 0);
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].num_t2h_dbells = 0;
	strncpy(pcie_ep->facility_conf[MV_FACILITY_CONTROL].name,
		MV_FACILITY_NAME_CONTROL, FACILITY_NAME_LEN-1);
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].num_dma_dev =
			get_dpi_dma_dev_count(handle);

	facility_map = &pcie_ep->bar_map.facility_map[MV_FACILITY_MGMT_NETDEV];
	handle = (pcie_ep->instance << 4) | HANDLE_TYPE_MGMT_NETDEV;
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].type = MV_FACILITY_MGMT_NETDEV;
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].dma_dev.target_dma_dev =
			get_dpi_dma_dev(handle, 0);
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].num_t2h_dbells = 0;
	strncpy(pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].name,
		MV_FACILITY_NAME_MGMT_NETDEV, FACILITY_NAME_LEN-1);
	pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV].num_dma_dev =
			get_dpi_dma_dev_count(handle);

	facility_map = &pcie_ep->bar_map.facility_map[MV_FACILITY_NW_AGENT];
	handle = (pcie_ep->instance << 4) | HANDLE_TYPE_NW_AGENT;
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].type = MV_FACILITY_NW_AGENT;
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].dma_dev.target_dma_dev =
			get_dpi_dma_dev(handle, 0);
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].num_t2h_dbells = 0;
	strncpy(pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].name,
		MV_FACILITY_NAME_NETWORK_AGENT, FACILITY_NAME_LEN-1);
	pcie_ep->facility_conf[MV_FACILITY_NW_AGENT].num_dma_dev =
			get_dpi_dma_dev_count(handle);

	facility_map = &pcie_ep->bar_map.facility_map[MV_FACILITY_RPC];
	handle = (pcie_ep->instance << 4) | HANDLE_TYPE_RPC;
	pcie_ep->facility_conf[MV_FACILITY_RPC].type = MV_FACILITY_RPC;
	pcie_ep->facility_conf[MV_FACILITY_RPC].dma_dev.target_dma_dev =
			get_dpi_dma_dev(handle, 0);
	pcie_ep->facility_conf[MV_FACILITY_RPC].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	pcie_ep->facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_RPC].memsize = facility_map->size;
	pcie_ep->facility_conf[MV_FACILITY_RPC].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	pcie_ep->facility_conf[MV_FACILITY_RPC].num_t2h_dbells = 0;
	strncpy(pcie_ep->facility_conf[MV_FACILITY_RPC].name,
		MV_FACILITY_NAME_RPC, FACILITY_NAME_LEN-1);
	pcie_ep->facility_conf[MV_FACILITY_RPC].num_dma_dev =
			get_dpi_dma_dev_count(handle);

	mv_dump_facility_conf(&pcie_ep->facility_conf[MV_FACILITY_CONTROL]);
	mv_dump_facility_conf(&pcie_ep->facility_conf[MV_FACILITY_MGMT_NETDEV]);
	mv_dump_facility_conf(&pcie_ep->facility_conf[MV_FACILITY_NW_AGENT]);
	mv_dump_facility_conf(&pcie_ep->facility_conf[MV_FACILITY_RPC]);
}

/* returns facility configuration structure filled up */
int mv_get_facility_conf(int handle, mv_facility_conf_t *conf)
{
	int instance, type;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);

	if (!is_facility_valid(type)) {
		printk("%s: Invalid facility type %d\n", __func__, type);
		return -EINVAL;
	}
	memcpy(conf, &g_pcie_ep_dev[instance]->facility_conf[type], sizeof(mv_facility_conf_t));
	return 0;
}
EXPORT_SYMBOL_GPL(mv_get_facility_conf);

int mv_facility_request_dbell_irq(int handle, int dbell,
				  irq_handler_t handler, void *arg)
{
	struct device *dev;
	char irq_name[32];
	int ret = -EINVAL, irq;
	int instance, type;
	struct npu_irq_info *irq_info;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);

	dev = g_pcie_ep_dev[instance]->plat_dev;
	switch(type) {
	case MV_FACILITY_MGMT_NETDEV:
		irq_info = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_MGMT_NETDEV_IRQ_IDX + dbell];
		if (dbell >= MV_FACILITY_MGMT_NETDEV_IRQ_CNT) {
			printk("request irq %d is out of range\n", dbell);
			return -ENOENT;
		}
		sprintf(irq_name, "mgmt_net_irq%d", dbell);
		irq = irq_info->irq;
		printk("registering irq %d arg %p\n", irq, arg);
		ret = devm_request_irq(dev, irq, handler, 0, irq_name, arg);
		if (ret < 0)
			return ret;
		break;
	default:
		printk("%s: IRQ's not supported\n", __func__);
		break;

	}
	return ret;
}
EXPORT_SYMBOL_GPL(mv_facility_request_dbell_irq);

void mv_facility_free_dbell_irq(int handle, int dbell, void *arg)
{
	struct device *dev;
	int irq;
	int instance, type;
	struct npu_irq_info *irq_info;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);

	dev = g_pcie_ep_dev[instance]->plat_dev;

	switch(type) {
	case MV_FACILITY_MGMT_NETDEV:
		irq_info = &g_pcie_ep_dev[instance]->irq_info[NPU_FACILITY_MGMT_NETDEV_IRQ_IDX + dbell];
		if (dbell >= MV_FACILITY_MGMT_NETDEV_IRQ_CNT) {
			printk("request irq %d is out of range\n", dbell);
			return;
		}
		irq = irq_info->irq;
		devm_free_irq(dev, irq, arg);
		break;
	default:
		printk("%s: IRQ's not supported\n", __func__);
		break;
	}
}
EXPORT_SYMBOL_GPL(mv_facility_free_dbell_irq);

int mv_facility_register_event_callback(int handle,
					mv_facility_event_cb handler,
					void *cb_arg)
{
	int instance, type;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);

	if (!is_facility_valid(type)) {
		printk("%s: Invalid facility type %d\n", __func__, type);
		return -EINVAL;
	}

	g_pcie_ep_dev[instance]->facility_handler[type].cb = handler;
	g_pcie_ep_dev[instance]->facility_handler[type].cb_arg = cb_arg;
	printk("Registered event handler for facility type %d\n", type);

	return 0;
}
EXPORT_SYMBOL_GPL(mv_facility_register_event_callback);

void mv_facility_unregister_event_callback(int handle)
{
	int instance, type;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);

	if (!is_facility_valid(type)) {
		printk("%s: Invalid facility type %d\n", __func__, type);
		return;
	}

	g_pcie_ep_dev[instance]->facility_handler[type].cb = NULL;
	g_pcie_ep_dev[instance]->facility_handler[type].cb_arg = NULL;
	printk("Unregistered event handler for facility type %d\n", type);
}
EXPORT_SYMBOL_GPL(mv_facility_unregister_event_callback);

int mv_send_facility_dbell(int handle, int dbell)
{
	printk("%s: invoked for type-%d, dbell-%d\n", __func__, handle, dbell);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_send_facility_dbell);

int mv_send_facility_event(int handle)
{
	int instance, type;

	instance = FACILITY_INSTANCE(handle);
	type = FACILITY_TYPE(handle);

	send_oei_trigger(g_pcie_ep_dev[instance], type);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_send_facility_event);
