CROSS=aarch64-marvell-linux-gnu-
MODULE_NAME = dpi_dma
SRCS = dma_api.c 
SRCS += dpi_vf_main.c
INCLUDE_DIR = -I$(KDIR)/drivers/net/ethernet/cavium/octeontx-83xx/
ccflags-y := $(INCLUDE_DIR)
ccflags-y += -DDMA_TRANSFER
 
OBJS =  $(SRCS:.c=.o)
 
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y = $(OBJS)
 
 
all:
	make ARCH=arm64 CROSS_COMPILE=$(CROSS) -C $(KDIR) SUBDIRS=$(PWD) modules
 
clean:
	make -C $(KDIR) SUBDIRS=$(PWD) clean
