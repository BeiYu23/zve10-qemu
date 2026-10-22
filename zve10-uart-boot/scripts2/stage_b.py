#!/usr/bin/env python3
"""第3步：空闲载波 + 9600 baud 发送多段数据，接收器就能收干净信号"""
import sys,time,struct
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink
blink=open('/root/fw/aux_fix_min/payload.bin','rb').read()
segs=[(0xFE020000,blink)]; entry=0xFE020000
packet=b"\xAA\x55\xAA\x55"
for a,d in segs: packet+=struct.pack("<II",a,len(d))+d
packet+=struct.pack("<II",0xFFFFFFFF,0)+struct.pack("<I",entry)
time.sleep(5)
l=SerialLink('/dev/ttyACM0'); time.sleep(0.8)
for _ in range(10):
    try: l.ping(); break
    except Exception: time.sleep(0.5)
try:
    l.set_link_baud(9600); print('已降到 9600 baud')
except Exception as e: print('降速失败',e)
print(f'发送 {len(packet)} B（每 32 B 间隔 60ms）…')
for i in range(0,len(packet),32):
    l.blob(packet[i:i+32]); time.sleep(0.06)
print('发送完成，观察相机灯（应有 1 秒长闪 + 3 快闪 + 慢闪）')
