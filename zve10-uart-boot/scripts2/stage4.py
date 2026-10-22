#!/usr/bin/env python3
"""stage4：记录组 → 手动回车 → 空闲载波发送 [魔数][addr][len][crc][entry][data] → CRC 校验闪灯
用法: stage4.py <目标文件> <加载地址> [entry地址|0xFFFFFFFF=不跳转]
"""
import sys, time, struct, zlib
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink

V = 0x8CB102DC; RX = 0xFE021000
PL  = sys.argv[1] if len(sys.argv)>1 else '/root/fw/aux_fix_min/payload.bin'
ADDR= int(sys.argv[2],0) if len(sys.argv)>2 else 0xFE020000
ENTRY = int(sys.argv[3],0) if len(sys.argv)>3 else ADDR
USER_BAUD = int(sys.argv[4],0) if len(sys.argv)>4 else 0

def rec(t,v,p=b""):
    body=struct.pack("<IHI",10+len(p),t,v)+p
    return body+struct.pack("<I", zlib.crc32(body)&0xffffffff)

rx = open('/root/fw/aux_fix_rx/payload.bin','rb').read()
data = open(PL,'rb').read()
recs=[rec(0x0000,V)]; off=0
while off<len(rx): recs.append(rec(0x0001,RX+off,rx[off:off+4086])); off+=4086
recs.append(rec(0xffff,RX)); recs.append(rec(0x0000,V))

link=None
def conn():
    global link
    try:
        if link: link.close()
    except Exception: pass
    link=SerialLink('/dev/ttyACM0'); time.sleep(0.6)
    for _ in range(5):
        try: link.ping(); break
        except Exception:
            try: link.carrier(False)
            except Exception: pass
            time.sleep(0.3)
    print('[conn] OK', flush=True)
conn()

link.carrier(True)
print('[A] 发记录组… 看到相机「一次短闪」后按回车', flush=True)
import select
while True:
    for r in recs: link.blob(r); time.sleep(0.0144)
    time.sleep(0.05)
    if select.select([sys.stdin],[],[],0)[0]:
        sys.stdin.readline(); break
print('[A] 结束，发送数据', flush=True)

idle_ok=False
try:
    link.send(0x0C); link.ack(); idle_ok=True; print('[B] 空闲载波', flush=True)
    _b = USER_BAUD if USER_BAUD else (9600 if len(data)<50000 else 48000)
    try:
        link.set_link_baud(_b); print(f'[B] 波特率设为 {_b}', flush=True)
    except Exception as e: print('[B] 设置波特率失败', e, flush=True)
except Exception:
    print('[B] 用翻转载波（可能对不齐）', flush=True)

hdr = b"\xAA\x55\xAA\x55" + struct.pack("<IIII", ADDR, len(data), zlib.crc32(data)&0xffffffff, ENTRY)
packet = hdr + data
chunk = (32768 if len(packet)>50000 else 32) if idle_ok else 32
gap   = (0.12 if len(packet)>50000 else 0.03) if idle_ok else 0.05
print(f'[B] 开始循环发送 {len(packet)} B 的包（每轮独立，接收器可重新对齐）', flush=True)
print('    按 Ctrl+C 停止；CRC 通过时相机会闪 1 次长闪并执行', flush=True)
rounds=0
try:
    while True:
        rounds += 1
        t0=time.time()
        for i in range(0,len(packet),chunk):
            link.blob(packet[i:i+chunk]); time.sleep(gap)
        print(f'  第 {rounds} 轮完成 ({time.time()-t0:.1f}s)', flush=True)
        time.sleep(0.8)
except KeyboardInterrupt:
    print('\n[停止]', flush=True)
except Exception as e:
    print('[err]', e, flush=True)
time.sleep(30)
