#!/usr/bin/env python3
"""agent_cmd.py — 在一次 USB 会话内完成：写命令 → 等 agent 执行 → 读输出

为什么要一体：服务模式有空闲超时，send 和 read 分开做会在间隙断开。

用法:
  ./agent_cmd.py "id; ls -la /log"
  ./agent_cmd.py "cat /proc/partitions" --wait 4
"""
import argparse, os, struct, sys, time

CAT, CMD_SEL, CMD_SET2, CMD_GET = 0x1001, 0x0F, 0x10, 0x11
SRC = os.environ.get("A6700_SRC", "/tmp/r3/应用/src")
TMJIT, DIAC = "/tmp/jiritsu/tmmc.jit", "/tmp/jiritsu/DIAC.csv"

def sel(path): return struct.pack("<HH", CAT, CMD_SEL) + b"\x00" + path.encode() + b"\x00"
def put(data): return struct.pack("<HH", CAT, CMD_SET2) + bytes([0x00, 0xFF, 0x00]) + data
def get():     return struct.pack("<HH", CAT, CMD_GET) + bytes([0x00, 0xFF, 0x00])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command")
    ap.add_argument("--wait", type=float, default=4.0)
    ap.add_argument("--pid", default="0x0D95")
    ap.add_argument("--service-pid", default="0x0336")
    ap.add_argument("--timeout", type=float, default=40.0)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()

    cmd = (a.command + "\n").encode()
    print("cmd: %r (%d B)" % (a.command, len(cmd)))

    sys.path.insert(0, SRC)
    from a6700_unlock.usb import legacy

    t = None
    try:
        t, ch = legacy.run_authentication(int(a.pid,0), int(a.service_pid,0), a.timeout, False)
        print("authenticated")

        # 1) 写命令到 tmmc.jit
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel(TMJIT)); print("  select cmd  code=%s" % c)
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=put(cmd))
        print("  write  cmd  code=%s status=%s" % (c, b.hex()))

        # 2) 等 agent 轮询执行
        time.sleep(a.wait)

        # 3) 读 DIAC.csv
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel(DIAC)); print("  select out  code=%s" % c)
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=get())
        print("  read   out  code=%s len=%d" % (c, len(b)))
        if len(b) > 4:
            print("--- DIAC.csv ---")
            print(b[4:].decode("utf-8", errors="replace"))
            print("--- end ---")
        else:
            print("  raw: " + b.hex() + "   (4 字节 = 读文件失败，agent 可能没运行)")

        if not a.keep:
            legacy.vendor_request_out(t.device, legacy.STOP_SENSER)
            print("STOP_SENSER sent")
        else:
            print("--keep 保持会话")
        return 0
    finally:
        try: legacy.close_target(t)
        except Exception: pass

if __name__ == "__main__":
    sys.exit(main())
