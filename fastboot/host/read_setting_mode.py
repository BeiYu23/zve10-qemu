#!/usr/bin/env python3
"""Read /setting/mode/ so the boot-mode file can be inspected.

nflasha2 FAT12: base 0x00800000, rsvd=1, nfats=1, rootent=512, fatsz=4
  root dir LBA 0x4005, data area 0x4025
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from bootstrap import Device, wait_for_device
from read_updater_log import upload_blocks

PART_START_LBA = 0x00800000 // 512
ROOT_LBA = PART_START_LBA + 5
DATA_LBA = PART_START_LBA + 0x25
SPC = 32
BPS = 512


def rd(part_lba, nsec):
    out, left, cur = b'', nsec, part_lba
    while left > 0:
        n = min(64, left)
        out += upload_blocks(d, cur, n)
        cur += n
        left -= n
    return out


def fat12(buf, cl):
    i = cl + cl // 2
    if i + 1 >= len(buf):
        return 0xFFF
    v = buf[i] | (buf[i + 1] << 8)
    return (v >> 4) if (cl & 1) else (v & 0xFFF)


def lfn(e):
    raw = e[1:11] + e[14:26] + e[28:32]
    s = ''
    for i in range(0, len(raw), 2):
        c = raw[i] | (raw[i + 1] << 8)
        if c in (0, 0xFFFF):
            break
        s += chr(c)
    return s


def parse(buf):
    ents, pend = [], {}
    for i in range(0, len(buf), 32):
        e = buf[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5:
            pend.clear()
            continue
        if e[11] == 0x0F:
            pend[e[0] & 0x3F] = lfn(e)
            continue
        name = ''
        if pend:
            m = max(pend)
            name = ''.join(pend.get(k, '?') for k in range(1, m + 1))
            pend = {}
        sn = e[:8].decode('latin1').rstrip()
        ext = e[8:11].decode('latin1').rstrip()
        ents.append(dict(off=i, name=name or (sn + ('.' + ext if ext else '')),
                         attr=e[11], cluster=int.from_bytes(e[26:28], 'little'),
                         size=int.from_bytes(e[28:32], 'little')))
    return ents


def read_chain(fat, cl, size):
    out, c = b'', cl
    while 2 <= c < 0xFF8 and len(out) < size:
        out += rd(DATA_LBA + (c - 2) * SPC, SPC)
        c = fat12(fat, c)
    return out[:size]


d = None
try:
    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    d.require('oem emmc-init')
    fat = rd(PART_START_LBA + 1, 4)
    root = rd(ROOT_LBA, 32)
    print('=== /setting 根目录 ===')
    mode_clus = None
    for e in parse(root):
        print('  %s %-20s clus=%-5d %8d B'
              % ('DIR ' if e['attr'] & 0x10 else 'FILE', e['name'], e['cluster'], e['size']))
        if e['name'].lower() == 'mode' and e['attr'] & 0x10:
            mode_clus = e['cluster']
    if mode_clus is None:
        raise SystemExit('/setting/mode not found')
    print()
    print('=== /setting/mode ===')
    mdir = rd(DATA_LBA + (mode_clus - 2) * SPC, SPC)
    ents = parse(mdir)
    if not ents:
        print('  (empty)')
    for e in ents:
        content = ''
        if not (e['attr'] & 0x10) and 0 < e['size'] <= 64 and e['cluster'] >= 2:
            raw = read_chain(fat, e['cluster'], e['size'])
            content = ' = %r  (hex %s)' % (raw, raw.hex())
        print('  %s %-16s clus=%-5d %4d B%s'
              % ('DIR ' if e['attr'] & 0x10 else 'FILE', e['name'], e['cluster'], e['size'], content))
finally:
    if d:
        d.close()
