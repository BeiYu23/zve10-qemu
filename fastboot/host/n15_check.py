#!/usr/bin/env python3
"""Step 0 baseline: sample-verify real-camera nflasha15 (/usr) against the
2026-10-16 full-disk dump, read-only.

Mapping (documented in 救砖操作手册 附录 B / 抢救状态):
  whole-disk image: user area starts at +0x400000 (boot0 2 MiB + boot1 2 MiB)
  nflasha15: user-area offset 0x2D800000 -> image offset 0x2DC00000, 300 MiB
  eMMC block of partition start: 0x2D800000 / 512 = 0x16C00

Samples: first and last 64 KiB chunks plus --samples evenly spaced 64 KiB
chunks; every byte must match the image. Any diff => the real /usr changed
since 10-16 (report, do not write anything).
"""
import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from stage2_if import Stage2, wait_device

IMG = '/mnt/build/zve10_full_emmc.img'
IMG_OFF = 0x2DC00000
PART_BLOCKS = 300 * 1024 * 1024 // 512      # 614400
PART_BLOCK0 = 0x2D800000 // 512             # 0x16C00


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--samples', type=int, default=66,
                    help='evenly spaced 64 KiB chunks (plus first/last)')
    args = ap.parse_args()

    img = Path(IMG)
    if not img.exists():
        raise SystemExit('full-disk image missing: %s' % IMG)
    f = img.open('rb')

    dev = wait_device()
    if dev is None:
        raise SystemExit('fastboot device not found')
    s = Stage2(dev)
    chunks = 64 * 1024 // 512                # 128 blocks per chunk

    positions = [0, PART_BLOCKS - chunks]
    step = (PART_BLOCKS - 2 * chunks) // max(1, args.samples)
    positions += list(range(chunks, PART_BLOCKS - chunks, step))[:args.samples]
    positions = sorted(set(positions))
    print('verifying %d chunks x 64 KiB (~%.1f MiB of 300 MiB partition)'
          % (len(positions), len(positions) * 64 / 1024))

    fails = 0
    try:
        for i, off in enumerate(positions):
            live = s.read(PART_BLOCK0 + off, chunks)
            f.seek(IMG_OFF + off * 512)
            ref = f.read(chunks * 512)
            if live == ref:
                print('  chunk %3d @ block 0x%06x (+%7.2f MiB) OK'
                      % (i, PART_BLOCK0 + off, off * 512 / 1048576))
            else:
                fails += 1
                diffs = [j for j in range(0, len(live)) if live[j] != ref[j]]
                print('  chunk %3d @ block 0x%06x MISMATCH: %d bytes, first at +0x%x'
                      ' (live %02x ref %02x)'
                      % (i, PART_BLOCK0 + off, len(diffs), diffs[0],
                         live[diffs[0]], ref[diffs[0]]))
        print('\n%s: %d/%d chunks match the 2026-10-16 dump'
              % ('BASELINE CONFIRMED' if fails == 0 else 'DIFFERENCES FOUND',
                 len(positions) - fails, len(positions)))
        return 1 if fails else 0
    finally:
        s.close()
        f.close()


if __name__ == '__main__':
    sys.exit(main())
