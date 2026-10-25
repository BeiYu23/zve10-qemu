#!/usr/bin/env python3
"""send_payload.py — 把 libJit 载荷送到 ZV-E10 并触发加载

设计：
  · 认证复用 a6700-unlock 的已实测实现（PYTHONPATH 引用，不复制其代码）
  · 文件发送用 a7c2-re 记录的 ZV-E10/A7C2 帧约定：
        SET_FILE_PATH (0x1001,0x0f) + b"\\0" + path + b"\\0"
        SET_FILE2     (0x1001,0x10) + b"\\x00\\xff\\x00" + <整个文件>
        GET_FILE      (0x1001,0x11) + b"\\x00\\xff\\x00"    ← 读回结果
    file_id=0xFF 表示"使用已选定的路径"，整文件放一帧（上限 0x500000 = 5 MB）

用法：
  export A6700_SRC=/tmp/r3/应用/src        # a6700-unlock 的 src 目录
  ./send_payload.py libJit.so --pid 0x0D95 --path /tmp/jiritsu/libJit.so
  ./send_payload.py libJit.so --pid 0x0D95 --read-result      # 同时读回 tmmc.jit
"""
import argparse
import os
import struct
import sys

CATEGORY = 0x1001
CMD_SET_PATH = 0x0F
CMD_SET_FILE2 = 0x10
CMD_GET_FILE = 0x11
FILE_ID_SELECTED = 0xFF
ARTIFACT_MAX_BYTES = 0x500000          # 固件单文件上限（a7c2 记录）

DEFAULT_SRC = os.environ.get("A6700_SRC", "/tmp/r3/应用/src")


def build_select(path: str) -> bytes:
    p = path.encode("ascii")
    if b"\x00" in p:
        raise ValueError("path must not contain NUL")
    return struct.pack("<HH", CATEGORY, CMD_SET_PATH) + b"\x00" + p + b"\x00"


def build_write(data: bytes) -> bytes:
    if len(data) > ARTIFACT_MAX_BYTES:
        raise ValueError("payload %d bytes exceeds firmware limit %d"
                         % (len(data), ARTIFACT_MAX_BYTES))
    return struct.pack("<HH", CATEGORY, CMD_SET_FILE2) + bytes([0x00, FILE_ID_SELECTED, 0x00]) + data


def build_read() -> bytes:
    return struct.pack("<HH", CATEGORY, CMD_GET_FILE) + bytes([0x00, FILE_ID_SELECTED, 0x00])


def load_a6700(src: str):
    if src not in sys.path:
        sys.path.insert(0, src)
    from a6700_unlock.usb import legacy           # noqa: E402
    return legacy


def main() -> int:
    ap = argparse.ArgumentParser(description="Send a libJit payload to a Sony camera")
    ap.add_argument("payload", help="local .so to upload")
    ap.add_argument("--pid", default="0x0D95", help="initial (mass storage) PID, default 0x0D95")
    ap.add_argument("--service-pid", default="0x0336", help="Senser service PID, default 0x0336")
    ap.add_argument("--path", default="/tmp/jiritsu/libJit.so", help="remote target path")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--read-result", action="store_true", help="GET_FILE after upload")
    ap.add_argument("--dry-run", action="store_true", help="build frames, do not touch USB")
    args = ap.parse_args()

    data = open(args.payload, "rb").read()
    if not data.startswith(b"\x7fELF"):
        print("error: payload is not an ELF object", file=sys.stderr)
        return 2
    print("payload: %d bytes  path=%s" % (len(data), args.path))

    sel = build_select(args.path)
    wr = build_write(data)
    rd = build_read()
    print("frames: select=%d write=%d read=%d bytes" % (len(sel), len(wr), len(rd)))
    if args.dry_run:
        print("dry-run: frames built, USB untouched")
        return 0

    legacy = load_a6700(DEFAULT_SRC)
    target = None
    try:
        print("authenticating (initial pid %s → service pid %s)…" % (args.pid, args.service_pid))
        target, channel = legacy.run_authentication(
            int(args.pid, 0), int(args.service_pid, 0), args.timeout,
            read_product_info=True,
        )
        print("authenticated")

        code, body = channel.send(legacy.PFUNC_ADJUST_CONTROL, body=sel)
        print("SET_FILE_PATH  code=%s body=%s" % (code, body.hex()))

        code, body = channel.send(legacy.PFUNC_ADJUST_CONTROL, body=wr)
        st = int.from_bytes(body, "little") if len(body) == 4 else None
        print("SET_FILE2       code=%s body=%s%s" % (
            code, body.hex(),
            "" if st is None else ("  status=0x%08x%s" % (st, " (OK)" if st == 0 else " (error)"))))

        if args.read_result:
            code, body = channel.send(legacy.PFUNC_ADJUST_CONTROL, body=rd)
            print("GET_FILE        code=%s len=%d" % (code, len(body)))
            if len(body) >= 4 and body[:4] == b"\x00" * 4:
                print("--- payload result ---")
                print(body[4:].decode("utf-8", errors="replace"))
                print("--- end ---")
            else:
                print("raw: " + body[:512].hex())

        legacy.vendor_request_out(target.device, legacy.STOP_SENSER)
        print("STOP_SENSER sent")
        return 0 if (st == 0 if st is not None else True) else 1
    finally:
        try:
            legacy.close_target(target)
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
