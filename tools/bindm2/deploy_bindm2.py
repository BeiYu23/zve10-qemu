#!/usr/bin/env python3
"""把 M2 的 /usr 镜像分块推到相机 /log/m2usr.img，并设置 preload。

用法:
  python3 deploy_bindm2.py --image /mnt/build/nflasha15_v9.ext2
  python3 deploy_bindm2.py --status      # 只看当前状态
  python3 deploy_bindm2.py --rollback    # 恢复 preload、删掉镜像

要点:
  * agent 的 PUSH 单次上限约 4 MB，且目标已存在会 ERR create ⇒ 每块先 rm 再 push，
    最后用 busybox cat a b >> out 逐块拼接
  * /log 是 500 MB vfat，237 MB 镜像放得下
  * 全程只写 /log 与 /setting，重启即恢复
"""
import argparse, os, socket, sys, time

IP, PORT = "10.42.0.158", 7777
CHUNK = 1 * 1024 * 1024          # 1 MB：相机 MemFree 仅 ~3 MB，agent PUSH 要 malloc(n+1)

def conn(ip=IP, port=PORT, timeout=30):
    s = socket.create_connection((ip, port), timeout=timeout)
    s.settimeout(300)
    return s
def read_line(s):
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1)
        if not c: raise EOFError("连接中断")
        b += c
    return b
