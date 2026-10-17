#!/bin/sh
# 构建 GRM/UI 进程内探针（ARMv7 soft-float 目标机、零依赖）
# 用法: TC=/path/to/armv7-toolchain/bin/arm-buildroot-linux-gnueabihf- ./build.sh
set -e
TC=${TC:-/path/to/armv7-toolchain/bin/arm-buildroot-linux-gnueabihf-}
${TC}gcc -shared -fPIC -O2 -march=armv7-a -marm -nostdlib -fno-stack-protector \
  -Wl,--unresolved-symbols=ignore-all -o libgrmlog.so grmlog.c
echo "built: $(ls -l libgrmlog.so | awk '{print $5}') bytes"
