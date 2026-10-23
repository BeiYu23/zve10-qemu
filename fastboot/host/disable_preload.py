#!/usr/bin/env python3
"""Disable /etc/ld.so.preload by clearing its FAT dirent size field.

Semantics identical to the VM-validated minimal_fix payload (10-18/10-20),
but with byte-exact ground-truth gates at every step:

  target: root-dir entry 35 'ld.so.preload' (LFN, exact match only),
          block 0x56006, dirent offset 0x60, size field at 0x7C:
          20 (0x00000014) -> 0. Net byte change: ONE byte (0x14->0x00).

  untouched: the .orig entry (block 0x56005), LFN chain, first cluster 393,
          FAT (cluster stays allocated - same as minimal_fix), file content
          (dead once size=0), zve10_agent.so itself.

Hard gates (any failure => ABORT before the real write):
  A. boot sector BPB + 55AA intact
  B. blocks 0x56006/0x56005/0x59104 byte-identical to saved ground truth
  C. session write-path health: byte-identical write-back of 0x58DC4 (.orig
     slack sector, zero-change) reads back identical
  D. full 32-sector root dir rescan: exactly one exact 'ld.so.preload' match,
     at root offset 0x460, size 20, cluster 393, attr 00; .orig at 0x3C0
  E. THE WRITE: modified block differs from GT at exactly one byte (0x7C);
     write_status==0 && write_block==0x56006; two independent readbacks equal
     the modified block
  F. post integrity sweep: boot sector, FAT[367]/[393]==0xFFF + neighbours 0,
     0x56005/0x58DC4/0x59104 == GT, root dir rescan: ld.so.preload size==0,
     full root dump differs from pre-write dump at exactly 4 bytes (0x47C)

Restore: host/restore_preload.py (one block write from the same GT), or
on-camera shell: cp /etc/ld.so.preload.orig /etc/ld.so.preload
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
GTDIR = HERE / 'build' / 'emmc-groundtruth'
PRE_DUMP = Path('/tmp/rootdir.bin')          # 32-sector root dir captured 22:30
POST_DUMP = HERE / 'build' / 'rootdir-post-fix.bin'

BLK = {'e35': 0x56006, 'e30': 0x56005, 'data': 0x59104, 'orig': 0x58DC4}
ENTRY_OFF = 0x60          # dirent 35 inside block 0x56006
SIZE_OFF = 0x7C           # size field (dirent+28)
ROOT_OFF = 35 * 32        # 0x460, offset inside the 16 KiB root dir


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


def scan_root(root):
    """Yield (offset, longname, shortname, attr, cluster, size) for entries."""
    pend = {}
    for i in range(0, len(root), 32):
        e = root[i:i + 32]
        if e[0] == 0x00:
            break
        if e[0] == 0xe5:
            pend.clear()
            continue
        if e[11] == 0x0f:
            pend[e[0] & 0x3f] = lfn_part(e)
            continue
        name = ''
        if pend:
            m = max(pend)
            name = ''.join(pend.get(k, '\ufffd') for k in range(1, m + 1))
            pend = {}
        sn = e[:8].decode('latin-1').rstrip()
        ext = e[8:11].decode('latin-1').rstrip()
        yield (i, name, sn + ('.' + ext if ext else ''), e[11],
               struct.unpack_from('<H', e, 26)[0],
               struct.unpack_from('<I', e, 28)[0])


class Stage2:
    def __init__(self, d):
        self.d = d
        blob = (HERE / 'stage2' / 'build' / 'stage2.bin').read_bytes()
        print('stage2 %d B sha256:%s' % (len(blob), sha(blob)))
        # v2 (single-block, 807c1ba6) or v3 (multi-block, 01f4f6d9) both work:
        # the single-block data paths are unchanged in v3.
        assert (sha(blob).startswith('807c1ba6') or
                sha(blob).startswith('01f4f6d9')), 'unexpected stage2 binary!'
        d.download(blob)
        st, msg, _ = d.send_command('oem load:%08x' % STAGE2)
        assert st == 'OKAY', msg

    def read(self, block, count=1):
        for cmd in ('oem poke:%08x:%08x' % (PARAMS, block),
                    'oem poke:%08x:%08x' % (PARAMS + 4, count),
                    'oem poke:%08x:%08x' % (PARAMS + 8, 0),
                    'oem call:%08x' % (STAGE2 + 1)):
            st, msg, _ = self.d.send_command(cmd)
            assert st == 'OKAY', (cmd, st, msg)
        _, data = peek_binary(self.d, DATA, count * 512)
        return data

    def write(self, block, payload):
        assert len(payload) == 512
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


def main():
    dev = None
    for _ in range(20):
        dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        raise SystemExit('fastboot device not found - re-inject first')

    d = Device(dev)
    try:
        gt = {k: (GTDIR / n).read_bytes() for k, n in (
            ('e35', 'root_e35_0x56006.bin'), ('e30', 'root_e30_0x56005.bin'),
            ('data', 'preload_data_0x59104.bin'), ('orig', 'orig_data_0x58dc4.bin'))}
        pre_root = PRE_DUMP.read_bytes()
        s = Stage2(d)

        # ---- Gate A: boot sector
        boot = s.read(0x56000)
        assert boot[:3] == b'\xeb\x3c\x90' and boot[510:512] == b'\x55\xaa', 'bad boot sector'
        bps, spc, resv, nfats, rootent = struct.unpack_from('<HBHBH', boot, 11)
        spf = struct.unpack_from('<H', boot, 22)[0]
        assert (bps, spc, resv, nfats, spf, rootent) == (512, 32, 1, 1, 3, 512), \
            'BPB changed: %r' % ((bps, spc, resv, nfats, spf, rootent),)
        print('A. boot sector + BPB intact')

        # ---- Gate B: target blocks match ground truth
        for k in ('e35', 'e30', 'data'):
            live = s.read(BLK[k])
            if live != gt[k]:
                diff = [i for i in range(512) if live[i] != gt[k][i]]
                raise SystemExit('GATE B FAILED: block 0x%x differs at %d bytes: %s'
                                 % (BLK[k], len(diff), diff[:8]))
            print('B. block 0x%x == ground truth (sha %s)' % (BLK[k], sha(gt[k])))

        # ---- Gate C: session write-path health (zero-change write-back)
        s.write(BLK['orig'], gt['orig'])
        assert s.read(BLK['orig']) == gt['orig'], 'write-path readback mismatch'
        print('C. write path healthy this session (identical write-back 0x58DC4)')

        # ---- Gate D: full root rescan, exact-match discipline
        root = b''
        for off in range(0, 32, 64 if False else 32):
            pass
        blk = 0x56004
        while blk < 0x56004 + 32:
            root += s.read(blk, min(64, 0x56004 + 32 - blk))
            blk += min(64, 0x56004 + 32 - blk)
        hits = [x for x in scan_root(root)
                if x[1].lower() == 'ld.so.preload']
        assert len(hits) == 1 and hits[0][0] == ROOT_OFF, \
            'expected exactly one exact ld.so.preload at 0x460, got %r' % hits
        off, name, sn, attr, clus, size = hits[0]
        assert (attr, clus, size) == (0x00, 393, 20), 'entry fields changed: %r' % hits[0]
        assert (attr & 0x18) == 0 and 0 < size < 0x100000, 'hardened-fixer sanity failed'
        orig = [x for x in scan_root(root) if x[1].lower() == 'ld.so.preload.orig']
        assert len(orig) == 1 and orig[0][0] == 30 * 32 and orig[0][5] == 20, 'orig entry check'
        if pre_root and root != pre_root:
            diff = [i for i in range(len(root)) if root[i] != pre_root[i]]
            raise SystemExit('GATE D FAILED: root dir drifted since 22:30 dump at %s'
                             % [hex(x) for x in diff[:8]])
        print('D. root rescan: ld.so.preload exact @0x460 (sn=%s attr=%02x clus=%d size=%d),'
              ' .orig @0x3C0 untouched, no drift vs 22:30 dump' % (sn, attr, clus, size))

        # ---- Gate E: THE WRITE (one byte)
        mod = bytearray(gt['e35'])
        assert mod[SIZE_OFF:SIZE_OFF + 4] == b'\x14\x00\x00\x00', 'size bytes unexpected'
        mod[SIZE_OFF:SIZE_OFF + 4] = b'\x00\x00\x00\x00'
        diff = [(i, gt['e35'][i], mod[i]) for i in range(512) if gt['e35'][i] != mod[i]]
        assert diff == [(SIZE_OFF, 0x14, 0x00)], 'diff not exactly one byte: %r' % diff
        print('E. writing block 0x56006, single byte change 0x%08x: 0x14 -> 0x00'
              % (0xAC00000 + 6 * 512 + SIZE_OFF))
        s.write(BLK['e35'], bytes(mod))
        rb1 = s.read(BLK['e35'])
        rb2 = s.read(BLK['e35'])
        assert rb1 == bytes(mod) and rb2 == bytes(mod), 'readback mismatch after write'
        print('   two independent readbacks identical to intended block (sha %s)' % sha(rb1))

        # ---- Gate F: post integrity sweep
        assert s.read(0x56000) == boot, 'boot sector changed?!'
        fat = b''
        for i in range(3):
            fat += s.read(0x56001 + i)
        def fat12(n):
            v = struct.unpack_from('<H', fat, n + n // 2)[0]
            return (v >> 4) if n & 1 else (v & 0xfff)
        assert fat12(367) == 0xfff and fat12(393) == 0xfff, 'FAT chain broken!'
        assert all(fat12(n) == 0 for n in (391, 392, 394, 395, 396)), 'FAT neighbours changed'
        for k in ('e30', 'data', 'orig'):
            assert s.read(BLK[k]) == gt[k], 'post-sweep: block %s changed!' % k
        root2 = b''
        blk = 0x56004
        while blk < 0x56004 + 32:
            root2 += s.read(blk, min(64, 0x56004 + 32 - blk))
            blk += min(64, 0x56004 + 32 - blk)
        POST_DUMP.write_bytes(root2)
        d2 = [i for i in range(len(root2)) if root2[i] != root[i]]
        assert d2 == [ROOT_OFF + 28], 'root dir delta not exactly the size low byte: %r' % d2
        hits = [x for x in scan_root(root2) if x[1].lower() == 'ld.so.preload']
        assert len(hits) == 1 and hits[0][5] == 0, 'post: ld.so.preload size not 0'
        print('F. sweep OK: boot/FAT/3 blocks untouched; root delta = 1 byte; '
              'ld.so.preload size == 0')
        print('\nDONE: /etc/ld.so.preload is now 0 bytes - preload disabled.')
        print('  pre  root sha %s / post sha %s' % (sha(root), sha(root2)))
        print('  restore: host/restore_preload.py  or  cp /etc/ld.so.preload.orig /etc/ld.so.preload')
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
