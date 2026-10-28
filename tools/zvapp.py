#!/usr/bin/env python3
"""ZV-E10 QEMU 模拟器控制台（单文件，无第三方依赖）。
画面: /proc/<qemu>/mem 物理 0x22c48000 (640x480 RGBA4444)
按键: 往 guest 内 InfraInput 队列 0x007a0020 投 20 字节 osal 消息
日志: QEMU -serial file，直接读文件（不依赖 socket/守护进程）"""
import json, os, re, shutil, signal, socket, struct, subprocess, threading, time, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT   = "/mnt/build/ref-chocotan-zve10"
KERNEL = f"{ROOT}/work/kbuild-ui/arch/arm/boot/Image"
RUN    = "/tmp/zv"
LOG    = f"{RUN}/serial.log"
KBD    = f"{RUN}/kbd.sock"
QEMU   = "/mnt/build/work-q/qemu-11.1.2/build-zve10/qemu-system-arm"
PAT    = "build-zve" + "10/qemu-system-arm"      # 只匹配本会话的 QEMU
APPEND = ("console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 "
          "memrsv=128M@0x20000000:uc rdinit=/init loglevel=8")
FB_PHYS = 0x22c48000
QID     = 0x007a0020
KEYS = {"UP": 0x3, "DOWN": 0x30, "LEFT": 0x15, "RIGHT": 0x1af,
        "OK": 0x1ac, "MENU": 0x1ab, "BACK": 0x1f8}
PORT = int(os.environ.get("PORT", "8099"))
MAX_ATTEMPTS = 5

os.makedirs(RUN, exist_ok=True)
LOCK = threading.Lock()
ST = {"proc": None, "pid": None, "state": "idle", "attempt": 0,
      "menu": False, "msg": "", "started": 0, "base": None, "hl": -1}

def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout.split()

def my_pids():
    out = []
    for p in sh(["pgrep", "-f", PAT]):
        try:
            if int(p) != os.getpid(): out.append(int(p))
        except ValueError: pass
    return out

def kill_all():
    for p in my_pids():
        try: os.kill(p, 9)
        except OSError: pass
    time.sleep(1)

def log_size():
    try: return os.path.getsize(LOG)
    except OSError: return 0

def menu_ready():
    try:
        with open(LOG, "rb") as f:
            return b"export_ready=0x1" in f.read()[-4_000_000:]
    except OSError:
        return False

def boot_stage():
    """从日志尾部推断阶段，给前端显示进度。"""
    try:
        d = open(LOG, "rb").read()[-20000:].decode("utf-8", "replace")
    except OSError:
        return "无日志"
    for pat, name in [("export_ready=0x1", "菜单已就绪"),
                      ("GEMINI.total.textrect", "绘制中"),
                      ("uxengine_init", "图形层初始化"),
                      ("UI_INIT_ENTER", "服务初始化"),
                      ("infra_lanc_init", "网络初始化"),
                      ("Freeing unused kernel", "内核启动用户态"),
                      ("Memory policy", "内核内存初始化"),
                      ("Booting Linux", "内核启动")]:
        if pat in d: return name
    return "启动中"

def launch_once():
    kill_all()
    for f in (KBD,):
        try: os.unlink(f)
        except OSError: pass
    open(LOG, "wb").close()
    args = [QEMU, "-M", "zve10", "-cpu", "cortex-a9", "-smp", "4", "-m", "1024",
            "-kernel", KERNEL, "-append", APPEND, "-display", "none",
            "-serial", f"file:{LOG}",
            "-serial", f"unix:{KBD},server=on,wait=off",
            "-monitor", "none", "-no-reboot"]
    err = open(f"{RUN}/qemu.err", "wb")
    p = subprocess.Popen(args, stdout=err, stderr=err, start_new_session=True)
    return p

