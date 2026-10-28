#!/usr/bin/env python3
"""Capture the ZV-E10 frame from the QEMU process memory (no Pillow needed).
Usage: cap.py <tag>   -> prints sha256 prefix + distinct colours, writes menu-<tag>.png"""
import os, subprocess, sys, hashlib, zlib, struct

def write_png(path, rgb, w, h):
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
    open(path, 'wb').write(png)

tag = sys.argv[1]
A='build-zve'; B='10/qemu-system-arm'
pids = subprocess.run(['pgrep','-f',A+B], capture_output=True, text=True).stdout.split()
if not pids:
    print('NO_QEMU'); raise SystemExit(2)
pid = int(pids[0])
bases = []
for line in open(f"/proc/{pid}/maps"):
    p = line.split()
    a, b = (int(x, 16) for x in p[0].split('-'))
    if 'rw' in p[1] and (b - a) >= 900 * 1024 * 1024:
        bases.append(a)
fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
SPAN = 640 * 480 * 2
for base in bases[:1]:
    d = os.pread(fd, SPAN, base + 0x22c48000)
    if len(d) != SPAN:
        continue
    distinct = len(set(d[i:i+2] for i in range(0, SPAN, 2)))
    # detect the orange highlight bar row (menu cursor)
    best_row, best_cnt = -1, 0
    rows = {}
    for y in range(480):
        cnt = 0
        base_y = y * 640
        for x in range(0, 640, 2):
            o = (base_y + x) * 2
            w = d[o] | d[o+1] << 8
            r = ((w >> 12) & 15) * 17; g = ((w >> 8) & 15) * 17; b = ((w >> 4) & 15) * 17
            if r > 120 and 40 < g < 140 and b < 80: cnt += 1
        if cnt > best_cnt: best_cnt, best_row = cnt, y
        rows[y] = cnt
    print(tag, hashlib.sha256(d).hexdigest()[:16], 'distinct=%d' % distinct,
          'hl_row=%d hl_cnt=%d' % (best_row, best_cnt))
    if distinct > 3:
        rgba = bytearray(SPAN * 2)
        for o in range(0, SPAN, 2):
            w = d[o] | d[o+1] << 8
            rgba[o*2:o*2+4] = bytes((((w >> 12) & 15) * 17, ((w >> 8) & 15) * 17,
                                     ((w >> 4) & 15) * 17, (w & 15) * 17))
        rgb = bytearray(640 * 480 * 3)
        for i in range(640 * 480):
            rgb[i*3:i*3+3] = rgba[i*4:i*4+3]
        write_png(f'/mnt/build/menu-{tag}.png', bytes(rgb), 640, 480)
os.close(fd)
