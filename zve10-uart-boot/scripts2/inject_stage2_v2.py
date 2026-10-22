#!/usr/bin/env python3
"""stage2：先灌接收器 payload，再流式发送目标程序"""
import sys, time, struct, zlib
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink

RX_LOAD   = 0xFE021000
RX_ENTRY  = 0xFE021000
V         = 0x8CB102DC
RX_PL     = '/root/fw/aux_fix_rx/payload.bin'
TGT_ADDR  = 0xFE020000
TGT_PL    = sys.argv[2] if len(sys.argv)>2 else '/root/fw/aux_fix_min/payload.bin'
_DUR_ARG  = int(sys.argv[1]) if len(sys.argv)>1 else 3600

def rec(t, v, p=b""):
    body = struct.pack("<IHI", 10 + len(p), t, v) + p
    return body + struct.pack("<I", zlib.crc32(body) & 0xffffffff)

rx  = open(RX_PL,'rb').read()
tgt = open(TGT_PL,'rb').read()
print(f'接收器 {len(rx)} B → {RX_LOAD:#x}；目标 {len(tgt)} B → {TGT_ADDR:#x}', flush=True)

recs = [rec(0x0000, V)]                    # ★ 组首 SET_UNLOCK
off=0
while off < len(rx):
    recs.append(rec(0x0001, RX_LOAD+off, rx[off:off+4086])); off += 4086
recs.append(rec(0xffff, RX_ENTRY))          # SET_ENTRY
recs.append(rec(0x0000, V))                 # ★ 组末 SET_UNLOCK



link = None
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
print(f'[开始] 载波常开，运行 {_DUR_ARG} 秒', flush=True)

hdr = b"\xAA\x55\xAA\x55" + struct.pack("<II", TGT_ADDR, len(tgt))
END = time.time() + _DUR_ARG
cycle = 0
while time.time() < END:
    cycle += 1
    link.carrier(False); time.sleep(0.05); link.carrier(True)   # 翻转载波
    t0 = time.time(); n = 0
    while time.time()-t0 < 20 and time.time() < END:      # 阶段A：记录组
        try:
            for r in recs:
                link.blob(r); time.sleep(0.0144)
            n += 1
        except Exception as e:
            print('  [err-A]', e, flush=True)
            try: conn(); link.carrier(True)
            except Exception: pass
    print(f'  [循环{cycle}] 记录组 {n} 次', flush=True)
    link.send(0x0C); link.ack()                            # ★ 切空闲载波
    t1 = time.time(); m = 0
    while time.time()-t1 < 15 and time.time() < END:      # 阶段B：目标程序
        try:
            link.blob(hdr); time.sleep(0.01)
            for k in range(0, len(tgt), 128):
                link.blob(tgt[k:k+128]); time.sleep(0.01)
            m += 1; time.sleep(0.03)
        except Exception as e:
            print('  [err-B]', e, flush=True); time.sleep(0.2)
    print(f'  [循环{cycle}] 目标程序 {m} 轮', flush=True)
print('[结束]', flush=True)
