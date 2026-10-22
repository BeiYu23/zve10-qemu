#!/usr/bin/env python3
"""严格按 A7C 笔记：SET_UNLOCK → MEM_WRITE(4086) → SET_ENTRY → SET_UNLOCK"""
import sys, time, struct, zlib
sys.path.insert(0,'/root/fw/aux_fix')
from flash_aux_fix_nodep import SerialLink

V = 0x8CB102DC
PL = sys.argv[2] if len(sys.argv)>2 else '/root/fw/aux_fix_min/payload.bin'
DUR = int(sys.argv[1]) if len(sys.argv)>1 else 600
LOAD = int(sys.argv[3],0) if len(sys.argv)>3 else 0xFE020000
ENTRY = LOAD                # ARM 入口（偶数）

def rec(t, v, p=b""):
    body = struct.pack("<IHI", 10 + len(p), t, v) + p
    return body + struct.pack("<I", zlib.crc32(body) & 0xffffffff)

img = open(PL,'rb').read()
recs = [rec(0x0000, V)]                       # ★ 组首 SET_UNLOCK
off = 0
while off < len(img):
    recs.append(rec(0x0001, LOAD+off, img[off:off+4086])); off += 4086
recs.append(rec(0xffff, ENTRY))               # SET_ENTRY
recs.append(rec(0x0000, V))                   # ★ 组末 SET_UNLOCK
print(f'[{len(img)} B] 记录 {len(recs)} 条，载波常开，运行 {DUR}s', flush=True)

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

link.carrier(True)                            # 载波常开，绝不留静默
print('[carrier] ON', flush=True)
END=time.time()+DUR; n=0
while time.time()<END:
    try:
        for r in recs:
            link.blob(r); time.sleep(0.0144)
        n+=1
        if n%20==0: print(f'  {n} 组 完成', flush=True)
    except Exception as e:
        print('  [err]', e, flush=True); time.sleep(0.3)
        try: conn(); link.carrier(True)
        except Exception: pass
print('[done]', flush=True)
