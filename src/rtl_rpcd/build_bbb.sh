#!/usr/bin/env sh

CROSS_COMPILE=/home/texane/repo/lfs/_work_bbb_home/host_install/armv6-rpi-linux-gnueabi/bin/armv6-rpi-linux-gnueabi-

RTLSDR_RPC_DIR=/home/texane/repo/librtlsdr

$CROSS_COMPILE\gcc \
-static -Wall -O2 \
-I../../tmp/install/include \
-I$RTLSDR_RPC_DIR/include \
-o rtl_rpcd.bbb \
$RTLSDR_RPC_DIR/src/rtl_rpcd.c \
$RTLSDR_RPC_DIR/src/rtlsdr_rpc_msg.c \
-L../../tmp/install/lib \
-lrtlsdr -lusb-1.0 -lpthread -lrt

$CROSS_COMPILE\strip rtl_rpcd.bbb
