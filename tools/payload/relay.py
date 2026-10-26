#!/usr/bin/env python3
"""relay.py — 在笔记本上把本机 7777 转发到相机（纯标准库，Windows/macOS/Linux 通用）

用法:
    python relay.py                 # 自动扫描 10.42.0.x 和 192.168.x.x 找相机
    python relay.py 10.42.0.158     # 指定相机 IP

之后我这边连 100.119.234.90:7777（你的 tailscale IP）就能到相机。
"""
import socket, threading, sys, time

PORT = 7777
LISTEN = ("0.0.0.0", PORT)

def probe(ip, timeout=0.35):
    """探测某 IP 的 7777 是否开着"""
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((ip, PORT))
        # 发个 PING 确认是 agent
        s.sendall(b"PING\n")
        s.settimeout(2.0)
        r = s.recv(64)
        s.close()
        return b"PONG" in r or b"OK" in r or r != b""
    except Exception:
        try: s.close()
        except Exception: pass
        return False

def find_camera():
    """扫描常见热点网段"""
    nets = []
    # 本机地址，推出所在网段
    try:
        h = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        h.connect(("8.8.8.8", 80))
        myip = h.getsockname()[0]
        h.close()
        nets.append(".".join(myip.split(".")[:3]) + ".")
    except Exception:
        pass
    for n in ("10.42.0.", "192.168.137.", "192.168.0.", "192.168.1.", "172.20.10."):
        if n not in nets:
            nets.append(n)

    seen = set()
    for net in nets:
        if net in seen: continue
        seen.add(net)
        print(f"  扫描 {net}x ...", flush=True)
        results = []
        lock = threading.Lock()
        def worker(i):
            ip = net + str(i)
            if probe(ip):
                with lock: results.append(ip)
        ths = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(1, 255)]
        for t in ths: t.start()
        for t in ths: t.join(timeout=1.2)
        if results:
            print(f"  ★ 找到: {results}")
            return results[0]
    return None

def pipe(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d: break
            b.sendall(d)
    except Exception:
        pass
    finally:
        for s in (a, b):
            try: s.shutdown(socket.SHUT_RDWR)
            except Exception: pass
            try: s.close()
            except Exception: pass

def handle(c):
    try:
        u = socket.create_connection(CAM, timeout=10)
    except Exception as e:
        print(f"  ! 连相机失败: {e}")
        try: c.close()
        except Exception: pass
        return
    threading.Thread(target=pipe, args=(c, u), daemon=True).start()
    threading.Thread(target=pipe, args=(u, c), daemon=True).start()

if len(sys.argv) > 1:
    CAM = (sys.argv[1], PORT)
    print(f"相机: {CAM[0]}")
else:
    print("自动找相机…")
    ip = find_camera()
    if not ip:
        print("✗ 没找到相机。检查：相机是否连上本机热点？agent 是否在跑？")
        sys.exit(1)
    CAM = (ip, PORT)

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    s.bind(LISTEN)
except OSError as e:
    print(f"✗ 绑定 {LISTEN} 失败: {e}（端口被占用？）")
    sys.exit(1)
s.listen(8)
print(f"\n✓ relay 就绪: 0.0.0.0:{PORT}  →  {CAM[0]}:{PORT}")
print("  现在可以让 AI 那边连你的 tailscale IP:7777")
print("  （Ctrl+C 停止）\n")
while True:
    try:
        c, addr = s.accept()
    except KeyboardInterrupt:
        break
    print(f"  连接来自 {addr[0]}")
    threading.Thread(target=handle, args=(c,), daemon=True).start()