def boot_worker():
    with LOCK:
        if ST["state"] == "running": return
        ST.update(state="booting", menu=False, msg="", attempt=0)
    for attempt in range(1, MAX_ATTEMPTS + 1):
        with LOCK: ST["attempt"] = attempt
        p = launch_once()
        with LOCK:
            ST["proc"], ST["pid"], ST["started"] = p, p.pid, time.time()
        for _ in range(30):                     # 最多 6 分钟
            time.sleep(12)
            if p.poll() is not None:
                break
            if menu_ready():
                with LOCK:
                    ST.update(state="running", menu=True, msg=f"第 {attempt} 次启动成功")
                return
        with LOCK: ST["msg"] = f"第 {attempt} 次未出画面，重试…"
        try: p.kill()
        except OSError: pass
    with LOCK: ST.update(state="failed", menu=False, msg=f"{MAX_ATTEMPTS} 次尝试均失败")

def stop_all():
    with LOCK: ST.update(state="idle", menu=False)
    kill_all()

# ---------- frame ----------
def ram_base(pid):
    try:
        best = None
        for line in open(f"/proc/{pid}/maps"):
            p = line.split()
            a, b = (int(x, 16) for x in p[0].split("-"))
            if "rw" in p[1] and (b - a) >= 900 * 1024 * 1024:
                if best is None or a < best: best = a
        return best
    except Exception:
        return None

def read_fb():
    pid = ST["pid"]
    if not pid or not os.path.exists(f"/proc/{pid}"):
        return None
    base = ram_base(pid)
    if base is None: return None
    try:
        fd = os.open(f"/proc/{pid}/mem", os.O_RDONLY)
        d = os.pread(fd, 640 * 480 * 2, base + FB_PHYS)
        os.close(fd)
        return d if len(d) == 640 * 480 * 2 else None
    except Exception:
        return None

def png_from(raw):
    rgb = bytearray(640 * 480 * 3)
    for i in range(640 * 480):
        w = raw[i*2] | raw[i*2+1] << 8
        o = i*3
        rgb[o]   = ((w >> 12) & 15) * 17
        rgb[o+1] = ((w >> 8) & 15) * 17
        rgb[o+2] = ((w >> 4) & 15) * 17
    raw2 = b"".join(b"\x00" + bytes(rgb[y*640*3:(y+1)*640*3]) for y in range(480))
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t+d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", 640, 480, 8, 2, 0, 0, 0))
            + ch(b"IDAT", zlib.compress(raw2, 6)) + ch(b"IEND", b""))

def hl_row(raw):
    if not raw: return -1
    best, bestcnt = -1, 0
    for y in range(0, 480, 2):
        cnt = 0; base = y*640
        for x in range(0, 640, 2):
            o = (base+x)*2
            w = raw[o] | raw[o+1] << 8
            r = ((w >> 12) & 15) * 17; g = ((w >> 8) & 15) * 17; b = ((w >> 4) & 15) * 17
            if r > 120 and 40 < g < 140 and b < 80: cnt += 1
        if cnt > bestcnt: best, bestcnt = y, cnt
    return best if bestcnt > 50 else -1

# ---------- keys ----------
def kbd_send(lines, wait=0.4):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(KBD); s.settimeout(3)
        s.sendall(b"\n"); time.sleep(0.15)          # guest tty drops the first line
        for ln in lines:
            s.sendall(ln.encode() + b"\n"); time.sleep(wait)
        s.close(); return True
    except Exception as e:
        return False

def press(name):
    k = KEYS.get(name.upper())
    if k is None: return False, "unknown key"
    ok = kbd_send([f"#Kq {QID:#x} {k} 1"])          # single press only
    return ok, ("sent" if ok else "inject failed (qemu not running?)")

FILTERS = {
 "all":  lambda l: True,
 "kern": lambda l: l.startswith("<") or "cxd" in l.lower() or "kernel" in l.lower() or "hwtimer" in l,
 "app":  lambda l: ("UI_" in l or "GEMINI" in l or "menu" in l.lower() or "INIT" in l),
 "err":  lambda l: ("error" in l.lower() or "fail" in l.lower() or "panic" in l.lower() or "segvert" in l.lower()),
}

