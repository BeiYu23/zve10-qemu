#!/usr/bin/env bash
set -euo pipefail
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
GCC=$TC/bin/arm-buildroot-linux-gnueabihf-gcc
FW=/root/fw/ref_e10/root/lib
$GCC -O2 -Wall -fno-stack-protector -Wl,--dynamic-linker=//lib/ld-linux.so.3 \
     -o zvegisk_testload zvegisk_testload.c \
     "$FW/libc-2.11.2.so" "$FW/libdl-2.11.2.so" "$FW/libpthread-2.11.2.so" 2>&1 \
     | grep -vE "ld-linux.so.3, needed" || true
echo "built zvegisk_testload ($(stat -c %s zvegisk_testload 2>/dev/null||echo 0) bytes)"
