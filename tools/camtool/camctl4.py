#!/usr/bin/env python3
"""测 select 任意路径 + write/read：/log、/setting（无害文件）"""
import os, sys, time
os.environ.setdefault('CAM_USB_TIMEOUT', '30000')
sys.path.insert(0, '/tmp/camtool')
from jiritsu_lib import connect_hardened

j = connect_hardened(attempts=1, timeout=60, verbose=True, verify=False)
if not j: print('连接失败'); sys.exit(1)
print('=== 已连接 ===')
def t(n, fn):
    try:
        r = fn(); print('%-52s -> %r' % (n, str(r)[:80])); return r
    except Exception as e:
        print('%-52s -> 异常 %r' % (n, e)); return None

print('--- ① 写 /tmp/jiritsu（基线，已知可行）---')
t("select('/tmp/jiritsu/a.txt')", lambda: j.select('/tmp/jiritsu/a.txt'))
t('  write(16) "AAA"', lambda: j.jiritsu(16, b'\0\xff\0AAA'))

print('--- ② 写 /log（关键：能否越过 /tmp/jiritsu 前缀）---')
t("select('/log/vtest.txt')", lambda: j.select('/log/vtest.txt'))
t('  write(16) "LOG_OK"', lambda: j.jiritsu(16, b'\0\xff\0LOG_OK'))
time.sleep(0.3)
t("  再 select('/log/vtest.txt')", lambda: j.select('/log/vtest.txt'))
t('  read(17)', lambda: j.jiritsu(17, b'\0\xff\0'))

print('--- ③ 读 /setting/mode/dmode（验证任意路径读）---')
t("select('/setting/mode/dmode')", lambda: j.select('/setting/mode/dmode'))
t('  read(17)', lambda: j.jiritsu(17, b'\0\xff\0'))

print('--- ④ 读 /etc/ld.so.preload（验证系统文件读）---')
t("select('/etc/ld.so.preload')", lambda: j.select('/etc/ld.so.preload'))
t('  read(17)', lambda: j.jiritsu(17, b'\0\xff\0'))

print('=== 结束，不 stop ===')
time.sleep(2)
