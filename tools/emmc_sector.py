#!/usr/bin/env python3
"""Read or patch ONE 512-byte eMMC sector through the fastboot stage2 path.

Purpose: change a single byte inside a file without rewriting the file. The
sector holding /setting/ULOGIO.BIN's TELN record is block 0x07085 (nflasha2
starts at block 0x04000, ULOGIO.BIN's first cluster 389 maps to partition
sector 12421), and offset 0x9C lives inside the first sector.

    # read a sector out, for cross-checking against a file read over the
    # service channel
    python3 emmc_sector.py read --block 0x07085 --out /tmp/sec.bin

    # patch one byte and write the sector back
    python3 emmc_sector.py patch --block 0x07085 --offset 0x9C --value 1 \
        --expect /tmp/ulogio.bin

`patch` verifies that the sector read from eMMC matches the first 512 bytes of
the reference file when --expect is given, refuses to write if it does not,
then re-reads and confirms that only the intended byte changed.
"""
import argparse
import hashlib
import struct
import sys
import time
from pathlib import Path

import usb.core

STAGE2 = 0xFE300000
PARAMS = 0xFE320000
DATA = 0xFE310000
RESULT = 0xFE308000
VID, PID = 0x18D1, 0x4EE0


class Dev:
    def __init__(self, dev):
        self.dev = dev
        try:
            cfg = dev.get_active_configuration()
        except usb.core.USBError:
            dev.set_configuration()
            cfg = dev.get_active_configuration()
        self.intf = cfg[(0, 0)]
        usb.util.claim_interface(dev, self.intf)
        self.out = usb.util.find_descriptor(
            self.intf, custom_match=lambda e: usb.util.endpoint_direction(
                e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        self.inp = usb.util.find_descriptor(
            self.intf, custom_match=lambda e: usb.util.endpoint_direction(
                e.bEndpointAddress) == usb.util.ENDPOINT_IN)

    def close(self):
        try:
            usb.util.release_interface(self.dev, self.intf)
        except Exception:
            pass
        usb.util.dispose_resources(self.dev)

    def reply(self, timeout=20000):
        infos = []
        while True:
            r = bytes(self.inp.read(64, timeout=timeout))
            tag, body = r[:4], r[4:]
            if tag == b'INFO':
                infos.append(body.decode('utf-8', 'replace').rstrip())
                continue
            return tag.decode('latin-1'), body, infos

    def cmd(self, c, timeout=20000):
        self.out.write(c.encode(), timeout=timeout)
        return self.reply(timeout)

    def download(self, blob):
        st, msg, _ = self.cmd('download:%08x' % len(blob))
        if st != 'DATA':
            raise RuntimeError('download rejected %s %r' % (st, msg))
        sent = 0
        while sent < len(blob):
            piece = blob[sent:sent + 4096]
            self.out.write(piece, timeout=10000)
            sent += len(piece)
        st, _, _ = self.reply()
        if st != 'OKAY':
            raise RuntimeError('data phase %s' % st)


def peek(d, addr, n):
    out = bytearray()
    while len(out) < n:
        take = min(0x1000, n - len(out))
        st, msg, infos = d.cmd('oem peek:%08x:%x' % (addr + len(out), take))
        if st != 'OKAY':
            raise RuntimeError('peek %s' % st)
        got = bytearray()
        for line in infos:
            p = line.split()
            if len(p) != 5:
                continue
            if int(p[0].rstrip(':'), 16) != addr + len(out) + len(got):
                raise RuntimeError('peek misaligned: %s' % line)
            for w in p[1:]:
                got += struct.pack('<I', int(w, 16))
        if len(got) != take:
            raise RuntimeError('peek short: %d/%d' % (len(got), take))
        out += got
    return bytes(out)


def stage2(d, block, count=1, mode=0, payload=None):
    if mode == 1:
        d.download(payload)
        st, _, _ = d.cmd('oem load:%08x' % DATA)
        if st != 'OKAY':
            raise RuntimeError('load failed')
    for c in ('oem poke:%08x:%08x' % (PARAMS, block),
              'oem poke:%08x:%08x' % (PARAMS + 4, count),
              'oem poke:%08x:%08x' % (PARAMS + 8, mode),
              'oem call:%08x' % (STAGE2 + 1)):
        st, _, _ = d.cmd(c)
        if st != 'OKAY':
            raise RuntimeError('%s -> %s' % (c, st))
    res = peek(d, RESULT, 0x60)
    status = struct.unpack_from('<I', res, 8)[0]
    wst, wblk = struct.unpack_from('<II', res, 0x50)
    if status != 0:
        raise RuntimeError('stage2 status=%d' % status)
    if mode == 1 and (wst != 0 or wblk != block):
        raise RuntimeError('write_status=%d block=0x%x' % (wst, wblk))


def open_device(seconds=60):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None:
            time.sleep(1.5)
            d = Dev(dev)
            blob = Path(__file__).resolve().parent.parent / 'fastboot' / \
                'stage2' / 'build' / 'stage2.bin'
            if not blob.is_file():
                blob = Path('/root/fw/zv-e10-toolkit/fastboot/stage2/build/stage2.bin')
            d.download(blob.read_bytes())
            st, _, _ = d.cmd('oem load:%08x' % STAGE2)
            if st != 'OKAY':
                raise RuntimeError('stage2 load failed')
            return d
        time.sleep(0.5)
    raise SystemExit('fastboot device not found')


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('read')
    r.add_argument('--block', type=lambda v: int(v, 0), required=True)
    r.add_argument('--out', required=True)
    p = sub.add_parser('patch')
    p.add_argument('--block', type=lambda v: int(v, 0), required=True)
    p.add_argument('--offset', type=lambda v: int(v, 0), required=True)
    p.add_argument('--value', type=lambda v: int(v, 0), required=True)
    p.add_argument('--expect', help='reference file whose first 512 bytes must match')
    args = ap.parse_args()

    d = open_device()
    try:
        if args.cmd == 'read':
            stage2(d, args.block, 1, 0)
            sec = peek(d, DATA, 512)
            Path(args.out).write_bytes(sec)
            print('block 0x%05x -> %s (md5 %s)'
                  % (args.block, args.out, hashlib.md5(sec).hexdigest()))
            return 0

        stage2(d, args.block, 1, 0)
        sec = peek(d, DATA, 512)
        print('read block 0x%05x md5 %s' % (args.block, hashlib.md5(sec).hexdigest()))
        if args.expect:
            ref = Path(args.expect).read_bytes()[:512]
            if sec != ref:
                diff = [i for i in range(512) if sec[i] != ref[i]]
                raise SystemExit('sector does NOT match %s (diff at %s) - refusing'
                                 % (args.expect, [hex(x) for x in diff[:8]]))
            print('sector matches the reference file\'s first 512 bytes OK')
        old = sec[args.offset]
        if old == args.value:
            print('byte 0x%x already 0x%02x - nothing to do' % (args.offset, old))
            return 0
        patched = bytearray(sec)
        patched[args.offset] = args.value
        diff = [i for i in range(512) if sec[i] != patched[i]]
        print('changing byte 0x%x: 0x%02x -> 0x%02x (only these offsets: %s)'
              % (args.offset, old, args.value, [hex(x) for x in diff]))
        stage2(d, args.block, 1, 1, bytes(patched))
        stage2(d, args.block, 1, 0)
        back = peek(d, DATA, 512)
        if back != bytes(patched):
            raise SystemExit('readback differs - INVESTIGATE before rebooting')
        print('write + readback verified: block 0x%05x now md5 %s'
              % (args.block, hashlib.md5(back).hexdigest()))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
