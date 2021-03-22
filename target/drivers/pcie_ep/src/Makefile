#Copyright (c) 2020 Marvell.
#SPDX-License-Identifier: GPL-2.0


CROSS_COMPILE=aarch64-marvell-linux-gnu-
ARCH ?= arm64
KDIR ?= ${OCTEONTX_TARGET_ROOT}/linux/kernel/linux
DMA_API_DIR ?= $(PWD)/../../dpi_dma
#DMA_API_DIR ?= $(OCTEONTX_TARGET_ROOT)/drivers/dpi_dma
ccflags-y +=-I$(DMA_API_DIR) -DCONFIG_MV_FACILITY_DMA_API

export KBUILD_EXTRA_SYMBOLS=$(DMA_API_DIR)/Module.symvers

OBJS += ep_base.o
OBJS += facility.o device_access.o

obj-m := pcie_ep.o
pcie_ep-y := $(OBJS)

all:
	make -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=`pwd` modules

clean:
	make -C $(KDIR) M=`pwd` clean
