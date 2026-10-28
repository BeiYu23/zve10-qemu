#!/usr/bin/env python3
"""读回启动黑匣子（nflasha16 原始扇区，不需要 UI、不需要内核配合）。

黑匣子布局（都由 bb.so / blackbox.ko 写入）：
  偏移 0      .. 8 KiB   bb.so 的 8 个实例槽（每槽 1 KiB，槽号 = pid%8）
  偏移 8 KiB  .. ~21 KiB blackbox.ko 的心跳记录（200 × 64 B）+ 头/尾标记

nflasha16 起始 LBA = 0x40400000 / 512 = 0x202000，64 个扇区（32 KiB）足够。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, '/root/fw/oss/fastboot/host')
from bootstrap import Device, wait_for_device     # noqa: E402
from read_updater_log import upload_blocks        # noqa: E402

LBA = 0x40400000 // 512
NSEC = 64


def main():
    dev = wait_for_device(60)
    if dev is None:
        raise SystemExit('✗ 没有 fastboot 设备（Pico 没插 / 相机没进 bootloader）')
    d = Device(dev)
    try:
        d.require('oem emmc-init')
        print('读取 nflasha16 @ LBA 0x%X，%d 个扇区 …' % (LBA, NSEC))
        img = upload_blocks(d, LBA, NSEC)
        print('拿到 %d B\n' % len(img))

        txt = img.split(b'\x00', 1)[0]        # 只取到第一个 NUL 为止
        if b'BB1' not in img and b'BBMOD1' not in img:
            print('⚠ 没找到黑匣子标记 —— 说明 bb.so 从未运行（或没写成功）')
            print('  前 64 字节: %s' % img[:64].hex())
            return 1

        print('=' * 68)
        print('bb.so 用户态槽位')
        print('=' * 68)
        for slot in range(8):
            blk = img[slot * 1024:(slot + 1) * 1024]
            if b'BB1' in blk:
                t = blk.split(b'\x00', 1)[0].decode('latin1', 'replace')
                print(t.rstrip())
                print('-' * 40)

        print('=' * 68)
        print('blackbox.ko 心跳')
        print('=' * 68)
        base = 8192
        recs = []
        for i in range(200):
            r = img[base + i * 64: base + (i + 1) * 64]
            if r[:1] == b'T':
                recs.append(r.split(b'\x00', 1)[0].decode('latin1', 'replace').strip())
        if recs:
            print('  共 %d 条心跳' % len(recs))
            print('  首: %s' % recs[0])
            print('  尾: %s' % recs[-1])
            # im.elf 存活情况
            vals = [r for r in recs if 'imelf=' in r]
            if vals:
                cnts = [int(v.split('imelf=')[1]) for v in vals]
                print('  im.elf 计数范围: %d ~ %d' % (min(cnts), max(cnts)))
        else:
            print('  （无心跳 ⇒ blackbox.ko 没加载，或还没跑起来）')
        hdr = img[base + 200 * 64: base + 200 * 64 + 64].split(b'\x00', 1)[0]
        print('  头: %r' % hdr)
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
