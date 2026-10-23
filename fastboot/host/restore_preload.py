#!/usr/bin/env python3
"""Restore /etc/ld.so.preload: write the ground-truth block 0x56006 back.

This re-enables the preload (size 20 -> content '/etc/zve10_agent.so\n'),
i.e. it UNDOES host/disable_preload.py exactly. Only run it if you really
want the agent preloaded again; run disable_preload.py to undo it once more.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from disable_preload import Stage2, BLK, GTDIR, sha
from read_root_dir import Device
import usb.core, time


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
        s = Stage2(d)
        live = s.read(BLK['e35'])
        gt = (GTDIR / 'root_e35_0x56006.bin').read_bytes()
        if live == gt:
            print('block 0x56006 already matches ground truth - nothing to do')
            return 0
        if live != gt and live[0x7C:0x80] != b'\x00\x00\x00\x00':
            raise SystemExit('unexpected current state (size=%r) - refusing'
                             % live[0x7C:0x80])
        print('current size field = 0 -> restoring GT block (sha %s)' % sha(gt))
        s.write(BLK['e35'], gt)
        rb = s.read(BLK['e35'])
        assert rb == gt, 'readback mismatch after restore'
        print('restored and verified: ld.so.preload size = 20 again')
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
