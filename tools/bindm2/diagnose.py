#!/usr/bin/env python3
"""把真机上的取证数据翻译成结论和下一步动作。

用法:
    python3 diagnose.py            # 用默认 IP
    python3 diagnose.py --ip 10.42.0.158
"""
import argparse, socket, sys, re

EXPECT = {                      # bind 后应有的 M2 尺寸
    "libObj.so":       3931624,
    "appFw.so":        64273528,
    "appFwBoot.so":    8312,
    "libIMDB.so":      40596,
    "libInfraSKD.so":  7259708,
    "global.xdb":      16112,
}
E10_SIZE = {                    # 工厂值，用于判断 bind 是否生效
    "libObj.so":       21305528,
    "libIMDB.so":      37204,
    "libInfraSKD.so":  10184,
    "global.xdb":      12252,
}

def conn(ip, port=7777, timeout=25):
    s = socket.create_connection((ip, port), timeout=timeout); s.settimeout(120); return s
def read_line(s):
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1)
        if not c: raise EOFError
        b += c
    return b
def read_exact(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n-len(b))
        if not c: raise EOFError
        b += c
    return b
def run(s, cmd):
    d = cmd.encode(); s.sendall(b"RUN %d\n"%len(d)+d)
    h = read_line(s).decode(errors="replace").strip()
    if not h.startswith("OUT "): return ""
    return read_exact(s, int(h[4:])).decode(errors="replace")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default="10.42.0.158")
    ap.add_argument("--port", type=int, default=7777)
    a = ap.parse_args()
    try:
        s = conn(a.ip, a.port)
    except Exception as e:
        print("✗ 连不上 agent (%s: %s)" % (type(e).__name__, e)); return 2
    try:
        print("=" * 68)
        print("① 基本状态")
        print("=" * 68)
        up = run(s, "cat /proc/uptime").strip()
        pre = run(s, "cat /setting/mode/preload 2>/dev/null").strip()
        dm  = run(s, "cat /setting/mode/dmode 2>/dev/null").strip()
        img = run(s, "/bin/busybox ls -l /log/m2usr.img 2>/dev/null").strip()
        print("  uptime  : %s" % up)
        print("  dmode   : [%s]" % dm)
        print("  preload : [%s]" % pre)
        print("  镜像    : %s" % (img or "(不存在)"))
        for k, want in (("/log/libIMDB_m2.so", 40596), ("/log/bindm2.so", None)):
            out = run(s, "/bin/busybox ls -l %s 2>/dev/null | /bin/busybox awk '{print $5}'" % k).strip()
            print("  %-20s %s" % (k, out or "(不存在)"))
        print()
        print("=" * 68)
        print("② bindm2 日志")
        print("=" * 68)
        log = run(s, "cat /log/bindm2.log 2>/dev/null")
        if not log.strip():
            log2 = run(s, "cat /tmp/.bindm2.log 2>/dev/null")
            if log2.strip():
                print("  （/log 没有，但 /tmp 里有一份）")
                log = log2
        print(log if log.strip() else "  (两份都不存在)")

        print()
        print("=" * 68)
        print("③ 挂载状态")
        print("=" * 68)
        mnt = run(s, "cat /proc/mounts | /bin/busybox grep -E 'm2root|/usr ' 2>/dev/null")
        print(mnt if mnt.strip() else "  (查不到 m2root / /usr)")
        # ★ 本机实测过的特征：bind 生效后 /usr 与 /usr/lib 会显示为同一个 loop 设备
        if "m2root" in mnt:
            print("  ★ 检测到 m2root 挂载 ⇒ loop 挂载成功")
        if re.search(r'/dev/loop\d+\s+/usr\b', mnt) or re.search(r'/dev/loop\d+\s+/usr/lib\b', mnt):
            print("  ★ /usr 或 /usr/lib 显示为 loop 设备 ⇒ bind 已生效（本机实测特征）")
        elif "m2root" in mnt:
            print("  ! m2root 已挂载，但 /usr 未见 loop 设备 ⇒ bind 可能没成功")

        print()
        print("=" * 68)
        print("④ im.elf 映射的库")
        print("=" * 68)
        maps = run(s, "for p in /proc/[0-9]*; do c=$(cat $p/comm 2>/dev/null); "
                      "[ \"$c\" = \"im.elf\" ] && { echo \"PID ${p#/proc/}\"; "
                      "cat $p/maps 2>/dev/null | /bin/busybox grep -oE '[^ ]+\\\\.so' | "
                      "/bin/busybox sort -u; }; done")
        print(maps if maps.strip() else "  (没有 im.elf 进程)")

        # ---------------- 判决 ----------------
        print()
        print("=" * 68)
        print("⑤ 判决")
        print("=" * 68)
        verdict = []
        got = {}
        for line in log.splitlines():
            m = re.match(r"\s*(\S+)\s*=\s*(-?\d+)", line)
            if m: got[m.group(1)] = int(m.group(2))
        if not log.strip():
            verdict.append(("✗", "bindm2 完全没有输出 ⇒ im.elf 没跑到 preload 阶段"))
            verdict.append(("→", "说明问题在 im.elf 被 exec 之前（挂载链 / bootin）"))
            verdict.append(("→", "下一步：核对 /usr 是否挂载、bootin.elf 首字节是 ELF 还是 SCRIPT"))
        else:
            ls = [l for l in log.splitlines()]
            if any("LOOP_SET_FD=" in l and "=-" in l for l in ls):
                verdict.append(("✗", "LOOP_SET_FD 失败 ⇒ loop 设备问题"))
            if any("mount ext2=" in l and "=-" in l for l in ls):
                verdict.append(("✗", "ext2 挂载失败 ⇒ 看日志里的 ext2_magic 行"))
                if any("ext2_magic" in l and "不是 ext2" in l for l in ls):
                    verdict.append(("!", "镜像不是有效 ext2 ⇒ 上传不完整，重新上传"))
            bind_ok = []
            for k, want in EXPECT.items():
                short = {"libObj.so":"libObj.so","appFw.so":"appFw.so","appFwBoot.so":"appFwBoot.so",
                         "libIMDB.so":"libIMDB.so","libInfraSKD.so":"libInfraSKD.so",
                         "global.xdb":"global.xdb"}[k]
                v = got.get(short)
                if v == want: bind_ok.append(k)
            if len(bind_ok) == len(EXPECT):
                verdict.append(("✓", "bind 完全生效（6/6 关键文件都是 M2 尺寸）"))
            elif bind_ok:
                verdict.append(("!", "bind 部分生效（%d/6）: %s" % (len(bind_ok), ", ".join(bind_ok))))
            elif got:
                mount_failed = any(("mount ext2=" in l and "=-" in l) for l in ls)
                if mount_failed:
                    verdict.append(("→", "mount 已失败 ⇒ bind 必然不生效；先解决镜像/权限问题"))
                else:
                    verdict.append(("✗", "bind 没生效（看到的还是工厂尺寸）"))
                    verdict.append(("→", "原因可能：mount/bind 返回负值；或 im.elf 在 bind 前已读了库"))
            if bind_ok and "appFw.so" in maps:
                verdict.append(("✓", "appFw.so 已被映射 ⇒ 新 UI 代码已装载"))
                verdict.append(("→", "若屏幕仍无 UI，问题在绘制/CP 层，不在装载层"))
            elif bind_ok and "appFwBoot.so" in maps and "appFw.so" not in maps:
                verdict.append(("✗", "appFwBoot 装载了但 appFw 没有 ⇒ 卡在 Application_System_Init"))
            elif bind_ok and "appFwBoot.so" not in maps:
                verdict.append(("✗", "appFwBoot.so 未映射 ⇒ M2 libIMDB 没有走装载流程"))
                verdict.append(("→", "确认 /log/libIMDB_m2.so 是否被 preload 加载（看 preload 字符串）"))
        for mark, text in verdict:
            print("  %s %s" % (mark, text))
        print()
        return 0
    finally:
        try: s.close()
        except Exception: pass

if __name__ == "__main__":
    sys.exit(main())
