Instructions to Build and Load pcie_host driver
===============================================
1. run make from root directory
   # cd pcie_host/src
   # make

   This will generate pcie_host.ko.

2. install module
   # cd pcie_host/src
   # make install

   This will install module to /lib/modules/<kernel-version>/extra/pcie_host.ko

3. Load newly built module
   # insmod ./pcie_host.ko
      (OR)
   # modprobe pcie_host

Note:
 - disabled soft reset of target upon driver initialization
