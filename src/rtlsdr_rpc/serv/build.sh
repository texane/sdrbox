#!/usr/bin/env sh
gcc -Wall -O2 -I../cli main.c ../cli/rtlsdr_rpc_msg.c -lrtlsdr
