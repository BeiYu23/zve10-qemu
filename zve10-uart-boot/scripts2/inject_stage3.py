#!/usr/bin/env python3
"""stage3：多段写入 + 跳转
用法: inject_stage3.py <模式> [时长]
  blink  = 只写闪光程序到 0xFE020000，跳 0xFE020000
  loader = 写闪光程序 + 完整 loader(161KB)，跳 0xFE047000
"""
import sys, time, struct, zlib
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink

V = 0x8CB102DC
RX_LOAD, RX_ENTRY = 0xFE021000, 0xFE021000
MODE = sys.argv[1] if len(sys.argv)>1 else 'blink'
d = int(sys.argv[2]) if len(sys.argv)>2 else 300

def rec(t, v, p=b""):
    body = struct.pack("<IHI", 10+len(p), t, v) + p
    return body + struct.pack("<I", zlib.crc32(body)&0xffffffff)

rx = open('/root/fw/aux_fix_rx/payload.bin','rb').read()
blink = open('/root/fw/aux_fix_min/payload.bin','rb').read()
segs = [(0xFE020000, blink)]
entry = 0xFE020000
if MODE == 'loader':
    ld = open('/root/fw/loader_re/loader.bin','rb').read()
    segs.append((0xFE040000, ld))
    entry = 0xFE047000
    print(f'loader {len(ld)} B', flush=True)

recs = [rec(0x0000, V)]
off=0
while off < len(rx):
    recs.append(rec(0x0001, RX_LOAD+off, rx[off:off+4086])); off += 4086
recs.append(rec(0xffff, RX_ENTRY))
recs.append(rec(0x0000, V))

link=None
def conn():
    global link
    try:
        if link: link.close()
    except Exception: pass
    link = SerialLink('/dev/ttyACM0'); time.sleep(0.6)
    for _ in range(5):
        try: link.ping(); break
        except Exception:
            try: link.carrier(False)
            except Exception: pass
            time.sleep(0.3)
    print('[conn] OK', flush=True)
conn()

link.carrier(True)
time.sleep(0.5)     # 笔记：载波开启后 0.5 秒再发第一条记录
print('[A] 翻转载波持续发记录组…', flush=True)
print('    ★ 看到「一次短闪」后，按回车进入 B 阶段（也可等超时自动进入）', flush=True)
import select as _sel
t0 = time.time(); _auto = False
while True:
    try:
        for r in recs:
            link.blob(r); time.sleep(0.0144)
        time.sleep(0.05)
    except Exception as e:
        print(' [errA]', e, flush=True); time.sleep(0.3)
        try: conn(); link.carrier(True)
        except Exception: pass
    r_,_,_ = _sel.select([sys.stdin], [], [], 0)
    if r_:
        try: sys.stdin.readline()
        except Exception: pass
        print('[A] 收到回车，进入 B 阶段', flush=True); break
    if time.time()-t0 > d:
        print('[A] 超时，自动进入 B 阶段', flush=True); _auto = True; break
print('[A] 记录组阶段结束', flush=True)

try:
    link.send(0x0C); link.ack(); print('[B] 已切空闲载波', flush=True)
except Exception as e:
    print('[B] 固件无空闲模式，继续用翻转载波', flush=True)
packet = b"\xAA\x55\xAA\x55"
for a, data in segs:
    packet += struct.pack("<II", a, len(data)) + data
    print(f'  段 {a:#x} 长度 {len(data)}', flush=True)
packet += struct.pack("<I", 0xFFFFFFFF) + struct.pack("<I", entry)
print(f'[B] 发送 {len(packet)} B（{MODE} 模式），预计 {len(packet)*11/48000:.0f}s', flush=True)

# 降速：9600 baud + 慢速分块
try:
    link.set_link_baud(9600)
    print('[B] 链路降到 9600 baud', flush=True)
except Exception as e:
    print('[B] 降速失败（继续 48000）:', e, flush=True)

t1=time.time()
for i in range(0, len(packet), 32):
    link.blob(packet[i:i+32]); time.sleep(0.05)
print(f'[B] 全部发送完成，用时 {time.time()-t1:.1f}s', flush=True)
time.sleep(30)
