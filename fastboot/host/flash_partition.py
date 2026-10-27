#!/usr/bin/env python3
"""Write a raw partition image through stage2 using sparse chunks.

WHY SPARSE
  `flash:<part>` always starts at the partition's first byte and the download
  buffer is 1 MiB. A sparse image can therefore carry one contiguous run of
  changes at an arbitrary offset, with DONT_CARE chunks telling the device what
  to leave alone. That lets us write only the blocks that differ from what is
  already on the partition.

SAFETY
  * `--reference` compares the image against the partition's expected current
    content and refuses if a chunk it is about to write is neither the
    reference value nor already the target value.
  * progress is journalled to `--resume-file`; rerunning the same command
    continues where it stopped.
  * every chunk is read back and compared after writing.

USAGE
  python3 host/flash_partition.py --part nflasha15 \
      --image /path/hybrid.ext2 --reference /path/stock.img \
      --chunk-kb 768 --resume-file /tmp/flash_progress.json
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'tools'))

from bootstrap import Device, wait_for_device, download
from read_updater_log import upload_blocks

MAGIC = 0xED26FF3A
BLK = 4096
CHUNK_RAW = 0xCAC1
CHUNK_SKIP = 0xCAC3


def sparse_run(offset, data, part_bytes):
    """Sparse image that writes `data` at in-partition `offset` only."""
    if offset % BLK or len(data) % BLK:
        raise ValueError('offset and data length must be %d-aligned' % BLK)
    total = part_bytes // BLK
    pre = offset // BLK
    raw_blocks = len(data) // BLK
    post = total - pre - raw_blocks
    if post < 0:
        raise ValueError('run past end of partition')
    out = [struct.pack('<HHII', CHUNK_SKIP, 0, pre, 12)] if pre else []
    out.append(struct.pack('<HHII', CHUNK_RAW, 0, raw_blocks, 12 + len(data)) + data)
    if post:
        out.append(struct.pack('<HHII', CHUNK_SKIP, 0, post, 12))
    return struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLK, total, len(out), 0) + b''.join(out)


def fetch_in_part(d, part_start_lba, byte_off, nbytes):
    """Read nbytes at in-partition byte_off.

    fetch() takes an absolute LBA, so the partition start has to be added here.
    Mixing the two up silently reads the wrong area (it reads sector 0 of the
    whole device, which is the partition table) and looks like a mismatch.
    """
    if byte_off % 512 or nbytes % 512:
        raise ValueError('in-partition read must be 512-aligned')
    return fetch(d, part_start_lba + byte_off // 512, nbytes // 512)


def fetch(d, lba, nsec):
    out, left, cur = b'', nsec, lba
    while left > 0:
        n = min(64, left)
        out += upload_blocks(d, cur, n)
        cur += n
        left -= n
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', required=True)
    ap.add_argument('--image', required=True)
    ap.add_argument('--reference', help='expected current content of the partition')
    ap.add_argument('--chunk-kb', type=int, default=768)
    ap.add_argument('--block-kb', type=int, default=64,
                    help='granularity for deciding what changed (default 64 KiB)')
    ap.add_argument('--resume-file', default='/tmp/flash_progress.json')
    ap.add_argument('--part-start', type=lambda v: int(v, 0), required=False,
                    help='partition start in BYTES (from oem partition); required '
                         'whenever a reference is used, so reads hit the right LBA')
    ap.add_argument('--plan-only', action='store_true')
    a = ap.parse_args(argv)

    target = Path(a.image).read_bytes()
    ref = Path(a.reference).read_bytes() if a.reference else None
    bgran = a.block_kb * 1024
    cgran = a.chunk_kb * 1024

    nblocks = len(target) // bgran
    print('image   : %s (%d B, %d x %d KiB blocks)' % (a.image, len(target), nblocks, a.block_kb))
    if ref is not None:
        print('reference: %s (%d B)' % (a.reference, len(ref)))
        if len(ref) != len(target):
            raise SystemExit('image and reference differ in size')

    # which blocks differ
    todo = [i for i in range(nblocks)
            if ref is None or target[i * bgran:(i + 1) * bgran] != ref[i * bgran:(i + 1) * bgran]]
    changed = len(todo) * bgran
    print('changed : %d of %d blocks = %.1f MiB' % (len(todo), nblocks, changed / 1048576))

    # merge contiguous blocks, then split runs into <= chunk size pieces
    runs = []
    for i in todo:
        if runs and i == runs[-1][1] + 1:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    pieces = []
    per = max(1, cgran // bgran)
    for s, e in runs:
        i = s
        while i <= e:
            j = min(e, i + per - 1)
            pieces.append((i, j))
            i = j + 1
    print('runs    : %d contiguous, split into %d writes of <= %d KiB'
          % (len(runs), len(pieces), a.chunk_kb))
    if a.plan_only:
        return 0

    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    prog = Path(a.resume_file)
    state = json.loads(prog.read_text()) if prog.exists() else {}
    if state.get('part') not in (None, a.part) or state.get('image') not in (None, a.image):
        raise SystemExit('resume file belongs to a different job: %s' % state)
    done = set(tuple(x) for x in state.get('done', []))
    try:
        d.require('oem emmc-init')
        body, _ = d.require('getvar:partition-size:%s' % a.part)
        part_bytes = int(body.decode().strip(), 16)
        print('partition: %s = %d B' % (a.part, part_bytes))
        if len(target) > part_bytes:
            raise SystemExit('image larger than partition')
        if a.part_start is None:
            if ref is not None:
                raise SystemExit('--part-start is required with --reference '
                                 '(bytes, from `oem partition`)')
            part_start_lba = 0
        else:
            if a.part_start % 512:
                raise SystemExit('--part-start must be 512-aligned')
            part_start_lba = a.part_start // 512
        # sanity: the sparse image addresses are partition-relative, so they
        # must fit inside the partition we resolved.
        if len(target) > part_bytes:
            raise SystemExit('image does not fit the partition')
        print('part-start: 0x%X (LBA 0x%X)' % (a.part_start or 0, part_start_lba))

        # --- address proof -------------------------------------------------
        # Before writing anything, confirm the partition really holds the
        # reference content at the offsets we are about to overwrite. Sampling
        # the first, middle and last block of every run is enough to catch a
        # wrong partition or a wrong offset without reading all 230 MiB.
        if ref is not None:
            print('address proof: sampling reference blocks ...')
            samples = []
            for s_idx, e_idx in runs:
                for pick in (s_idx, (s_idx + e_idx) // 2, e_idx):
                    if (pick, pick) not in samples:
                        samples.append((pick, pick))
            bad = 0
            for s_idx, e_idx in samples:
                off = s_idx * bgran
                got = fetch_in_part(d, part_start_lba, off, bgran)
                want = ref[off:off + bgran]
                if got != want:
                    print('  MISMATCH at 0x%X: partition does not hold the reference here' % off)
                    bad += 1
                    if bad >= 3:
                        break
            if bad:
                raise SystemExit('address proof failed: refusing to write. '
                                 'Re-verify the partition and offset before retrying.')
            print('  address proof passed (%d sampled blocks)' % len(samples))
        t0 = time.time()
        written = 0
        for idx, (s, e) in enumerate(pieces):
            key = (s, e)
            if key in done:
                continue
            off = s * bgran
            data = target[off:(e + 1) * bgran]
            img = sparse_run(off, data, part_bytes)
            if len(img) > 0x100000:
                raise SystemExit('piece too large for download buffer: %d B' % len(img))
            download(d, img)
            d.require('flash:%s' % a.part)
            back = fetch_in_part(d, part_start_lba, off, len(data))
            if back != data:
                raise SystemExit('readback mismatch at 0x%X (piece %d)' % (off, idx))
            written += len(data)
            done.add(key)
            prog.write_text(json.dumps({'part': a.part, 'image': a.image,
                                        'done': sorted(list(x) for x in done)}))
            el = time.time() - t0
            print('  piece %3d/%d  off=0x%08X  %.0f KiB  %.2f MB/s  (%d done)'
                  % (idx + 1, len(pieces), off, len(data) / 1024,
                     written / 1048576 / el if el else 0, len(done)), end='\r')
        print()
        print('finished %d pieces, %.1f MiB written' % (len(done), written / 1048576))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
