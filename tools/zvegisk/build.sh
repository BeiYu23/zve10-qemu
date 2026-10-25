#!/usr/bin/env bash
# 构建 zvegisk Phase 0 探针
set -euo pipefail
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
GCC=$TC/bin/arm-buildroot-linux-gnueabihf-gcc
SRC=${1:-zvegisk_probe.c}
OUT=${2:-zvegisk_probe.so}
# -nostdlib 保持零 libc 依赖；GRM_* 保持未定义，靠动态链接器在
# im.elf 的全局符号表（已含 libObj.so）里解析
$GCC -shared -fPIC -nostdlib -O2 -Wall -o "$OUT" "$SRC"
echo "built $OUT ($(stat -c %s "$OUT") bytes)"
