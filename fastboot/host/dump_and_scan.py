#!/usr/bin/env python3
"""Dump /setting and scan /log for boot-failure evidence.

Both live on eMMC and are readable over fastboot regardless of whether the
camera's userspace ever started, which is exactly the situation where the
filesystem contents matter most.

  --which setting   dump nflasha2 (20 MiB), list every file, show small ones
  --which log       dump nflasha11 (500 MiB) and grep for failure strings
  --which both
"""
import argparse
import re
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from bootstrap import Device, wait_for_device
from read_updater_log import upload_blocks

PARTS = {
    'setting': (0x00800000 // 512, 20971520),
    'log':     (0x0B800000 // 512, 0x1F400000),
}

PATTERNS = [
    'im.elf', 'bootin', 'appFw', 'libObj', 'Application_Start',
    'dlopen', 'cannot open', 'No such file', 'Permission denied',
    'error', 'Error', 'ERROR', 'fail', 'Fail', 'FAIL', 'abort', 'crash',
    'segfault', 'SIGSEGV', 'signal', 'panic', 'Oops',
    'LD_PRELOAD', 'preload', 'dmode', 'mode_file',
    'libMWF', 'viewUnified', 'Caution', 'libmpr', 'libBizFw',
]


def fetch(d, lba, nsec, log_every=0):
    out, left, cur = b'', nsec, lba
    done = 0
    while left > 0:
        n = min(64, left)
        out += upload_blocks(d, cur, n)
        cur += n
        left -= n
        done += n
        if log_every and done % log_every == 0:
            print('    %d / %d sectors' % (done, nsec), end='\r')
    return out


def lfn(e):
    raw = e[1:11] + e[14:26] + e[28:32]
    s = ''
    for i in range(0, len(raw), 2):
        c = raw[i] | (raw[i + 1] << 8)
        if c in (0, 0xFFFF):
            break
        s += chr(c)
    return s


def fat_parse(d, base_lba, nbytes):
    """Minimal FAT12/16/32 directory walker over an in-memory image."""
    b = d[:512]
    bps = int.from_bytes(b[11:13], 'little')
    spc = b[13]
    rsvd = int.from_bytes(b[14:16], 'little')
    nfats = b[16]
    rootent = int.from_bytes(b[17:19], 'little')
    fat16 = int.from_bytes(b[22:24], 'little')
    tot32 = int.from_bytes(b[32:36], 'little')
    if not bps or spc == 0:
        return None
    root_lba = rsvd + nfats * (fat16 if fat16 else 0)
    if fat16 == 0:  # FAT32
        fat32 = int.from_bytes(b[36:40], 'little')
        root_lba = rsvd + nfats * fat32
        data_lba = root_lba
        root_bytes = spc * bps
        fat_off = rsvd * bps
        fat_sz = fat32 * bps
        is32 = True
    else:
        data_lba = root_lba + (rootent * 32 + bps - 1) // bps
        root_bytes = rootent * 32
        fat_off = rsvd * bps
        fat_sz = fat16 * bps
        is32 = False
    return dict(bps=bps, spc=spc, root_lba=root_lba, data_lba=data_lba,
                root_bytes=root_bytes, fat_off=fat_off, fat_sz=fat_sz, is32=is32,
                nfats=nfats, rsvd=rsvd)


def next_cluster(img, fs, cl):
    if fs['is32']:
        o = fs['fat_off'] + cl * 4
        if o + 4 > len(img):
            return 0x0FFFFFFF
        v = int.from_bytes(img[o:o + 4], 'little') & 0x0FFFFFFF
        return v if v < 0x0FFFFFF8 else 0x0FFFFFFF
    i = cl + cl // 2
    o = fs['fat_off'] + i
    if o + 1 >= len(img):
        return 0xFFF
    v = img[o] | (img[o + 1] << 8)
    v = (v >> 4) if (cl & 1) else (v & 0xFFF)
    return v if v < 0xFF8 else 0xFFF


def read_file(img, fs, cl, size):
    out, c = b'', cl
    while 2 <= c < 0x0FFFFFF8 and len(out) < size:
        off = fs['data_lba'] * fs['bps'] + (c - 2) * fs['spc'] * fs['bps']
        out += img[off:off + fs['spc'] * fs['bps']]
        c = next_cluster(img, fs, c)
    return out[:size]


def walk(img, fs, clus, path='', depth=0):
    """Yield (path, size, cluster, isdir)."""
    if depth > 6:
        return
    if clus == 0:
        start = fs['root_lba'] * fs['bps']
        buf = img[start:start + fs['root_bytes']]
    else:
        buf = read_file(img, fs, clus, fs['spc'] * fs['bps'] * 64)
    pend = {}
    for i in range(0, len(buf), 32):
        e = buf[i:i + 32]
        if len(e) < 32 or e[0] == 0:
            break
        if e[0] == 0xE5:
            pend.clear()
            continue
        if e[11] == 0x0F:
            pend[e[0] & 0x3F] = lfn(e)
            continue
        name = ''
        if pend:
            m = max(pend)
            name = ''.join(pend.get(k, '?') for k in range(1, m + 1))
            pend = {}
        sn = e[:8].decode('latin1').rstrip()
        ext = e[8:11].decode('latin1').rstrip()
        name = name or (sn + ('.' + ext if ext else ''))
        if name in ('.', '..'):
            continue
        attr = e[11]
        c = int.from_bytes(e[26:28], 'little')
        sz = int.from_bytes(e[28:32], 'little')
        p = (path + '/' + name) if path else name
        yield p, sz, c, bool(attr & 0x10)
        if attr & 0x10:
            yield from walk(img, fs, c, p, depth + 1)


def do_setting(d):
    lba, nbytes = PARTS['setting']
    print('reading nflasha2 (%d B) ...' % nbytes)
    img = fetch(d, lba, nbytes // 512, log_every=4096)
    print()
    out = Path('/root/fw/zve10_backup/nflasha2_current.img')
    out.write_bytes(img)
    print('saved %s (%d B)' % (out, len(img)))
    fs = fat_parse(img, lba, nbytes)
    if not fs:
        print('!! not a FAT image')
        return
    print('\n=== /setting tree ===')
    for p, sz, c, isdir in walk(img, fs, 0):
        line = '  %s %-42s %9d B' % ('DIR ' if isdir else 'FILE', p, sz)
        if not isdir and 0 < sz <= 64 and c >= 2:
            raw = read_file(img, fs, c, sz)
            line += '  = %r' % raw
        print(line)
    return img, fs


def do_log(d):
    lba, nbytes = PARTS['log']
    print('reading nflasha11 (%d B, this takes a while) ...' % nbytes)
    img = bytearray()
    step = 64 * 1024 * 1024
    done = 0
    t0 = time.time()
    while done < nbytes:
        n = min(step, nbytes - done)
        img += fetch(d, lba + done // 512, n // 512, log_every=8192)
        done += n
        el = time.time() - t0
        print('  %.0f%%  %.1f MB/s' % (100.0 * done / nbytes, done / 1048576 / el if el else 0), end='\r')
    print()
    out = Path('/root/fw/zve10_backup/nflasha11_current.img')
    out.write_bytes(bytes(img))
    print('saved %s (%d B)' % (out, len(img)))
    print('\n=== scanning for failure evidence ===')
    for pat in PATTERNS:
        idx = []
        start = 0
        pb = pat.encode()
        while True:
            j = img.find(pb, start)
            if j < 0:
                break
            idx.append(j)
            start = j + 1
            if len(idx) >= 3:
                break
        if idx:
            print('  %-18s %d+ hits, first @ %s' % (pat, len(idx), ['0x%X' % x for x in idx]))
            for j in idx[:2]:
                ctx = bytes(img[max(0, j - 60):j + 90])
                ctx = re.sub(rb'[^\x20-\x7e]', b'.', ctx).decode('latin1')
                print('      ... %s ...' % ctx)
    return img


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--which', choices=['setting', 'log', 'both'], default='both')
    a = ap.parse_args(argv)
    dev = wait_for_device(120)
    if dev is None:
        raise SystemExit('no fastboot device')
    d = Device(dev)
    try:
        d.require('oem emmc-init')
        if a.which in ('setting', 'both'):
            do_setting(d)
        if a.which in ('log', 'both'):
            do_log(d)
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
