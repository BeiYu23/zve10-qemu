#!/bin/sh
# 重新生成两个运行时工具（libIMDB_m2.so 是 M2 原版拷贝；bindm2.so 需编译）
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
M2=/mnt/build/fw/COMPARE/extracted/ZV1M2/usr
cp -f "$M2/lib/libIMDB.so" "$HERE/libIMDB_m2.so"
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
LD_LIBRARY_PATH=/mnt/build/toolchain/compat:$TC/lib \
  $TC/bin/arm-buildroot-linux-gnueabihf-gcc -shared -fPIC -nostdlib -Os \
  -o "$HERE/bindm2.so" "$HERE/bindm2.c"
echo "生成完成:"
ls -l "$HERE/bindm2.so" "$HERE/libIMDB_m2.so"
