#!/usr/bin/env python3
"""Read/write whole files on the ZV-E10 over the service channel (FileControl).

Uses PMCA's own FileControl packet layout (pFunc 0xff01, cmd 1=write 2=read),
which needs no camera shell: the camera resolves the path itself. Verified
against the real camera for /setting/ULOGIO.BIN (3796 bytes, md5 a401b16a...).

    # read a file to disk
    python3 zve10_file.py read /setting/ULOGIO.BIN --out /tmp/ulogio.bin

    # write it back (byte-identical round-trip is the safe way to prove writing)
    python3 zve10_file.py write /setting/ULOGIO.BIN --in /tmp/ulogio.bin

    # enable the ulogio telnet flag: TELN record data at offset 0x9C -> 1
    python3 zve10_file.py patch-telnet --backup /tmp/ulogio.bin.bak

`patch-telnet` always reads the current file first, refuses to touch anything
unless the expected TELN record is found, saves a backup, flips one u32, writes
the whole file back, re-reads it and verifies that exactly that u32 changed.

Needs the toolkit venv + PMCA:

    PYTHONPATH=/root/fw/zv-e10-toolkit/vendor/Sony-PMCA-RE \
    /root/fw/zv-e10-toolkit/.venv/bin/python tools/zve10_file.py ...
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

from pmca.commands.usb import senserShellCommand

FILE_CONTROL = 0xFF01
CMD_WRITE = 1
CMD_READ = 2
ULOGIO = "/setting/ULOGIO.BIN"
TELN_DATA_OFF = 0x9C      # TELN record: len@0x94 tag@0x98 data@0x9C


def read_file(camera, path: str) -> bytes:
    name = path.encode("ascii")
    name += b"\0" * (4 - len(name) % 4)
    payload = struct.pack("<HH", CMD_READ, len(name)) + name
    res, data = camera.dev.sendSenserPacket(FILE_CONTROL, payload)
    if res != 1:
        raise RuntimeError("FileControl READ %s -> %r" % (path, res))
    return data or b""


def write_file(camera, path: str, data: bytes) -> None:
    name = path.encode("ascii")
    name += b"\0" * (4 - len(name) % 4)
    payload = struct.pack("<HH", CMD_WRITE, len(name)) + name + data
    res, _ = camera.dev.sendSenserPacket(FILE_CONTROL, payload)
    if res != 1:
        raise RuntimeError("FileControl WRITE %s -> %r" % (path, res))


def md5(b: bytes) -> str:
    return hashlib.md5(b).hexdigest()


def do_patch_telnet(camera, backup: str | None) -> int:
    cur = read_file(camera, ULOGIO)
    print("read %s: %d bytes md5 %s" % (ULOGIO, len(cur), md5(cur)))
    if cur[0x98:0x9C] != b"TELN":
        raise SystemExit("TELN tag not at 0x98 (found %r) - refusing" % cur[0x98:0x9C])
    val = struct.unpack_from("<I", cur, TELN_DATA_OFF)[0]
    print("TELN @0x9C = 0x%08x (%s)" % (val, "already on" if val else "off"))
    if val == 1:
        print("telnet already enabled - nothing to do")
        return 0

    if backup:
        Path(backup).write_bytes(cur)
        print("backup -> %s (md5 %s)" % (backup, md5(cur)))

    patched = bytearray(cur)
    struct.pack_into("<I", patched, TELN_DATA_OFF, 1)
    diff = [i for i in range(len(cur)) if cur[i] != patched[i]]
    print("bytes changing: %s" % [hex(i) for i in diff])
    if diff != [TELN_DATA_OFF]:
        raise SystemExit("unexpected diff - refusing")

    write_file(camera, ULOGIO, bytes(patched))
    print("written, re-reading...")
    back = read_file(camera, ULOGIO)
    if back[:TELN_DATA_OFF] != cur[:TELN_DATA_OFF] or \
       back[TELN_DATA_OFF + 4:] != cur[TELN_DATA_OFF + 4:]:
        raise SystemExit("readback differs outside 0x9C - INVESTIGATE")
    got = struct.unpack_from("<I", back, TELN_DATA_OFF)[0]
    if got != 1:
        raise SystemExit("readback TELN = 0x%08x, expected 1" % got)
    print("OK: TELN = 1, rest of the file byte-identical (md5 %s)" % md5(back))
    print("reboot the camera for telnetd to start")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("read")
    r.add_argument("path")
    r.add_argument("--out", required=True)

    w = sub.add_parser("write")
    w.add_argument("path")
    w.add_argument("--in", dest="src", required=True)

    p = sub.add_parser("patch-telnet")
    p.add_argument("--backup", default="/tmp/ulogio.bin.bak")

    ap.add_argument("--driver", default="libusb")
    args = ap.parse_args()

    rc = {"code": 0}

    def callback(camera):
        try:
            # PMCA's default USB timeout is too short for FileControl responses
            try:
                camera.dev.driver.backend.dev.default_timeout = 30000
            except AttributeError:
                pass
            if args.cmd == "read":
                data = read_file(camera, args.path)
                Path(args.out).write_bytes(data)
                print("%s -> %s (%d bytes md5 %s)"
                      % (args.path, args.out, len(data), md5(data)))
            elif args.cmd == "write":
                src = Path(args.src).read_bytes()
                write_file(camera, args.path, src)
                back = read_file(camera, args.path)
                ok = back == src
                print("%s written (%d bytes md5 %s); readback %s"
                      % (args.path, len(src), md5(src),
                         "IDENTICAL" if ok else "DIFFERS (%d bytes)" % len(back)))
                if not ok:
                    rc["code"] = 2
            else:
                rc["code"] = do_patch_telnet(camera, args.backup)
        except BaseException as exc:          # noqa: BLE001
            rc["code"] = 1
            print("ERROR: %s: %s" % (type(exc).__name__, exc), file=sys.stderr)

    senserShellCommand(driverName=args.driver, complete=callback)
    return rc["code"]


if __name__ == "__main__":
    sys.exit(main())
