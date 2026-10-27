#!/usr/bin/env python3
"""Pull the updater's own log off nflasha13 so a rejection reason is exact.

The updater mounts /dev/nflasha13 as its "nonvolatile storage" and writes
  /tmp_updater/storage/nonvolatile/history/update.log
there. That partition is 4 MiB (0x2D400000, LBA 0x16A000), so reading it costs
only a few seconds of fastboot time, and the log names the failing check:
"Old Version:u.u Model:0x... Region:0x...", "New Version:...",
"not meet UUD conditions (ver, model, etc.)".

This speaks the stage2 command set directly:
  oem emmc-init            (once)
  oem upload:<lba>:<count> -> DATA<8 hex>, <raw bytes>, OKAY   (count 1..64)

Requires prebuilt/stage1.bin injected and prebuilt/stage2-flash.bin loaded
(host/bootstrap.py).
"""
import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import usb.core
import usb.util
from bootstrap import Device, wait_for_device

LBA_DEFAULT = 0x16A000      # nflasha13, 0x2D400000 / 512
SECTORS_DEFAULT = 0x2000    # 4 MiB


def upload_blocks(d, lba, count):
    """Read `count` (1..64) blocks and return them."""
    if not 1 <= count <= 64:
        raise ValueError('count must be 1..64')
    tag, body, _ = d.command('oem upload:%x:%x' % (lba, count))
    if tag != b'DATA':
        raise ValueError('oem upload returned %r %r' % (tag, body))
    n = int(body.decode('ascii'), 16)
    got = b''
    while len(got) < n:
        chunk = bytes(d.ep_in.read(min(0x10000, n - len(got)), timeout=15000))
        if not chunk:
            raise ValueError('short read from oem upload')
        got += chunk
    tag2, body2, _ = d.read_reply()
    if tag2 != b'OKAY':
        raise ValueError('oem upload trailer %r %r' % (tag2, body2))
    return got


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='/root/fw/zve10_backup/nflasha13_current.img')
    ap.add_argument('--lba', type=lambda v: int(v, 0), default=LBA_DEFAULT)
    ap.add_argument('--sectors', type=lambda v: int(v, 0), default=SECTORS_DEFAULT)
    a = ap.parse_args(argv)

    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device (18d1:4ee0)')
    d = Device(dev)
    try:
        print('emmc-init ...')
        d.require('oem emmc-init')

        blob = b''
        done = 0
        t0 = time.time()
        while done < a.sectors:
            n = min(64, a.sectors - done)
            blob += upload_blocks(d, a.lba + done, n)
            done += n
            if done % (64 * 16) == 0 or done == a.sectors:
                el = time.time() - t0
                rate = (len(blob) / 1024.0 / el) if el > 0 else 0
                print('  %5d/%d sectors  %.1f KB/s' % (done, a.sectors, rate), end='\r')
        print()
        out = Path(a.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(blob)
        print('saved %s (%d bytes)' % (out, len(blob)))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
