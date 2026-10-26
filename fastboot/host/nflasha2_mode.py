#!/usr/bin/env python3
"""Rescue for /setting/mode/preload on nflasha2, via BootROM/USB fastboot.

WHY THIS EXISTS
  Every other rescue script here targets /etc/ld.so.preload on nflasha10.
  The boot entry we are about to use is a DIFFERENT file on a DIFFERENT
  partition:

      /setting/mode/dmode     -> selects mode, "3p" = preload enabled
      /setting/mode/preload   -> the LD_PRELOAD value bootin.elf applies

  If a bad preload value hangs or kills im.elf, pulling the SD card does NOT
  help (the file is on internal eMMC), and the existing nflasha10 scripts do
  not touch it. This script is the way back.

VERIFIED LAYOUT (checked against zve10_backup/nflasha2.img, 20 MiB)
  partition base absolute LBA   0x6000
  FAT12, bps=512 spc=32 rsvd=1 nfats=1 fatsz=4 rootent=512 media=0xf8
  root dir absolute LBA         0x6005        (base + 5)
  data area absolute LBA        0x6025        (base + 37)
  MODE dir = cluster 468 -> absolute LBA 0x9A65
  MODE dirent itself at root offset 0x2E0 (LFN "mode", short MODE, attr 0x10)
  factory state: MODE is empty (first byte 0x00), FAT[468] = 0xFFF

WHAT IT CHANGES
  Only the directory entry. Deleting the preload dirent makes bootin.elf's
  open() fail with ENOENT, which its code path handles by skipping putenv
  entirely (bootin +0x99aa `blt 0x9a1c`), so the next boot is stock.
  File data clusters are NOT freed or overwritten; the pre-write sector is
  saved so --restore can put the entry back verbatim.

GATES (any failure aborts before the write)
  A. boot sector + BPB exactly the expected values
  B. root dir scan: exactly one MODE entry, expected offset/cluster/attr
  C. session write path healthy (byte-identical write-back of a zero-change
     block, read back and compared)
  D. MODE cluster: exactly one exact-match "preload" entry, sane fields;
     the target sector is saved to disk before anything is modified
  E. the write itself, then TWO independent readbacks
  F. post sweep: boot sector, FAT and root dir unchanged; MODE delta is
     exactly the bytes we intended and nothing else

USAGE
  python3 nflasha2_mode.py --list          # read-only, always safe
  python3 nflasha2_mode.py --dump          # also save root/MODE copies
  python3 nflasha2_mode.py --disable-preload
  python3 nflasha2_mode.py --restore <sector.bin>

  Default when no action is given is --list.
"""
import hashlib
import struct
import sys
import time
from pathlib import Path

import usb.core

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, peek_binary, STAGE2, DATA, RESULT, PARAMS

HERE = Path(__file__).resolve().parent.parent
OUTDIR = HERE / 'build' / 'nflasha2-mode'

# ---- verified geometry -------------------------------------------------
BASE = 0x6000
ROOT_LBA = BASE + 5              # 0x6005
DATA_LBA = BASE + 37             # 0x6025
SPC = 32
BPS = 512
ROOT_SECTORS = 32                # rootent 512 * 32 / 512
EXPECT_BPB = dict(bps=512, spc=32, rsvd=1, nfats=1, fatsz=4, rootent=512)

MODE_CLUS = 468
MODE_LBA = DATA_LBA + (MODE_CLUS - 2) * SPC          # 0x9A65
MODE_ROOT_OFF = 0x2E0                                # dirent inside root dir


def sha(b):
    return hashlib.sha256(b).hexdigest()[:16]


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
    """Return entries as dicts, preserving raw offsets and LFN chains."""
    out = []
    pend = {}
    for i in range(0, len(buf), 32):
        e = buf[i:i + 32]
        if e[0] == 0x00 and not pend:
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
        sn = e[:8].decode('latin-1').rstrip()
        ext = e[8:11].decode('latin-1').rstrip()
        out.append(dict(off=i,
                        name=name,
                        short=sn + ('.' + ext if ext else ''),
                        lfn_offsets=lfn_offs,
                        attr=e[11],
                        cluster=struct.unpack_from('<H', e, 26)[0],
                        size=struct.unpack_from('<I', e, 28)[0],
                        raw=bytes(e)))
    return out


