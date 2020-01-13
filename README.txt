Instructions to Build and Load pcie_host driver
===============================================
1. Setup the environment
   # cd pcie_host
   # source nic-env-setup.sh

   Run below command to undo the setup
   # source nic-env-setup.sh undo

2. Build base and nic kernel modules
   # cd modules/driver/
   # make

   This will generate .ko files under pcie_host/modules/driver/bin/.

3. Load newly built module
   # cd pcie_host/modules/driver/bin/
   # insmod octeon_drv.ko
   # insmod octnic.ko
