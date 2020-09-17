Update bootloader
=================
1. Install OCTEON TX SDK version 6.2.0-p2, Build 35
2. Apply dts patch pcie_ep/cn83xx-linux-dts-firewall.patch to OCTEONTX-SDK
3. Rebuild bootloader
4. update u-boot on target with new image

Instructions to Build and Load pcie_ep driver
=============================================
1. Setup OCTEONTX-SDK environment
   # cd OCTEONTX-SDK
   # source env-setup

2. Build pcie_ep kernel module
   # cd pcie_ep/src
   # make

   This will generate pcie_ep.ko under pcie_ep/src/.

3. Copy the pcie_ep.ko to Target

4. Load newly built module
   # insmod ./pcie_ep.ko
