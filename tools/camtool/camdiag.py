#!/usr/bin/env python3
"""执行通道诊断：进程 / LD_PRELOAD / 挂载"""
import os, sys, time
os.environ.setdefault('CAM_USB_TIMEOUT', '30000')
sys.path.insert(0, '/tmp/camtool')
from jiritsu_lib import connect_hardened

PAYLOAD = open('/tmp/camtool/payload/libJit_exec.so','rb').read()

SCRIPT = """#!/bin/sh
echo "===BEGIN==="
echo "--- procs ---"
cat /proc/[0-9]*/comm 2>/dev/null | sort | uniq -c
echo "--- LD_PRELOAD environ ---"
for d in /proc/[0-9]*; do
  e=$d/environ
  [ -r "$e" ] || continue
  p=$(tr '\\000' '\\n' < $e 2>/dev/null | grep -a LD_PRELOAD)
  if [ -n "$p" ]; then c=$(cat $d/comm 2>/dev/null); echo "  PID=$(basename $d) $c : $p"; fi
done
echo "--- mounts ---"
mount | grep -E "nflasha|/usr|/setting|/log"
echo "--- version ---"
cat /version.txt 2>/dev/null
echo "===END==="
""".encode()

j = connect_hardened(attempts=1, timeout=60, verbose=False, verify=False)
if not j: print('连接失败'); sys.exit(1)
print('=== 已连接 ===')
def wf(p, d):
    j.select(p); return j.jiritsu(16, b'\0\xff\0' + d)
def rf(p):
    j.select(p); r = j.jiritsu(17, b'\0\xff\0')
    return r[1] if (isinstance(r[0],int) and r[0]==0) else None

try:
    j.select('/tmp/jiritsu/out.txt'); j.jiritsu(18, b'\0\xff')
except Exception: pass

print('[1] run.sh:', wf('/tmp/jiritsu/run.sh', SCRIPT))
print('[2] libJit.so:', wf('/tmp/jiritsu/libJit.so', PAYLOAD))
time.sleep(2.5)
print('[3] out.txt:')
for _ in range(8):
    d = rf('/tmp/jiritsu/out.txt')
    if d and b'===END===' in d:
        print(d.decode('utf-8','replace')); break
    time.sleep(1.0)
else:
    print('  未拿到完整输出:', (d[:400] if d else None))
time.sleep(1)
