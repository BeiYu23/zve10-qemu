#!/usr/bin/env python3
"""Console daemon: log guest output; send lines appended to /mnt/build/tmp/cmd.txt."""
import os, socket, sys, threading, time
SOCK = '/mnt/build/tmp/cons.sock'
LOG = sys.argv[1]
CMD = '/mnt/build/tmp/cmd.txt'
s = None
for _ in range(300):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(SOCK); break
    except OSError: time.sleep(0.2)
else:
    raise SystemExit('no console socket')
out = open(LOG, 'wb', buffering=0)
def reader():
    s.settimeout(1.0)
    while True:
        try:
            d = s.recv(65536)
            if not d: break
            out.write(d)
        except socket.timeout: pass
        except OSError: break
threading.Thread(target=reader, daemon=True).start()
try: pos = os.path.getsize(CMD)
except OSError: open(CMD, 'wb').close(); pos = 0
while True:
    time.sleep(0.2)
    try: sz = os.path.getsize(CMD)
    except OSError: continue
    if sz > pos:
        with open(CMD, 'rb') as f: f.seek(pos); data = f.read()
        pos = sz
        for line in data.split(b'\n'):
            if line.strip(): s.sendall(line + b'\n')
