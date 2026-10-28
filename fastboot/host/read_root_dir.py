#!/usr/bin/env python3
"""Read-only scan of the FAT root directory on nflasha10 via stage2.

Stages stage2 once, reads the FAT boot sector, parses the BPB (no fixed
offsets for the root directory), then reads the whole root directory and
decodes the 32-byte directory entries (LFN chains included).  Reports any
entry whose long name matches --name (default ld.so.preload) together with
its absolute eMMC byte offset and size-field position.

eMMC traffic is CMD17 read-only.  Nothing is written.
"""
import argparse
import struct
import sys
import time
from pathlib import Path

import usb.core
import usb.util

VID, PID = 0x18D1, 0x4EE0
CHUNK = 4096
STAGE2 = 0xFE300000
PARAMS = 0xFE320000
RESULT = 0xFE308000
DATA = 0xFE310000
DATA_MAX = 64 * 512          # stage2 clamps count to 64 blocks


class Device:
    def __init__(self, dev):
        self.dev = dev
        try:
            cfg = dev.get_active_configuration()
        except usb.core.USBError:
            dev.set_configuration()
            cfg = dev.get_active_configuration()
        self.intf = cfg[(0, 0)]
        usb.util.claim_interface(dev, self.intf)
        self.ep_out = usb.util.find_descriptor(
            self.intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
            == usb.util.ENDPOINT_OUT)
        self.ep_in = usb.util.find_descriptor(
            self.intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
            == usb.util.ENDPOINT_IN)
        if self.ep_out is None or self.ep_in is None:
            raise RuntimeError('bulk endpoints missing')

    def close(self):
        try:
            usb.util.release_interface(self.dev, self.intf)
        except Exception:
            pass
        usb.util.dispose_resources(self.dev)

    def read_reply(self, timeout=20000):
        infos = []
        while True:
            resp = bytes(self.ep_in.read(64, timeout=timeout))
            if len(resp) < 4:
                return 'BAD', resp, infos
            tag, body = resp[:4], resp[4:]
            if tag == b'INFO':
                infos.append(body.decode('utf-8', 'replace').rstrip('\r\n'))
                continue
            return tag.decode('latin-1'), body, infos

    def send_command(self, cmd, timeout=20000):
        if isinstance(cmd, str):
            cmd = cmd.encode()
        self.ep_out.write(cmd, timeout=timeout)
        return self.read_reply(timeout)

    def download(self, blob):
        status, msg, _ = self.send_command('download:%08x' % len(blob))
        if status != 'DATA':
            raise RuntimeError('download rejected: %s %r' % (status, msg))
        sent = 0
        while sent < len(blob):
            piece = blob[sent:sent + CHUNK]
            self.ep_out.write(piece, timeout=10000)
            sent += len(piece)
        status, msg, _ = self.read_reply()
        if status != 'OKAY':
            raise RuntimeError('data phase failed: %s %r' % (status, msg))


def peek_binary(dev, addr, length):
    """oem peek:addr:length -> bytes (accepts 0x1000 max per call)."""
    out = bytearray()
    base = addr
    while length > 0:
        n = min(length, 0x1000)
        status, msg, infos = dev.send_command('oem peek:%08x:%x' % (addr, n))
        if status != 'OKAY':
            raise RuntimeError('peek %#x: %s %r' % (addr, status, msg))
        got = bytearray()
        for line in infos:
            parts = line.split()
            if len(parts) != 5:
                continue
            line_addr = int(parts[0].rstrip(':'), 16)
            if line_addr != addr + len(got):
                raise RuntimeError('peek line addr mismatch: %s' % line)
            for w in parts[1:]:
                got += struct.pack('<I', int(w, 16))
        if len(got) != n:
            raise RuntimeError('peek %#x: got %d of %d bytes' % (addr, len(got), n))
        out += got
        addr += n
        length -= n
    return base, bytes(out)


def read_blocks(dev, first_block, count):
    """Run one stage2 pass: poke params, call, peek the data buffer."""
    for cmd in ('oem poke:%08x:%08x' % (PARAMS, first_block),
                'oem poke:%08x:%08x' % (PARAMS + 4, count),
                'oem call:%08x' % (STAGE2 + 1)):
        status, msg, _ = dev.send_command(cmd)
        if status != 'OKAY':
            raise RuntimeError('%s: %s %r' % (cmd, status, msg))
    _, blob = peek_binary(dev, DATA, count * 512)
    return blob


