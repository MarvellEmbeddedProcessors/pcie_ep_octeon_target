#! /bin/bash
export CNNIC_ROOT=$PWD

mkdir -p $CNNIC_ROOT/modules/driver/bin
sh sym-link.sh $*;
