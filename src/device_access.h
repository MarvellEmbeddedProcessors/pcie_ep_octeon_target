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
#ifndef __HOST_DEVICE_ACCESS_H__
#define __HOST_DEVICE_ACCESS_H__

#include "facility.h"

enum mv_target {
	TARGET_HOST = 0,
	TARGET_EP = 1,
};

typedef mv_facility_map_addr_t mv_bar_map_addr_t;

typedef struct {
	mv_bar_map_addr_t addr;
	uint32_t memsize;
} mv_bar_map_t;


int mv_get_facility_handle(char *name);
int mv_get_bar_mem_map(int handle, mv_bar_map_t *bar_map);
int mv_pci_get_dma_dev(int handle, struct device **dev);
int mv_get_num_dbell(int handle, enum mv_target target, uint32_t *num_dbells);
int mv_request_dbell_irq(int handle, u32 dbell, irq_handler_t irq_handler,
			 void *arg);
int mv_free_dbell_irq(int handle, uint32_t dbell, void *arg);
int mv_pci_sync_dma(dma_addr_t host, dma_addr_t target, int direction, int size);
#endif
