# Copyright (c) 2020 Marvell.
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/sh

# Copyright (c) 2020 Marvell.
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/sh

MAX_PFS=1
MAX_VFS=3
MIN_PFS=1
MIN_VFS=0
DEF_NUM_PFS=1
DEF_NUM_VFS=0


# Change according to connected CGX ports
CGX_PF0="0002:05:00.0"
CGX_PF0_VF0="0002:05:00.1"
CGX_PF0_VF1="0002:05:00.2"
CGX_PF0_VF2="0002:05:00.3"
CGX_PF0_VF3="0002:05:00.4"
CGX_PF0_VF4="0002:05:00.5"
CGX_PF0_VF5="0002:05:00.6"
CGX_PF0_VF6="0002:05:00.7"

# Dont change SDP DEVFN
SDP0_PF0="0002:0f:00.1"
SDP0_PF0_VF0="0002:0f:00.2"
SDP0_PF0_VF1="0002:0f:00.3"
SDP0_PF0_VF2="0002:0f:00.4"
SDP0_PF0_VF4="0002:0f:00.6"
SDP0_PF0_VF5="0002:0f:00.7"
SDP0_PF0_VF6="0002:0f:01.0"

if [[ ! -z "$1" ]]; then
        NUM_SDP0_PFS=$1
else
        NUM_SDP0_PFS=$DEF_NUM_PFS
fi

if [[ ! -z "$2" ]]; then
        NUM_SDP0_VFS=$2
else
        NUM_SDP0_VFS=$DEF_NUM_VFS
fi

# only for 98xx
#NUM_SDP1_PFS=$3
#NUM_SDP1_VFS_PER_PF=$4

CONF_FILE="/etc/l2fwd_conf.csv"
COREMASK=0xf
GENERATOR_MODE=0
ENABLE_GEN_JUMBO=1
REFLECTOR_MODE=0
DEBUG_MODE=0

ARGS=""
if [[ $GENERATOR_MODE -eq 1 ]]; then
        ARGS+=" -g"
elif [[ $REFLECTOR_MODE -eq 1 ]]; then
        ARGS+=" -r"
fi

if [[ $ENABLE_GEN_JUMBO -eq 1 ]]; then
        ARGS+=" -j"
fi

if [[ $DEBUG_MODE -eq 1 ]]; then
        ARGS+=" -d"
fi
ARGS+=" -f $CONF_FILE"

if [[ $NUM_SDP0_PFS -lt $MIN_PFS || $NUM_SDP0_PFS -gt $MAX_PFS ]]; then
        NUM_SDP0_PFS=$DEF_NUM_PFS
fi

if [[ $NUM_SDP0_VFS -lt $MIN_VFS || $NUM_SDP0_VFS -gt $MAX_VFS ]]; then
        NUM_SDP0_VFS=$DEF_NUM_VFS
fi

echo "num sdp0 pfs $NUM_SDP0_PFS"
echo "num sdp0 vfs $NUM_SDP0_VFS"

NUM_PFS=$NUM_SDP0_PFS
NUM_VFS=$NUM_SDP0_VFS
mkdir -p /dev/huge
mount -t hugetlbfs none /dev/huge
echo 24 > /proc/sys/vm/nr_hugepages
WHITELIST=""
PORT_CNT=0
for (( i=0; i<$NUM_PFS; i++ ))
do
        PF="CGX_PF${i}"
        if [ $NUM_VFS -gt 0 ]; then
                echo $NUM_VFS > /sys/bus/pci/devices/${!PF}/sriov_numvfs

        else
                dpdk-devbind.py -b vfio-pci ${!PF}
                WHITELIST+=" -w ${!PF}"
                PORT_CNT=$((PORT_CNT + 1))
        fi

        for (( j=0; j<$NUM_VFS; j++ ))
        do
                VF="CGX_PF${i}_VF${j}"
		dpdk-devbind.py -b vfio-pci ${!VF}
                WHITELIST+=" -w ${!VF}"
                PORT_CNT=$((PORT_CNT + 1))
        done
done

for (( i=0; i<$NUM_PFS; i++ ))
do
        PF="SDP0_PF${i}"
        if [ $NUM_VFS -eq 0 ]; then
                dpdk-devbind.py -b vfio-pci ${!PF}
                WHITELIST+=" -w ${!PF}"
                PORT_CNT=$((PORT_CNT + 1))
        fi

        for (( j=0; j<$NUM_VFS; j++ ))
        do
                VF="SDP0_PF${i}_VF${j}"
                dpdk-devbind.py -b vfio-pci ${!VF}
                WHITELIST+=" -w ${!VF}"
                PORT_CNT=$((PORT_CNT + 1))
        done
done


rm -f $CONF_FILE
echo "cgx port, sdp port" >  $CONF_FILE
OFFSET=$(($PORT_CNT/2))
for (( i=0; i<$OFFSET; i++ ))
do
	echo "$i,$(($i+$OFFSET))" >> $CONF_FILE
done


echo " /usr/bin/l2fwd_pcie_ep  -c $COREMASK -n 4 $WHITELIST -- ${ARGS}"
/usr/bin/l2fwd_pcie_ep  -c $COREMASK -n 4 $WHITELIST -- ${ARGS}
