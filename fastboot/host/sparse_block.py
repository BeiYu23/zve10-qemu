#!/usr/bin/env python3
"""Write one 4096-byte block at an arbitrary byte offset inside a partition.

WHY THIS EXISTS
  stage2's `flash:<part>` always starts at the partition's first byte, so a
  naive "download 512 bytes then flash:nflasha2" would overwrite the FAT boot
  sector. Sparse images solve that: a DONT_CARE chunk first tells the device to
  leave the preceding range alone, so only the RAW chunk is written.

  Layout:  SKIP N blocks | RAW 1 block | SKIP rest
  The whole image is a few hundred bytes, so it fits one download.

SPARSE FORMAT (matches tools/make_sparse.py and stage2/src/fb.c)
  file header 28 B : magic 0xED26FF3A, major 1, minor 0, fhdr 28, chdr 12,
                     blk_sz 4096, total_blks, total_chunks, checksum 0
  chunk header 12 B: type, reserved, chunk_sz (blocks), total_sz (bytes)

USAGE
  # replace the 4096-byte block containing /setting/updater at 0x?? with a file
  python3 host/sparse_block.py --part nflasha2 --offset 0x610A00 \
      --data /tmp/newblock.bin [--yes]

  # read-only: just show which block would be touched
  python3 host/sparse_block.py --part nflasha2 --offset 0x610A00 --dry-run
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from bootstrap import Device, wait_for_device, download

MAGIC = 0xED26FF3A
BLK = 4096
CHUNK_RAW = 0xCAC1
CHUNK_SKIP = 0xCAC3


def build_single_block(offset, data, part_bytes):
    """Sparse image that writes `data` at `offset` and leaves everything else."""
    if len(data) != BLK:
        raise ValueError('data must be exactly %d bytes, got %d' % (BLK, len(data)))
    if offset % BLK:
        raise ValueError('offset must be %d-aligned' % BLK)
    if offset + BLK > part_bytes:
        raise ValueError('block past end of partition')
    total = part_bytes // BLK
    pre = offset // BLK
    post = total - pre - 1
    chunks = []
    if pre:
        chunks.append(struct.pack('<HHII', CHUNK_SKIP, 0, pre, 12))
    chunks.append(struct.pack('<HHII', CHUNK_RAW, 0, 1, 12 + BLK) + data)
    if post:
        chunks.append(struct.pack('<HHII', CHUNK_SKIP, 0, post, 12))
    hdr = struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLK, total, len(chunks), 0)
    return hdr + b''.join(chunks)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', required=True)
    ap.add_argument('--offset', required=True, type=lambda v: int(v, 0))
    ap.add_argument('--data', help='file with exactly 4096 bytes of replacement content')
    ap.add_argument('--out', help='write the sparse image here instead of sending it')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--yes', action='store_true')
    a = ap.parse_args(argv)

    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    try:
        d.require('oem emmc-init')
        body, _ = d.require('getvar:partition-size:%s' % a.part)
        part_bytes = int(body.decode('ascii').strip(), 16)
        print('%s = %d bytes (%d sparse blocks)' % (a.part, part_bytes, part_bytes // BLK))
        blk_index = a.offset // BLK
        print('target block %d at byte 0x%X' % (blk_index, blk_index * BLK))
        if a.dry_run:
            print('dry run, nothing sent')
            return 0
        if not a.data:
            raise SystemExit('--data required unless --dry-run')
        data = Path(a.data).read_bytes()
        img = build_single_block(blk_index * BLK, data, part_bytes)
        print('sparse image: %d bytes' % len(img))
        if a.out:
            Path(a.out).write_bytes(img)
            print('written to %s' % a.out)
            return 0
        if not a.yes:
            print('add --yes to send it')
            return 0
        download(d, img)
        d.require('flash:%s' % a.part)
        print('flashed one block into %s at 0x%X' % (a.part, blk_index * BLK))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
