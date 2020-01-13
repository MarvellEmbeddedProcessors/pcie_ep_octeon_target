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



DRIVER_ROOT = $(PWD)

all: check_cnnic host_drv host_api utils host_nic

check_cnnic:
ifndef CNNIC_ROOT
	@echo "####################################################"
	@echo "              CNNIC_ROOT not defined"     
	@echo " Run the nic-env-setup.sh script in the CNNIC root directory"
	@echo "####################################################"
	@exit 2
endif

.NOTPARALLEL: host_drv
host_drv: 
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/kernel/drv; $(MAKE) all;
	cd $(DRIVER_ROOT)/host/linux/kernel/drv/octvf; $(MAKE) all;

.NOTPARALLEL: host_api
host_api: 
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/user/api; $(MAKE) all; 

.NOTPARALLEL: utils
utils: 
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/user/utils; $(MAKE) all;

user_drv: nic_app
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/user/drv; $(MAKE) clean; $(MAKE) all;

tests:
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/user/test; $(MAKE) all;
	cd $(DRIVER_ROOT)/host/linux/kernel/req_resp; $(MAKE) all;
	cd $(DRIVER_ROOT)/host/linux/kernel/droq_test; $(MAKE) all;
	
clean:
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/kernel/drv; $(MAKE) clean; 
	cd $(DRIVER_ROOT)/host/linux/kernel/drv/octvf; $(MAKE) clean; 
	cd $(DRIVER_ROOT)/host/linux/user/api; $(MAKE) clean; 
	cd $(DRIVER_ROOT)/host/linux/user/utils; $(MAKE) clean

user_drv_clean: check_cnnic
	cd $(DRIVER_ROOT)/host/linux/user/drv; $(MAKE) clean; 

tests_clean:
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/user/test; $(MAKE) clean;
	cd $(DRIVER_ROOT)/host/linux/kernel/req_resp; $(MAKE) clean;
	cd $(DRIVER_ROOT)/host/linux/kernel/droq_test; $(MAKE) clean;

links:
	@$(MAKE) check_cnnic;
	cd $(DRIVER_ROOT)/host/linux/user/api; $(MAKE) links;


host_nic: check_cnnic host_drv 
	@if [ -d $(DRIVER_ROOT)/host/linux/kernel/drv/octnic ]; \
	then cd $(DRIVER_ROOT)/host/linux/kernel/drv/octnic; $(MAKE); \
	else echo -e "\nOcteon NIC Host Driver not installed\n"; fi;


host_clean:
	@$(MAKE) check_cnnic;
	@if [ -d $(DRIVER_ROOT)/host/linux/kernel/drv/octnic ]; then cd $(DRIVER_ROOT)/host/linux/kernel/drv/octnic; $(MAKE) clean; fi;
	
all_clean:
	$(MAKE) clean;
	$(MAKE) host_clean;
	$(MAKE) tests_clean;

# $Id: Makefile 170855 2018-03-30 12:30:15Z asasidharan $
