# Copyright (c) 2020 Marvell.
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/sh

CGX_PF="0002:05:00.0"
SDP_VF_FOR_HOST_PF="0002:0f:00.1"
SDP_VF_FOR_HOST_VF="0002:0f:00.2"

SDP_VF=$SDP_VF_FOR_HOST_PF
if [ "$1" = "vf" ]; then
	echo "Host VF test"
        SDP_VF=$SDP_VF_FOR_HOST_VF
fi

mkdir -p /dev/huge
mount -t hugetlbfs none /dev/huge
echo 24 > /proc/sys/vm/nr_hugepages

while [ ! -f /sys/bus/pci/devices/0002\:0f\:00.0/sdp_ring_attr/sdp_vf0_rings ]
do
     sleep 10;
done
echo "Handshake with host complete; continuing with loading application..."

dpdk-devbind -b vfio-pci $CGX_PF $SDP_VF

/usr/bin/l2fwd_pcie_ep  -l 0-3 -n 4 -w $CGX_PF -w $SDP_VF -- -q 8 -p 0x3 -f /etc/l2fwd_conf.csv --no-mac-updating
