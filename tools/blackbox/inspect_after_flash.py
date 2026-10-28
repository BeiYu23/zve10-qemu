#!/usr/bin/env python3
"""刷完 v9 开机后（无 UI 时）的诊断：看 im.elf 到底加载了哪一版的库。

用法：
  1) 拔插 USB，让相机回到 mass-storage 模式
  2) cd /root/fw/oss/tools/payload && source env.sh
     python3 send_payload.py vendor/agent_v14.so --pid 0x0D95
  3) python3 /root/fw/oss/tools/blackbox/inspect_after_flash.py
"""
import re, socket, sys

IP, PORT = "10.42.0.158", 7777

# 期望尺寸：E10 工厂 vs M2
EXPECT = {
    "libObj.so":      {"E10": 21305528, "M2": 3931624},
    "libIMDB.so":     {"E10": 37204,    "M2": 40596},
    "libInfraSKD.so": {"E10": 10184,    "M2": 7259708},
    "libSysDef.so":   {"E10": 3267652,  "M2": 6033848},
    "appFwBoot.so":   {"E10": None,     "M2": 8312},
    "appFw.so":       {"E10": None,     "M2": 64273528},
}

def conn(ip, port):
    s = socket.create_connection((ip, port), timeout=20); s.settimeout(120); return s
def rl(s):
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1)
        if not c: raise EOFError
        b += c
    return b
def rx(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError
        b += c
    return b
def run(s, cmd):
    d = cmd.encode(); s.sendall(b"RUN %d\n" % len(d) + d)
    h = rl(s).decode(errors="replace").strip()
    return rx(s, int(h[4:])).decode(errors="replace", replace="?") if h.startswith("OUT ") else ""

def main():
    s = conn(IP, PORT)
    try:
        print("=" * 70)
        print("① 找 im.elf 进程")
        print("=" * 70)
        procs = run(s, "for p in /proc/[0-9]*; do c=$(cat $p/comm 2>/dev/null); "
                       "[ \"$c\" = \"im.elf\" ] && echo ${p#/proc/}; done").split()
        flags = run(s, "for p in /proc/[0-9]*; do c=$(cat $p/comm 2>/dev/null); "
                       "[ \"$c\" = \"im.elf\" ] && echo ${p#/proc/}; done | wc -l").strip()
        print("  im.elf 实例: %s" % (procs or "（无）"))
        if not procs:
            print("  ✗ im.elf 没在跑 ⇒ 应用层根本没起来")
            return 1

        pid = procs[0]
        print("  第一个实例的 argv: %s" % run(s, "cat /proc/%s/cmdline" % pid).replace("\x00", " ").strip())
        env = run(s, "cat /proc/%s/environ" % pid)
        print("  LD_PRELOAD : %s" % ("有" if "LD_PRELOAD" in env else "无"))

        print()
        print("=" * 70)
        print("② 关键库的实际尺寸（判断刷写/加载的是哪一版）")
        print("=" * 70)
        verdict = []
        for name, v in EXPECT.items():
            sz = run(s, "stat -c %%s /usr/lib/%s 2>/dev/null" % name).strip()
            if not sz.isdigit():
                sz = run(s, "cat /usr/lib/%s 2>/dev/null | wc -c" % name).strip()
            actual = int(sz) if sz.isdigit() else None
            if actual in (v["M2"], None) and v["M2"] and actual == v["M2"]:
                tag = "M2 ✓"
                verdict.append((name, "M2"))
            elif actual == v["E10"]:
                tag = "E10（工厂，没刷进去）"
                verdict.append((name, "E10"))
            elif actual is None:
                tag = "不存在"
                verdict.append((name, "missing"))
            else:
                tag = "其他 %s" % actual
                verdict.append((name, "other"))
            print("  %-16s 实际=%-10s %s" % (name, actual, tag))

        print()
        print("=" * 70)
        print("③ im.elf 的 maps 里出现了哪些关键库")
        print("=" * 70)
        for key in ("libObj", "libIMDB", "appFwBoot", "appFw", "libInfraSKD", "libSysDef"):
            n = run(s, "grep -c %s /proc/%s/maps" % (key, pid)).strip()
            print("  %-14s %s 条" % (key, n or "0"))

        print()
        print("=" * 70)
        print("④ 判决")
        print("=" * 70)
        kinds = [k for _, k in verdict]
        if kinds.count("M2") >= 5:
            if "appFwBoot" in run(s, "grep -c appFwBoot /proc/%s/maps" % pid) or \
               run(s, "grep -c appFwBoot /proc/%s/maps" % pid).strip() not in ("", "0"):
                print("  ✓ M2 的库都在盘上，且 appFwBoot 已被映射 ⇒ 新架构链路启动成功")
                print("  → 若屏幕仍无 UI，问题在绘制/CP 层")
            elif run(s, "grep -c libObj /proc/%s/maps" % pid).strip() not in ("", "0"):
                print("  ! M2 的库在盘上，libObj 已映射，但 appFwBoot 没被映射")
                print("  → 卡在 M2 libIMDB 的模块装载（Application_System_Init）")
            else:
                print("  ! M2 的库在盘上但 libObj 都没映射 ⇒ im.elf 没走到 dlopen")
        elif kinds.count("E10") >= 3:
            print("  ✗ 盘上还是工厂 E10 的库 ⇒ 【刷写没生效】")
            print("  → 先解决刷写，别再改镜像内容")
        else:
            print("  ? 混合状态，见上表")
        return 0
    finally:
        s.close()

if __name__ == "__main__":
    sys.exit(main())