class Stage2:
    """Same transport as disable_preload.py; stage2 blob is hash-checked."""

    def __init__(self, d):
        self.d = d
        blob = (HERE / 'stage2' / 'build' / 'stage2.bin').read_bytes()
        h = sha(blob)
        print('stage2 %d B sha256:%s' % (len(blob), h))
        assert (h.startswith('807c1ba6') or h.startswith('01f4f6d9')), \
            'unexpected stage2 binary!'
        d.download(blob)
        st, msg, _ = d.send_command('oem load:%08x' % STAGE2)
        assert st == 'OKAY', msg

    def read(self, block, count=1):
        assert count <= 64, 'stage2 clamps reads to 64 blocks'
        for cmd in ('oem poke:%08x:%08x' % (PARAMS, block),
                    'oem poke:%08x:%08x' % (PARAMS + 4, count),
                    'oem poke:%08x:%08x' % (PARAMS + 8, 0),
                    'oem call:%08x' % (STAGE2 + 1)):
            st, msg, _ = self.d.send_command(cmd)
            assert st == 'OKAY', (cmd, st, msg)
        _, data = peek_binary(self.d, DATA, count * BPS)
        assert len(data) == count * BPS, 'short read: %d' % len(data)
        return data

    def read_range(self, first_lba, nbytes):
        out = b''
        lba = first_lba
        remaining = nbytes
        while remaining > 0:
            n = min(64, (remaining + BPS - 1) // BPS)
            chunk = self.read(lba, n)
            take = min(len(chunk), remaining)
            out += chunk[:take]
            lba += n
            remaining -= take
        return out

    def write(self, block, payload):
        assert len(payload) == BPS, 'must write exactly one sector'
        self.d.download(payload)
        st, msg, _ = self.d.send_command('oem load:%08x' % DATA)
        assert st == 'OKAY', msg
        for cmd in ('oem poke:%08x:%08x' % (PARAMS, block),
                    'oem poke:%08x:%08x' % (PARAMS + 4, 1),
                    'oem poke:%08x:%08x' % (PARAMS + 8, 1),
                    'oem call:%08x' % (STAGE2 + 1)):
            st, msg, _ = self.d.send_command(cmd)
            assert st == 'OKAY', (cmd, st, msg)
        _, res = peek_binary(self.d, RESULT, 0x60)
        status = struct.unpack_from('<I', res, 8)[0]
        wstatus, wblock = struct.unpack_from('<II', res, 0x50)
        assert status == 0, 'stage2 status=%d' % status
        assert wstatus == 0, 'write_status=%d' % wstatus
        assert wblock == block, 'write_block=0x%x expected 0x%x' % (wblock, block)


def connect():
    dev = None
    for _ in range(20):
        dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        raise SystemExit('fastboot device not found - re-inject first')
    return Device(dev)


def gate_a(s):
    boot = s.read(BASE)
    assert boot[:3] == b'\xeb\x3c\x90' and boot[510:512] == b'\x55\xaa', \
        'bad boot sector: %s / %s' % (boot[:3].hex(), boot[510:512].hex())
    bps, spc, rsvd, nfats, rootent = struct.unpack_from('<HBHBH', boot, 11)
    fatsz = struct.unpack_from('<H', boot, 22)[0]
    got = dict(bps=bps, spc=spc, rsvd=rsvd, nfats=nfats, fatsz=fatsz,
               rootent=rootent)
    assert got == EXPECT_BPB, 'nflasha2 BPB changed: %r' % (got,)
    print('A. boot sector + BPB intact %r' % (got,))
    return boot


def gate_b(s):
    root = s.read_range(ROOT_LBA, ROOT_SECTORS * BPS)
    entries = parse_dir(root)
    modes = [e for e in entries
             if e['short'].upper() == 'MODE' or e['name'].lower() == 'mode']
    assert len(modes) == 1, 'expected exactly one MODE entry, got %d' % len(modes)
    m = modes[0]
    assert m['off'] == MODE_ROOT_OFF, \
        'MODE dirent at 0x%X, expected 0x%X - layout drifted!' % (m['off'], MODE_ROOT_OFF)
    assert m['cluster'] == MODE_CLUS, \
        'MODE cluster %d, expected %d - layout drifted!' % (m['cluster'], MODE_CLUS)
    assert m['attr'] == 0x10, 'MODE attr 0x%02X is not a directory' % m['attr']
    print('B. root dir: MODE @0x%X cluster %d attr 0x10 OK (%d entries)'
          % (m['off'], m['cluster'], len(entries)))
    return root, entries


def list_mode(s):
    buf = s.read_range(MODE_LBA, SPC * BPS)
    entries = parse_dir(buf)
    print('\n-- /setting/mode contents (%d entr%s) --'
          % (len(entries), 'y' if len(entries) == 1 else 'ies'))
    if not entries:
        print('   (empty - nothing to disable)')
    for e in entries:
        print('   off=0x%03X attr=0x%02X clus=%-5d size=%-8d %-14s (short %s)'
              % (e['off'], e['attr'], e['cluster'], e['size'],
                 e['name'] or '-', e['short']))
    return buf, entries


def find_preload(entries):
    hits = [e for e in entries
            if (e['name'].lower() == 'preload' or e['short'].upper() == 'PRELOAD')]
    return hits


def save_dumps(root, mode):
    OUTDIR.mkdir(parents=True, exist_ok=True)
    (OUTDIR / 'root.bin').write_bytes(root)
    (OUTDIR / 'mode.bin').write_bytes(mode)
    print('   saved %s root.bin (%d B) / mode.bin (%d B)'
          % (OUTDIR, len(root), len(mode)))


def do_disable(s, root, root_entries):
    mode = s.read_range(MODE_LBA, SPC * BPS)
    entries = parse_dir(mode)
    hits = find_preload(entries)
    if not hits:
        print('\n!! no preload entry in /setting/mode - nothing to disable')
        print('   (this is the safe/normal state; nothing was written)')
        return 0
    assert len(hits) == 1, 'multiple preload-like entries: %r' % hits
    p = hits[0]
    print('\n-- found preload: off=0x%03X attr=0x%02X clus=%d size=%d'
          ' (lfn chain %s)' % (p['off'], p['attr'], p['cluster'], p['size'],
                               p['lfn_offsets'] or 'none'))

    # sector containing the target dirent
    sec_in_clus = (p['off'] // BPS)
    target_lba = MODE_LBA + sec_in_clus
    sec_off = p['off'] % BPS

    # ---- Gate C: session write-path health, zero-change write-back
    live_sec = s.read(target_lba)
    fat_lba = BASE + 1
    fat_sector0 = s.read(fat_lba)
    s.write(fat_lba, fat_sector0)
    assert s.read(fat_lba) == fat_sector0, 'write-path readback mismatch'
    probe = s.read(target_lba)
    print('C. write path healthy this session (identical write-back of FAT[0])')

    # ---- Gate D: record ground truth BEFORE touching anything
    OUTDIR.mkdir(parents=True, exist_ok=True)
    gt_path = OUTDIR / ('mode_lba_0x%X_pre.bin' % target_lba)
    gt_path.write_bytes(live_sec)
    (OUTDIR / 'mode_pre.bin').write_bytes(mode)
    (OUTDIR / 'root_pre.bin').write_bytes(root)
    print('D. ground truth saved: %s (sha %s)' % (gt_path, sha(live_sec)))

    # ---- Build the modified sector: mark this dirent and its LFN chain deleted
    mod = bytearray(live_sec)
    touched = []
    for off in [sec_off] + [sec_off - 32 * k for k in range(1, len(p['lfn_offsets']) + 1)]:
        if 0 <= off < len(mod) and mod[off] != 0xE5:
            mod[off] = 0xE5
            touched.append(off)
    assert touched, 'nothing to mark deleted - refusing to write'
    assert mod[sec_off] == 0xE5, 'short entry not marked'
    diff = [(i, live_sec[i], mod[i]) for i in range(len(mod)) if live_sec[i] != mod[i]]
    assert all(b == 0xE5 for _, _, b in diff), 'unexpected byte change: %r' % diff[:4]
    print('E. will mark %d dirent byte(s) 0xE5 (short entry at sector off 0x%X)'
          % (len(touched), sec_off))

    s.write(target_lba, bytes(mod))
    rb1 = s.read(target_lba)
    rb2 = s.read(target_lba)
    assert rb1 == bytes(mod) and rb2 == bytes(mod), 'readback mismatch'
    print('   two independent readbacks identical (sha %s)' % sha(rb1))

    # ---- Gate F: post sweep
    boot2 = s.read(BASE)
    assert boot2[510:512] == b'\x55\xaa', 'boot sector damaged!'
    fat2 = s.read(fat_lba)
    assert fat2 == fat_sector0, 'FAT sector changed!'
    root2 = s.read_range(ROOT_LBA, ROOT_SECTORS * BPS)
    assert root2 == root, 'root dir changed!'
    mode2 = s.read_range(MODE_LBA, SPC * BPS)
    d = [i for i in range(len(mode2)) if mode2[i] != mode[i]]
    assert d == sorted(touched), 'MODE delta %r != intended %r' % (d[:8], touched[:8])
    left = find_preload(parse_dir(mode2))
    assert not left, 'preload still visible after delete: %r' % left
    (OUTDIR / 'mode_post.bin').write_bytes(mode2)
    print('F. sweep OK: boot/FAT/root untouched; MODE delta = %d byte(s);'
          ' preload no longer present' % len(d))
    print('\nDONE: /setting/mode/preload removed from the directory entry.')
    print('  next boot: bootin opens preload -> ENOENT -> skips putenv -> stock boot')
    print('  restore:  python3 nflasha2_mode.py --restore %s' % gt_path)
    return 0


def do_restore(s, path):
    gt = Path(path).read_bytes()
    assert len(gt) == BPS, 'restore file must be one 512-byte sector'
    import re
    m = re.search(r'0x([0-9A-Fa-f]+)', Path(path).name)
    assert m, 'cannot read target LBA from filename %s' % Path(path).name
    lba = int(m.group(1), 16)
    assert lba == MODE_LBA or (MODE_LBA <= lba < MODE_LBA + SPC), \
        'refusing: 0x%X is not inside the MODE directory' % lba
    cur = s.read(lba)
    print('restoring sector 0x%X (current sha %s -> target sha %s)'
          % (lba, sha(cur), sha(gt)))
    s.write(lba, gt)
    assert s.read(lba) == gt, 'restore readback mismatch'
    print('restored and verified')
    return 0


def main(argv):
    action = 'list'
    arg = None
    if '--list' in argv:
        action = 'list'
    if '--dump' in argv:
        action = 'dump'
    if '--disable-preload' in argv:
        action = 'disable'
    if '--restore' in argv:
        action = 'restore'
        i = argv.index('--restore')
        assert i + 1 < len(argv), '--restore needs a sector file'
        arg = argv[i + 1]

    d = connect()
    try:
        s = Stage2(d)
        gate_a(s)
        root, entries = gate_b(s)

        if action in ('list', 'dump'):
            mode, mode_entries = list_mode(s)
            hits = find_preload(mode_entries)
            print('\n-- preload state: %s'
                  % ('*** PRESENT - boot will apply it ***' if hits
                     else 'absent (stock boot)'))
            if action == 'dump':
                save_dumps(root, mode)
            return 0

        if action == 'dump':
            return 0

        if action == 'disable':
            return do_disable(s, root, entries)

        if action == 'restore':
            return do_restore(s, arg)

        raise SystemExit('unknown action')
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
