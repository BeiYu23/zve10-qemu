#!/usr/bin/env python3
"""Read /tmp (nflasha10) to see how far the failed boot got.

/tmp is a real partition (0x0AC00000, 12 MiB) so it survives a reset, unlike a
tmpfs. bootin.elf writes /tmp/qsi.elf.pid when it launches qsi.elf, and the
input layer writes /tmp/appfw_infra/ilog_data_*.bin, so the presence of those
files shows exactly which stage ran before the boot stopped.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from bootstrap import Device, wait_for_device
from read_updater_log import upload_blocks
from dump_and_scan import fat_parse, walk, read_file

LBA = 0x0AC00000 // 512
NBYTES = 0x00C00000          # 12 MiB


def fetch(d, lba, nsec):
    out, left, cur = b'', nsec, lba
    while left > 0:
        n = min(64, left)
        out += upload_blocks(d, cur, n)
        cur += n
        left -= n
    return out


def main():
    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    try:
        d.require('oem emmc-init')
        print('reading nflasha10 (/tmp, %d B) ...' % NBYTES)
        img = fetch(d, LBA, NBYTES // 512)
        out = Path('/root/fw/zve10_backup/nflasha10_current.img')
        out.write_bytes(img)
        print('saved %s' % out)
        fs = fat_parse(img, LBA, NBYTES)
        if not fs:
            print('!! not FAT; first 64 bytes: %s' % img[:64].hex())
            return 1
        print('\n=== /tmp tree ===')
        found = []
        for p, sz, c, isdir in walk(img, fs, 0):
            print('  %s %-50s %9d B' % ('DIR ' if isdir else 'FILE', p, sz))
            found.append(p)
            if not isdir and 0 < sz <= 256 and c >= 2:
                raw = read_file(img, fs, c, sz)
                print('        = %r' % raw)
        print('\n=== markers ===')
        for m in ('qsi.elf.pid', 'appfw_infra', 'lt', 'leaktracer'):
            hit = [p for p in found if m in p]
            print('  %-16s %s' % (m, ('FOUND ' + str(hit[:3])) if hit else 'absent'))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
