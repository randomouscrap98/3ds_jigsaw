#!/bin/sh

CURRENTPATH=`pwd`
CURRENTNAME=jigsaw_lite

if [ $# -ne 1 ]
then
   echo "Must provide 3ds ip address!"
   exit 1
fi

make -j && $DEVKITPRO/tools/bin/3dslink -a $1 $CURRENTNAME.3dsx

