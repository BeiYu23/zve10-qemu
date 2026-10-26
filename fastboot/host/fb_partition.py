#!/usr/bin/env python3
"""Bulk read / write of an eMMC partition over BootROM fastboot + stage2.

WHY
  The existing host scripts only ever used stage2 with count=1 (one 512-byte
  block). stage2 itself DOES implement CMD25/CMD18 multi-block transfers with a
  64 KiB ceiling (STAGE2_DATA_MAX = 0x10000, i.e. 128 blocks), but that path had
  never been exercised on hardware. Flashing /usr needs it.

SAFETY MODEL
  `verify` is the zero-risk step: read a window, write the SAME bytes back with
  the multi-block path, read again and compare. Net change to flash is zero, so
  a bug in the multi-block writer cannot destroy anything, yet a correct result
  proves the writer works before anything real is attempted.

  `write` then streams an image to a partition. It refuses to run unless the
  image size is an exact multiple of the chunk and optionally re-reads chunks to
  confirm they landed.

TARGET
  nflasha15 (/usr): first block 0x16C00, 300 MiB = 614400 sectors.
  (Numbers come from host/n15_check.py, which verified them against the camera.)

USAGE
  python3 fb_partition.py verify  --lba 0x16C00 --blocks 128
  python3 fb_partition.py read    --lba 0x16C00 --blocks 8 --out /tmp/head.bin
  python3 fb_partition.py write   --lba 0x16C00 --image IMG [--verify-every 32]
"""
import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, peek_binary, STAGE2, DATA, RESULT, PARAMS  # noqa: E402

CHUNK_BLOCKS = 128              # 64 KiB, matches STAGE2_DATA_MAX
CHUNK_BYTES = CHUNK_BLOCKS * 512
PART_NFLASHA15_LBA = 0x16C00
PART_NFLASHA15_SECTORS = 300 * 1024 * 1024 // 512

HERE = Path(__file__).resolve().parent.parent
STAGE2_BIN = HERE / 'stage2' / 'build' / 'stage2.bin'
EXPECT_STAGE2_SHA = ('807c1ba6', '01f4f6d9')


def sha16(b):
    import hashlib
    return hashlib.sha256(b).hexdigest()[:16]


def connect():
    import usb.core
    dev = None
    for _ in range(30):
        dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        raise SystemExit('fastboot device not found - is the camera in BootROM '
                         'fastboot mode with the payload injected?')
    return Device(dev)


def load_stage2(d):
    blob = STAGE2_BIN.read_bytes()
    h = sha16(blob)
    if not h.startswith(EXPECT_STAGE2_SHA):
        raise SystemExit('unexpected stage2 binary: %s' % h)
    print('stage2 %d B sha256:%s' % (len(blob), h))
    d.download(blob)
    st, msg, _ = d.send_command('oem load:%08x' % STAGE2)
    if st != 'OKAY':
        raise SystemExit('stage2 load failed: %s %r' % (st, msg))


def stage2_call(d, block, count, mode):
    for cmd in ('oem poke:%08x:%08x' % (PARAMS, block),
                'oem poke:%08x:%08x' % (PARAMS + 4, count),
                'oem poke:%08x:%08x' % (PARAMS + 8, mode),
                'oem call:%08x' % (STAGE2 + 1)):
        st, msg, _ = d.send_command(cmd)
        if st != 'OKAY':
            raise RuntimeError('%s -> %s %r' % (cmd, st, msg))


def read_result(d):
    _, res = peek_binary(d, RESULT, 0x60)
    magic, step, status = struct.unpack_from('<III', res, 0)
    rstatus, rblock = struct.unpack_from('<II', res, 0x48)
    wstatus, wblock = struct.unpack_from('<II', res, 0x50)
    return dict(magic=magic, step=step, status=status,
                read_status=rstatus, read_block=rblock,
                write_status=wstatus, write_block=wblock)


