#Copyright (c) 2020 Marvell.
#SPDX-License-Identifier: GPL-2.0

CROSS = aarch64-marvell-linux-gnu-
MODULE_NAME = dpi_dma
SRCS = dma_api.c 

ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

SRCS = dpi_vf_main.c

####################################################################
# Adding a new soc implementation
####################################################################
# 1) Add soc_XXX.h with definitions for function pointers in
#    struct vf_soc_ops defined in soc_api.h
# 2) Add soc_XXX.c with implementation of struct vf_soc_ops functions
# 3) Add new soc enumeration in enum soc_e in soc_api.h
# 4) Add soc implementation function handler block in
#    struct vf_soc_ops socs[SOC_MAX] and add correct condition in the
#    device probe function to select the soc implementation in
#    dpi_vf_main.c
# 5) Add check for kernel config required for soc in this Makefile
#    and add source files, compile flags etc inside the condition
####################################################################

OTXX_LINKED = 0

#Octeontx2 support
ifneq ($(CONFIG_OCTEONTX2_DPI_PF),)
SRCS += soc_otx2.c soc_otxx.c
ccflags-$(CONFIG_OCTEONTX2_DPI_PF) += -DCONFIG_SOC_OCTEONTX2
CFLAGS_soc_otx2.o += -I$(KDIR)/drivers/soc/marvell/octeontx2-dpi/
CFLAGS_soc_otx2.o += -I$(KDIR)/drivers/soc/marvell/octeontx2-npa/
CFLAGS_soc_otx2.o += -I$(KDIR)/drivers/net/ethernet/marvell/octeontx2/af
CFLAGS_soc_otx2.o += -I$(KDIR)/drivers/net/ethernet/marvell/octeontx2/nic

# We prefer to use header files from octeontx2 driver
CFLAGS_dpi_vf_main.o += -I$(KDIR)/drivers/soc/marvell/octeontx2-dpi/
CFLAGS_soc_otxx.o += -I$(KDIR)/drivers/soc/marvell/octeontx2-dpi/
OTXX_LINKED = 1
endif

# Octeontx support
ifneq ($(CONFIG_OCTEONTX_DPI),)
SRCS += soc_otx.c soc_otxx.c
ccflags-$(CONFIG_OCTEONTX_DPI) += -DCONFIG_SOC_OCTEONTX
CFLAGS_soc_otx.o += -I$(KDIR)/drivers/net/ethernet/cavium/octeontx-83xx/

ifeq ($(OTXX_LINKED),0)
# Use header files from octeontx-83xx if octeontx2 support is not available
CFLAGS_dpi_vf_main.o += -I$(KDIR)/drivers/net/ethernet/cavium/octeontx-83xx/
CFLAGS_soc_otxx.o += -I$(KDIR)/drivers/net/ethernet/cavium/octeontx-83xx/
endif
endif

obj-m = $(MODULE_NAME).o
$(MODULE_NAME)-y = $(SRCS:.c=.o)

else
# User Makefile which calls into kbuild

ifeq ($(KDIR),)
$(error "Please define KDIR.")
endif

all:
	make ARCH=arm64 CROSS_COMPILE=$(CROSS) -C $(KDIR) M=$(PWD) modules
 
clean:
	make -C $(KDIR) M=$(PWD) clean

endif
