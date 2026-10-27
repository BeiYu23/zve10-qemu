#!/usr/bin/env python3
"""Clear the SD-update trigger flags so the camera stops re-entering the updater.

/setting (nflasha2) is FAT12: bps=512 spc=32 rsvd=1 nfats=1 rootent=512 fatsz=4
  base LBA 0x6000, root dir 0x6005, data area 0x6025
  updater/ = cluster 5 -> LBA 0x6085  (confirmed against our nflasha2 backup)

A dirent is removed the FAT way: 0xE5 into its first name byte, plus the same
into the leading bytes of its long-name entries. Because flash:<part> always
starts at the partition's first byte, the modified 4096-byte block is written as
a one-block sparse image (SKIP | RAW | SKIP) so nothing else is touched.

The original block is saved to /tmp before the write, and the whole directory is
re-read afterwards to prove the result.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from bootstrap import Device, wait_for_device, download
from sparse_block import build_single_block, BLK
from read_updater_log import upload_blocks

BASE = 0x6000
ROOT_LBA = BASE + 5
UPDATER_LBA = BASE + 0x25 + (5 - 2) * 32      # 0x6085
SPC = 32
BPS = 512
UPDATER_BYTE_OFF = UPDATER_LBA * 512


def fetch(d, lba, count):
    out = b''
    left, cur = count, lba
    while left > 0:
        n = min(64, left)
        out += upload_blocks(d, cur, n)
        cur += n
        left -= n
    return out


def lfn_text(e):
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
            pend[e[0] & 0x3F] = (i, lfn_text(e))
            continue
        name, lfn = '', []
        if pend:
            m = max(pend)
            name = ''.join(pend.get(k, (0, '?'))[1] for k in range(1, m + 1))
            lfn = [pend[k][0] for k in range(1, m + 1) if k in pend]
            pend = {}
        sn = e[:8].decode('latin1').rstrip()
        ext = e[8:11].decode('latin1').rstrip()
        ents.append(dict(off=i, name=name, short=sn + ('.' + ext if ext else ''),
                         lfn=lfn, cluster=int.from_bytes(e[26:28], 'little'),
                         size=int.from_bytes(e[28:32], 'little')))
    return ents


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--delete', default='mode,mode5')
    ap.add_argument('--yes', action='store_true')
    a = ap.parse_args(argv)

    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    try:
        d.require('oem emmc-init')
        body, _ = d.require('getvar:partition-size:nflasha2')
        part_bytes = int(body.decode('ascii').strip(), 16)
        print('nflasha2 = %d bytes' % part_bytes)

        buf = fetch(d, UPDATER_LBA, SPC)
        ents = parse(buf)
        print('/setting/updater (LBA 0x%X):' % UPDATER_LBA)
        for e in ents:
            print('   off=0x%03X %-14s clus=%-4d size=%-6d'
                  % (e['off'], e['name'] or e['short'], e['cluster'], e['size']))
        if not ents:
            print('   (empty)')
        if a.list or not a.delete:
            return 0

        want = [x.strip() for x in a.delete.split(',') if x.strip()]
        hits = []
        for w in want:
            hits += [e for e in ents
                     if e['name'].lower() == w.lower() or e['short'].upper() == w.upper()]
        if not hits:
            print('nothing to delete')
            return 0
        if not a.yes:
            print('would delete: %s   (add --yes)'
                  % ', '.join(h['name'] or h['short'] for h in hits))
            return 0

        # One sparse block can hold several dirents; group absolute byte
        # offsets by the 4096-byte block that contains them.
        blocks = {}
        for h in hits:
            for off_in_file in [h['off']] + h['lfn']:
                absb = UPDATER_BYTE_OFF + off_in_file
                base = (absb // BLK) * BLK
                blocks.setdefault(base, set()).add(absb - base)

        for base, offs in sorted(blocks.items()):
            orig = fetch(d, base // 512, BLK // 512)
            Path('/tmp/updater_block_0x%X.bin' % base).write_bytes(orig)
            mod = bytearray(orig)
            for o in sorted(offs):
                mod[o] = 0xE5
            img = build_single_block(base, bytes(mod), part_bytes)
            print('  block 0x%X: clearing %d byte(s), sparse image %d B'
                  % (base, len(offs), len(img)))
            download(d, img)
            d.require('flash:nflasha2')
            back = fetch(d, base // 512, BLK // 512)
            if back != bytes(mod):
                raise SystemExit('readback mismatch at block 0x%X' % base)
            print('    readback OK')

        after = fetch(d, UPDATER_LBA, SPC)
        left = [e['name'] or e['short'] for e in parse(after)]
        print('\nremaining: %s' % (', '.join(left) or '(none)'))
        print('power cycle the camera.')
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
