MODULE_MAME = mgmt_net
PCIE_HOST ?= ../pcie_host/src
 
SRCS = host_ethdev.c 
 
INCLUDE_DIR = -I$(src) -I$(src)/$(PCIE_HOST)
 
ccflags-y := $(INCLUDE_DIR)
 
OBJS =  $(SRCS:.c=.o)
 
obj-m += $(MODULE_MAME).o
$(MODULE_MAME)-y = $(OBJS)
 
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
