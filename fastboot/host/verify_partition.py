#!/usr/bin/env python3
"""Verify a whole partition against an image, byte for byte.

Reads in 32 KiB fastboot chunks (the oem upload limit is 64 blocks) and compares
against the local image, reporting every mismatching 64 KiB block. Used after a
resumed write, where a sampled check is not enough evidence.
"""
import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from bootstrap import Device, wait_for_device
from read_updater_log import upload_blocks

CH = 64 * 1024          # 64 KiB compared at a time
READ = 64               # blocks per oem upload call (max)


def read_bytes(d, lba, nbytes):
    out, left, cur = b'', nbytes // 512, lba
    while left > 0:
        n = min(READ, left)
        out += upload_blocks(d, cur, n)
        cur += n
        left -= n
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--part-start', required=True, type=lambda v: int(v, 0))
    ap.add_argument('--image', required=True)
    ap.add_argument('--max-report', type=int, default=40)
    a = ap.parse_args(argv)

    img = Path(a.image).read_bytes()
    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    lba = a.part_start // 512
    try:
        d.require('oem emmc-init')
        n = len(img) // CH
        bad = []
        t0 = time.time()
        for i in range(n):
            got = read_bytes(d, lba + i * CH // 512, CH)
            if got != img[i * CH:(i + 1) * CH]:
                bad.append(i)
            if (i + 1) % 200 == 0 or i == n - 1:
                el = time.time() - t0
                pct = 100.0 * (i + 1) / n
                print('  %5.1f%%  %.1f MB/s  bad=%d' %
                      (pct, (i + 1) * CH / 1048576 / el if el else 0, len(bad)), end='\r')
        print()
        print('checked %d x 64 KiB = %.1f MiB in %.0f s' % (n, n * CH / 1048576, time.time() - t0))
        if not bad:
            print('RESULT: 0 mismatching blocks -- partition matches the image exactly')
            return 0
        print('RESULT: %d mismatching 64 KiB blocks' % len(bad))
        for i in bad[:a.max_report]:
            print('   block %d  offset 0x%08X' % (i, i * CH))
        if len(bad) > a.max_report:
            print('   ... and %d more' % (len(bad) - a.max_report))
        return 1
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
