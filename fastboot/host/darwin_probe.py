#!/usr/bin/env python3
"""Probe the Darwin power-MCU link (SIO + chip-select GPIO) on ZV-E10.

Read-only: uses `oem peek` only, so nothing is transmitted to Darwin and no
register changes. Answers the three questions in docs/darwin-power-mcu.md:
  1. is the SIO block at 0xF1005000 initialised?
  2. which GPIO port/bit is driven and idle high (the active-low XCS)?
  3. does a Darwin read attempt even look plausible?

Run with the payload up:  python3 host/darwin_probe.py
"""
import struct
import sys
import time
from pathlib import Path

import usb.core

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, peek_binary

SIO_BASE = 0xF1005000
GPIO_BASE = 0xF2910000
GPIO_STRIDE = 0x100


def show(dev, name, addr, length=0x20):
    try:
        _, b = peek_binary(dev, addr, length)
    except Exception as e:
        print('  %-18s 0x%08x  READ FAILED: %s' % (name, addr, e))
        return None
    words = struct.unpack_from('<%dI' % (len(b) // 4), b)
    print('  %-18s 0x%08x  %s' % (name, addr, ' '.join('%08x' % w for w in words)))
    return words


def main():
    dev = None
    for _ in range(20):
        dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        raise SystemExit('fastboot device not found - inject first')
    d = Device(dev)
    try:
        print('1. SIO channel 0 (0xF1005000): CTRL CTRL2 REG08 LEN CFG RATE_LO RATE_HI')
        w = show(d, 'SIO ch0', SIO_BASE, 0x20)
        if w is not None:
            non_zero = sum(1 for x in w if x)
            print('   -> %d/8 registers non-zero%s'
                  % (non_zero, '' if non_zero else '  (block may be clock-gated)'))

        print('\n2. GPIO ports 0-7: DIR(0x04) SET(0x44) CLR(0x48) WDATA(0x40)'
              ' / RDATA(0x18)')
        for port in range(8):
            base = GPIO_BASE + port * GPIO_STRIDE
            print('  port %d:' % port)
            show(d, '  DIR_SET', base + 0x04, 0x10)

        print('\n3. SIO data window (first 0x20 of the 0x80-byte buffer)')
        show(d, 'SIO_DATA', SIO_BASE + 0x100, 0x20)

        print('\nRead the raw values above against docs/darwin-power-mcu.md:')
        print('  - a plausible SIO setup means the ROM left the link configured')
        print('  - XCS is a line that is driven (DIR bit set) and idle HIGH')
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
