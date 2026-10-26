#!/usr/bin/env python3
"""OFFLINE dry-run for nflasha2_mode.py against a local partition image.

No camera, no fastboot device: a Fake stage2 maps Stage2.read/write onto
the image file (partition-relative), so every gate and the single-byte
directory change can be exercised and diffed byte for byte.

  python3 test_nflasha2_mode.py            # positive: list/disable/restore
  python3 test_nflasha2_mode_negative.py   # negative: gates must abort
"""
import sys, struct, shutil
from pathlib import Path
sys.path.insert(0, '/root/fw/oss/fastboot/host')
import nflasha2_mode as M

SRC = Path('/root/fw/zve10_backup/nflasha2.img')
M.OUTDIR = Path('/tmp/nflasha2-negative')

class Fake:
    BASE = 0x6000
    def __init__(self, img):
        self.img = bytearray(img); self.writes = []
    def read(self, block, count=1):
        a = (block - self.BASE) * 512
        assert a >= 0
        return bytes(self.img[a:a + count*512])
    def read_range(self, lba, n):
        out=b''; rem=n
        while rem>0:
            k=min(64,(rem+511)//512); c=self.read(lba,k)
            out+=c[:min(len(c),rem)]; lba+=k; rem-=min(len(c),rem)
        return out
    def write(self, block, payload):
        a=(block-self.BASE)*512
        self.img[a:a+512]=payload; self.writes.append(block)

def expect_abort(name, fn, why):
    try:
        fn()
    except AssertionError as e:
        print("  ✓ %s → ABORT: %s" % (name, str(e)[:72]))
        return True
    except SystemExit as e:
        print("  ✓ %s → ABORT: %s" % (name, str(e)[:72]))
        return True
    print("  ✗ %s → 没有拦截！%s" % (name, why))
    return False

base = SRC.read_bytes()
ok = True

print("负向测试 1：BPB 被改（模拟分错区/错盘）")
img = bytearray(base)
img[11:13] = struct.pack('<H', 1024)     # bps 改错
s = Fake(bytes(img))
ok &= expect_abort("bad BPB", lambda: M.gate_a(s), "应拒绝错误 BPB")

print()
print("负向测试 2：boot 签名损坏")
img = bytearray(base)
img[510:512] = b'\x00\x00'
s = Fake(bytes(img))
ok &= expect_abort("bad boot sig", lambda: M.gate_a(s), "应拒绝无 55AA")

print()
print("负向测试 3：MODE cluster 被改（布局漂移）")
img = bytearray(base)
root_off = (M.ROOT_LBA - 0x6000)*512
e = root_off + M.MODE_ROOT_OFF
struct.pack_into('<H', img, e+26, 999)    # cluster 改成 999
s = Fake(bytes(img))
ok &= expect_abort("MODE cluster drift", lambda: M.gate_b(s), "应拒绝错误 cluster")

print()
print("负向测试 4：MODE dirent 位置漂移")
img = bytearray(base)
# 把 MODE 短名条目挪到别处：先把原条目改成已删除，再在 0x400 造一个
struct.pack_into('<B', img, e, 0xE5)
alt = root_off + 0x400
img[alt:alt+11] = b'MODE       '
struct.pack_into('<B', img, alt+11, 0x10)
struct.pack_into('<H', img, alt+26, 468)
s = Fake(bytes(img))
ok &= expect_abort("MODE dirent drift", lambda: M.gate_b(s), "应拒绝错误 offset")

print()
print("负向测试 5：MODE 里没有 preload 时不写盘")
s = Fake(base)
M.gate_a(s); root, ents = M.gate_b(s)
before = bytes(s.img)
M.do_disable(s, root, ents)
after = bytes(s.img)
assert len(s.writes) == 0 and before == after, "空目录竟然写了！"
print("  ✓ 无 preload → 零写入，镜像未变")

print()
print("负向测试 6：restore 指向 MODE 之外应拒绝")
s = Fake(base)
try:
    M.do_restore(s, "/tmp/nflasha2-mode-test/mode_lba_0x7000_pre.bin")
    print("  ✗ 未拒绝越界 restore")
    ok = False
except (AssertionError, FileNotFoundError) as ex:
    print("  ✓ 越界 restore 被拒: %s" % str(ex)[:60])

print()
print("=" * 60)
print("负向测试全部通过" if ok else "有未拦截的情况！")
print("=" * 60)
