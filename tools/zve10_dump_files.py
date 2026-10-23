#!/usr/bin/env python3
"""Read-only dump of selected camera files over the JIRITSU GET_FILE channel.

No shell needed and nothing is written to the camera. Use it to compare what is
really on the device against a partition image.

    python3 tools/zve10_dump_files.py --out /tmp/live_dump \
        /usr/bin/im.elf /usr/bin/bootin.elf /usr/share/app/global.xdb

Each file is saved under the output directory with its path flattened, and the
size plus md5 is printed so it can be diffed against `debugfs -R dump` output
from nflasha15.img.
"""
from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path

from pmca.commands.usb import importDriver, listDevices
from pmca.usb.sony import SonySenserDevice, SonySenserAuthDevice, SonySenserCamera

# JIRITSU_GET_FILE is reached through the AdjustControl packet family.
ADJ_CATEGORY = 0x1001
GET_FILE = 0x4008


def read_one(cam, remote: str):
    path = remote if remote.startswith("/") else "/" + remote
    payload = bytes([1, 0, 1]) + b"/.." + path.encode()
    res, data = cam.dev.sendSenserPacket(
        cam.SONY_PFUNC_AdjustControl,
        cam.CommandHeader.pack(category=ADJ_CATEGORY, command=GET_FILE) + payload)
    data = data or b""
    if len(data) < 4:
        return data.hex(), b""
    status, body = data[:4], data[4:]
    if not body:
        return status.hex(), b""
    # AES-128-CTR keystream, key MD5("jiritsu team" + 4*0x00), IV 00..0f
    import hashlib as _h
    try:
        from Cryptodome.Cipher import AES
    except ImportError:                                  # pragma: no cover
        from Crypto.Cipher import AES
    key = _h.md5(b"jiritsu team\x00\x00\x00\x00").digest()
    iv = bytes(range(16))
    ks = AES.new(key, AES.MODE_CTR, nonce=b"", initial_value=iv).encrypt(b"\x00" * len(body))
    return status.hex(), bytes(a ^ b for a, b in zip(body, ks))


def stop_senser():
    for d in listDevices(None, True) if False else []:
        pass


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--out", default="/tmp/live_dump")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    with importDriver("libusb") as drv:
        # If the camera is stuck in service mode from an earlier session, drop it
        for d in listDevices(drv, True):
            if isinstance(d, SonySenserDevice):
                try:
                    SonySenserAuthDevice(d.driver).stop()
                except Exception:
                    pass
                time.sleep(5)
                break
        # minimal inline flow (same steps as zve10_read.full_flow)
        def sensers():
            return [d for d in listDevices(drv, True) if isinstance(d, SonySenserDevice)]
        if not sensers():
            devs = list(listDevices(drv, True))
            if not devs:
                for _ in range(15):
                    time.sleep(1)
                    devs = list(listDevices(drv, True))
                    if devs:
                        break
            a = SonySenserAuthDevice(devs[0].driver)
            a.start(); a.authenticate()
            for _ in range(24):
                time.sleep(0.5)
                if sensers():
                    break
        ds = sensers()
        if not ds:
            raise SystemExit("camera did not enter service mode")
        a2 = SonySenserAuthDevice(ds[0].driver)
        a2.start(); a2.authenticate()
        cam = SonySenserCamera(ds[0])
        try:
            cam.dev.driver.backend.dev.default_timeout = 30000
        except AttributeError:
            pass

        rc = 0
        for p in args.paths:
            try:
                st, data = read_one(cam, p)
            except Exception as exc:                     # noqa: BLE001
                print("%-44s ERROR %s: %s" % (p, type(exc).__name__, exc))
                rc = 1
                continue
            dest = out / (p.strip("/").replace("/", "_"))
            if data:
                dest.write_bytes(data)
            print("%-44s status=%s len=%-8d md5=%s%s"
                  % (p, st, len(data),
                     hashlib.md5(data).hexdigest() if data else "-",
                     "" if data else "   (0 bytes - unreadable type?)"))
            if not data:
                rc = 1
        return rc


if __name__ == "__main__":
    sys.exit(main())
