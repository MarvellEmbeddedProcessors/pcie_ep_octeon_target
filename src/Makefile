CROSS_COMPILE=aarch64-thunderx-linux-gnu-
ARCH ?= arm64

OBJS += ep_base.o
OBJS += facility.o

obj-m := pcie_ep.o
pcie_ep-y := $(OBJS)

all:
	make -C ${THUNDER_ROOT}/linux/kernel/linux-aarch64 ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) SUBDIRS=`pwd` modules

clean:
	make -C ${THUNDER_ROOT}/linux/kernel/linux-aarch64 SUBDIRS=`pwd` clean
