#!/bin/bash
set -euo pipefail
HERE=$(dirname "$(readlink -f "$0")")
OUT=${OUT:-/mnt/build/tmp/menu-fast-v1}
TC=${TC:-/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin/arm-buildroot-linux-gnueabihf-gcc}
REF=${REF:-/root/fw/ref_e10/root/lib}
mkdir -p "$OUT"
export LD_LIBRARY_PATH="$(dirname "$TC")/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$TC" -O1 -g -mcpu=cortex-a9 -marm -mfloat-abi=softfp -fno-pie -no-pie -nostdlib \
  -Wl,-e,_start,--export-dynamic,--dynamic-linker,//lib/ld-linux.so.3,--hash-style=both \
  -o "$OUT/menu-host" "$HERE/start.S" "$HERE/menu_host.c" \
  "$REF/libdl-2.11.2.so" "$REF/libc-2.11.2.so" "$REF/ld-2.11.2.so"
readelf -hA "$OUT/menu-host"
readelf -d "$OUT/menu-host"
sha256sum "$OUT/menu-host"
