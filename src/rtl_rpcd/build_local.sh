#!/usr/bin/env sh

CROSS_COMPILE=

RTLSDR_RPC_DIR=/home/texane/repo/librtlsdr

$CROSS_COMPILE\gcc \
-static -Wall -O2 -ggdb \
-I$RTLSDR_RPC_DIR/include \
-o rtl_rpcd.local \
$RTLSDR_RPC_DIR/src/rtl_rpcd.c \
$RTLSDR_RPC_DIR/src/rtlsdr_rpc_msg.c \
-lrtlsdr -lusb-1.0 -lpthread -lrt

$CROSS_COMPILE\strip rtl_rpcd.local