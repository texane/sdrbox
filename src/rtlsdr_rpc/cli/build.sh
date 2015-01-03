#!/usr/bin/env sh
gcc -Wall -O2 main.c librtlsdr.c ../common/rtlsdr_rpc.c -lpthread
