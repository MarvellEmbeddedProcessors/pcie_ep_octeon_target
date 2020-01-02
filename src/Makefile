#
#
# CNNIC SDK
#
# Copyright (c) 2018 Cavium Networks. All rights reserved.
#
# This file, which is part of the CNNIC SDK which also includes the
# CNNIC SDK Package from Cavium Networks, contains proprietary and
# confidential information of Cavium Networks and in some cases its
# suppliers. 
#
# Any licensed reproduction, distribution, modification, or other use of
# this file or the confidential information or patented inventions
# embodied in this file is subject to your license agreement with Cavium
# Networks. Unless you and Cavium Networks have agreed otherwise in
# writing, the applicable license terms "OCTEON SDK License Type 5" can be
# found under the directory: $CNNIC_ROOT/licenses/
#
# All other use and disclosure is prohibited.
#
# Contact Cavium Networks at info@caviumnetworks.com for more information.
#
################################################################################


#
# Makefile for Linux OcteonTX PCI driver
#


#
# Setup compilation flags here
#
WARNINGS = -Wall -Werror -Wuninitialized -Wunused-function -Werror-implicit-function-declaration

#Disable warnings realted to __DATE__  and __TIME__, if the gcc version greather than 4.8
GCC_VER := $(shell echo `gcc -dumpversion | cut -f1-2 -d.` \>= 4.9 | sed -e 's/\./*100+/g' | bc )
ifeq ($(GCC_VER),1)
WARNINGS += -Wno-error=date-time -Wno-date-time
endif

INCLUDE += -I. 

OCTDRVFLAGS  += -DOCTEON_HOST
EXTRA_CFLAGS +=	-D__SMP__ -O2 -finline-functions $(INCLUDE) $(WARNINGS)
EXTRA_CFLAGS += ${OCTDRVFLAGS}

#
# Driver object files
#
default: all 

OBJS += eth_mux.o
OBJS += cn83xx_pf_device.o
OBJS += cn83xx_common.o
OBJS += octeon_main.o
OBJS += octeon_poll.o
OBJS += cavium_proc.o
OBJS += octeon_device.o
OBJS += octeon_mem_ops.o
OBJS += facility.o

# The driver object file
obj-m         :=  pcie_host.o
pcie_host-y  :=  $(OBJS)

kernel_source := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(kernel_source) SUBDIRS=`pwd`  modules
	@awk 'BEGIN {print "EXTRA_CFLAGS += \t\\"} {print "-D"$$2"=vf_"$$2"\t\\"}' Module.symvers  > redefine_symbols.mk

install:
	$(MAKE) -C $(kernel_source) SUBDIRS=`pwd`  modules_install 

clean:
	@rm -f *.ko .*.cmd *.mod.* *.o *~ .*.o.d
	@rm -f modules.order Module.symvers *.ko.unsigned
	@rm -f redefine_symbols.mk
	@rm -rf .tmp_versions
