#!/usr/bin/env python3
"""Read-only: dump ld.so.preload / .orig contents + FAT entries via stage2.

Single fastboot session. eMMC traffic is CMD17 read-only.
"""
import struct
import sys
import time
from pathlib import Path

import usb.core

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, read_blocks, STAGE2

BOOT = 0x56000
DATA_SEC = 36                      # reserved(1) + nfats(1)*fat16(3) + root(32)


def clus_block(c):
    return BOOT + DATA_SEC + (c - 2) * 32


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
        blob = Path(__file__).resolve().parent.parent / 'stage2' / 'build' / 'stage2.bin'
        b = blob.read_bytes()
        d.download(b)
        st, msg, _ = d.send_command('oem load:%08x' % STAGE2)
        assert st == 'OKAY', msg

        for label, c in (('ld.so.preload.orig', 367), ('ld.so.preload', 393)):
            blk = clus_block(c)
            data = read_blocks(d, blk, 1)
            print('%s: cluster %d -> block 0x%x' % (label, c, blk))
            print('  first 64 B: %s' % data[:64].hex())
            print('  as text   : %r' % data[:48].decode('latin-1'))

        fat = b''
        for s in range(3):
            fat += read_blocks(d, BOOT + 1 + s, 1)

        def fat12(n):
            off = n + n // 2
            v = struct.unpack_from('<H', fat, off)[0]
            return (v >> 4) if n & 1 else (v & 0xfff)

        for n in (367, 393):
            print('FAT12[%d] = 0x%03x' % (n, fat12(n)))
        print('FAT12 around 393:', {n: hex(fat12(n)) for n in range(391, 397)})
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
