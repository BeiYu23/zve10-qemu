#!/usr/bin/env python3
"""纯诊断：相机当前 USB gadget 栈状态（不写文件、不 insmod）"""
import os, sys, time
os.environ.setdefault('CAM_USB_TIMEOUT', '30000')
sys.path.insert(0, '/tmp/camtool')
from jiritsu_lib import connect_hardened
PAYLOAD = open('/tmp/camtool/payload/libJit_exec.so','rb').read()

SCRIPT = """#!/bin/sh
echo "===BEGIN==="
echo "--- lsmod (usb/gadget) ---"
lsmod | head -40
echo "--- /proc/modules usb ---"
cat /proc/modules | grep -E "usb|gadget|composite|mass" || echo "(none)"
echo "--- UDC ---"
ls /sys/class/udc/ 2>/dev/null || echo "(no /sys/class/udc)"
echo "--- UDC state ---"
for d in /sys/class/udc/*; do echo "$d state=$(cat $d/state 2>/dev/null)"; done 2>/dev/null
echo "--- /dev gadget nodes ---"
ls -l /dev/ 2>/dev/null | grep -E "gadget|usb|mass" || echo "(none)"
echo "--- /sys/kernel/config ---"
ls /sys/kernel/config 2>/dev/null || echo "(no configfs)"
echo "===END==="
""".encode()

j = connect_hardened(attempts=1, timeout=60, verbose=False, verify=False)
if not j: print('连接失败'); sys.exit(1)
print('=== 已连接 ===')
def wf(p,d): j.select(p); return j.jiritsu(16, b'\0\xff\0'+d)
def rf(p):
    j.select(p); r=j.jiritsu(17,b'\0\xff\0')
    return r[1] if (isinstance(r[0],int) and r[0]==0) else None
try: j.select('/tmp/jiritsu/out.txt'); j.jiritsu(18,b'\0\xff')
except Exception: pass
print('[1]', wf('/tmp/jiritsu/run.sh', SCRIPT))
print('[2]', wf('/tmp/jiritsu/libJit.so', PAYLOAD))
time.sleep(2.5)
for _ in range(8):
    d = rf('/tmp/jiritsu/out.txt')
    if d and b'===END===' in d:
        print(d.decode('utf-8','replace')); break
    time.sleep(1.0)
else: print('未拿到:', d[:500] if d else None)
time.sleep(1)
