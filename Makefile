MODULE_MAME = mgmt_net
PCIE_HOST ?= $(src)/../pcie_host/src
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
 
SRCS = host_ethdev.c 
 
INCLUDE_DIR = -I$(src) -I$(PCIE_HOST)
 
ccflags-y := $(INCLUDE_DIR)
 
OBJS =  $(SRCS:.c=.o)
 
obj-m += $(MODULE_MAME).o
$(MODULE_MAME)-y = $(OBJS)
 
 
all:
	make -C $(KERNEL_DIR) M=`pwd` modules
 
clean:
	make -C $(KERNEL_DIR) M=`pwd` clean
