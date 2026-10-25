#!/usr/bin/env bash
# 构建 zvegisk.so —— 纯 syscall，零 libc 依赖
set -euo pipefail
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
GCC=$TC/bin/arm-buildroot-linux-gnueabihf-gcc
$GCC -shared -fPIC -nostdlib -O2 -Wall -o zvegisk.so zvegisk.c
echo "built zvegisk.so ($(stat -c %s zvegisk.so) bytes)"
