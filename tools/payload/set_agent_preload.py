#!/usr/bin/env python3
"""Set (or inspect) the nflasha2 mode/preload so the agent loads at every boot.

Stage A of the boot test: prove the preload path works and get a persistent
debug channel, WITHOUT touching /usr or loading any M2 code.

What it writes (via Sony's own change_mode.sh, not by hand):

    /setting/mode/dmode   = "3p"
    /setting/mode/preload = "/usr/tool/libefence.so:/log/agent_v14.so"

Why that prefix: bootin.elf only accepts a preload value that STARTS WITH one
of four debug library paths. /usr/tool/libefence.so does not exist on ZV-E10,
so it passes the check and then ld.so ignores it with a warning; the second
entry is the real payload.

Usage:
    python3 set_agent_preload.py --show      # read-only: current state
    python3 set_agent_preload.py --backup    # copy /setting/mode to /log
    python3 set_agent_preload.py --apply     # backup + write + verify
    python3 set_agent_preload.py --revert    # restore mode to "3", empty preload
"""
import socket
import sys

IP, PORT = "10.42.0.158", 7777
PRELOAD = "/usr/tool/libefence.so:/log/agent_v14.so"
BACKUP = "/log/zvegisk/mode_backup"


def connect():
    s = socket.create_connection((IP, PORT), timeout=20)
    s.settimeout(120)
    return s


def line(s):
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1)
        if not c:
            break
        b += c
    return b.decode(errors="replace").strip()


def run(s, cmd):
    d = cmd.encode()
    s.sendall(b"RUN %d\n" % len(d) + d)
    h = line(s)
    if not h.startswith("OUT "):
        return "ERR:" + h
    n = int(h[4:])
    b = b""
    while len(b) < n:
        c = s.recv(min(65536, n - len(b)))
        if not c:
            break
        b += c
    return b.decode(errors="replace")


SHOW = r"""
echo "--- /setting/mode ---"
/bin/busybox ls -la /setting/mode/ 2>&1
echo "--- dmode ---"
/bin/busybox cat /setting/mode/dmode 2>/dev/null; echo
echo "--- preload ---"
/bin/busybox cat /setting/mode/preload 2>/dev/null; echo
echo "--- 探测 libefence.so（应为不存在）---
"
/bin/busybox ls -l /usr/tool/libefence.so 2>&1
echo "--- agent 是否存在 ---"
/bin/busybox ls -l /log/agent_v14.so 2>&1
echo "--- bind.txt（watchdog 开关，必须不存在）---"
/bin/busybox ls -l /log/zv1f_ui/bind.txt 2>&1
"""

BACKUP = r"""
/bin/busybox mkdir -p /log/zvegisk/mode_backup
/bin/busybox cp -f /setting/mode/dmode /log/zvegisk/mode_backup/dmode 2>&1
/bin/busybox cp -f /setting/mode/preload /log/zvegisk/mode_backup/preload 2>&1
/bin/busybox ls -la /setting/mode/ > /log/zvegisk/mode_backup/listing_before.txt
echo "--- 备份内容 ---"
/bin/busybox ls -la /log/zvegisk/mode_backup/
echo "--- 备份的 dmode ---"
/bin/busybox cat /log/zvegisk/mode_backup/dmode 2>/dev/null; echo
echo "--- 备份的 preload ---"
/bin/busybox cat /log/zvegisk/mode_backup/preload 2>/dev/null; echo
"""

APPLY = r"""
echo "=== 写前状态 ==="
/bin/busybox ls -la /setting/mode/
echo "--- 调用 Sony change_mode.sh ---"
/usr/bin/change_mode.sh p 'PRELOAD_PLACEHOLDER'
echo "--- 写后 dmode（应为 3p）---"
/bin/busybox cat /setting/mode/dmode; echo
echo "--- 写后 preload（应完全等于期望值）---"
/bin/busybox cat /setting/mode/preload; echo
echo "--- 写后目录 ---"
/bin/busybox ls -la /setting/mode/
"""

REVERT = r"""
echo "=== 回退到 mode 3（无 preload）==="
/usr/bin/change_mode.sh 3
/bin/busybox cat /setting/mode/dmode; echo
echo "--- preload 大小（应为 0）---"
/bin/busybox ls -l /setting/mode/preload 2>&1
"""


def main(argv):
    if '--show' in argv:
        action = 'show'
    elif '--backup' in argv:
        action = 'backup'
    elif '--apply' in argv:
        action = 'apply'
    elif '--revert' in argv:
        action = 'revert'
    else:
        action = 'show'

    s = connect()
    try:
        print("=== 当前状态 ===")
        print(run(s, SHOW).strip())

        if action == 'show':
            return 0

        print()
        print("=== 备份 ===")
        print(run(s, BACKUP).strip())

        if action == 'backup':
            return 0

        if action == 'revert':
            print()
            print(run(s, REVERT).strip())
            print("\n已回退。手动断电重启即可。")
            return 0

        # apply
        print()
        print("=== 写入 ===")
        # 用单引号包住路径，避免 shell 展开
        assert "'" not in PRELOAD
        out = run(s, APPLY.replace('PRELOAD_PLACEHOLDER', PRELOAD)).strip()
        print(out)

        # 硬性校验
        print()
        print("=== 校验 ===")
        got = run(s, "/bin/busybox cat /setting/mode/preload").strip()
        dm = run(s, "/bin/busybox cat /setting/mode/dmode").strip()
        ok = True
        if got != PRELOAD:
            print("  ✗ preload 内容不符")
            print("    期望: %r" % PRELOAD)
            print("    实际: %r" % got)
            ok = False
        else:
            print("  ✓ preload = %r" % got)
        if dm != '3p':
            print("  ✗ dmode 不是 3p，实际 %r" % dm)
            ok = False
        else:
            print("  ✓ dmode = '3p'")
        print()
        if ok:
            print("★ 配置正确。现在可以手动断电重启。")
            print("  重启后【不要插 USB】，等 WiFi 上线，7777 应自动可连。")
        else:
            print("!! 校验失败，请用 --revert 回退后再查。")
        return 0 if ok else 1
    finally:
        s.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