def parse_bpb(boot):
    if boot[510:512] != b'\x55\xaa':
        raise SystemExit('boot sector signature 55AA missing')
    bps = struct.unpack_from('<H', boot, 11)[0]
    spc = boot[13]
    reserved = struct.unpack_from('<H', boot, 14)[0]
    nfats = boot[16]
    root_entries = struct.unpack_from('<H', boot, 17)[0]
    total16 = struct.unpack_from('<H', boot, 19)[0]
    fat16 = struct.unpack_from('<H', boot, 22)[0]
    total32 = struct.unpack_from('<I', boot, 32)[0]
    fat32 = struct.unpack_from('<I', boot, 36)[0]
    root_cluster = struct.unpack_from('<I', boot, 44)[0]

    total = total16 or total32
    print('BPB: bps=%d spc=%d reserved=%d nfats=%d root_entries=%d '
          'fat16=%d fat32=%d root_cluster=%d total=%d'
          % (bps, spc, reserved, nfats, root_entries, fat16, fat32,
             root_cluster, total))
    if not total:
        raise SystemExit('volume size is zero; refusing to guess')
    if bps != 512:
        raise SystemExit('unexpected bytes/sector %d (stage2 reads 512B blocks)' % bps)
    if fat16 == 0 and fat32:
        raise SystemExit('FAT32 root dir is a cluster chain; not supported here '
                         '(root_cluster=%d)' % root_cluster)

    root_secs = (root_entries * 32 + bps - 1) // bps
    root_start = reserved + nfats * fat16
    fat_bits = 12 if (total - root_start - root_secs) / spc < 4085 else 16
    print('root dir: +%d sectors, %d sectors, FAT%02d'
          % (root_start, root_secs, fat_bits))
    return root_start, root_secs, root_entries


def lfn_name(part):
    # LFN entry: bytes 1-10, 14-25, 28-31 are UCS-2 chars
    raw = part[1:11] + part[14:26] + part[28:32]
    chars = []
    for i in range(0, len(raw), 2):
        c = raw[i] | (raw[i + 1] << 8)
        if c == 0 or c == 0xffff:
            break
        chars.append(c)
    return ''.join(chr(c) for c in chars)


def short_name(raw):
    base = raw[:8].decode('latin-1').rstrip()
    ext = raw[8:11].decode('latin-1').rstrip()
    return base + ('.' + ext if ext else '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--boot-block', type=lambda v: int(v, 0), default=0x56000,
                    help='absolute eMMC block of the FAT boot sector')
    ap.add_argument('--name', default='ld.so.preload')
    ap.add_argument('--dump', default='')
    ap.add_argument('--seconds', type=int, default=60)
    args = ap.parse_args()

    deadline = time.monotonic() + args.seconds
    dev = None
    while time.monotonic() < deadline:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        raise SystemExit('fastboot device not found')

    time.sleep(1.0)
    d = Device(dev)
    try:
        stage2 = Path(__file__).resolve().parent.parent / 'stage2' / 'build' / 'stage2.bin'
        blob = stage2.read_bytes()
        print('staging %d B stage2 at 0x%x' % (len(blob), STAGE2))
        d.download(blob)
        status, msg, _ = d.send_command('oem load:%08x' % STAGE2)
        if status != 'OKAY':
            raise SystemExit('load: %s %r' % (status, msg))

        boot = read_blocks(d, args.boot_block, 1)
        print('boot block 0x%x: %s ... %s'
              % (args.boot_block, boot[:11].hex(), boot[510:512].hex()))
        root_start, root_secs, root_entries = parse_bpb(boot)

        first = args.boot_block + root_start
        rootdir = b''
        off = 0
        while off < root_secs:
            n = min(64, root_secs - off)
            rootdir += read_blocks(d, first + off, n)
            off += n
        print('read %d root sectors at block 0x%x (%d bytes)'
              % (root_secs, first, len(rootdir)))
        if args.dump:
            Path(args.dump).write_bytes(rootdir)
            print('dumped to %s' % args.dump)

        # decode entries; assemble LFN chains
        lfn_parts = {}
        found = []
        for i in range(0, len(rootdir), 32):
            e = rootdir[i:i + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xe5:
                lfn_parts.clear()
                continue
            attr = e[11]
            if attr == 0x0f and e[0] < 0x40:
                seq = e[0] & 0x3f
                lfn_parts[seq] = lfn_name(e)
                continue
            long_name = ''
            if lfn_parts:
                n = max(lfn_parts)
                long_name = ''.join(lfn_parts.get(k, '') for k in range(1, n + 1))
                if long_name and (lfn_parts.get(1, '')[:1] or n == 1):
                    pass
                lfn_parts = {}
            sn = short_name(e[:11])
            cluster = struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            entry_off = first * 512 + i
            display = long_name or sn
            mark = '  <-- MATCH' if display.lower() == args.name.lower() else ''
            print('entry %3d @block 0x%05x +0x%03x (abs 0x%08x): %-20s '
                  'attr=%02x clus=%5d size=%d%s'
                  % (i // 32, first, i, entry_off, display, attr, cluster, size, mark))
            if display.lower() == args.name.lower():
                found.append((entry_off, e, size))
        if not found:
            print('!! %s not found in root directory' % args.name)
            return 1
        for entry_off, e, size in found:
            print('\nMATCH at absolute eMMC byte 0x%08x (block 0x%x + 0x%x)'
                  % (entry_off, entry_off // 512, entry_off % 512))
            print('  raw entry: %s' % e.hex())
            print('  size field (entry+28..31) at absolute 0x%08x, value %d (0x%x)'
                  % (entry_off + 28, size, size))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
