#!/usr/bin/env python3
"""Read the boot tracer's raw marker from the tail of /log.

tracer2 writes 512 bytes at the very end of /dev/nflasha11 without touching any
filesystem, so the marker proves init reached that exec even when no partition
was mounted. The offset was verified to sit in unallocated space (the last megabyte
of the log partition is entirely zero).
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from bootstrap import Device, wait_for_device
from read_updater_log import upload_blocks

LOG_START = 0x0B800000
LOG_SIZE = 0x1F400000
MARK_OFF = LOG_SIZE - 512
MARK_LBA = (LOG_START + MARK_OFF) // 512


def main():
    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    try:
        d.require('oem emmc-init')
        print('reading marker at LBA 0x%X (offset 0x%X in /log)' % (MARK_LBA, MARK_OFF))
        raw = upload_blocks(d, MARK_LBA, 1)
        print()
        print('raw 512 bytes (first 96):')
        print('  ' + raw[:96].hex())
        print()
        text = raw.split(b'\x00')[0]
        if not text:
            print('RESULT: sector is all zero -- tracer never wrote here')
            print('  => init did not reach the bootin exec')
            return 1
        print('RESULT: marker found')
        print('  %r' % text.decode('latin1', 'replace'))
        if text.startswith(b'REACHED'):
            print('  => init DID reach /usr/bin/bootin.elf, so /usr mounted')
            return 0
        if text.startswith(b'EXECFAILED'):
            print('  => /usr mounted but /usr/bin/bootin.real would not start')
            return 2
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
