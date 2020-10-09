CROSS=aarch64-marvell-linux-gnu-
MODULE_NAME = dpi_dma
SRCS = dma_api.c 
SRCS += dpi_vf_main.c
# dpi.h is included from tx2
INCLUDE_DIR = -I$(KDIR)/drivers/soc/marvell/octeontx2-dpi/
INCLUDE_DIR += -I$(KDIR)/drivers/soc/marvell/octeontx2-npa/
INCLUDE_DIR += -I$(KDIR)/drivers/net/ethernet/cavium/octeontx-83xx/
INCLUDE_DIR += -I$(KDIR)/drivers/net/ethernet/marvell/octeontx2/af
INCLUDE_DIR += -I$(KDIR)/drivers/net/ethernet/marvell/octeontx2/nic
ccflags-y := $(INCLUDE_DIR)
 
OBJS =  $(SRCS:.c=.o)
 
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y = $(OBJS)
 
all:
	make ARCH=arm64 CROSS_COMPILE=$(CROSS) -C $(KDIR) SUBDIRS=$(PWD) modules
 
clean:
	make -C $(KDIR) SUBDIRS=$(PWD) clean
