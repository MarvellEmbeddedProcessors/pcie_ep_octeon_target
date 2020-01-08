CROSS=aarch64-marvell-linux-gnu-
KERNEL?=${OCTEONTX_ROOT}/linux/kernel/linux
MODULE_NAME = mgmt_net
SRCS = target_ethdev.c 
FACILITY_DIR?=$(src)/../pcie_ep/src
DMA_API_DIR?=$(src)/../dma_api
INCLUDE_DIR = -I$(src) -I$(FACILITY_DIR)  -I$(DMA_API_DIR)
ccflags-y := $(INCLUDE_DIR)
 
OBJS =  $(SRCS:.c=.o)
 
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y = $(OBJS)
 
 
all:
	make ARCH=arm64 CROSS_COMPILE=$(CROSS) -C $(KERNEL) SUBDIRS=$(PWD) modules
 
clean:
	make -C $(KERNEL) SUBDIRS=$(PWD) clean
