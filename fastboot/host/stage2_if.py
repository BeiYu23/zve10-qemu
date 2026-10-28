#!/usr/bin/env python3
"""Shared host-side stage2 interface: chunked multi-block read/write.

stage2 v3: CMD18/CMD25 multi-block (128 blocks = 64 KiB per call) and a
~25 MHz data-phase clock. Single-block requests stay on the proven
CMD17/CMD24 path. All chunking happens host-side here.
"""
import struct
import time
from pathlib import Path

import usb.core

from read_root_dir import Device, peek_binary, STAGE2, DATA, RESULT, PARAMS

CHUNK_BLOCKS = 128                     # 64 KiB, matches STAGE2_DATA_MAX
VID, PID = 0x18D1, 0x4EE0


def wait_device(seconds=30):
    dev = None
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None:
            return dev
        time.sleep(0.5)
    return None


class Stage2:
    def __init__(self, dev=None):
        if dev is None:
            dev = wait_device()
            if dev is None:
                raise SystemExit('fastboot device not found - re-inject first')
        self.dev = dev
        self.d = Device(dev)
        blob = Path(__file__).resolve().parent.parent / 'stage2' / 'build' / 'stage2.bin'
        self.image = blob.read_bytes()
        self.d.download(self.image)
        st, msg, _ = self.d.send_command('oem load:%08x' % STAGE2)
        assert st == 'OKAY', msg

    def close(self):
        self.d.close()

    def _call(self, block, count, mode):
        for cmd in ('oem poke:%08x:%08x' % (PARAMS, block),
                    'oem poke:%08x:%08x' % (PARAMS + 4, count),
                    'oem poke:%08x:%08x' % (PARAMS + 8, mode),
                    'oem call:%08x' % (STAGE2 + 1)):
            st, msg, _ = self.d.send_command(cmd)
            assert st == 'OKAY', (cmd, st, msg)

    def _result(self):
        _, res = peek_binary(self.d, RESULT, 0x60)
        status = struct.unpack_from('<I', res, 8)[0]
        rstatus, rblock = struct.unpack_from('<II', res, 0x48)
        wstatus, wblock = struct.unpack_from('<II', res, 0x50)
        clk = struct.unpack_from('<I', res, 0x58)[0]
        return dict(status=status, read_status=rstatus, read_block=rblock,
                    write_status=wstatus, write_block=wblock, data_clk_div=clk)

    def read(self, block, count):
        """Read count blocks (chunked in 128s) -> bytes."""
        out = b''
        off = 0
        while off < count:
            n = min(CHUNK_BLOCKS, count - off)
            self._call(block + off, n, 0)
            res = self._result()
            assert res['status'] == 0, 'stage2 failed: %r' % res
            assert res['read_status'] == 0, 'read failed: %r' % res
            assert res['read_block'] == block + off, 'block mismatch: %r' % res
            _, data = peek_binary(self.d, DATA, n * 512)
            out += data
            off += n
        return out

    def write(self, block, payload):
        """Write len(payload)/512 blocks (chunked in 128s)."""
        assert len(payload) % 512 == 0
        count = len(payload) // 512
        off = 0
        while off < count:
            n = min(CHUNK_BLOCKS, count - off)
            chunk = payload[off * 512:(off + n) * 512]
            self.d.download(chunk)
            st, msg, _ = self.d.send_command('oem load:%08x' % DATA)
            assert st == 'OKAY', msg
            self._call(block + off, n, 1)
            res = self._result()
            assert res['status'] == 0, 'stage2 failed: %r' % res
            assert res['write_status'] == 0, 'write failed: %r' % res
            assert res['write_block'] == block + off, 'block mismatch: %r' % res
            off += n
