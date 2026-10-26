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

class FakeStage2:
    """把 Stage2 的 read/write 映射到镜像文件的绝对 LBA 上。"""
    def __init__(self, img):
        self.img = bytearray(img)
        self.writes = []
    BASE = 0x6000
    def read(self, block, count=1):
        assert count <= 64, 'stage2 限制 64 块'
        a = (block - self.BASE) * 512
        assert a >= 0, 'block 0x%X 在分区之前' % block
        return bytes(self.img[a:a + count * 512])
    def read_range(self, first_lba, nbytes):
        out = b''; lba = first_lba; rem = nbytes
        while rem > 0:
            n = min(64, (rem + 511) // 512)
            chunk = self.read(lba, n)
            out += chunk[:min(len(chunk), rem)]
            lba += n; rem -= min(len(chunk), rem)
        return out
    def write(self, block, payload):
        assert len(payload) == 512
        a = (block - self.BASE) * 512
        assert a >= 0, 'block 0x%X 在分区之前' % block
        self.img[a:a + 512] = payload
        self.writes.append(block)

M.OUTDIR = Path('/tmp/nflasha2-mode-test')
shutil.rmtree(M.OUTDIR, ignore_errors=True)

print("=" * 60)
print("测试 1：空 MODE（原厂状态）")
print("=" * 60)
s = FakeStage2(SRC.read_bytes())
M.gate_a(s)
root, entries = M.gate_b(s)
mode, mentries = M.list_mode(s)
assert M.find_preload(mentries) == [], "原厂不该有 preload"
print("  → list 正常，未发现 preload ✓")
rc = M.do_disable(s, root, entries)
print("  → do_disable 返回 %d，写入次数 %d（应为0）" % (rc, len(s.writes)))
assert len(s.writes) == 0, "空目录不该有任何写入！"
print("  ✓ 空目录不写盘")

print()
print("=" * 60)
print("测试 2：造一个带 preload 的 MODE，验证删除逻辑")
print("=" * 60)
img = bytearray(SRC.read_bytes())
# 在 MODE 目录写入 dmode + LFN + preload + LFN 两组条目
mode_off = (M.MODE_LBA - 0x6000) * 512  # 镜像内偏移
def lfn_entry(seq, last, text):
    """构造合法的 FAT LFN 条目：5+6+2 个 UTF-16 字符分三段存放。"""
    e = bytearray(32)
    e[0] = seq | (0x40 if last else 0)
    e[11] = 0x0F
    e[12] = 0
    e[13] = 0                      # checksum（本解析器不校验）
    chars = text.encode('utf-16-le')
    def put(chars16, slot):
        off = slot
        for i in range(len(chars16) // 2):
            e[off:off+2] = chars16[i*2:i*2+2]
            off += 2
    seg = [chars[i*2:(i+1)*2] for i in range(len(chars) // 2)]
    seg += [b'\xff\xff'] * (13 - len(seg))
    # 第 0-4 个字符 → 偏移 1,3,5,7,9
    for i in range(5): e[1+i*2:3+i*2] = seg[i]
    # 第 5-10 个字符 → 偏移 14,16,...,24
    for i in range(6): e[14+i*2:16+i*2] = seg[5+i]
    # 第 11-12 个字符 → 偏移 28,30
    for i in range(2): e[28+i*2:30+i*2] = seg[11+i]
    return bytes(e)
def short_entry(name83, attr, clus, size):
    e = bytearray(32)
    e[0:11] = name83
    e[11] = attr
    struct.pack_into('<H', e, 26, clus)
    struct.pack_into('<I', e, 28, size)
    return bytes(e)

buf = bytearray()
buf += lfn_entry(1, True, 'dmode') + short_entry(b'DMODE      ', 0x20, 2, 3)
buf += lfn_entry(1, True, 'preload') + short_entry(b'PRELOAD    ', 0x20, 3, 61)
buf += b'\x00' * (32 * 512 - len(buf))
img[mode_off:mode_off + 32*512] = buf
s2 = FakeStage2(bytes(img))
M.gate_a(s2)
root2, entries2 = M.gate_b(s2)
mode2, mentries2 = M.list_mode(s2)
hits = M.find_preload(mentries2)
assert len(hits) == 1, "应找到 1 个 preload，实际 %d" % len(hits)
print("  → 找到 preload，dirent off=0x%X" % hits[0]['off'])

before = bytes(s2.img)
rc = M.do_disable(s2, root2, entries2)
after = bytes(s2.img)
diff = [i for i in range(len(before)) if before[i] != after[i]]
print("  → 全镜像差异字节数: %d" % len(diff))
print("  → 差异偏移: %s" % [hex(x) for x in diff])
assert len(diff) <= 4, "改动字节过多: %d" % len(diff)
assert all(after[i] == 0xE5 for i in diff), "改动值不是 0xE5"
# 确认 dmode 条目未受影响
e_dmode = [e for e in M.parse_dir(after[mode_off:mode_off+32*512]) if e['short'].upper()=='DMODE']
assert len(e_dmode)==1 and e_dmode[0]['off']==0x020, "dmode 被误删或移位！"
print("  ✓ dmode 条目完好，preload 已删除")

print()
print("=" * 60)
print("测试 3：restore 回滚")
print("=" * 60)
gt_files = sorted(M.OUTDIR.glob('mode_lba_0x*_pre.bin'))
assert gt_files, "没找到 ground truth 文件"
s3 = FakeStage2(after)
M.do_restore(s3, str(gt_files[0]))
restored = bytes(s3.img)
d2 = [i for i in range(len(img)) if img[i] != restored[i]]
print("  → 与原始（含伪造 preload）差异: %d 字节" % len(d2))
assert d2 == [], "restore 未完全还原: %s" % [hex(x) for x in d2[:5]]
print("  ✓ restore 精确还原")

print()
print("=" * 60)
print("全部离线测试通过")
print("=" * 60)