def read_exact(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError("连接中断")
        b += c
    return b
def run(s, cmd):
    d = cmd.encode()
    s.sendall(b"RUN %d\n" % len(d) + d)
    h = read_line(s).decode(errors="replace").strip()
    if not h.startswith("OUT "): return "ERR:" + h
    return read_exact(s, int(h[4:])).decode(errors="replace")
def push(s, path, data):
    blob = path.encode() + b"\0" + data
    s.sendall(b"PUSH %d\n" % len(blob) + blob)
    return read_line(s).decode(errors="replace").strip()

def status(s):
    print("=== 当前状态 ===")
    print("dmode   :", run(s, "cat /setting/mode/dmode 2>/dev/null").strip())
    print("preload :", run(s, "cat /setting/mode/preload 2>/dev/null").strip())
    print("镜像    :", run(s, "ls -l /log/m2usr.img 2>/dev/null || echo '(不存在)'").strip())
    print("工具有无:", run(s, "ls -l /log/bindm2.so 2>/dev/null || echo '(不存在)'").strip())
    print("日志    :", run(s, "cat /log/bindm2.log 2>/dev/null | tail -20 || echo '(无)'").strip())
    print("挂载    :", run(s, "cat /proc/mounts | grep -E 'm2root|/usr' || true").strip())

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--rollback", action="store_true")
    ap.add_argument("--ip", default=IP)
    a = ap.parse_args()
    s = conn(a.ip)
    try:
        if a.status:
            status(s); return 0
        if a.rollback:
            run(s, "printf '3e' > /setting/mode/dmode; "
                   "printf '/usr/tool/libefence.so:/log/agent_v14.so' > /setting/mode/preload; sync")
            run(s, "/bin/busybox rm -f /log/m2usr.img /log/m2usr.part.* /log/bindm2.log")
            print("已回滚 preload，已删除镜像")
            status(s); return 0
        if not a.image:
            ap.error("需要 --image")
        img = open(a.image, "rb").read()
        total = len(img)
        nch = (total + CHUNK - 1) // CHUNK
        print("镜像 %d B (%.1f MB)，分 %d 块上传" % (total, total/1048576, nch))
        # 空间检查
        free = run(s, "/bin/busybox df -k /log 2>/dev/null | tail -1 || echo ''").strip()
        print("  /log 余量:", free or "(df 不可用，先试)")
        # 先推工具：bindm2.so（做 loop+bind）+ libIMDB_m2.so（顶替 im.elf 的 DT_NEEDED）
        here = os.path.dirname(os.path.abspath(__file__))
        for name in ("bindm2.so", "libIMDB_m2.so"):
            src = os.path.join(here, name)
            if not os.path.exists(src):
                print("  ✗ 缺少 %s（先运行 make_tools.sh）" % name); return 1
            run(s, "/bin/busybox rm -f /log/" + name)
            print("  推 %-16s" % name, push(s, "/log/" + name, open(src,"rb").read()))
        # 分块上传 + 立即追加（每条命令都很短，避免 1022 字符上限被静默丢弃）
        # 断点续传：用 /log/m2usr.size 记录已提交字节数；不一致就重来
        cur = 0
        sz_out = run(s, "/bin/busybox ls -l /log/m2usr.img 2>/dev/null | /bin/busybox awk '{print $5}'").strip()
        rec_out = run(s, "cat /log/m2usr.size 2>/dev/null").strip()
        try: cur = int(sz_out) if sz_out.isdigit() else 0
        except Exception: cur = 0
        try: rec = int(rec_out) if rec_out.isdigit() else -1
        except Exception: rec = -1
        if cur > 0 and cur == rec and cur <= total:
            print("  ★ 断点续传：已提交 %d B (%.1f MB)，从第 %d 块继续" % (cur, cur/1048576, cur//CHUNK + 1))
            start_chunk = cur // CHUNK
        else:
            if cur > 0: print("  已存在的镜像与记录不一致（%d vs %d）⇒ 重新上传" % (cur, rec))
            run(s, "/bin/busybox rm -f /log/m2usr.img /log/m2usr.part /log/m2usr.size")
            start_chunk = 0
        t0 = time.time()
        for i in range(start_chunk, nch):
            part = img[i*CHUNK:(i+1)*CHUNK]
            p = "/log/m2usr.part"
            ok = False
            for attempt in range(3):
                run(s, "/bin/busybox rm -f " + p)
                r = push(s, p, part)
                if r == "OK": ok = True; break
                print("    块 %d 第 %d 次失败: %s，重试" % (i+1, attempt+1, r), flush=True)
                time.sleep(0.5)
            if not ok:
                print("  ✗ 上传失败（块 %d，已重试 3 次）" % (i+1)); return 1
            # 追加（短命令）
            r2 = run(s, "/bin/busybox cat %s >> /log/m2usr.img && /bin/busybox rm -f %s && echo %d > /log/m2usr.size; echo ok"
                     % (p, p, min((i+1)*CHUNK, total))).strip()
            if "ok" not in r2:
                print("  ✗ 追加失败（块 %d）: %r" % (i+1, r2[:120])); return 1
            el = time.time() - t0
            print("  块 %2d/%d  %6.2f MB  累计 %.1f MB  %.1f MB/s" %
                  (i+1, nch, len(part)/1048576, (i+1)*len(part)/1048576,
                   (i+1)*len(part)/1048576/el if el else 0), flush=True)
        run(s, "/bin/busybox rm -f /log/m2usr.size")
        sz = run(s, "/bin/busybox ls -l /log/m2usr.img 2>/dev/null | /bin/busybox awk '{print $5}'").strip()
        got = int(sz) if sz.isdigit() else -1
        print("  最终文件: %s B  (期望 %d B)  %s"
              % (sz or "?", total, "✓ 一致" if got == total else "✗ 不一致！"))
        if got != total:
            print("  ⚠ 大小不符 ⇒ 上传不完整，请重新运行本脚本（会自动重传）")
        # 校验：读回文件头 4 字节应等于镜像头（ext2 引导区通常全零，故用 1024 偏移的超块 magic）
        chk = run(s, "/bin/busybox dd if=/log/m2usr.img bs=1 skip=1080 count=2 2>/dev/null | /bin/busybox md5sum").strip()
        print("  超块 magic 处 md5:", chk, "  期望 = 53ef 的 md5:",
              __import__("hashlib").md5(bytes.fromhex("53ef")).hexdigest())
        # 设置 preload
        PRE = "/usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so"
        r = run(s, "printf '3e' > /setting/mode/dmode; "
                   "printf '%s' > /setting/mode/preload; sync; "
                   "echo -n 'preload='; cat /setting/mode/preload; echo" % PRE)
        print("  preload 已设:", r.strip())
        print("\n⇒ 现在重启相机。重启后 im.elf 装载时会先 loop 挂载镜像并 bind 到 /usr/lib,/usr/share，")
        print("   然后才 dlopen libIMDB ⇒ 应该看到 M2 新菜单。")
        print("   若仍无 UI：重启即可完全恢复（不写任何 flash）")
        return 0
    finally:
        try: s.close()
        except Exception: pass

if __name__ == "__main__":
    sys.exit(main())
