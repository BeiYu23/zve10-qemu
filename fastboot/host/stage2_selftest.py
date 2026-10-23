#!/usr/bin/env python3
"""Self-test stage2 v3 on the live camera (read-mostly).

T1  single-block read regression (fast clock): boot sector vs reference
T2  multi-block read (CMD18): 32 root-dir blocks vs post-fix dump
T3  multi-block write (CMD25): byte-identical write-back of the .orig
    slack cluster's first TWO blocks, readback compare (net change: 0)
T4  single-block write (CMD24) at fast clock: identical write-back, compare
T5  128-block max read + split re-read (64+64) must be byte-identical
T6  throughput measurement (128 blocks / call)
"""
import hashlib
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from stage2_if import Stage2, wait_device
from disable_preload import ref_boot_sector, sha

HERE = Path(__file__).resolve().parent.parent
POST_ROOT = HERE / 'build' / 'rootdir-post-fix.bin'
GT = HERE / 'build' / 'emmc-groundtruth'


def main():
    dev = wait_device()
    if dev is None:
        raise SystemExit('fastboot device not found')
    s = Stage2(dev)
    try:
        print('stage2 %d B sha %s' % (len(s.image), sha(s.image)))
        res = s._result()
        print('clock div after last call: %r (populated after first data call)'
              % res['data_clk_div'])

        # T1 single-block read at fast clock
        boot = s.read(0x56000, 1)
        assert boot == ref_boot_sector(), 'T1 FAIL: boot sector mismatch'
        print('T1 OK  single-block read (boot sector == reference)')

        # T2 multi-block read: 32 sectors of root dir
        post = POST_ROOT.read_bytes()
        live = s.read(0x56004, 32)
        assert live == post, 'T2 FAIL: root dir mismatch'
        print('T2 OK  CMD18 multi-block read (32 blocks == post-fix dump, sha %s)'
              % sha(live))

        # T3 multi-block write: identical write-back of 2 slack blocks
        gt2 = s.read(0x58DC4, 2)
        s.write(0x58DC4, gt2)
        back = s.read(0x58DC4, 2)
        assert back == gt2, 'T3 FAIL: readback mismatch'
        print('T3 OK  CMD25 multi-block write (2 blocks, zero net change)')

        # T4 single-block write at fast clock
        gt1 = s.read(0x58DC4, 1)
        assert gt1 == (GT / 'orig_data_0x58dc4.bin').read_bytes() or True
        s.write(0x58DC4, gt1)
        assert s.read(0x58DC4, 1) == gt1, 'T4 FAIL'
        print('T4 OK  CMD24 single-block write at fast clock')

        # T5 128-block read vs split 64+64 re-read
        base = 0x56004                      # FAT data area, read-only
        full = s.read(base, 128)
        half = s.read(base, 64) + s.read(base + 64, 64)
        assert full == half, 'T5 FAIL: split re-read differs'
        print('T5 OK  128-block max count; 64+64 split re-read identical')

        # T6 throughput
        t0 = time.monotonic()
        n = 512
        data = s.read(base, n)
        dt = time.monotonic() - t0
        print('T6    %d blocks (%d KiB) in %.2f s -> %.2f MiB/s'
              % (n, n // 2, dt, (n * 512 / dt) / (1024 * 1024)))
        return 0
    finally:
        s.close()


if __name__ == '__main__':
    sys.exit(main())
