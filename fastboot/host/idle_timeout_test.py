#!/usr/bin/env python3
"""Power-domain idle-timeout experiment (community: C-port models auto-boot
from serial/BootROM mode when idle).

Phase 1 (heartbeat): getvar every --hb seconds for --minutes. If the
  session survives, USB/host activity resets the power-domain idle timer
  and a host-side heartbeat is the fix.
Phase 2 (pure idle): no device traffic at all; poll lsusb/sysfs only
  (which does not touch the device) and measure the exact time-to-death.

Also: sets the device's power/control to "on" to remove host-side USB
autosuspend as a variable.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, peek_binary

WDT_TIMER = 0xF2402000
MISC_RSTREQ = 0xF2915040


def devpath():
    out = subprocess.run(['ls', '/sys/bus/usb/devices/'], capture_output=True,
                         text=True).stdout.split()
    for d in out:
        try:
            if open('/sys/bus/usb/devices/%s/idVendor' % d).read().strip() == '18d1':
                return '/sys/bus/usb/devices/%s' % d
        except OSError:
            continue
    return None


def present():
    p = devpath()
    return p is not None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hb', type=float, default=2.0, help='heartbeat seconds')
    ap.add_argument('--minutes', type=float, default=10.0)
    args = ap.parse_args()

    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        if present():
            break
        time.sleep(0.5)
    else:
        raise SystemExit('device never appeared')
    print('device at %s' % devpath())
    # remove host autosuspend as a variable
    p = devpath()
    try:
        open(p + '/power/control', 'w').write('on')
        print('power/control -> on (was %s)' % open(p + '/power/control').read().strip())
    except OSError as e:
        print('power/control: %s' % e)

    import usb.core
    dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
    d = Device(dev)
    t0 = time.monotonic()
    try:
        _, wdt = peek_binary(d, WDT_TIMER, 0x10)
        _, rst = peek_binary(d, MISC_RSTREQ, 0x10)
        print('WDT state: ctl=0x%08x cmp=0x%08x  RSTREQ=0x%08x'
              % (wdt[0], wdt[8], rst[0]))
        print('Phase 1: heartbeat getvar every %.1fs for %.1f min' %
              (args.hb, args.minutes))
        n = 0
        end = time.monotonic() + args.minutes * 60
        while time.monotonic() < end:
            st, msg, _ = d.send_command('getvar:version', timeout=4000)
            n += 1
            if st != 'OKAY':
                print('  +%.0fs: getvar -> %s %r - DEAD at heartbeat #%d'
                      % (time.monotonic() - t0, st, msg, n))
                return 1
            time.sleep(args.hb)
        print('  +%.0fs: survived %d heartbeats - ALIVE' % (time.monotonic() - t0, n))

        print('Phase 2: pure idle, sysfs-only polling (no device traffic)')
        idle0 = time.monotonic()
        while True:
            if not present():
                dt = time.monotonic() - idle0
                print('  DEAD after %.0fs idle (total from connect: %.0fs)'
                      % (dt, time.monotonic() - t0))
                return 0
            if time.monotonic() - t0 > args.minutes * 60 + 1200:
                print('  still alive after %.0fs idle - no idle timeout?!'
                      % (time.monotonic() - idle0))
                return 0
            time.sleep(2)
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
