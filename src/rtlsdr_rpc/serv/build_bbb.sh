#!/usr/bin/env sh

CROSS_COMPILE=/home/texane/repo/lfs/_work_bbb_home/host_install/armv6-rpi-linux-gnueabi/bin/armv6-rpi-linux-gnueabi-

$CROSS_COMPILE\gcc \
-static -Wall -O2 \
-I../../../tmp/install/include -I../cli \
main.c ../cli/rtlsdr_rpc_msg.c \
-L../../../tmp/install/lib -lrtlsdr -lusb-1.0 -lpthread -lrt

$CROSS_COMPILE\strip a.out
