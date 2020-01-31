CROSS_COMPILE=aarch64-marvell-linux-gnu-
ARCH ?= arm64
KERNEL_DIR ?= ${OCTEONTX_ROOT}/linux/kernel/linux
DMA_API_DIR?=$(src)/../../dpi_dma
ccflags-y +=-I$(DMA_API_DIR)

OBJS += ep_base.o
OBJS += facility.o device_access.o

obj-m := pcie_ep.o
pcie_ep-y := $(OBJS)

all:
	make -C $(KERNEL_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) SUBDIRS=`pwd` modules

clean:
	make -C $(KERNEL_DIR) SUBDIRS=`pwd` clean
