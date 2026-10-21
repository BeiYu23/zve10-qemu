#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""flash_aux_fix_nodep.py —— 零依赖版灌入脚本（POSIX / Linux / macOS）

不需要 pyserial：只用标准库 termios + select 直接操作串口。
Windows 上没有 termios，请改用 flash_aux_fix.py（需 pip install pyserial）。

用法：
  python3 flash_aux_fix_nodep.py --port /dev/ttyACM0 \
      --payload payload/payload_fix.bin --retry 10
"""
import argparse
import os
import select
import struct
import sys
import time
import zlib

try:
    import termios
except ImportError:
    sys.exit("这个版本只支持 POSIX（Linux/macOS）；Windows 请用 flash_aux_fix.py + pyserial")

# ---------- BootROM 串行启动协议常量 ----------
SET_UNLOCK = 0x0000
MEM_WRITE = 0x0001
END_MARKER = 0x0123
SET_ENTRY = 0xFFFF

LOAD_ADDR = 0xFE020000
MAX_RECORD_PAYLOAD = 512
UNLOCK_CODE = 0xdeadbeef
UNLOCK_STACK_ADDR = 0xFE02F398

# ---------- RP2040 桥接固件协议 ----------
CMD_PING = 0x01
CMD_RESET = 0x02
CMD_CARRIER = 0x03
CMD_BLOB = 0x04
CMD_LINK = 0x07   # 设置串口启动波特率（sweep 固件）

EVT_ACK = 0x81
EVT_ERR = 0x82
EVT_UART = 0x83
RP_VERSION = b"uart_boot 1"


class LinkError(Exception):
    pass


class SerialLink:
    def __init__(self, path, baud=115200, timeout=5.0):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.timeout = timeout
        attrs = termios.tcgetattr(self.fd)
        iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
        # 全 raw：8N1，无流控，无回显
        iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
                   termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON |
                   termios.IXOFF | termios.IXANY)
        oflag &= ~termios.OPOST
        lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                   termios.ISIG | termios.IEXTEN)
        cflag &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL
        speed = getattr(termios, "B%d" % baud, termios.B115200)
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW,
                          [iflag, oflag, cflag, lflag, speed, speed, cc])

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass

    def write(self, data):
        total = 0
        deadline = time.time() + self.timeout
        while total < len(data):
            try:
                total += os.write(self.fd, data[total:])
            except BlockingIOError:
                if time.time() > deadline:
                    raise LinkError("串口写超时")
                time.sleep(0.002)

    def read_exact(self, n):
        out = b""
        deadline = time.time() + self.timeout
        while len(out) < n:
            remain = deadline - time.time()
            if remain <= 0:
                return None
            r, _, _ = select.select([self.fd], [], [], min(remain, 0.2))
            if not r:
                continue
            try:
                chunk = os.read(self.fd, n - len(out))
            except BlockingIOError:
                continue
            if not chunk:
                continue
            out += chunk
        return out

    def send(self, cmd, data=b""):
        self.write(bytes([cmd]) + struct.pack("<H", len(data)) + data)

    def recv(self):
        hdr = self.read_exact(3)
        if hdr is None:
            return None
        (n,) = struct.unpack("<H", hdr[1:3])
        data = self.read_exact(n) if n else b""
        if data is None:
            return None
        return hdr[0], data

    def ack(self):
        while True:
            pkt = self.recv()
            if pkt is None:
                raise LinkError("等 RP2040 应答超时（查端口/固件是否已刷）")
            evt, data = pkt
            if evt == EVT_ACK:
                return data
            if evt == EVT_ERR:
                raise LinkError("RP2040 报错: %r" % (data,))
            if evt == EVT_UART:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()

    def ping(self):
        self.send(CMD_PING)
        ver = self.ack()
        if ver != RP_VERSION:
            raise LinkError("固件版本不匹配: %r（应为 %r）" % (ver, RP_VERSION))

    def carrier(self, on):
        self.send(CMD_CARRIER, bytes([1 if on else 0]))
        self.ack()

    def set_link_baud(self, baud):
        self.send(CMD_LINK, struct.pack("<I", baud))
        self.ack()

    def blob(self, rec):
        self.send(CMD_BLOB, rec)
        self.ack()


def record(rtype, value, payload=b""):
    body = struct.pack("<IHI", 10 + len(payload), rtype, value) + payload
    return body + struct.pack("<I", zlib.crc32(body))


def build_records(payload, addr, entry, unlock_code, unlock_stack):
    recs = []
    recs.append(record(END_MARKER, 0))
    off = 0
    while off < len(payload):
        chunk = payload[off:off + MAX_RECORD_PAYLOAD]
        recs.append(record(MEM_WRITE, addr + off, chunk))
        off += len(chunk)
    if unlock_code is None:
        unlock_code = UNLOCK_CODE
        val = unlock_code.to_bytes(4, "little")
        val += (zlib.crc32(val) ^ 0xFFFFFFFF).to_bytes(4, "little")
        recs.append(record(MEM_WRITE, unlock_stack, val))
    recs.append(record(SET_ENTRY, entry))
    recs.append(record(SET_UNLOCK, unlock_code))
    recs.append(record(END_MARKER, 0))
    return recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--rp-baud", type=int, default=115200)
    ap.add_argument("--payload", required=True)
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=LOAD_ADDR)
    ap.add_argument("--entry", type=lambda s: int(s, 0), default=None)
    ap.add_argument("--unlock-code", type=lambda s: int(s, 0), default=None)
    ap.add_argument("--unlock-stack", type=lambda s: int(s, 0), default=UNLOCK_STACK_ADDR)
    ap.add_argument("--no-reset", action="store_true")
    ap.add_argument("--retry", type=int, default=1)
    ap.add_argument("--link-baud", type=int, default=0,
                    help="串口启动波特率（需 sweep 固件；0=用固件默认 48000）")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    payload = open(args.payload, "rb").read()
    recs = build_records(payload, args.addr, args.entry or args.addr,
                         args.unlock_code, args.unlock_stack)
    stream = sum(len(r) for r in recs)

    print("payload : %s (%d B)" % (args.payload, len(payload)))
    print("records : %d 条 / %d B" % (len(recs), stream))

    if args.dry_run:
        for i, r in enumerate(recs):
            ln, ty, val = struct.unpack_from("<IHI", r, 0)
            print("  %2d  0x%04X  0x%08X  %4d B" % (i, ty, val, ln - 10))
        print("(dry-run：未连接设备)")
        return 0

    link = SerialLink(args.port, args.rp_baud)
    try:
        print("[1/4] 连接 %s ..." % args.port, end="", flush=True)
        link.ping()
        print(" OK 固件版本匹配")
        if args.link_baud:
            print(" 设置链路波特率 %d ..." % args.link_baud, end="", flush=True)
            link.set_link_baud(args.link_baud)
            print(" OK")

        for rnd in range(max(1, args.retry)):
            if args.retry > 1:
                print("===== 第 %d/%d 轮 =====" % (rnd + 1, args.retry))
            if not args.no_reset:
                link.send(CMD_RESET)
                link.ack()

            link.carrier(True)
            t0 = time.time()
            for i, rec in enumerate(recs):
                link.blob(rec)
                if i % 5 == 4 or i == len(recs) - 1:
                    print("  [%2d/%2d] %.1fs" % (i + 1, len(recs), time.time() - t0))
            time.sleep(0.3)
            try:
                link.carrier(False)
            except LinkError:
                pass

        print()
        print("★ 发送完成")
        print("  ① 等 2~3 秒（payload 在跑：mmc init → 扫 32 块 → 改 1 字节 → 读回校验）")
        print("  ② 断电重启相机（拔电池再装回）")
        print("  ③ 能正常开机 = 修好；仍黑屏/循环 = 换 --unlock-code 0x2CF25621 再试")
        return 0
    except LinkError as e:
        print("\n!! 失败: %s" % e)
        return 2
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
