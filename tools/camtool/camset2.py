#!/usr/bin/env python3
"""用正确格式 LD_PRELOAD=... 重写 /setting/mode/preload"""
import os, sys, time
os.environ.setdefault('CAM_USB_TIMEOUT', '30000')
sys.path.insert(0, '/tmp/camtool')
from jiritsu_lib import connect_hardened

CONTENT = b'LD_PRELOAD=/usr/tool/LeakTracer.so:/log/probe.so'
print('目标内容 (%d B): %r' % (len(CONTENT), CONTENT.decode()))
print('        buf+11 处: %r' % CONTENT[11:].decode())

j = connect_hardened(attempts=1, timeout=60, verbose=False, verify=False)
if not j: print('连接失败'); sys.exit(1)
print('=== 已连接 ===')

j.select('/setting/mode/preload')
r = j.jiritsu(16, b'\0\xff\0' + CONTENT)
print('write(16) ->', r)
time.sleep(0.4)
j.select('/setting/mode/preload')
r = j.jiritsu(17, b'\0\xff\0')
print('read(17)  ->', r if not (isinstance(r[0],int) and r[0]==0) else (r[0], r[1]))
print('=== 完成，不 stop ===')
time.sleep(1)
