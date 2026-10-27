#!/usr/bin/env python3
"""ZV-E10 QEMU 网页控制台：启停、实时看屏、抓图、看日志（无第三方依赖，除 Pillow）。"""
import io, json, os, re, signal, subprocess, time, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from PIL import Image, ImageDraw

ROOT = "/mnt/build/ref-chocotan-zve10"
LOGDIR = "/mnt/build/tmp/webui"
SPAN = 640 * 480 * 2          # RGBA4444 frame
PHYS = (0x22c48000, 0x22bb2000)
PORT = int(os.environ.get("PORT", "8099"))
os.makedirs(LOGDIR, exist_ok=True)
LOG = os.path.join(LOGDIR, "serial.log")

state = {"proc": None, "base": None, "pid": None, "t0": 0}

def qemu_pid():
    try:
        out = subprocess.check_output(["pgrep", "-x", "qemu-system-arm"]).split()
        return int(out[0]) if out else None
    except Exception:
        return None

def ram_base(pid):
    if state["pid"] == pid and state["base"]:
        return state["base"]
    best = None
    try:
        for line in open(f"/proc/{pid}/maps"):
            p = line.split()
            a, b = (int(x, 16) for x in p[0].split("-"))
            if "rw" in p[1] and (b - a) >= 900 * 1024 * 1024:
                if best is None or a < best:
                    best = a
    except Exception:
        return None
    state["pid"], state["base"] = pid, best
    return best

def grab(phys=PHYS[0]):
    """返回 (raw, distinct) 或 (None, 0)"""
    pid = qemu_pid()
    if not pid:
        return None, 0
    base = ram_base(pid)
    if base is None:
        return None, 0
    try:
        fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        try:
            d = os.pread(fd, SPAN, base + phys)
        finally:
            os.close(fd)
    except Exception:
        state["base"] = None
        return None, 0
    if len(d) != SPAN:
        return None, 0
    distinct = len(set(d[i:i+2] for i in range(0, SPAN, 2)))
    return d, distinct

def decode(raw):
    rgba = bytearray(SPAN * 2)
    for o in range(0, SPAN, 2):
        w = raw[o] | raw[o + 1] << 8
        rgba[o*2:o*2+4] = bytes((((w >> 12) & 15) * 17, ((w >> 8) & 15) * 17,
                                 ((w >> 4) & 15) * 17, (w & 15) * 17))
    return Image.frombytes("RGBA", (640, 480), bytes(rgba)).convert("RGB")

def placeholder(msg):
    im = Image.new("RGB", (640, 480), (16, 16, 20))
    d = ImageDraw.Draw(im)
    d.text((24, 220), msg, fill=(200, 200, 200))
    return im

def start():
    if qemu_pid():
        return "already running"
    env = dict(os.environ)
    env["CROSS_COMPILE"] = f"{ROOT}/work/armv7-eabihf--glibc--stable-2020.08-1/bin/arm-linux-"
    f = open(LOG, "wb")
    state["proc"] = subprocess.Popen(["./start-zve10-ui.sh", "-smp", "2"], cwd=ROOT,
                                     stdout=f, stderr=subprocess.STDOUT, env=env,
                                     start_new_session=True)
    state["t0"] = time.time()
    return "started"

def stop():
    pid = qemu_pid()
    if pid:
        try: os.kill(pid, signal.SIGTERM)
        except Exception: pass
    p = state.get("proc")
    if p and p.poll() is None:
        try: p.terminate()
        except Exception: pass
    state["base"] = None
    return "stopped"

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def _send(self, code, body, ctype="text/plain; charset=utf-8", extra=None):
        if isinstance(body, str): body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            return self._send(200, PAGE, "text/html; charset=utf-8")
        if path == "/frame.jpg":
            raw, distinct = grab()
            if raw is None:
                im = placeholder("模拟器未运行" if not qemu_pid() else "读取呈现缓冲失败")
            elif distinct < 3:
                im = placeholder("已启动，画面还是空的（菜单尚未绘制完成）")
            else:
                im = decode(raw)
                im.thumbnail((640, 480))
            buf = io.BytesIO(); im.save(buf, "JPEG", quality=85)
            return self._send(200, buf.getvalue(), "image/jpeg", {"Cache-Control": "no-store"})
        if path == "/status":
            pid = qemu_pid()
            raw, distinct = grab() if pid else (None, 0)
            return self._send(200, json.dumps({
                "running": bool(pid), "pid": pid, "distinct": distinct,
                "uptime": int(time.time() - state["t0"]) if state["t0"] else 0,
                "frame_ready": distinct >= 3}), "application/json")
        if path == "/log":
            try:
                data = open(LOG, "rb").read()[-40000:].decode("utf-8", "replace")
            except Exception:
                data = ""
            return self._send(200, data)
        if path == "/snap":
            raw, distinct = grab()
            if raw is None or distinct < 3:
                return self._send(409, "no nonuniform frame yet")
            p = os.path.join(LOGDIR, time.strftime("menu-%Y%m%d-%H%M%S.png"))
            decode(raw).save(p)
            return self._send(200, p)
        return self._send(404, "not found")
    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n: self.rfile.read(n)
        if self.path == "/start": return self._send(200, start())
        if self.path == "/stop":  return self._send(200, stop())
        return self._send(404, "not found")

PAGE = """<!doctype html><html lang=zh><meta charset=utf-8>
<title>ZV-E10 模拟器控制台</title>
<style>
body{background:#111;color:#ddd;font:14px/1.5 system-ui,sans-serif;margin:24px}
h1{font-size:18px;margin:0 0 16px}
#wrap{display:flex;gap:24px;flex-wrap:wrap}
img{width:640px;height:480px;background:#000;border:1px solid #333;image-rendering:pixelated}
button{background:#246;color:#fff;border:0;padding:8px 14px;border-radius:4px;cursor:pointer;margin-right:8px}
button:hover{background:#357}
pre{background:#000;border:1px solid #222;padding:10px;height:480px;overflow:auto;width:520px;font-size:12px}
.bar{margin-bottom:12px}.s{color:#7c7}
</style>
<h1>ZV-E10 原厂模拟器控制台</h1>
<div class=bar>
  <button onclick="act('/start')">启动</button>
  <button onclick="act('/stop')">停止</button>
  <button onclick="snap()">抓图存 PNG</button>
  <span id=st class=s></span>
</div>
<div id=wrap>
  <div><img id=f src="/frame.jpg"></div>
  <div><pre id=log></pre></div>
</div>
<script>
setInterval(()=>{document.getElementById('f').src='/frame.jpg?t='+Date.now()},700);
setInterval(async()=>{const r=await (await fetch('/status')).json();
  document.getElementById('st').textContent =
    (r.running? '运行中 pid='+r.pid+' 用时'+r.uptime+'s' : '未运行') +
    (r.frame_ready? ' · 画面已绘制' : (r.running? ' · 等待菜单绘制…' : ''));
  const t=await (await fetch('/log')).text();
  const p=document.getElementById('log'); p.textContent=t.slice(-12000); p.scrollTop=p.scrollHeight;
},2000);
async function act(u){await fetch(u,{method:'POST'});}
async function snap(){const r=await fetch('/snap');alert(await r.text());}
</script></html>"""

if __name__ == "__main__":
    print(f"http://127.0.0.1:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
