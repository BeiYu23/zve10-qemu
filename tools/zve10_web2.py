#!/usr/bin/env python3
"""ZV-E10 QEMU web console: live frame + key injection buttons."""
import io, os, socket, struct, subprocess, time, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = "/mnt/build/ref-chocotan-zve10"
KERNEL = f"{ROOT}/work/kbuild-ui/arch/arm/boot/Image"
SOCK_DIR = "/mnt/build/tmp"
CONS, KBD = f"{SOCK_DIR}/cons.sock", f"{SOCK_DIR}/kbd.sock"
LOG = "/mnt/build/tmp/webui/serial.log"
PORT = int(os.environ.get("PORT", "8099"))
HOST = os.environ.get("HOST", "0.0.0.0")
QEMU = "/mnt/build/work-q/qemu-11.1.2/build-zve10/qemu-system-arm"
A, B = "build-zve", "10/qemu-system-arm"
PAT = A + B
APPEND = ("console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 "
          "memrsv=128M@0x20000000:uc rdinit=/init loglevel=8")
KEYS = {"UP": 3, "DOWN": 0x30, "LEFT": 0x15, "RIGHT": 0x1af,
        "OK": 0x1ac, "MENU": 0x1ab, "BACK": 0x1f8}
QID = "0x007a0020"
os.makedirs("/mnt/build/tmp/webui", exist_ok=True)

def qemu_pid():
    try:
        out = subprocess.run(["pgrep", "-f", PAT], capture_output=True, text=True).stdout.split()
        return int(out[0]) if out else None
    except Exception:
        return None

def ram_base(pid):
    best = None
    for line in open(f"/proc/{pid}/maps"):
        p = line.split()
        a, b = (int(x, 16) for x in p[0].split("-"))
        if "rw" in p[1] and (b - a) >= 900 * 1024 * 1024:
            if best is None or a < best: best = a
    return best

def png(rgb, w, h):
    raw = b"".join(b"\x00" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t+d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + ch(b"IDAT", zlib.compress(raw, 6)) + ch(b"IEND", b""))

def frame_png(phys=0x22c48000):
    pid = qemu_pid()
    if not pid: return None, "no qemu"
    try:
        base = ram_base(pid)
        fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        d = os.pread(fd, 640*480*2, base + phys)
        os.close(fd)
        if len(d) != 640*480*2: return None, "short read"
        rgb = bytearray(640*480*3)
        for i in range(640*480):
            w = d[i*2] | d[i*2+1] << 8
            o = i*3
            rgb[o] = ((w >> 12) & 15) * 17
            rgb[o+1] = ((w >> 8) & 15) * 17
            rgb[o+2] = ((w >> 4) & 15) * 17
        return png(bytes(rgb), 640, 480), "ok"
    except Exception as e:
        return None, str(e)

def kbd_send(lines, wait=0.5):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(KBD); s.settimeout(3)
        for ln in lines:
            s.sendall(ln.encode() + b"\n"); time.sleep(wait)
        s.close(); return True
    except Exception:
        return False

def inject(name):
    k = KEYS.get(name.upper())
    if k is None: return False
    # dummy line first: the guest tty drops the first line after connect
    return kbd_send(["", f"#Kq {QID} {k}"], 0.4)

def start_qemu():
    if qemu_pid(): return "already running"
    for f in (CONS, KBD):
        try: os.unlink(f)
        except OSError: pass
    open(LOG, "wb").close()
    args = [QEMU, "-M", "zve10", "-cpu", "cortex-a9", "-smp", "4", "-m", "1024",
            "-kernel", KERNEL, "-append", APPEND, "-display", "none",
            "-serial", f"unix:{CONS},server=on,wait=off",
            "-serial", f"unix:{KBD},server=on,wait=off",
            "-monitor", "none", "-no-reboot"]
    subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)
    time.sleep(2)
    subprocess.Popen(["python3", "/mnt/build/consd.py", LOG], start_new_session=True,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return "started"

def stop_qemu():
    for p in subprocess.run(["pgrep", "-f", PAT], capture_output=True, text=True).stdout.split():
        try: os.kill(int(p), 9)
        except Exception: pass
    return "stopped"

PAGE = """<!doctype html><meta charset=utf-8><title>ZV-E10 QEMU</title>
<style>body{background:#111;color:#ddd;font-family:system-ui;margin:16px}
img{background:#000;border:1px solid #333;max-width:100%}
button{background:#222;color:#eee;border:1px solid #444;padding:10px 16px;margin:4px;font-size:15px;border-radius:6px}
button:hover{background:#333}.row{margin:10px 0}pre{background:#000;color:#8f8;padding:8px;height:150px;overflow:auto;font-size:12px}</style>
<h2>ZV-E10 原厂菜单 · QEMU 控制台</h2>
<div><img id=f width=640 height=480 src="/frame.png?t=0"></div>
<div class=row>
<button onclick="k('UP')">▲ UP</button><button onclick="k('DOWN')">▼ DOWN</button>
<button onclick="k('LEFT')">◀ LEFT</button><button onclick="k('RIGHT')">▶ RIGHT</button>
<button onclick="k('OK')">● OK</button><button onclick="k('MENU')">MENU</button>
<button onclick="k('BACK')">↩ BACK</button>
</div>
<div class=row>
<button onclick="f('/start')">启动 QEMU</button>
<button onclick="f('/stop')">停止</button>
<button onclick="f('/snap')">保存截图</button>
</div>
<div id=msg></div><pre id=log></pre>
<script>
function k(n){fetch('/key?k='+n,{method:'POST'}).then(r=>r.text()).then(t=>document.getElementById('msg').textContent=t);}
function f(u){fetch(u,{method:'POST'}).then(r=>r.text()).then(t=>document.getElementById('msg').textContent=t);}
setInterval(()=>{document.getElementById('f').src='/frame.png?t='+Date.now();},1200);
setInterval(()=>{fetch('/log').then(r=>r.text()).then(t=>{const l=document.getElementById('log');l.textContent=t;l.scrollTop=l.scrollHeight;});},2000);
</script>"""

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, body, ctype="text/plain; charset=utf-8"):
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body))); self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        if self.path.startswith("/frame.png"):
            b, why = frame_png()
            if b: return self._send(200, b, "image/png")
            return self._send(503, why.encode())
        if self.path == "/":
            return self._send(200, PAGE.encode(), "text/html; charset=utf-8")
        if self.path == "/log":
            try: data = open(LOG, "rb").read()[-4000:].decode("utf-8", "replace")
            except Exception: data = ""
            return self._send(200, data.encode())
        if self.path == "/status":
            pid = qemu_pid()
            return self._send(200, f"qemu_pid={pid}\n".encode())
        return self._send(404, b"nf")
    def do_POST(self):
        if self.path.startswith("/key"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            n = (q.get("k") or [""])[0]
            ok = inject(n)
            return self._send(200, f"key {n} {'sent' if ok else 'FAILED'}".encode())
        if self.path == "/start": return self._send(200, start_qemu().encode())
        if self.path == "/stop": return self._send(200, stop_qemu().encode())
        if self.path == "/snap":
            b, why = frame_png()
            if not b: return self._send(503, why.encode())
            p = f"/mnt/build/tmp/webui/snap-{int(time.time())}.png"
            open(p, "wb").write(b)
            return self._send(200, p.encode())
        return self._send(404, b"nf")

if __name__ == "__main__":
    print("serving on", HOST, PORT, flush=True)
    ThreadingHTTPServer((HOST, PORT), H).serve_forever()
