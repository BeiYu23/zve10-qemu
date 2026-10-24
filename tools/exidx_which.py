#!/usr/bin/env python3
"""
exidx_which.py —— 用 .ARM.exidx 权威地回答"某个地址属于哪个函数"

背景：dynsym 的 st_size 可能不可靠，"下一个命名符号"会因夹着无符号 static 函数而高估。
      .ARM.exidx 由编译器生成，ARM EHABI 规定每个函数由下一条目起点界定 —— 这是权威来源。

用法：
    python3 exidx_which.py <lib.so> 0x117DF6 [0x117F0A ...]
    python3 exidx_which.py <lib.so> --list 0x117000 0x118500   # 列出该范围内的条目
"""
import struct, sys, subprocess, re, bisect

OBJDUMP = 'arm-buildroot-linux-gnueabihf-objdump'
READELF = 'arm-buildroot-linux-gnueabihf-readelf'

def _run(tool, *args):
    try:
        return subprocess.run([tool, *args], capture_output=True, text=True).stdout
    except FileNotFoundError:
        sys.exit("找不到 %s —— 请把工具链 bin 加进 PATH" % tool)

def load_exidx(path):
    """返回 (排序后的函数起始地址列表, dynsym 名字表{addr&~1: name})"""
    out = _run(READELF, '-SW', path)
    m = re.search(r'\[\s*\d+\]\s+\.ARM\.exidx\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', out)
    if not m:
        return None, None
    va, fo, sz = int(m.group(1),16), int(m.group(2),16), int(m.group(3),16)
    d = open(path,'rb').read()
    def u32(o): return struct.unpack_from('<I', d, o)[0]
    def prel31(v, base):
        off = v & 0x7FFFFFFF
        if off & 0x40000000: off -= 0x80000000
        return (base + off) & 0xFFFFFFFF
    starts = sorted(prel31(u32(fo + i*8), va + i*8) for i in range(sz // 8))

    names = {}
    for line in _run(READELF, '-sW', '--dyn-syms', path).splitlines():
        p = line.split()
        if len(p) >= 8 and p[3] == 'FUNC':
            try: names.setdefault(int(p[1],16) & ~1, p[7])
            except ValueError: pass
    return starts, names

def which(starts, addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0: return None
    start = starts[i]
    end = starts[i+1] if i+1 < len(starts) else None
    return start, end

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    path = sys.argv[1]
    starts, names = load_exidx(path)
    if starts is None:
        sys.exit("%s 没有 .ARM.exidx" % path)
    if sys.argv[2] == '--list':
        lo, hi = int(sys.argv[3],16), int(sys.argv[4],16)
        for a in starts:
            if lo <= a <= hi:
                print("  0x%06X  %s" % (a, names.get(a, "（无名）")))
        return
    print("%-10s %-10s %-10s %-7s %s" % ("地址","函数起始","函数结束","长度","符号"))
    for arg in sys.argv[2:]:
        a = int(arg, 0)
        r = which(starts, a)
        if not r: print("  0x%06X  (低于第一个条目)" % a); continue
        s, e = r
        ln = (e - s) if e is not None else -1
        print("0x%06X  0x%06X  0x%06X  %-7d %s" %
              (a, s, e if e is not None else 0, ln, names.get(s, "（无名）")))

if __name__ == '__main__':
    main()
