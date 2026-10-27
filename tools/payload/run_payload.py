#!/usr/bin/env python3
"""run_payload.py — 一次会话内：上传载荷 → 等它执行 → 读回结果

解决两个问题：
  1. 服务模式有【空闲超时】，send 和 read 分开跑会在间隙断开
  2. STOP_SENSER 会让相机 detached，需要长时间等待或插拔

用法:
  ./run_payload.py libJit.so --read /tmp/jiritsu/probe.txt [--wait 8]
"""
import argparse, os, struct, sys, time

CAT, CMD_SEL, CMD_SET2, CMD_GET = 0x1001, 0x0F, 0x10, 0x11
SRC = os.environ.get("A6700_SRC", "/tmp/r3/应用/src")

def frames(path, data):
    sel = struct.pack("<HH", CAT, CMD_SEL) + b"\x00" + path.encode() + b"\x00"
    wr  = struct.pack("<HH", CAT, CMD_SET2) + bytes([0x00, 0xFF, 0x00]) + data
    return sel, wr

def read_frame(path):
    sel = struct.pack("<HH", CAT, CMD_SEL) + b"\x00" + path.encode() + b"\x00"
    get = struct.pack("<HH", CAT, CMD_GET) + bytes([0x00, 0xFF, 0x00])
    return sel, get


def open_service_channel(legacy, service_pid, timeout):
    import time
    deadline = time.time() + timeout
    dev = None
    while time.time() < deadline:
        for d in legacy.iter_usb_devices():
            if d.idVendor == 0x054C and d.idProduct == service_pid:
                dev = d; break
        if dev: break
        time.sleep(0.5)
    if dev is None: raise RuntimeError("0x%04x 不在总线上" % service_pid)
    target = legacy.open_target(dev)
    legacy.authenticate_target(target)
    return target, legacy.SenserChannel(target)

def auto_channel(legacy, initial_pid, service_pid, timeout):
    """自动判断：相机在 0x0d95 走两阶段；已在 0x0336 走 service-only。"""
    for d in legacy.iter_usb_devices():
        if d.idVendor == 0x054C and d.idProduct == service_pid:
            print("      检测到服务模式 0x%04x ⇒ service-only" % service_pid)
            return open_service_channel(legacy, service_pid, timeout)
    print("      检测到普通模式 ⇒ 两阶段认证")
    return legacy.run_authentication(initial_pid, service_pid, timeout, False)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("payload")
    ap.add_argument("--remote", default="/tmp/jiritsu/libJit.so")
    ap.add_argument("--read", default="/tmp/jiritsu/probe.txt")
    ap.add_argument("--wait", type=float, default=8.0, help="载荷执行等待秒数")
    ap.add_argument("--pid", default="0x0D95")
    ap.add_argument("--service-pid", default="0x0336")
    ap.add_argument("--timeout", type=float, default=40.0)
    ap.add_argument("--keep", action="store_true", help="不发送 STOP_SENSER")
    ap.add_argument("--no-read", action="store_true",
                    help="跳过读回（读回常失败，且失败会阻止 STOP_SENSER）")
    a = ap.parse_args()

    data = open(a.payload, "rb").read()
    sel, wr = frames(a.remote, data)
    print("payload %d B → %s ; frames select=%d write=%d" % (len(data), a.remote, len(sel), len(wr)))

    sys.path.insert(0, SRC)
    from a6700_unlock.usb import legacy

    t = None
    try:
        print("[1/4] 认证…")
        t, ch = auto_channel(legacy, int(a.pid,0), int(a.service_pid,0), a.timeout)
        print("      authenticated")

        print("[2/4] SET_FILE_PATH + SET_FILE2…")
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel); print("      select  code=%s" % c)
        c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=wr)
        st = int.from_bytes(b,"little") if len(b)==4 else None
        print("      write   code=%s status=%s" % (c, ("0x%08x"%st) if st is not None else "?"))

        print("[3/4] 等载荷执行 %.1fs…" % a.wait)
        time.sleep(a.wait)

        if a.no_read:
            print("[4/4] 跳过读回（--no-read）")
        else:
            print("[4/4] 读回 %s…" % a.read)
            try:
                s2, g2 = read_frame(a.read)
                c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=s2); print("      select  code=%s" % c)
                c,b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=g2)
                print("      read    code=%s len=%d" % (c, len(b)))
                if len(b) > 4:
                    print("--- content ---")
                    print(b[4:].decode("utf-8", errors="replace"))
                    print("--- end ---")
                else:
                    print("      raw: " + b.hex())
            except Exception as e:
                print("      读回失败（不影响载荷已写入）: %s" % type(e).__name__)

        if not a.keep:
            legacy.vendor_request_out(t.device, legacy.STOP_SENSER)
            print("STOP_SENSER 已发（相机会断开，需要等它自己回来）")
        else:
            print("--keep：保持服务模式（尽快做下一次操作，有超时）")
        return 0
    finally:
        try: legacy.close_target(t)
        except Exception: pass

if __name__ == "__main__":
    sys.exit(main())
