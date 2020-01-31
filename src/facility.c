//#include <string.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "barmap.h"
#include "facility.h"
#include "dma_api.h"

/* TODO: should make it use dynamic alloc memory ?? */
mv_facility_conf_t facility_conf[MV_FACILITY_COUNT];

/* TODO: should make it use dynamic alloc memory ?? */
mv_facility_event_cb_t facility_handler[MV_FACILITY_COUNT];

void mv_dump_facility_conf(int type)
{
	mv_facility_conf_t *conf = &facility_conf[type];
	printk("##### Target facility \"%s\" configuration #####\n", conf->name);
	printk("BAR memory virt addr= %p; size=%u\n",
	       conf->memmap.target_addr, conf->memsize);
	printk("h2t_dbells = %u; t2h_dbells = %u\n",
	       conf->num_h2t_dbells, conf->num_t2h_dbells);
}

void mv_facility_conf_init(struct device *dev, void *mapaddr, struct npu_bar_map *bar_map)
{
	struct facility_bar_map *facility_map;
	struct device *dma_dev = get_dpi_dma_dev();

	memset(&facility_conf, 0,
	       sizeof(mv_facility_conf_t) * MV_FACILITY_COUNT);

	/* TODO: set name for all facility names */
	facility_map = &bar_map->facility_map[MV_FACILITY_CONTROL];
	facility_conf[MV_FACILITY_CONTROL].type = MV_FACILITY_CONTROL;
	facility_conf[MV_FACILITY_CONTROL].dma_dev.target_dma_dev = dma_dev;
	facility_conf[MV_FACILITY_CONTROL].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	facility_conf[MV_FACILITY_CONTROL].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	facility_conf[MV_FACILITY_CONTROL].num_t2h_dbells = 0;
	strncpy(facility_conf[MV_FACILITY_CONTROL].name,
		MV_FACILITY_NAME_CONTROL, FACILITY_NAME_LEN-1);

	facility_map = &bar_map->facility_map[MV_FACILITY_MGMT_NETDEV];
	facility_conf[MV_FACILITY_MGMT_NETDEV].type = MV_FACILITY_MGMT_NETDEV;
	facility_conf[MV_FACILITY_MGMT_NETDEV].dma_dev.target_dma_dev = dma_dev;
	facility_conf[MV_FACILITY_MGMT_NETDEV].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	facility_conf[MV_FACILITY_MGMT_NETDEV].memsize = facility_map->size;
	facility_conf[MV_FACILITY_MGMT_NETDEV].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	facility_conf[MV_FACILITY_MGMT_NETDEV].num_t2h_dbells = 0;
	strncpy(facility_conf[MV_FACILITY_MGMT_NETDEV].name,
		MV_FACILITY_NAME_MGMT_NETDEV, FACILITY_NAME_LEN-1);

	facility_map = &bar_map->facility_map[MV_FACILITY_NW_AGENT];
	facility_conf[MV_FACILITY_NW_AGENT].type = MV_FACILITY_NW_AGENT;
	facility_conf[MV_FACILITY_NW_AGENT].dma_dev.target_dma_dev = dma_dev;
	facility_conf[MV_FACILITY_NW_AGENT].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	facility_conf[MV_FACILITY_NW_AGENT].memsize = facility_map->size;
	facility_conf[MV_FACILITY_NW_AGENT].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	facility_conf[MV_FACILITY_NW_AGENT].num_t2h_dbells = 0;
	strncpy(facility_conf[MV_FACILITY_NW_AGENT].name,
		MV_FACILITY_NAME_NETWORK_AGENT, FACILITY_NAME_LEN-1);

	facility_map = &bar_map->facility_map[MV_FACILITY_RPC];
	facility_conf[MV_FACILITY_RPC].type = MV_FACILITY_RPC;
	facility_conf[MV_FACILITY_RPC].dma_dev.target_dma_dev = dma_dev;
	facility_conf[MV_FACILITY_RPC].memmap.target_addr =
				mapaddr + facility_map->offset -
				NPU_BARMAP_FIREWALL_OFFSET;
	facility_conf[MV_FACILITY_CONTROL].memsize = facility_map->size;
	facility_conf[MV_FACILITY_RPC].memsize = facility_map->size;
	facility_conf[MV_FACILITY_RPC].num_h2t_dbells =
				facility_map->h2t_dbell_count;
	facility_conf[MV_FACILITY_RPC].num_t2h_dbells = 0;
	strncpy(facility_conf[MV_FACILITY_RPC].name,
		MV_FACILITY_NAME_RPC, FACILITY_NAME_LEN-1);

	mv_dump_facility_conf(MV_FACILITY_CONTROL);
	mv_dump_facility_conf(MV_FACILITY_MGMT_NETDEV);
	mv_dump_facility_conf(MV_FACILITY_NW_AGENT);
	mv_dump_facility_conf(MV_FACILITY_RPC);
}

/* returns facility configuration structure filled up */
int mv_get_facility_conf(int type, mv_facility_conf_t *conf)
{
	if (!is_facility_valid(type)) {
		printk("%s: Invalid facility type %d\n", __func__, type);
		return -EINVAL;
	}
	memcpy(conf, &facility_conf[type], sizeof(mv_facility_conf_t));
	return 0;
}
EXPORT_SYMBOL_GPL(mv_get_facility_conf);

int mv_facility_request_dbell_irq(int type, int dbell,
				  irq_handler_t handler, void *arg)
{
	printk("%s: IRQ's not supported\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(mv_facility_request_dbell_irq);

void mv_facility_free_dbell_irq(int type, int dbell, void *arg)
{
	printk("%s: IRQ's not supported\n", __func__);
}
EXPORT_SYMBOL_GPL(mv_facility_free_dbell_irq);

int mv_facility_register_event_callback(int type,
					mv_facility_event_cb handler,
					void *cb_arg)
{
	if (!is_facility_valid(type)) {
		printk("%s: Invalid facility type %d\n", __func__, type);
		return -EINVAL;
	}

	facility_handler[type].cb = handler;
	facility_handler[type].cb_arg = cb_arg;
	printk("Registered event handler for facility type %d\n", type);

	return 0;
}
EXPORT_SYMBOL_GPL(mv_facility_register_event_callback);

void mv_facility_unregister_event_callback(int type)
{
	if (!is_facility_valid(type)) {
		printk("%s: Invalid facility type %d\n", __func__, type);
		return;
	}

	facility_handler[type].cb = NULL;
	facility_handler[type].cb_arg = NULL;
	printk("Unregistered event handler for facility type %d\n", type);
}
EXPORT_SYMBOL_GPL(mv_facility_unregister_event_callback);

int mv_send_facility_dbell(int type, int dbell)
{
	printk("%s: invoked for type-%d, dbell-%d\n", __func__, type, dbell);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_send_facility_dbell);

int mv_send_facility_event(int type)
{
	printk("%s: invoked for type-%d\n", __func__, type);
	return 0;
}
EXPORT_SYMBOL_GPL(mv_send_facility_event);
