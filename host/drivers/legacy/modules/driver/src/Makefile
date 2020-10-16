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
# Top-level Makefile for Linux Octeon PCI driver packages
#


DRIVER_ROOT = $(shell pwd)
CNNIC_ROOT = $(DRIVER_ROOT)/../../..
export CNNIC_ROOT

all: check_cnnic host_drv host_nic

check_cnnic:
	@echo "####################################################"
	@echo "            Setting the build environment"
	@echo " CNNIC_ROOT = $(CNNIC_ROOT)"
	@echo "####################################################"
	mkdir -p $(CNNIC_ROOT)/modules/driver/bin
	cd $(CNNIC_ROOT); sh sym-link.sh

.NOTPARALLEL: host_drv
host_drv: 
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/kernel/drv; $(MAKE) all;

clean:
	cd $(DRIVER_ROOT)/host/linux/kernel/drv; $(MAKE) clean; 
	cd $(DRIVER_ROOT)/host/linux/kernel/drv/octnic; $(MAKE) clean;
	@cd $(CNNIC_ROOT); sh sym-link.sh undo

host_nic: check_cnnic host_drv 
	@if [ -d $(DRIVER_ROOT)/host/linux/kernel/drv/octnic ]; \
	then cd $(DRIVER_ROOT)/host/linux/kernel/drv/octnic; $(MAKE); \
	else echo -e "\nOcteon NIC Host Driver not installed\n"; fi;

host_clean:
	@if [ -d $(DRIVER_ROOT)/host/linux/kernel/drv/octnic ]; then cd $(DRIVER_ROOT)/host/linux/kernel/drv/octnic; $(MAKE) clean; fi;
	
all_clean:
	$(MAKE) clean;
	$(MAKE) host_clean;
	@cd $(CNNIC_ROOT); sh sym-link.sh undo

# $Id: Makefile 170855 2018-03-30 12:30:15Z asasidharan $
