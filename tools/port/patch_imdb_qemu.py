#!/usr/bin/env python3
"""把 libIMDB.so 里 10 个「在 QEMU 里会出问题」的服务屏蔽掉。

来源：chocotan/a7c2-re 的做法（`prepare-assets.py: patch_imdb`）。
那边是 64 位（步长 0x48、mask@+8、name@+16）；ARM32 这里是步长 0x2C、
mask 同样在 +8、name 指针在 +0x10。

它们关掉的都是 USB 供电 / tethering / crypt / sensapi / hotplug 这一类，
在模拟器里没有真实硬件支撑，会让启动链卡住或空指针。

用法：
    python3 patch_imdb_qemu.py 输入/libIMDB.so 输出/libIMDB.so
"""
import struct, subprocess, sys

SKIP = {
    'libUsbBusPower.so', 'libInfraUsbCmn.so', 'libsensapi.so',
    'libUsbControl.so', 'libBusPower.so', 'libHotplug.so', 'libInfraCrypt.so',
    'libInfraMediaPwrMgr.so', 'libNetServiceTethering.so', 'libTetheringResource.so',
}
STRIDE = 0x2C      # ARM32 记录步长
MASK_OFF = 0x8     # 掩码在记录内的偏移
NAME_OFF = 0x10    # 名字指针


def sections(path):
    out = subprocess.check_output(['readelf', '-SW', path], text=True)
    secs = []
    for line in out.splitlines():
        p = line.replace('[', '').replace(']', ' ').split()
        if len(p) < 6:
            continue
        try:
            name, addr, off, size = p[1], int(p[3], 16), int(p[4], 16), int(p[5], 16)
        except ValueError:
            continue
        if size:
            secs.append((name, addr, off, size))
    return secs


def main(src, dst):
    data = bytearray(open(src, 'rb').read())
    secs = sections(src)

    def va2off(va):
        for _, a, o, s in secs:
            if a <= va < a + s:
                return o + (va - a)
        return None

    ds = subprocess.check_output(['readelf', '-sW', src], text=True)
    imdb = None
    for line in ds.splitlines():
        if 'imdb_raw' in line:
            imdb = int(line.split()[1], 16)
    if imdb is None:
        raise SystemExit('找不到 imdb_raw 符号')

    base = imdb + 0x84
    changed = []
    for i in range(1000):
        rec = base + STRIDE * i
        o = va2off(rec)
        if o is None:
            break
        f0 = struct.unpack_from('<I', data, o)[0]
        if f0 == 40:                      # TERMINATOR
            break
        nptr = struct.unpack_from('<I', data, o + NAME_OFF)[0]
        no = va2off(nptr) if nptr else None
        if no is None:
            continue
        name = data[no:data.index(b'\0', no)].decode('latin1')
        if name not in SKIP:
            continue
        mask = struct.unpack_from('<I', data, o + MASK_OFF)[0]
        if not mask & 1:
            raise SystemExit('记录 %d (%s) 的 mask 缺 bit0，结构不符' % (i, name))
        struct.pack_into('<I', data, o + MASK_OFF, mask & ~7)
        changed.append((i, name, mask, mask & ~7))

    if len(changed) != len(SKIP):
        raise SystemExit('只改到 %d 条，期望 %d 条' % (len(changed), len(SKIP)))
    for i, name, old, new in changed:
        print('  [%3d] %-28s 0x%08X -> 0x%08X' % (i, name, old, new))
    open(dst, 'wb').write(bytes(data))
    print('已写出 %s（%d 条已屏蔽）' % (dst, len(changed)))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
