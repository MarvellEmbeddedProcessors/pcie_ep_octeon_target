CROSS=aarch64-marvell-linux-gnu-
KDIR?=${OCTEONTX_TARGET_ROOT}/linux/kernel/linux
MODULE_NAME = mgmt_net
SRCS = target_ethdev.c 
FACILITY_DIR?=$(PWD)/../pcie_ep/src
DMA_API_DIR?=$(PWD)/../dpi_dma
#FACILITY_DIR?=$(OCTEONTX_TARGET_ROOT)/drivers/pcie_ep/src
#DMA_API_DIR?=$(OCTEONTX_TARGET_ROOT)/drivers/dpi_dma
INCLUDE_DIR = -I$(PWD) -I$(FACILITY_DIR) -I$(DMA_API_DIR)
ccflags-y := $(INCLUDE_DIR)

KBUILD_EXTRA_SYMBOLS=$(BUILD_DIR)/pcie_ep_octeontx-$(PCIE_EP_OCTEONTX_VERSION)/src/Module.symvers
KBUILD_EXTRA_SYMBOLS+=$(BUILD_DIR)/dpi_dma_octeontx-$(DPI_DMA_OCTEONTX_VERSION)/Module.symvers
export KBUILD_EXTRA_SYMBOLS

$(info $$DMA_API_DIR is $(DMA_API_DIR))
 
OBJS =  $(SRCS:.c=.o)
 
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y = $(OBJS)
 
 
all:
	make ARCH=arm64 CROSS_COMPILE=$(CROSS) -C $(KDIR) SUBDIRS=$(PWD) modules
 
clean:
	make -C $(KDIR) SUBDIRS=$(PWD) clean
