#!/usr/bin/env python3
"""First eMMC WRITE validation via stage2 (CMD24), risk-minimised.

Sequence:
  1. regression: read block 0x56000 (boot sector) with the NEW stage2 and
     byte-compare against the reference captured with the old one
  2. ground truth: read 0x56005, 0x56006, 0x58dc4, 0x59104 and save them
  3. TEST A: write block 0x58dc4 back byte-identically, re-read, compare
  4. TEST B: write a test pattern into the SLACK of 0x58dc4 (offsets >= 20,
     invisible to the file), read back, verify, then restore and verify
  5. TEST C: write block 0x56006 (contains the ld.so.preload size field at
     offset 0x7c, value 20) back byte-identically, re-read, compare
  6. final: re-read all four blocks and compare against ground truth

The ld.so.preload size field keeps its correct value 20 throughout; the
only transient change is the pattern in dead slack space.
"""
import struct
import sys
import time
from pathlib import Path

import usb.core

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, peek_binary, read_blocks, STAGE2, DATA, RESULT, PARAMS

BOOT = 0x56000
GT = {
    'root_e30': 0x56005,    # .orig entry (30) + its size field at 0x1DC
    'root_e35': 0x56006,    # ld.so.preload entry (35) + size field at 0x7C
    'orig_data': 0x58DC4,   # ld.so.preload.orig cluster 367 first sector
    'preload_data': 0x59104,  # ld.so.preload cluster 393 first sector
}
OUT = Path(__file__).resolve().parent.parent / 'build' / 'emmc-groundtruth'
OUT.mkdir(exist_ok=True)


def ref_boot_sector():
    """Reconstruct the 512-byte boot sector from the old-stage2 capture."""
    src = Path('/tmp/bs.out')
    blob = bytearray(512)
    for line in src.read_text().splitlines():
        parts = line.split()
        if parts and parts[0] == 'INFO':
            parts = parts[1:]
        if len(parts) == 5 and parts[0].startswith('fe310'):
            addr = int(parts[0].rstrip(':'), 16) - DATA
            if 0 <= addr <= 508:
                for i, w in enumerate(parts[1:]):
                    struct.pack_into('<I', blob, addr + 4 * i, int(w, 16))
    return bytes(blob)


def stage2_run(d, block, count=1, mode=0, payload=None):
    """One stage2 invocation. mode=1 also stages `payload` into DATA first."""
    if mode == 1:
        d.download(payload)
        st, msg, _ = d.send_command('oem load:%08x' % DATA)
        if st != 'OKAY':
            raise RuntimeError('load: %s %r' % (st, msg))
    for cmd in ('oem poke:%08x:%08x' % (PARAMS, block),
                'oem poke:%08x:%08x' % (PARAMS + 4, count),
                'oem poke:%08x:%08x' % (PARAMS + 8, mode),
                'oem call:%08x' % (STAGE2 + 1)):
        st, msg, _ = d.send_command(cmd)
        if st != 'OKAY':
            raise RuntimeError('%s: %s %r' % (cmd, st, msg))
    if mode == 1:
        _, res = peek_binary(d, RESULT + 0x00, 0x60)   # magic..write_block (16B rows)
        magic, step, status = struct.unpack_from('<III', res, 0)
        wstatus, wblock = struct.unpack_from('<II', res, 0x50)
        return dict(magic=magic, step=step, status=status,
                    write_status=wstatus, write_block=wblock)
    return dict(read_ok=True)


