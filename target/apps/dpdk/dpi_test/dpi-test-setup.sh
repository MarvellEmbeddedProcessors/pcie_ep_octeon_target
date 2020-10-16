#Copyright (c) 2020 Marvell.
#SPDX-License-Identifier: BSD-3-Clause

# Enable DPI VFs
NUMVFS=8
DPIPF=$(ls -d /sys/bus/pci/drivers/octeontx2-dpi/0* 2>/dev/null)
DPIVFS=$(cat $DPIPF/sriov_numvfs)

mkdir -p /dev/huge
mount -t hugetlbfs nodev /dev/huge
echo 12 > /sys/kernel/mm/hugepages/hugepages-524288kB/nr_hugepages

if [ "x$DPIVFS" != x"$NUMVFS" ]; then
	echo $NUMVFS > $DPIPF/sriov_numvfs
	if [ x"$?" != "x0" ]; then
		echo -n \
"""Failed to enable $DPI DMA queues.
""" >&2
	exit 1
fi
fi

# bind only required NPA and DPI VFs to vfio-pci
dpi_devs=(0000:05:00.1 0000:05:00.2 0000:05:00.3 0000:05:00.4 0000:05:00.5 0000:05:00.6 0000:05:00.7 0000:05:01.0 0002:0c:00.0)

for DEV in ${dpi_devs[*]}; do
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