def read_chunk(d, block, nblocks):
    """CMD18 multi-block read into DATA, then pull it out over peek."""
    if nblocks > CHUNK_BLOCKS:
        raise ValueError('max %d blocks per read' % CHUNK_BLOCKS)
    stage2_call(d, block, nblocks, 0)
    r = read_result(d)
    if r['read_status'] != 0:
        raise RuntimeError('read_status=%d at block 0x%x' %
                           (r['read_status'], block))
    if r['read_block'] != block:
        raise RuntimeError('read_block=0x%x expected 0x%x' % (r['read_block'], block))
    nbytes = nblocks * 512
    out = bytearray()
    off = 0
    while off < nbytes:
        take = min(0x1000, nbytes - off)
        _, piece = peek_binary(d, DATA + off, take)
        out += piece
        off += take
    return bytes(out)


def write_chunk(d, block, blob, do_readback=False):
    """Stage blob into DATA then CMD25 multi-block write."""
    nblocks = len(blob) // 512
    if nblocks > CHUNK_BLOCKS:
        raise ValueError('max %d blocks per write' % CHUNK_BLOCKS)
    d.download(blob)
    st, msg, _ = d.send_command('oem load:%08x' % DATA)
    if st != 'OKAY':
        raise RuntimeError('load DATA: %s %r' % (st, msg))
    stage2_call(d, block, nblocks, 1)
    r = read_result(d)
    if r['status'] != 0:
        raise RuntimeError('status=%d at block 0x%x' % (r['status'], block))
    if r['write_status'] != 0:
        raise RuntimeError('write_status=%d at block 0x%x' %
                           (r['write_status'], block))
    if r['write_block'] != block:
        raise RuntimeError('write_block=0x%x expected 0x%x' %
                           (r['write_block'], block))
    if do_readback:
        back = read_chunk(d, block, nblocks)
        if back != blob:
            bad = [i for i in range(min(len(back), len(blob))) if back[i] != blob[i]]
            raise RuntimeError('readback mismatch at block 0x%x, %d bytes differ,'
                               ' first at %s' % (block, len(bad), bad[:4]))
    return r


def cmd_verify(d, a):
    """Zero-risk proof that multi-block write works: same-bytes write-back."""
    print('\n== verify: read -> write back identical -> read -> compare ==')
    total = 0
    t0 = time.time()
    for i in range(a.count):
        block = a.lba + i * a.blocks
        orig = read_chunk(d, block, a.blocks)
        t_read1 = time.time()
        write_chunk(d, block, orig, do_readback=False)
        t_write = time.time()
        back = read_chunk(d, block, a.blocks)
        t_read2 = time.time()
        if back != orig:
            bad = [j for j in range(len(orig)) if orig[j] != back[j]]
            raise SystemExit(
                'FAIL at block 0x%x: %d of %d bytes differ (%s)\n'
                '  => multi-block write does NOT round-trip. Do not flash.'
                % (block, len(bad), len(orig), bad[:8]))
        total += len(orig)
        print('  [%2d/%d] block 0x%06x  %d blocks  read %.2fs write %.2fs reread %.2fs  OK'
              % (i + 1, a.count, block, a.blocks,
                 t_read1 - t0, t_write - t_read1, t_read2 - t_write))
        t0 = t_read2
    print('\nPASS: %d bytes round-tripped identically through CMD25.'
          % total)
    print('  write path is usable; net change to flash = 0')
    return 0


def cmd_read(d, a):
    blob = read_chunk(d, a.lba, a.blocks)
    Path(a.out).write_bytes(blob)
    print('read %d blocks (%d B) from 0x%x -> %s  sha256:%s'
          % (a.blocks, len(blob), a.lba, a.out, sha16(blob)))
    return 0


