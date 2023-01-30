#!/bin/sh
###################################################################
# This script configures the SDP input queue rate limiting
# This script takes the configuration params from the sdp-iq-rl.cfg 
# file given and applies the rate limiting.
###################################################################

RATE_LIMIT_CFG_FILE=./sdp-iq-rl.cfg
EPF_INFO=/tmp/srn
TXCSR=/usr/bin/txcsr

MODEL_PATH="/sys/devices/system/cpu/cpu0/regs/identification/midr_el1"

#Default Rate limit settings
WEIGHT=0x7
PKT_SZ=1500
MAX_TOKENS=0x1000
ENABLE=1

SDP_IN_RATE_ENA_BIT_POS=0
SDP_IN_RATE_REF_TOKENS_POS=1
SDP_IN_RATE_MAX_TOKENS_POS=17
SDP_IN_RATE_INIT_AP_POS=33
SDP_IN_RATE_WGT_POS=49

SDP_IN_RING_TB_MAP_POS=0
SDP_IN_RING_TB_MAP_ENA=8

TARGET_MODEL_105XX=0xd49
REF_TOKEN_8Mbs=8

DEBUG_DATA=1

debug() {

    if [ $DEBUG_DATA -eq 1 ]; then
	    echo $@
    fi
}

validate_input_format() {

	OLDIFS=$IFS
	IFS=','

	if [ -f "$RATE_LIMIT_CFG_FILE" ]; then
		while read VF_ID QUEUE_ID RATE_LIMIT
		do
			if [ "$VF_ID" == "" ]; then
				echo "Invalid input format: VF ID is empty"
				IFS=$OLDIFS
				exit
			fi
			if [ "$QUEUE_ID" == "" ]; then
				echo "Invalid input format: Queue ID is empty"
				IFS=$OLDIFS
				exit
			fi
			if [ "$RATE_LIMIT" == "" ]; then
				echo "RATE_LIMIT is empty or no value set"
				echo "Invalid input format: Rate limit is empty"
				IFS=$OLDIFS
				exit
			fi
			if [ $VF_ID -ge $NVFS ]; then
				echo "Invalid VF ID $VF_ID, It should be less than $NVFS"
				IFS=$OLDIFS
				exit
			fi
			if [ $QUEUE_ID -ge $RPVF ]; then
				echo "Invalid Queue ID $QUEUE_ID, It should be less than $RPVF"
				IFS=$OLDIFS
				exit
			fi

		done <$RATE_LIMIT_CFG_FILE
		IFS=$OLDIFS
	fi
}

start() {
	echo "Configuring Rate Limit"

	$TXCSR SDPX_EPFX_RINFO -a 0 -b 0 -x >$EPF_INFO

	if [ -f "$EPF_INFO" ]; then
		SRN=`grep -e SRN $EPF_INFO |cut -f2 -d '=' | awk '{print $1}';`
		NVFS=`grep -e NVFS $EPF_INFO |cut -f2 -d '=' | awk '{print $1}';`
		RPVF=`grep -e RPVF $EPF_INFO |cut -f2 -d '=' | awk '{print $1}';`
		echo "SRN=$SRN NVFS=$NVFS RPVF=$RPVF"

		# Validate input rate limiting configuration format and paramters

		validate_input_format

		OLDIFS=$IFS
		IFS=','

		if [ -f "$RATE_LIMIT_CFG_FILE" ]; then
			while read VF_ID_CFG QUEUE_ID_CFG RATE_LIMIT_CFG
			do

				echo "VF_ID_CFG :$VF_ID_CFG QUEUE_ID_CFG :$QUEUE_ID_CFG RATE_LIMIT_CFG :$RATE_LIMIT_CFG"

				#Determine the Effective queue number from configured queue number
				if [ $VF_ID_CFG -eq 0 ] ; then
					EFFECTIVE_QUEUE_NUM=$QUEUE_ID_CFG
				else
					EFFECTIVE_QUEUE_NUM=`expr $SRN \+ $(($VF_ID_CFG - 1)) \* $RPVF \+ $QUEUE_ID_CFG`
				fi
				echo "EFFECTIVE_QUEUE_NUM=$EFFECTIVE_QUEUE_NUM"

				#Determine the Effective rate limit value from configured rate limit
				#The rate limit value must be multiples of 8Mb/s
				#Each REF_TOKENS corresponds to 8 Mb/s of data bandwidth at 1 GHz.
				if [ `expr $RATE_LIMIT_CFG % $REF_TOKEN_8Mbs` -ne 0 ]; then
					mod=`expr $RATE_LIMIT_CFG % $REF_TOKEN_8Mbs`
					RATE_LIMIT_CFG=`expr $RATE_LIMIT_CFG \+ $(($REF_TOKEN_8Mbs - $mod))`

				fi
				echo "EFFECTIVE_RATE_LIMIT=$RATE_LIMIT_CFG"

				#Disable queue to configure rate limit
				$TXCSR SDPX_RX_IN_ENABLE -a 0 -b $EFFECTIVE_QUEUE_NUM 0
				MODEL=`cat $MODEL_PATH`
				MODEL=$(( ( MODEL>>4 )&(0xFFF) ))

				if (( $MODEL == $TARGET_MODEL_105XX )); then
					SDP_IN_RING_TB_MAP_ENA=7
				fi
				REF_TOKENS=`expr $RATE_LIMIT_CFG / $REF_TOKEN_8Mbs`
				echo "REF_TOKENS=$REF_TOKENS"

				#Configure token bucket to requested rate limit.

				RATE_CFG=$(( ( WEIGHT<<SDP_IN_RATE_WGT_POS )|( PKT_SZ<<SDP_IN_RATE_INIT_AP_POS )|( MAX_TOKENS<<SDP_IN_RATE_MAX_TOKENS_POS )|( REF_TOKENS<<SDP_IN_RATE_REF_TOKENS_POS )|( ENABLE<<0 ) ))

				$TXCSR SDPX_IN_RATE_LIMITX -a 0 -b $EFFECTIVE_QUEUE_NUM $RATE_CFG
				TB_MAP=$(( ( EFFECTIVE_QUEUE_NUM<<SDP_IN_RING_TB_MAP_POS ) ))
				if [ $RATE_LIMIT_CFG -eq 0 ] ; then
					#Detach token bucket from queue and disable rate limit
					TB_MAP=$(( ( TB_MAP )|( 0<<SDP_IN_RING_TB_MAP_ENA ) ))
					$TXCSR SDPX_IN_RING_TB_MAPX -a 0 -b $EFFECTIVE_QUEUE_NUM $TB_MAP
				else
					#Attach token bucket to queue and apply rate limit
					TB_MAP=$(( ( TB_MAP )|( 1<<SDP_IN_RING_TB_MAP_ENA ) ))
					$TXCSR SDPX_IN_RING_TB_MAPX -a 0 -b $EFFECTIVE_QUEUE_NUM $TB_MAP
				fi
				#Enable the queue to configure rate limit
				$TXCSR SDPX_RX_IN_ENABLE -a 0 -b $EFFECTIVE_QUEUE_NUM 1
			done <$RATE_LIMIT_CFG_FILE
			IFS=$OLDIFS
			cp $RATE_LIMIT_CFG_FILE $RATE_LIMIT_CFG_FILE.run

		else
			echo "Rate Limit configuration file doesn't exist"
		fi
	fi
}
start $1
