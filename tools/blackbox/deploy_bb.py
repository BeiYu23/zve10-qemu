#!/usr/bin/env python3
"""把黑匣子推到相机：/log/bb.so + /log/blackbox.ko，并把 bb.so 加进 preload。

preload 变成四项：
  /usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so:/log/bb.so

bb.so 只做取证（写 trace），不改变启动行为；不想要了随时 --rollback。
"""
import argparse, os, socket, sys

def conn(ip, port=7777, t=25):
    s = socket.create_connection((ip, port), timeout=t); s.settimeout(120); return s
def rl(s):
    b = b''
    while not b.endswith(b'\n'):
        c = s.recv(1)
        if not c: raise EOFError
        b += c
    return b
def rx(s, n):
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b
def run(s, cmd):
    d = cmd.encode(); s.sendall(b'RUN %d\n' % len(d) + d)
    h = rl(s).decode(errors='replace').strip()
    return rx(s, int(h[4:])).decode(errors='replace') if h.startswith('OUT ') else ''
def push(s, path, data):
    blob = path.encode() + b'\0' + data
    s.sendall(b'PUSH %d\n' % len(blob) + blob)
    return rl(s).decode(errors='replace').strip()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ip', default='10.42.0.158')
    ap.add_argument('--rollback', action='store_true')
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    s = conn(a.ip)
    try:
        if a.rollback:
            run(s, "printf '/usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so' > /setting/mode/preload; sync")
            run(s, "/bin/busybox rm -f /log/bb.so /log/blackbox.ko")
            print('已回滚 preload，删除 /log/bb.so 与 /log/blackbox.ko')
            return 0
        for name in ('bb.so', 'blackbox.ko'):
            src = os.path.join(here, name)
            if not os.path.exists(src):
                print('✗ 缺少 %s' % name); return 1
            run(s, '/bin/busybox rm -f /log/' + name)
            r = push(s, '/log/' + name, open(src, 'rb').read())
            print('  推 %-14s %s' % (name, r))
            if r != 'OK': return 1
        PRE = '/usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so:/log/bb.so'
        out = run(s, "printf '%s' > /setting/mode/preload; sync; echo -n 'preload='; cat /setting/mode/preload; echo" % PRE)
        print('  %s' % out.strip())
        print('\n⇒ 重启相机。之后即使 UI 不出现，黑匣子也会一直记录。')
        print('   读回：python3 read_bb.py   （需要 Pico + 相机在 bootloader）')
        return 0
    finally:
        s.close()

if __name__ == '__main__':
    sys.exit(main())
