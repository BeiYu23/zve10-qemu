#!/usr/bin/env python3
"""第1步：翻转载波发记录组，让 ROM 跳进接收器。看到短闪后按回车结束。"""
import sys, time, struct, zlib
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink
V=0x8CB102DC; RX=0xFE021000
def rec(t,v,p=b""):
    body=struct.pack("<IHI",10+len(p),t,v)+p
    return body+struct.pack("<I", zlib.crc32(body)&0xffffffff)
rx=open('/root/fw/aux_fix_rx/payload.bin','rb').read()
recs=[rec(0x0000,V)]; off=0
while off<len(rx): recs.append(rec(0x0001,RX+off,rx[off:off+4086])); off+=4086
recs.append(rec(0xffff,RX)); recs.append(rec(0x0000,V))
l=SerialLink('/dev/ttyACM0'); time.sleep(0.6)
for _ in range(5):
    try: l.ping(); break
    except Exception:
        try: l.carrier(False)
        except Exception: pass
        time.sleep(0.3)
l.carrier(True)
print('发记录组中… 看到相机「一次短闪」后按回车结束本步', flush=True)
import select
t0=time.time()
while True:
    for r in recs: l.blob(r); time.sleep(0.0144)
    time.sleep(0.05)
    if select.select([sys.stdin],[],[],0)[0]:
        sys.stdin.readline(); break
    if time.time()-t0>600: break
l.carrier(False)
print('第1步完成。现在运行： python3 -u flash_idle.py  然后 python3 -u stage_b.py', flush=True)
