#!/usr/bin/env bash
# 构建 zvegisk_loader：直接链接【相机固件自带】的 libc 2.11.2
# 理由：相机 glibc 是 2.11.2，用新版工具链的 libc 编出来的二进制跑不了
set -euo pipefail
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
GCC=$TC/bin/arm-buildroot-linux-gnueabihf-gcc

FW=/root/fw/ref_e10/root/lib
# 用固件 libc / libdl / libpthread，解释器用相机自己的 ld-linux.so.3
$GCC -O2 -Wall -fno-stack-protector \
     -Wl,--dynamic-linker=//lib/ld-linux.so.3 \
     -o zvegisk_loader zvegisk_loader.c \
     "$FW/libc-2.11.2.so" "$FW/libdl-2.11.2.so" "$FW/libpthread-2.11.2.so" \
     2>&1 | grep -vE "warning: libc|warning: Using|^$" || true

echo "built zvegisk_loader ($(stat -c %s zvegisk_loader 2>/dev/null || echo 0) bytes)"
