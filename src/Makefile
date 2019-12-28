CROSS_COMPILE=aarch64-thunderx-linux-gnu-
ARCH ?= arm64

obj-m += pcie_ep.o

all:
	make -C ${THUNDER_ROOT}/linux/kernel/linux-aarch64 ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) SUBDIRS=`pwd` modules

clean:
	make -C ${THUNDER_ROOT}/linux/kernel/linux-aarch64 SUBDIRS=`pwd` clean