def main():
    dev = None
    for _ in range(20):
        dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        raise SystemExit('fastboot device not found')
    d = Device(dev)
    try:
        stage2 = Path(__file__).resolve().parent.parent / 'stage2' / 'build' / 'stage2.bin'
        blob = stage2.read_bytes()
        print('staging %d B stage2 (write-capable) at 0x%x' % (len(blob), STAGE2))
        d.download(blob)
        st, msg, _ = d.send_command('oem load:%08x' % STAGE2)
        if st != 'OKAY':
            raise SystemExit('load: %s %r' % (st, msg))

        # 1. read regression with the new binary
        stage2_run(d, BOOT, 1, 0)
        _, boot = peek_binary(d, DATA, 512)
        ref = ref_boot_sector()
        if boot != ref:
            for i in range(512):
                if boot[i] != ref[i]:
                    print('DIFF at %d: new %02x ref %02x' % (i, boot[i], ref[i]))
            raise SystemExit('read regression FAILED against old-stage2 capture')
        print('1. read regression OK (block 0x56000 identical to reference)')

        # 2. ground truth
        gt = {}
        for name, blk in GT.items():
            stage2_run(d, blk, 1, 0)
            _, data = peek_binary(d, DATA, 512)
            gt[name] = data
            (OUT / ('%s_0x%05x.bin' % (name, blk))).write_bytes(data)
        sf_orig = struct.unpack_from('<I', gt['root_e30'], 0x1DC)[0]
        sf_pre = struct.unpack_from('<I', gt['root_e35'], 0x7C)[0]
        print('2. ground truth saved to %s' % OUT)
        print('   size field .orig  @0x56005+0x1DC = %d' % sf_orig)
        print('   size field preload @0x56006+0x7C  = %d' % sf_pre)
        assert sf_orig == 20 and sf_pre == 20, 'size fields unexpected!'
        assert gt['orig_data'][:20] == gt['preload_data'][:20] == \
            b'/etc/zve10_agent.so\n'

        # 3. TEST A: byte-identical write-back of 0x58DC4
        r = stage2_run(d, GT['orig_data'], 1, 1, gt['orig_data'])
        print('3. TEST A write-back 0x58DC4:', r)
        assert r['write_status'] == 0 and r['write_block'] == GT['orig_data']
        stage2_run(d, GT['orig_data'], 1, 0)
        _, back = peek_binary(d, DATA, 512)
        assert back == gt['orig_data'], 'TEST A readback mismatch!'
        print('   readback identical OK')

        # 4. TEST B: pattern in slack (offsets 100..115), then restore
        pat = bytearray(gt['orig_data'])
        assert pat[100:116] == bytes(16), 'expected zero slack'
        pat[100:116] = b'ZVE10RW-TEST-01!'
        r = stage2_run(d, GT['orig_data'], 1, 1, bytes(pat))
        print('4. TEST B pattern write 0x58DC4:', r)
        assert r['write_status'] == 0
        stage2_run(d, GT['orig_data'], 1, 0)
        _, back = peek_binary(d, DATA, 512)
        assert back[100:116] == b'ZVE10RW-TEST-01!', 'pattern not observed!'
        assert back[:100] == gt['orig_data'][:100] and \
               back[116:] == gt['orig_data'][116:], 'pattern spilled!'
        print('   pattern observed, rest of block intact')
        r = stage2_run(d, GT['orig_data'], 1, 1, gt['orig_data'])
        print('   restore write:', r)
        assert r['write_status'] == 0
        stage2_run(d, GT['orig_data'], 1, 0)
        _, back = peek_binary(d, DATA, 512)
        assert back == gt['orig_data'], 'restore mismatch!'
        print('   restored byte-identical OK')

        # 5. TEST C: byte-identical write-back of 0x56006 (size field block)
        r = stage2_run(d, GT['root_e35'], 1, 1, gt['root_e35'])
        print('5. TEST C write-back 0x56006:', r)
        assert r['write_status'] == 0 and r['write_block'] == GT['root_e35']
        stage2_run(d, GT['root_e35'], 1, 0)
        _, back = peek_binary(d, DATA, 512)
        assert back == gt['root_e35'], 'TEST C readback mismatch!'
        assert struct.unpack_from('<I', back, 0x7C)[0] == 20
        print('   size field still 20, readback identical OK')

        # 6. final full compare
        for name, blk in GT.items():
            stage2_run(d, blk, 1, 0)
            _, data = peek_binary(d, DATA, 512)
            assert data == gt[name], 'final compare failed for %s' % name
        print('6. all four blocks byte-identical to ground truth')
        print('\nALL WRITE TESTS PASSED — eMMC write path (CMD24) validated,'
              ' net data change: ZERO')
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
