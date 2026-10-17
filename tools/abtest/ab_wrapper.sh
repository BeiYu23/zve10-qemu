#!/bin/sh
# ZV10 libObj.so A/B 验证（与文件名无关；exec "$0.real" 收尾 ✓）
say(){ echo "$1" >&2; echo "$1" > /dev/console 2>/dev/null; }
say "===ZV10-AB-BEGIN ($0) pid=$$==="
for e in "ORIG /usr/lib/libObj_orig.so" "PATCHED /usr/lib/libObj.so"; do
  n=${e%% *}; l=${e##* }
  say "--- $n : $l ---"
  ( LD_LIBRARY_PATH=/usr/lib LD_PRELOAD="$l" /bin/busybox true ) 2>&1 | while read -r line; do say "  [$n] $line"; done
  say "--- $n rc=$? ---"
done
say "===ZV10-AB-END==="
exec "$0.real" "$@"
