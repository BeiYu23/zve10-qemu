#!/usr/bin/env python3
"""mkcolor.py — 生成修改过的 color_cmn.uxc（用于验证 overlay 机制）

格式（zv-e10-re 解出，302/302 文件验证）：
  0x58..0x137  28 条记录 × 8 字节
    +0  u16  id      连续 0x4000..0x4022
    +2  u16  const   0x3a09
    +4  u8 r  +5 g  +6 b  +7 a

★ 文件无校验和 ⇒ 改色不会被完整性拒绝。
★ 0x400c 已被硬件验证：改它会让实时取景框线变色。

用法:
  ./mkcolor.py <输入 color_cmn.uxc> <输出> [--id 0x400c] [--rgba ff00ffff]
"""
import argparse, struct, sys

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--id", default="0x400c")
    ap.add_argument("--rgba", default="ff00ffff", help="RRGGBBAA")
    ap.add_argument("--list", action="store_true", help="只列出所有记录")
    a = ap.parse_args()

    d = bytearray(open(a.src, "rb").read())
    print("源文件: %s (%d B)" % (a.src, len(d)))
    if d[:3] not in (b'uxc', b'uxb', b'uxa'):
        print("!! 魔数不对: %r" % d[:3]); return 1

    need = 0x137 + 1
    if len(d) < need:
        print("!! 文件太短，期望至少 %d B" % need); return 1

    target = int(a.id, 0)
    rgba = bytes.fromhex(a.rgba)
    if len(rgba) != 4:
        print("!! rgba 必须是 4 字节"); return 1

    found = False
    print("\n索引  id      const   rgba")
    for i in range(28):
        off = 0x58 + i*8
        rid, rc = struct.unpack_from("<HH", d, off)
        r, g, b, al = d[off+4], d[off+5], d[off+6], d[off+7]
        mark = ""
        if rid == target:
            mark = "  ← 目标"
            if a.list:
                pass
            else:
                d[off+4:off+8] = rgba
            found = True
        print("  %2d   0x%04x  0x%04x  %02x%02x%02x%02x%s" % (i, rid, rc, r, g, b, al, mark))

    if not found:
        print("\n!! 没找到 id=0x%04x" % target); return 1

    if a.list:
        print("\n（--list 模式，未修改）")
        return 0

    open(a.dst, "wb").write(d)
    print("\n已写: %s (%d B)" % (a.dst, len(d)))
    print("  目标 0x%04x → rgba %s" % (target, a.rgba))
    print("  ★ 文件无校验和，只有语义风险")
    return 0

if __name__ == "__main__":
    sys.exit(main())
