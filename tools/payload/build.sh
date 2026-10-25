#!/usr/bin/env bash
# 构建 libJit 载荷
#
# ⚠️ 工具链陷阱：buildroot 工具链的 cc1 需要 libmpfr.so.4，
#    系统只有 libmpfr.so.6，所以必须把 /mnt/build/toolchain/compat 加进 LD_LIBRARY_PATH，
#    否则报 "cc1: error while loading shared libraries: libmpfr.so.4"。
set -euo pipefail

TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
GCC=$TC/bin/arm-buildroot-linux-gnueabihf-gcc

SRC=${1:-payload_probe.c}
OUT=${2:-libJit.so}

# -nostdlib + 纯 syscall：不依赖目标机任何 libc 符号，规避 glibc 2.11.2 版本陷阱
$GCC -shared -fPIC -nostdlib -O2 -Wall \
     -Wl,-soname,libJit.so \
     -o "$OUT" "$SRC"

echo "built $OUT ($(stat -c %s "$OUT") bytes)"
