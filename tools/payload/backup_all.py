#!/usr/bin/env python3
"""backup_all.py — 通过 TCP 7777 备份 ZV-E10 全部分区

⚠️ 安全约束（见 docs/SAFETY_RULES.md 规矩 0）：
   本脚本【绝不】写入或覆盖任何 .so，只使用已存在的 agent_v14 的
   RUN / PULL 通道。所有操作都是只读分区 + 写 /log 临时块。

原理：
   agent 的 PULL 有 8 MB 固定缓冲上限且不支持偏移，
   所以按 8 MB 分块：dd(skip/count) → PULL → 本地拼接。

临时块位置：
   默认 /log/_bchunk.img（nflasha11）
   ★ 备份 nflasha11 自己时改用 /setting/_bchunk.img（避免读自己时写入自己）

用法:
   ./backup_all.py --out /root/fw/zve10_backup --ip 10.42.0.158
   ./backup_all.py --out DIR --only 11,13          # 只备份指定分区
   ./backup_all.py --out DIR --dry-run             # 只列清单
"""
import argparse, hashlib, json, os, re, socket, sys, time

DEFAULT_IP = "10.42.0.158"
PORT = 7777
CHUNK = 8 * 1024 * 1024          # PULL 上限 8 MB
BS = 64 * 1024                   # dd 块大小
SKIP_BLOCKS = CHUNK // BS        # 每块 128 个 64k


class Cam:
    def __init__(self, ip, port=PORT, timeout=60):
        self.s = socket.create_connection((ip, port), timeout=timeout)
        self.s.settimeout(timeout)

    def _line(self):
        b = b""
        while not b.endswith(b"\n"):
            c = self.s.recv(1)
            if not c:
                raise EOFError("连接中断")
            b += c
        return b.decode(errors="replace").strip()

    def _exact(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(min(65536, n - len(b)))
            if not c:
                raise EOFError("传输中断 (%d/%d)" % (len(b), n))
            b += c
        return b

    def ping(self):
        self.s.sendall(b"PING\n")
        return self._line()

    def run(self, cmd):
        d = cmd.encode()
        self.s.sendall(b"RUN %d\n" % len(d) + d)
        h = self._line()
        if not h.startswith("OUT "):
            raise RuntimeError("RUN 失败: %s" % h)
        return self._exact(int(h[4:]))

    def pull(self, path):
        p = path.encode()
        self.s.sendall(b"PULL %d\n" % len(p) + p)
        h = self._line()
        if not h.startswith("DATA "):
            raise RuntimeError("PULL 失败: %s (%s)" % (h, path))
        return self._exact(int(h[5:]))

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def parse_partitions(text):
    """解析 /proc/partitions，返回 [(name, blocks)]"""
    out = []
    for line in text.splitlines():
        f = line.split()
        if len(f) != 4:
            continue
        if not f[3].startswith("nflasha"):
            continue
        out.append((f[3], int(f[2])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default=DEFAULT_IP)
    ap.add_argument("--out", default="/root/fw/zve10_backup")
    ap.add_argument("--only", help="逗号分隔的分区后缀，如 11,13")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--resume", action="store_true", help="跳过已存在且大小正确的镜像")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    c = Cam(a.ip)
    try:
        print("PING →", c.ping())
        parts = parse_partitions(c.run("cat /proc/partitions").decode(errors="replace"))
        # 去掉整盘条目
        parts = [(n, b) for n, b in parts if re.fullmatch(r"nflasha[0-9AB]+", n)]
        print("发现 %d 个分区:" % len(parts))
        for n, b in parts:
            print("   %-12s %10d blocks  %7.1f MB" % (n, b, b * 1024 / 2**20))

        if a.only:
            want = set(a.only.split(","))
            parts = [(n, b) for n, b in parts
                     if n.replace("nflasha", "") in want]
            print("筛选后 %d 个" % len(parts))

        if a.dry_run:
            print("dry-run: 不做任何写入")
            return 0

        ok, fail = [], []
        for name, blocks in parts:
            size = blocks * 1024
            dest = os.path.join(a.out, name + ".img")
            if a.resume and os.path.exists(dest) and os.path.getsize(dest) == size:
                print("[skip] %s 已存在且大小正确" % name)
                ok.append((name, size, hashlib.sha256(open(dest, "rb").read()).hexdigest()))
                continue

            # 临时块位置：备份 nflasha11(/log) 时改用 /setting，避免读自己时写自己
            tmp = "/setting/_bchunk.img" if name == "nflasha11" else "/log/_bchunk.img"
            print("[%s] %.1f MB → %s (temp %s)" % (name, size / 2**20, dest, tmp))
            t0 = time.time()
            written = 0
            h = hashlib.sha256()
            try:
                with open(dest, "wb") as f:
                    skip = 0
                    while written < size:
                        remain = size - written
                        cnt = min(SKIP_BLOCKS, (remain + BS - 1) // BS)
                        cmd = ("dd if=/dev/%s of=%s bs=%d skip=%d count=%d 2>/dev/null; "
                               "ls -l %s" % (name, tmp, BS, skip, cnt, tmp))
                        c.run(cmd)
                        data = c.pull(tmp)
                        if not data:
                            break
                        f.write(data)
                        h.update(data)
                        written += len(data)
                        skip += cnt
                        pct = written * 100.0 / size
                        sys.stdout.write("\r    %5.1f%%  %6.1f/%6.1f MB  %4.1f MB/s"
                                         % (pct, written / 2**20, size / 2**20,
                                            written / 2**20 / max(0.001, time.time() - t0)))
                        sys.stdout.flush()
                        if len(data) < min(CHUNK, remain):
                            break
                    print()
                c.run("/bin/busybox rm -f %s" % tmp)
                if written != size:
                    print("    ⚠ 大小不符: %d != %d" % (written, size))
                    fail.append((name, "size %d != %d" % (written, size)))
                    continue
                dg = h.hexdigest()
                print("    SHA256 %s  (%d B)" % (dg, written))
                ok.append((name, size, dg))
            except Exception as e:
                print("    ✗ %s: %s" % (type(e).__name__, e))
                fail.append((name, str(e)))

        manifest = {
            "device": "ZV-E10",
            "ip": a.ip,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "method": "TCP7777 chunked dd+pull (8MB chunks)",
            "partitions": [{"name": n, "bytes": s, "sha256": h} for n, s, h in ok],
            "failed": [{"name": n, "error": e} for n, e in fail],
        }
        mp = os.path.join(a.out, "manifest.json")
        json.dump(manifest, open(mp, "w"), indent=2, ensure_ascii=False)
        print("\n==== 完成 ====")
        print("成功 %d / 失败 %d" % (len(ok), len(fail)))
        print("清单: %s" % mp)
        for n, e in fail:
            print("  失败 %s: %s" % (n, e))
        return 0 if not fail else 1
    finally:
        c.close()


if __name__ == "__main__":
    sys.exit(main())
