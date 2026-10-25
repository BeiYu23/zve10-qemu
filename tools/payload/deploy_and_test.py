#!/usr/bin/env python3
"""deploy_and_test.py — 一次 USB 会话内：部署 agent → 发命令 → 读输出 → 干净退出

设计教训：
  · 服务模式有空闲超时，send 和 read 分开做会在间隙断开
  · --keep（留在服务模式）会让相机卡在过渡态，甚至需要拔电池
  ⇒ 所有操作必须在一次会话内完成，且结尾一定发 STOP_SENSER 干净退出
"""
import argparse, os, struct, sys, time

CAT, CMD_SEL, CMD_SET2, CMD_GET = 0x1001, 0x0F, 0x10, 0x11
SRC = os.environ.get("A6700_SRC", "/tmp/r3/应用/src")
TMJIT, DIAC = "/tmp/jiritsu/tmmc.jit", "/tmp/jiritsu/DIAC.csv"

def sel(p): return struct.pack("<HH", CAT, CMD_SEL) + b"\x00" + p.encode() + b"\x00"
def put(d): return struct.pack("<HH", CAT, CMD_SET2) + bytes([0x00, 0xFF, 0x00]) + d
def get():  return struct.pack("<HH", CAT, CMD_GET) + bytes([0x00, 0xFF, 0x00])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("agent", help="agent .so 文件")
    ap.add_argument("--cmd", default="id; echo ---; ls -la /log 2>&1 | head -5")
    ap.add_argument("--pid", default="0x0D95")
    ap.add_argument("--timeout", type=float, default=40.0)
    ap.add_argument("--agent-wait", type=float, default=4.0, help="部署后等待 agent 起来")
    ap.add_argument("--cmd-wait", type=float, default=4.0, help="写命令后等待执行")
    a = ap.parse_args()

    agent = open(a.agent, "rb").read()
    cmd = (a.cmd + "\n").encode()
    print("agent %d B ; cmd %r" % (len(agent), a.cmd))

    sys.path.insert(0, SRC)
    from a6700_unlock.usb import legacy

    t = None
    try:
        t, ch = legacy.run_authentication(int(a.pid,0), 0x0336, a.timeout, False)
        print("[1] authenticated")

        print("[2] 部署 agent → /tmp/jiritsu/libJit.so")
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel("/tmp/jiritsu/libJit.so"))
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=put(agent))
        print("    status=%s" % b.hex())
        time.sleep(a.agent_wait)

        print("[3] 写命令 → %s" % TMJIT)
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel(TMJIT))
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=put(cmd))
        print("    status=%s" % b.hex())
        time.sleep(a.cmd_wait)

        print("[4] 读 %s" % DIAC)
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel(DIAC))
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=get())
        print("    code=%s len=%d" % (c, len(b)))
        if len(b) > 4:
            print("--- DIAC.csv ---")
            print(b[4:].decode("utf-8", errors="replace"))
            print("--- end ---")
        else:
            print("    raw=%s  ⇒ agent 可能没运行（4字节=读失败）" % b.hex())

        print("[5] 干净退出")
        legacy.vendor_request_out(t.device, legacy.STOP_SENSER)
        print("    STOP_SENSER sent")
        return 0
    finally:
        try: legacy.close_target(t)
        except Exception: pass

if __name__ == "__main__":
    sys.exit(main())
