#!/bin/sh

# DEBUG=1 adds debug flags (CPU line on the hold-Y overlay). Set here or: DEBUG=1 ./makerun.sh IP
DEBUG=${DEBUG:-0}

CURRENTPATH=`pwd`
CURRENTNAME=jigsaw_lite

if [ $# -ne 1 ]
then
   echo "Must provide 3ds ip address!"
   exit 1
fi

FLAGS=""
if [ "$DEBUG" = "1" ]
then
   FLAGS="-DGAME_SHOWCPU=1"
fi

# make doesn't rebuild when only the flags change: clean when they differ from last time
if [ "`cat build/.makerun_flags 2>/dev/null`" != "$FLAGS" ]
then
   make clean
fi

make EXTRA_CFLAGS="$FLAGS" && mkdir -p build && echo "$FLAGS" > build/.makerun_flags && $DEVKITPRO/tools/bin/3dslink -a $1 $CURRENTNAME.3dsx
