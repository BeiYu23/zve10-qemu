#!/usr/bin/env python3
"""Delete the SD-update trigger flags from /setting/updater via fastboot.

WHY
  Setting /setting/updater/mode and mode5 makes every boot enter the updater.
  If the updater rejects the package it blinks an error and never boots normally,
  and the flags are on eMMC, so pulling the card does not help.

  This tool walks nflasha2's FAT to the updater directory and clears the first
  byte of the named dirents to 0xE5 (deleted), which is the smallest possible
  change. It saves the original sector before writing.

TARGET
  nflasha2 (/setting): base LBA 0x6000
    FAT12, bps=512 spc=32 rsvd=1 nfats=1 rootent=512 fatsz=4
    root dir absolute LBA 0x6005, data area 0x6025
    updater/ = cluster 5 -> absolute LBA 0x6085 (verified against our backup)

USAGE
  python3 updater_flags.py --list                 # read-only
  python3 updater_flags.py --delete mode,mode5    # clear those dirents
  python3 updater_flags.py --delete mode,mode5 --yes
"""
import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from fb_partition import connect, read_chunk, write_chunk   # reuse transport

BASE = 0x6000
ROOT_LBA = BASE + 5
DATA_LBA = BASE + 37
SPC = 32
BPS = 512
UPDATER_CLUS = 5          # verified from our nflasha2 backup
UPDATER_LBA = DATA_LBA + (UPDATER_CLUS - 2) * SPC   # 0x6085


def lfn_part(e):
    raw = e[1:11] + e[14:26] + e[28:32]
    out = ''
    for i in range(0, len(raw), 2):
        c = raw[i] | (raw[i + 1] << 8)
        if c in (0, 0xffff):
            break
        out += chr(c)
    return out


def parse_dir(buf):
    out = []
    pend = {}
    for i in range(0, len(buf), 32):
        e = buf[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5:
            pend.clear()
            continue
        if e[11] == 0x0F:
            pend[e[0] & 0x3F] = (i, lfn_part(e))
            continue
        name = ''
        lfn_offs = []
        if pend:
            m = max(pend)
            name = ''.join(pend.get(k, (0, '\ufffd'))[1] for k in range(1, m + 1))
            lfn_offs = [pend[k][0] for k in range(1, m + 1) if k in pend]
            pend = {}
        sn = e[:8].decode('latin1').rstrip()
        ext = e[8:11].decode('latin1').rstrip()
        out.append(dict(off=i, name=name, short=sn + ('.' + ext if ext else ''),
                        lfn_offsets=lfn_offs, attr=e[11],
                        cluster=struct.unpack_from('<H', e, 26)[0],
                        size=struct.unpack_from('<I', e, 28)[0]))
    return out


def read_dir(d, lba, nsec=SPC):
    """Read nsec sectors and return them concatenated."""
    out = b''
    left = nsec
    cur = lba
    while left > 0:
        n = min(128, left)
        out += read_chunk(d, cur, n)
        cur += n
        left -= n
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--delete', default=None, help='comma separated names')
    ap.add_argument('--yes', action='store_true')
    a = ap.parse_args(argv)

    d = connect()
    try:
        print('reading /setting/updater (LBA 0x%X) ...' % UPDATER_LBA)
        buf = read_dir(d, UPDATER_LBA, SPC)
        ents = parse_dir(buf)
        print('\n-- /setting/updater contents (%d entries) --' % len(ents))
        for e in ents:
            print('   off=0x%03X attr=0x%02X clus=%-4d size=%-6d %s'
                  % (e['off'], e['attr'], e['cluster'], e['size'],
                     e['name'] or e['short']))
        if not ents:
            print('   (empty)')

        if not a.delete:
            return 0

        wanted = [x.strip() for x in a.delete.split(',') if x.strip()]
        hits = []
        for w in wanted:
            m = [e for e in ents
                 if e['name'].lower() == w.lower() or e['short'].upper() == w.upper()]
            if not m:
                print('!! %s not present' % w)
                continue
            hits.extend(m)

        if not hits:
            print('nothing to delete')
            return 0
        if not a.yes:
            print('\nwould delete: %s' % ', '.join(h['name'] or h['short'] for h in hits))
            print('re-run with --yes to apply')
            return 0

        # group by sector
        touched = {}
        for h in hits:
            sec = UPDATER_LBA + (h['off'] // BPS)
            off = h['off'] % BPS
            offs = [off] + [o for o in h['lfn_offsets']]
            touched.setdefault(sec, set()).update(offs)

        for sec, offs in sorted(touched.items()):
            orig = read_chunk(d, sec, 1)
            Path('/tmp/updater_flag_sec_0x%X.bin' % sec).write_bytes(orig)
            mod = bytearray(orig)
            for o in sorted(offs):
                if 0 <= o < len(mod) and mod[o] != 0xE5:
                    mod[o] = 0xE5
            write_chunk(d, sec, bytes(mod))
            back = read_chunk(d, sec, 1)
            if back != bytes(mod):
                raise SystemExit('readback mismatch at sector 0x%X' % sec)
            print('  cleared %d dirent byte(s) in sector 0x%X (backup in /tmp)' % (len(offs), sec))

        # verify
        buf2 = read_dir(d, UPDATER_LBA, SPC)
        left = [e['name'] or e['short'] for e in parse_dir(buf2)]
        print('\nremaining entries: %s' % (', '.join(left) or '(none)'))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
