#!/usr/bin/env python3
"""第3步：空闲载波发送 [魔数][addr][len][crc][entry][data]"""
import sys, time, struct, zlib
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink
PL  = sys.argv[1] if len(sys.argv)>1 else '/root/fw/aux_fix_min/payload.bin'
ADDR= int(sys.argv[2],0) if len(sys.argv)>2 else 0xFE020000
ENTRY = int(sys.argv[3],0) if len(sys.argv)>3 else ADDR
data=open(PL,'rb').read()
hdr=b"\xAA\x55\xAA\x55"+struct.pack("<IIII",ADDR,len(data),zlib.crc32(data)&0xffffffff,ENTRY)
packet=hdr+data
time.sleep(3)
l=SerialLink('/dev/ttyACM0'); time.sleep(0.8)
for _ in range(10):
    try: l.ping(); break
    except Exception: time.sleep(0.5)
l.carrier(True); time.sleep(0.3)      # 空闲载波固件：carrier=线路空闲高电平
print(f'发送 {len(packet)} B（数据 {len(data)} B）…', flush=True)
t0=time.time()
for i in range(0,len(packet),64):
    l.blob(packet[i:i+64]); time.sleep(0.02)
    if i and i%(64*40)==0: print(f'  {i}/{len(packet)} {time.time()-t0:.0f}s', flush=True)
print(f'完成，用时 {time.time()-t0:.0f}s', flush=True)
print('观察：1 次长闪=CRC 通过；3 次长闪=CRC 失败', flush=True)
time.sleep(60)