def cmd_write(d, a):
    img = Path(a.image).read_bytes()
    n = len(img)
    print('image %s: %d B (%.1f MiB)' % (a.image, n, n / 1048576))
    if n % CHUNK_BYTES:
        raise SystemExit('image size %d is not a multiple of %d bytes; pads are '
                         'too risky, refusing' % (n, CHUNK_BYTES))
    total_chunks = n // CHUNK_BYTES
    end = a.lba + n // 512
    print('target: LBA 0x%x .. 0x%x  (%d sectors, %d chunks of 64 KiB)'
          % (a.lba, end - 1, n // 512, total_chunks))
    if a.expect_lba is not None and a.lba != a.expect_lba:
        raise SystemExit('lba 0x%x is not the expected 0x%x' % (a.lba, a.expect_lba))

    done_chunks = 0
    if a.resume_file:
        rf = Path(a.resume_file)
        if rf.exists():
            try:
                done_chunks = int(rf.read_text().strip() or 0)
            except ValueError:
                done_chunks = 0
        print('resume: %d/%d chunks already written (%.1f MiB), continuing'
              % (done_chunks, total_chunks, done_chunks * CHUNK_BYTES / 1048576))

    def save_progress(n):
        if a.resume_file:
            Path(a.resume_file).write_text('%d\n' % n)

    if a.verify_target:
        print('ADDRESS PROOF: reading one chunk at LBA 0x%x and comparing with %s'
              ' at 0x%x ...' % (a.lba, a.verify_target, a.target_offset))
        ref = Path(a.verify_target).read_bytes()
        seg = ref[a.target_offset:a.target_offset + CHUNK_BYTES]
        if len(seg) != CHUNK_BYTES:
            raise SystemExit('proof target too short')
        live = read_chunk(d, a.lba, CHUNK_BLOCKS)
        if live != seg:
            bad = [i for i in range(CHUNK_BYTES) if live[i] != seg[i]]
            raise SystemExit(
                'ADDRESS PROOF FAILED at LBA 0x%x: %d of %d bytes differ from '
                'the reference at offset 0x%x (first diffs %s)\n'
                '  => this LBA is NOT where you think it is. REFUSING TO WRITE.'
                % (a.lba, len(bad), CHUNK_BYTES, a.target_offset, bad[:6]))
        print('  proof OK: LBA 0x%x holds exactly the reference bytes at 0x%x'
              % (a.lba, a.target_offset))

    t0 = time.time()
    written_this_run = 0
    for i in range(done_chunks, total_chunks):
        block = a.lba + i * CHUNK_BLOCKS
        chunk = img[i * CHUNK_BYTES:(i + 1) * CHUNK_BYTES]
        do_rb = a.verify_every and ((i + 1) % a.verify_every == 0)
        write_chunk(d, block, chunk, do_readback=do_rb)
        written_this_run += 1
        save_progress(i + 1)
        if (i + 1) % 8 == 0 or i + 1 == total_chunks:
            done_b = (i + 1) * CHUNK_BYTES
            el = time.time() - t0
            rate = written_this_run * CHUNK_BYTES / el / 1024 / 1024 if el else 0
            left = (total_chunks - i - 1) * CHUNK_BYTES
            eta = left / (written_this_run * CHUNK_BYTES / el) if el and written_this_run else 0
            print('  %5d/%d  %.1f/%.1f MiB  %.2f MiB/s  ETA %.0fs%s'
                  % (i + 1, total_chunks, done_b / 1048576, n / 1048576,
                     rate, eta, '  (verified)' if do_rb else ''),
                  flush=True)
    print('\nwrote %d chunks (%d bytes) this session in %.1fs'
          % (written_this_run, written_this_run * CHUNK_BYTES, time.time() - t0))
    if done_chunks + written_this_run >= total_chunks:
        print('ALL %d chunks written - flashing complete.' % total_chunks)
        print('power-cycle and see whether the camera boots.')
    else:
        print('INCOMPLETE: %d of %d chunks done (%0.1f%%).'
              % (done_chunks + written_this_run, total_chunks,
                 100.0 * (done_chunks + written_this_run) / total_chunks))
        print('re-inject and run the same command again to continue.')
    return 0


def cmd_compare(d, a):
    """Read chunks from flash and compare against an image file. Read-only."""
    img = Path(a.image).read_bytes()
    n = len(img)
    if n % CHUNK_BYTES:
        raise SystemExit('image size must be a multiple of %d' % CHUNK_BYTES)
    total = n // CHUNK_BYTES
    start = getattr(a, 'start_chunk', 0)
    limit = getattr(a, 'max_chunks', 0) or total
    mism = []
    t0 = time.time()
    for i in range(start, min(total, limit)):
        block = a.lba + i * CHUNK_BLOCKS
        want = img[i * CHUNK_BYTES:(i + 1) * CHUNK_BYTES]
        got = read_chunk(d, block, CHUNK_BLOCKS)
        if got != want:
            bad = [j for j in range(len(want)) if want[j] != got[j]]
            mism.append((i, block, len(bad), bad[:6]))
            print('  MISMATCH chunk %d (block 0x%x): %d bytes differ, first %s'
                  % (i, block, len(bad), bad[:6]), flush=True)
        elif (i + 1) % 16 == 0 or i == min(total, limit) - 1:
            print('  chunk %d/%d OK  (%.1fs)' % (i + 1, min(total, limit),
                                                 time.time() - t0), flush=True)
    print()
    if mism:
        print('DIFFER: %d of %d chunks mismatch (first bad chunk %d)'
              % (len(mism), min(total, limit) - start, mism[0][0]))
        return 1
    print('MATCH: chunks %d..%d identical to %s'
          % (start, min(total, limit) - 1, a.image))
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)

    p = sub.add_parser('verify', help='zero-risk multi-block write-back test')
    p.add_argument('--lba', type=lambda x: int(x, 0), default=PART_NFLASHA15_LBA)
    p.add_argument('--blocks', type=int, default=CHUNK_BLOCKS)
    p.add_argument('--count', type=int, default=4)
    p.set_defaults(fn=cmd_verify)

    p = sub.add_parser('read', help='read blocks to a file')
    p.add_argument('--lba', type=lambda x: int(x, 0), required=True)
    p.add_argument('--blocks', type=int, default=CHUNK_BLOCKS)
    p.add_argument('--out', required=True)
    p.set_defaults(fn=cmd_read)

    p = sub.add_parser('compare', help='read flash and compare with an image')
    p.add_argument('--lba', type=lambda x: int(x, 0), default=PART_NFLASHA15_LBA)
    p.add_argument('--image', required=True)
    p.add_argument('--start-chunk', type=int, default=0)
    p.add_argument('--max-chunks', type=int, default=0)
    p.set_defaults(fn=cmd_compare)

    p = sub.add_parser('write', help='stream an image into the partition')
    p.add_argument('--lba', type=lambda x: int(x, 0), default=PART_NFLASHA15_LBA)
    p.add_argument('--expect-lba', type=lambda x: int(x, 0),
                   default=PART_NFLASHA15_LBA)
    p.add_argument('--image', required=True)
    p.add_argument('--verify-every', type=int, default=0,
                   help='read back one chunk every N chunks (0 = never)')
    p.add_argument('--verify-target', default=None, metavar='BACKUP_IMG',
                   help='READ-ONLY address proof: before writing, read the first '
                        'chunk at --lba and require it to equal BACKUP_IMG at '
                        '--target-offset. Refuses to write on mismatch. Use this '
                        'to prove the LBA is where you think it is.')
    p.add_argument('--target-offset', type=lambda x: int(x, 0), default=0,
                   help='byte offset inside --verify-target that corresponds to --lba')
    p.add_argument('--resume-file', default=None,
                   help='progress file: skip chunks already written, so a '
                        'session that dies can be continued after re-injection')
    p.set_defaults(fn=cmd_write)

    a = ap.parse_args(argv)
    if getattr(a, 'blocks', 0) > CHUNK_BLOCKS:
        raise SystemExit('--blocks max is %d' % CHUNK_BLOCKS)

    d = connect()
    try:
        load_stage2(d)
        return a.fn(d, a)
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
