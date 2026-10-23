#!/usr/bin/env python3
"""Minimal authenticated service shell for the ZV-E10 (no firmware gate).

The toolkit's terminal server refuses to continue unless it can read
/version.txt through FileControl and match a hard-coded baseline. That check is
irrelevant for driving ``/setting/ULOGIO.BIN``, and on this camera it fails
(FileControl READ returns 128), so this script performs the same PMCA service
handshake and simply skips the version gate.

Camera must be in USB mass-storage mode (enumerates 054c:0d95).

    python3 zve10_shell.py "uname -a"                 # one command
    python3 zve10_shell.py --script cmds.txt          # batch, one per line
    python3 zve10_shell.py --interactive

Every command is echoed with a unique marker so output can be attributed even
when the terminal fragments. Read-only unless the command itself writes; this
script imposes no allow-list, so mind what you paste in.

Requires the toolkit venv (PMCA + pyusb):

    PYTHONPATH=/root/fw/zv-e10-toolkit/vendor/Sony-PMCA-RE \
    /root/fw/zv-e10-toolkit/.venv/bin/python tools/zve10_shell.py "ls -la /setting"
"""
from __future__ import annotations

import argparse
import sys
import time
import uuid

from pmca.commands.usb import senserShellCommand
from pmca.usb.sony import SonySenserCamera  # noqa: F401  (imported for typing)

READ_TIMEOUT = 0.1
IDLE_LIMIT = 6.0


def drain(camera, seconds: float) -> bytes:
    """Discard pending terminal output."""
    end = time.time() + seconds
    while time.time() < end:
        camera.dev.readTerminal()
    return b""


def run(camera, command: str, quiet_idle: float = IDLE_LIMIT) -> str:
    """Send one command and collect output until it has been quiet a while."""
    tag = uuid.uuid4().hex[:10].encode()
    wrapped = b"echo __B%s__; %s; echo __E%s__\n" % (tag, command.encode(), tag)

    drain(camera, 0.2)
    camera.dev.writeTerminal(wrapped)

    out = b""
    last = time.time()
    deadline = time.time() + 60.0
    while time.time() < deadline:
        chunk = camera.dev.readTerminal()
        if chunk:
            out += chunk
            last = time.time()
            if b"__E" + tag + b"__" in out:
                # keep draining briefly so trailing prompt noise is consumed
                drain(camera, 0.15)
                break
        elif out and time.time() - last > quiet_idle:
            break
        else:
            time.sleep(READ_TIMEOUT)

    text = out.decode("utf-8", "replace")
    begin = "__B%s__" % tag.decode()
    end = "__E%s__" % tag.decode()
    if begin in text:
        text = text.split(begin, 1)[1]
    if end in text:
        text = text.split(end, 1)[0]
    return text.strip("\r\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("command", nargs="?")
    ap.add_argument("--script", help="file with one command per line")
    ap.add_argument("--interactive", action="store_true")
    ap.add_argument("--driver", default="libusb")
    args = ap.parse_args()

    if not (args.command or args.script or args.interactive):
        ap.error("give a command, --script or --interactive")

    rc = {"code": 0}

    def callback(camera):
        try:
            camera.setTerminalEnable(True)
            try:
                drain(camera, 0.4)
                if args.command:
                    print(run(camera, args.command))
                elif args.script:
                    for line in open(args.script):
                        line = line.strip()
                        if not line or line.startswith("#"):
                            continue
                        print("$ %s" % line)
                        print(run(camera, line))
                        print()
                else:
                    while True:
                        try:
                            line = input("zve10# ")
                        except (EOFError, KeyboardInterrupt):
                            print()
                            break
                        if line.strip() in ("exit", "quit"):
                            break
                        print(run(camera, line))
            finally:
                camera.setTerminalEnable(False)
        except Exception as exc:            # noqa: BLE001 - report, do not crash
            rc["code"] = 1
            print("ERROR: %s: %s" % (type(exc).__name__, exc), file=sys.stderr)

    senserShellCommand(driverName=args.driver, complete=callback)
    return rc["code"]


if __name__ == "__main__":
    sys.exit(main())
