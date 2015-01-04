#!/usr/bin/env sh
gcc -Wall -O2 main.c ../common/rtlsdr_rpc_common.c -lrtlsdr
