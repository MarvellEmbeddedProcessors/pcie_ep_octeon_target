#!/bin/sh

SDP_NIX_VF0="0002:12:00.1"
ETH_PF0="0002:02:00.0"

if [ -z "$SDP_NIX_VF0" ]; then
	echo "provide sdp nix vf"
	exit
fi

if [ -z "$ETH_PF0" ]; then
	echo "provide eth  pf"
	exit
fi

modprobe pcie_marvell_cnxk_ep

mkdir -p /dev/huge
mount -t hugetlbfs nodev /dev/huge
echo 12 > /sys/kernel/mm/hugepages/hugepages-524288kB/nr_hugepages

devs=($SDP_NIX_VF0 $ETH_PF0)

for DEV in ${devs[*]}; do
	echo $devs
	if [ -e /sys/bus/pci/devices/$DEV/driver/unbind ]; then
                drv="$(readlink -f /sys/bus/pci/devices/$DEV/driver)"
                drv="$(basename $drv)"
                if [ "$drv" != "vfio-pci" ]; then
                        echo $DEV > "/sys/bus/pci/devices/$DEV/driver/unbind"
                fi
        fi
        echo vfio-pci > "/sys/bus/pci/devices/$DEV/driver_override"
        echo $DEV > /sys/bus/pci/drivers_probe
        echo "  Device $DEV moved to VFIO-PCI"
done

# /usr/bin/l2fwd_pcie_ep -l0-8 --main-lcore=0 -a $SDP_NIX_VF0 -a $ETH_PF0 --
# -f /usr/bin/cn98xx-l2fwd-pem0-pf0-3.cfg -d 8
