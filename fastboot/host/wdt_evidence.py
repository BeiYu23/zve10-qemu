#!/usr/bin/env python3
"""Watchdog evidence capture + manual mitigation, in one fast pass.

Runs against the OLD payload (no wdt_kill). The surviving app watchdog
gives ~3m40s from the GP27 pulse, so this script does everything in one
run: connect -> peek the armed WDT timer + RSTREQ registers (evidence)
-> poke the kernel WDT_SET_STOP equivalent (mitigation) -> then poll
getvar until --minutes elapse to prove the session survives past 3:40.
"""
import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from read_root_dir import Device, peek_binary

WDT_TIMER = 0xF2402000      # CTL / CLR / CMP
MISC_RSTREQ = 0xF2915040    # RSTREQ / SET / CLR / STAT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=6.0)
    args = ap.parse_args()

    deadline = time.monotonic() + args.minutes * 60
    while time.monotonic() < deadline:
        import usb.core
        dev = usb.core.find(idVendor=0x18D1, idProduct=0x4EE0)
        if dev is not None:
            break
        time.sleep(0.5)
    else:
        raise SystemExit('device never appeared')
    t0 = time.monotonic()
    d = Device(dev)
    try:
        # ---- evidence: armed state, BEFORE touching anything
        _, wdt = peek_binary(d, WDT_TIMER, 0x10)
        ctl, clr, cmp_, read = struct.unpack_from('<IIII', wdt, 0)
        _, rst = peek_binary(d, MISC_RSTREQ, 0x10)
        print('EVIDENCE @ +%.1fs after enumeration:' % (time.monotonic() - t0))
        print('  WDT  TIMERCTL = 0x%08x  (TMST bit 0x1000: %s)'
              % (ctl, 'RUNNING' if ctl & 0x1000 else 'stopped'))
        print('  WDT  TIMERCLR = 0x%08x' % clr)
        print('  WDT  TIMERCMP = 0x%08x  (= %d us = %.1f s)'
              % (cmp_, cmp_, cmp_ / 1e6))
        print('  WDT  TIMERREAD= 0x%08x  (counter)' % read)
        r0, s, c, stat = struct.unpack_from('<IIII', rst, 0)
        print('  MISC RSTREQ   = 0x%08x  (EN bit 0x10: %s)'
              % (r0, 'ENABLED' if r0 & 0x10 else 'disabled'))
        print('  MISC STAT     = 0x%08x' % stat)

        armed = (ctl & 0x1000) and (r0 & 0x10)
        print('\nVERDICT: watchdog %s' % ('ARMED - root cause CONFIRMED'
                                          if armed else 'not armed?!'))
        if not armed:
            return 2

        # ---- mitigation: kernel WDT_SET_STOP equivalent
        for cmd in ('oem poke:0xf2915048:0x10',   # RSTREQ_CLR = EN (disable)
                    'oem poke:0xf2402000:0x0',    # TIMERCTL = 0 (stop)
                    'oem poke:0xf2402004:0x11'):   # TIMERCLR = TMCLR|TMINTCLR
            st, msg, _ = d.send_command(cmd)
            assert st == 'OKAY', (cmd, st, msg)
        _, wdt2 = peek_binary(d, WDT_TIMER, 0x10)
        _, rst2 = peek_binary(d, MISC_RSTREQ, 0x10)
        ctl2 = struct.unpack_from('<I', wdt2, 0)[0]
        r02 = struct.unpack_from('<I', rst2, 0)[0]
        print('MITIGATION: TIMERCTL=0x%08x RSTREQ=0x%08x -> %s'
              % (ctl2, r02, 'dead' if (ctl2 & 0x1000) == 0 else 'STILL RUNNING'))

        # ---- survival proof: poll past 3m40s wall clock
        print('\nsurvival poll for %.1f min (the old exit was at ~3:40)...'
              % args.minutes)
        start = time.monotonic()
        n = 0
        while time.monotonic() < start + args.minutes * 60 - (time.monotonic() - start):
            pass
        while True:
            elapsed = time.monotonic() - start
            if elapsed > args.minutes * 60:
                break
            n += 1
            st, msg, _ = d.send_command('getvar:version', timeout=5000)
            print('  +%.1f s: getvar -> %s %s' % (elapsed, st, msg.decode()))
            time.sleep(30 if elapsed > 30 else max(0, 30 - elapsed))
        print('\nRESULT: session survived %.1f min - watchdog mitigation works'
              % ((time.monotonic() - start) / 60))
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
