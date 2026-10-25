#!/usr/bin/env python3
"""read_remote.py — 从相机读一个文件（SET_FILE_PATH 后 GET_FILE）
用法: ./read_remote.py /tmp/jiritsu/probe.txt
"""
import argparse, os, struct, sys
CAT, CMD_SEL, CMD_GET = 0x1001, 0x0F, 0x11
SRC = os.environ.get("A6700_SRC", "/tmp/r3/应用/src")


def open_service_channel(legacy, service_pid, timeout):
    """相机已在服务模式（0x0336）时，直接打开+单阶段认证，不走两阶段流程。"""
    import time, usb.core
    deadline = time.time() + timeout
    dev = None
    while time.time() < deadline:
        for d in legacy.iter_usb_devices():
            if d.idVendor == 0x054C and d.idProduct == service_pid:
                dev = d; break
        if dev: break
        time.sleep(0.5)
    if dev is None:
        raise RuntimeError("0x%04x 不在总线上" % service_pid)
    target = legacy.open_target(dev)
    legacy.authenticate_target(target)
    return target, legacy.SenserChannel(target)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--pid", default="0x0D95")
    ap.add_argument("--service-pid", default="0x0336")
    ap.add_argument("--service-only", action="store_true",
                    help="相机已在服务模式(0x0336)，跳过两阶段流程")
    ap.add_argument("--no-stop", action="store_true",
                    help="不发送 STOP_SENSER（保持服务模式，避免相机断开 USB）")
    ap.add_argument("--timeout", type=float, default=30.0)
    a = ap.parse_args()
    sys.path.insert(0, SRC)
    from a6700_unlock.usb import legacy

    sel = struct.pack("<HH", CAT, CMD_SEL) + b"\x00" + a.path.encode() + b"\x00"
    get = struct.pack("<HH", CAT, CMD_GET) + bytes([0x00, 0xFF, 0x00])

    t = None
    try:
        if a.service_only:
            t, ch = open_service_channel(legacy, int(a.service_pid,0), a.timeout)
        else:
            t, ch = legacy.run_authentication(int(a.pid,0), int(a.service_pid,0), a.timeout, True)
        print("authenticated")
        c, b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=sel)
        print("SELECT %s -> code=%s body=%s" % (a.path, c, b.hex()))
        c, b = ch.send(legacy.PFUNC_ADJUST_CONTROL, body=get)
        print("GET_FILE -> code=%s len=%d" % (c, len(b)))
        print("raw: " + b[:256].hex())
        if len(b) > 4:
            print("--- content ---")
            print(b[4:].decode("utf-8", errors="replace"))
            print("--- end ---")
        if not getattr(a, "no_stop", False):
            legacy.vendor_request_out(t.device, legacy.STOP_SENSER)
        else:
            print("no-stop: 保持服务模式")
        return 0
    finally:
        try: legacy.close_target(t)
        except Exception: pass

if __name__ == "__main__":
    sys.exit(main())
