#!/bin/sh

mkdir -p /dev/huge
mount -t hugetlbfs none /dev/huge
echo 24 > /proc/sys/vm/nr_hugepages

# 0002:0c:00.0 is NIX PF and 0002:0f:00.1 is SDP VF
dpdk-devbind -b vfio-pci 0002:0c:00.0 0002:0f:00.1
/usr/bin/l2fwd_otx2  -l 0-3 -n 4 -w 0002:0c:00.0 -w 0002:0f:00.1 -- -q 8 -p 0x3 -f /root/conf.csv --no-mac-updating
