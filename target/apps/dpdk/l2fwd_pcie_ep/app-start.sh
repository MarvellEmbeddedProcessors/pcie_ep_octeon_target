#!/bin/sh

mkdir -p /dev/huge
mount -t hugetlbfs none /dev/huge
echo 24 > /proc/sys/vm/nr_hugepages

dpdk-devbind -b vfio-pci 0002:04:00.0 0002:05:00.0 0002:0f:00.1
/usr/bin/l2fwd_otx2  -l 0-3 -n 4 -w 0002:04:00.0 -w 0002:05:00.0 -w 0002:0f:00.1 -- -q 8 -p 0x7 -f conf.csv --no-mac-updating