def tail(tab="all", n=400):
    try:
        with open(LOG, "rb") as f:
            f.seek(max(0, log_size() - 300_000))
            txt = f.read().decode("utf-8", "replace")
    except OSError:
        return ""
    lines = [l for l in txt.splitlines() if FILTERS.get(tab, FILTERS["all"])(l)]
    return "\n".join(lines[-n:])

PAGE = r"""<!doctype html><html lang=zh><meta charset=utf-8><title>ZV-E10 模拟器</title>
<style>
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:#0b0c10;color:#d7dae0;font:14px/1.5 system-ui,"Noto Sans CJK SC",sans-serif;overflow:hidden}
header{display:flex;align-items:center;gap:10px;padding:8px 14px;background:#14161c;border-bottom:1px solid #23262f;height:46px}
header h1{font-size:14px;margin:0;font-weight:600;color:#9cc4ff;letter-spacing:.3px}
.pill{padding:2px 9px;border-radius:999px;font-size:12px;border:1px solid #2c313c;background:#191c23;color:#9aa3b2;white-space:nowrap}
.ok{color:#7ee2a8;border-color:#2e5a41}.warn{color:#ffcc70;border-color:#5a4a24}.bad{color:#ff8a8a;border-color:#5a2e2e}
main{display:grid;grid-template-columns:minmax(420px,660px) minmax(360px,1fr);gap:12px;padding:12px;height:calc(100vh - 46px);overflow:hidden}
@media(max-width:1000px){main{grid-template-columns:1fr;grid-template-rows:auto 1fr}}
.col{display:flex;flex-direction:column;gap:12px;min-height:0;min-width:0}
.card{background:#12141a;border:1px solid #23262f;border-radius:10px;padding:10px;min-height:0;display:flex;flex-direction:column}
.card h2{margin:0 0 8px;font-size:12px;color:#8fa1bd;font-weight:600;letter-spacing:.4px;text-transform:uppercase;flex:0 0 auto}
.screenwrap{background:#000;border:1px solid #222;border-radius:6px;display:flex;align-items:center;justify-content:center;
  width:100%;aspect-ratio:4/3;max-height:52vh;overflow:hidden;flex:0 0 auto}
#screen{width:100%;height:100%;object-fit:contain;image-rendering:pixelated;display:block}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}
.pad button,.row button,.row3 button{background:#1b1f27;color:#e6e9ef;border:1px solid #2f3542;border-radius:8px;padding:10px 4px;font-size:14px;cursor:pointer;transition:background .1s}
.pad button:hover,.row button:hover,.row3 button:hover{background:#242a34;border-color:#40506b}
.pad button:active,.row button:active,.row3 button:active{background:#2f3a4d}
.row{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:7px}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px;margin-top:7px}
.k-ok{background:#204a35!important;border-color:#2f6b4d!important}
.k-menu{background:#2a2418!important;border-color:#5a4a24!important}
.k-back{background:#2a1c1c!important;border-color:#5a2e2e!important}
#msg{height:20px;line-height:20px;font-size:12px;color:#8fd4a8;overflow:hidden;white-space:nowrap;flex:0 0 auto}
.logcard{flex:1 1 auto;min-height:0}
#log{flex:1 1 auto;min-height:220px;overflow:auto;background:#07080b;border:1px solid #1d2027;border-radius:6px;padding:8px;margin:0;
  font-family:ui-monospace,"DejaVu Sans Mono","Liberation Mono",monospace;font-size:11.5px;line-height:1.5;color:#9fb0c8;
  white-space:pre;overflow-x:auto;letter-spacing:0;font-variant-ligatures:none}
.tabs{display:flex;gap:6px;margin-bottom:8px;flex-wrap:wrap;flex:0 0 auto}
.tabs button{background:#171a21;border:1px solid #262b35;color:#93a0b3;padding:4px 10px;border-radius:6px;font-size:12px;cursor:pointer}
.tabs button.on{background:#20304a;color:#cfe2ff;border-color:#37527d}
.stat{display:flex;justify-content:space-between;font-size:11.5px;color:#8b95a7;padding:3px 0}
.hint{font-size:11px;color:#6d7688;margin-top:6px;line-height:1.45;flex:0 0 auto}
.spin{display:inline-block;width:10px;height:10px;border:2px solid #3a4356;border-top-color:#7ea6e0;border-radius:50%;animation:s .8s linear infinite;vertical-align:-1px}
@keyframes s{to{transform:rotate(360deg)}}
</style>
<header>
 <h1>ZV-E10 原厂系统 · 模拟器</h1>
 <span id=p_state class=pill>空闲</span>
 <span id=p_menu class=pill>菜单 —</span>
 <span id=p_stage class=pill>—</span>
 <span id=p_at class=pill style="margin-left:auto">—</span>
</header>
<main>
 <div class="col">
  <div class="card">
   <h2>屏幕 · 原厂 App 渲染 640×480</h2>
   <div class=screenwrap><img id=screen src="/frame.png?t=0" width=640 height=480 alt=""></div>
   <div class=row><button onclick="act('start')">▶ 启动</button><button onclick="act('stop')">■ 停止</button></div>
   <div class=row3><button onclick="act('snap')">截图</button><button onclick="setZoom()" id=zoombtn>缩放 100%</button><button onclick="act('start')">重启</button></div>
   <div id=msg></div>
   <div class=hint>用方向键在菜单里移动，按「确定」进入，按「菜单」开关菜单，按「返回」退回上一层。也可以直接用键盘的方向键 / 回车 / Esc 操作。</div>
  </div>
 </div>
 <div class="col">
  <div class="card" style="flex:0 0 auto">
   <h2>虚拟按键</h2>
   <div class=pad>
     <span></span><button onmousedown="k('UP')">▲</button><span></span>
     <button onmousedown="k('LEFT')">◀</button><button onmousedown="k('OK')" class=k-ok>确定</button><button onmousedown="k('RIGHT')">▶</button>
     <span></span><button onmousedown="k('DOWN')">▼</button><span></span>
   </div>
   <div class=row3>
     <button onmousedown="k('MENU')" class=k-menu>菜单</button>
     <button onmousedown="k('BACK')" class=k-back>返回</button>
     <button onclick="k('OK')">确定</button>
   </div>
   <div class=stat><span>按键队列</span><span>0x007a0020</span></div>
  </div>
  <div class="card logcard">
   <h2>日志</h2>
   <div class=tabs>
    <button id=t_all class=on onclick="tab('all')">全部</button>
    <button id=t_kern onclick="tab('kern')">内核</button>
    <button id=t_app onclick="tab('app')">应用</button>
    <button id=t_err onclick="tab('err')">错误</button>
    <button id=auto class=on style="margin-left:auto" onclick="autoScroll=!autoScroll;this.className=autoScroll?'on':''">跟随</button>
   </div>
   <pre id=log></pre>
  </div>
 </div>
</main>
<script>
let T='all', autoScroll=true, zoom=false;
function act(a){fetch('/'+a,{method:'POST'}).then(r=>r.text()).then(t=>msg(t));}
function msg(t){const m=document.getElementById('msg');m.textContent=t||'';}
function k(n){msg(n+' …');fetch('/key?k='+n,{method:'POST'}).then(r=>r.text()).then(t=>msg(n+': '+t));}
function tab(t){T=t;['all','kern','app','err'].forEach(x=>document.getElementById('t_'+x).className=(x==t?'on':''));pollLog();}
function setZoom(){zoom=!zoom;const w=document.querySelector('.screenwrap');w.style.maxHeight=zoom?'88vh':'52vh';document.getElementById('zoombtn').textContent='缩放 '+(zoom?'放大':'100%');}
let lastLog='';
function pollLog(){
 fetch('/log?f='+T).then(r=>r.text()).then(t=>{
   if(t===lastLog)return;lastLog=t;
   const el=document.getElementById('log');el.textContent=t;
   if(autoScroll)el.scrollTop=el.scrollHeight;
 });
}
function pollState(){
 fetch('/state').then(r=>r.json()).then(s=>{
   const pe=document.getElementById('p_state');
   pe.textContent={idle:'空闲',booting:'启动中',running:'运行中',failed:'失败'}[s.state]||s.state;
   pe.className='pill '+(s.state==='running'?'ok':s.state==='booting'?'warn':s.state==='failed'?'bad':'');
   const pm=document.getElementById('p_menu');
   pm.textContent='菜单 '+(s.menu?'就绪':'未出画面');
   pm.className='pill '+(s.menu?'ok':'');
   document.getElementById('p_stage').textContent=s.stage||'—';
   const sp=s.state==='booting'?' <span class=spin></span>':'';
   document.getElementById('p_at').innerHTML=(s.attempt?('第 '+s.attempt+' 次 · '):'')+s.pid_str+sp;
 });
}
let lastFrame=0;
function pollFrame(){
 if(Date.now()-lastFrame<900)return;lastFrame=Date.now();
 const img=document.getElementById('screen');
 const u='/frame.png?t='+lastFrame;
 const nx=new Image();nx.onload=()=>{img.src=u;};nx.src=u;
}
document.addEventListener('keydown',e=>{
 const map={ArrowUp:'UP',ArrowDown:'DOWN',ArrowLeft:'LEFT',ArrowRight:'RIGHT',Enter:'OK',Escape:'BACK',m:'MENU',M:'MENU'};
 if(map[e.key]){e.preventDefault();k(map[e.key]);}
});
setInterval(pollState,1500);
setInterval(pollLog,1500);
setInterval(pollFrame,900);
pollState();pollLog();pollFrame();
</script></html>"""

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def _send(self, code, body, ctype="text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/":
            return self._send(200, PAGE.encode(), "text/html; charset=utf-8")
        if u.path == "/state":
            raw = read_fb(); hl = hl_row(raw) if raw else -1
            with LOCK:
                s = dict(ST)
            return self._send(200, json.dumps({
                "state": s["state"], "menu": s["menu"], "attempt": s["attempt"],
                "pid_str": (f"进程 {s['pid']}" if s["pid"] else "无进程"),
                "stage": boot_stage() if s["state"] != "idle" else "—",
                "hl_row": hl, "msg": s["msg"], "msgset": bool(s["msg"]),
            }).encode(), "application/json")
        if u.path == "/frame.png":
            raw = read_fb()
            if not raw: return self._send(503, b"no frame")
            return self._send(200, png_from(raw), "image/png")
        if u.path == "/log":
            f = (parse_qs(u.query).get("f") or ["all"])[0]
            return self._send(200, tail(f).encode())
        return self._send(404, b"nf")
    def do_POST(self):
        u = urlparse(self.path)
        if u.path == "/start":
            if ST["state"] in ("booting", "running"):
                return self._send(200, "已在运行".encode())
            threading.Thread(target=boot_worker, daemon=True).start()
            return self._send(200, "开始启动（自动重试，最多 5 次）".encode())
        if u.path == "/stop":
            stop_all(); return self._send(200, "已停止".encode())
        if u.path == "/snap":
            raw = read_fb()
            if not raw: return self._send(503, b"no frame")
            p = f"{RUN}/snap-{time.strftime('%Y%m%d-%H%M%S')}.png"
            open(p, "wb").write(png_from(raw))
            return self._send(200, p.encode())
        if u.path == "/key":
            n = (parse_qs(u.query).get("k") or [""])[0]
            ok, why = press(n)
            return self._send(200, (why).encode())
        return self._send(404, b"nf")

if __name__ == "__main__":
    print("serving", PORT, flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
