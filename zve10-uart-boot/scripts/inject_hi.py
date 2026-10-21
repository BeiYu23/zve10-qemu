#!/usr/bin/env python3
"""完全按 A7C 笔记复刻：
 · 先开载波并等 0.5s，再发数据（载波全程不关）
 · 每块记录之间留 14.4ms
 · 真实 token V=0x8CB102DC/C=0xA86DA25E，且埋进镜像偏移 0x200
 · len = 10 + payload
 · 整组重放间隔 0.1s
"""
import sys, time, struct, zlib
sys.path.insert(0, 'scripts')
from flash_aux_fix_nodep import SerialLink

LOAD = 0xFE021000
V = 0x8CB102DC
C = 0xA86DA25E
PL = 'payload/payload.bin'
GAP = 0.0144
START = 0.5
INTERVAL = 0.1


def rec(t, v, p=b""):
    body = struct.pack("<IHI", 10 + len(p), t, v) + p
    return body + struct.pack("<I", zlib.crc32(body) & 0xffffffff)


img = open(PL, 'rb').read()
tok = struct.pack("<II", V, C)
if len(img) < 0x208:
    img = img + b'\xff' * (0x208 - len(img))
im = bytearray(img)
im[0x200:0x208] = tok                      # ★ token 埋进镜像 0x200
img = bytes(im)

recs = [rec(0x0123, 0)]                    # 首条定义组
off = 0
while off < len(img):
    recs.append(rec(0x0001, LOAD + off, img[off:off + 4086]))
    off += 4086
recs.append(rec(0xffff, LOAD))             # SET_ENTRY
recs.append(rec(0x0000, V))                # SET_UNLOCK = 真实 V
recs.append(rec(0x0123, 0))                # 关组
print(f'[a7c] {len(recs)} 条 / 总 {sum(len(x) for x in recs)} B', flush=True)

link = None


def conn():
    global link
    try:
        if link:
            link.close()
    except Exception:
        pass
    link = SerialLink('/dev/ttyACM0')
    time.sleep(0.6)
    link.ping()
    print('[conn] OK', flush=True)


conn()
link.carrier(True)                          # ★ 先开载波，之后不再关
print('[carrier] ON — 现在请装电池', flush=True)
time.sleep(3.0)                             # 给用户装电池的时间
n = 0
while True:
    try:
        time.sleep(START)
        for r in recs:
            link.blob(r)
            time.sleep(GAP)
        n += 1
        if n % 5 == 0:
            print(f'  [{n}组] 已发送（载波保持）', flush=True)
        time.sleep(INTERVAL)
    except Exception as e:
        print(f'  [err] {type(e).__name__}: {e}', flush=True)
        time.sleep(0.5)
        try:
            conn()
            link.carrier(True)
        except Exception:
            time.sleep(1)
