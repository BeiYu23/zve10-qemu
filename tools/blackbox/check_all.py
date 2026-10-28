#!/usr/bin/env python3
"""整机分区体检 —— 不依赖 UI、不依赖 agent，只靠 bootloader 读扇区。

每条判据都来自我们手上的工厂镜像（/root/fw/zve10_backup/*.img）。

用法：Pico 插好、相机进 bootloader →  python3 check_all.py
"""
import hashlib
import struct
import sys

sys.path.insert(0, '/root/fw/oss/fastboot/host')
from bootstrap import Device, wait_for_device     # noqa: E402
from read_updater_log import upload_blocks        # noqa: E402

def pb(part_start, off=0):
    return (part_start + off) // 512

def fat_ok(b):
    return b[0x36:0x3B] in (b'FAT12', b'FAT16', b'FAT32') and b[0x1FE:0x200] == b'\x55\xaa'

CHECKS = [
    # name, part_start, offset, 说明, 校验函数, 期望描述
    ('nflasha2  /setting', 0x00800000, 0,
     'FAT 引导扇区', lambda b: fat_ok(b), 'FAT12 + 55AA'),
    ('nflasha3  /system',  0x01C00000, 0,
     'FAT 引导扇区', lambda b: fat_ok(b), 'FAT12 + 55AA'),
    ('nflasha10 /tmp',     0x0AC00000, 0,
     'FAT 引导扇区', lambda b: fat_ok(b), 'FAT12 + 55AA'),
    ('nflasha11 /log',     0x0B800000, 0,
     'FAT 引导扇区', lambda b: fat_ok(b), 'FAT16 + 55AA'),
]

def main():
    dev = wait_for_device(60)
    if dev is None:
        raise SystemExit('✗ 没有 fastboot 设备（Pico 没插 / 相机没进 bootloader）')
    d = Device(dev)
    bad = 0
    try:
        d.require('oem emmc-init')

        print('=' * 74)
        print('① 各分区引导扇区')
        print('=' * 74)
        for name, start, off, what, fn, exp in CHECKS:
            blk = upload_blocks(d, pb(start, off), 1)
            ok = fn(blk)
            if not ok:
                bad += 1
            print('  %-20s %s  期望 %s' % (name, '✓' if ok else '✗ 异常', exp))
            if not ok:
                print('       首 16 字节: %s' % blk[:16].hex())

        print()
        print('=' * 74)
        print('② /usr 文件系统（决定性判据）')
        print('=' * 74)
        sb = upload_blocks(d, pb(0x2D800000, 1024), 1)
        magic = struct.unpack_from('<H', sb, 56)[0]
        blocks = struct.unpack_from('<I', sb, 4)[0]
        free = struct.unpack_from('<I', sb, 12)[0]
        uuid = sb[104:120].hex()
        print('  magic = 0x%04X   blocks = %d   free = %d' % (magic, blocks, free))
        print('  uuid  = %s-%s-%s-%s-%s' % (uuid[0:8], uuid[8:12], uuid[12:16],
                                            uuid[16:20], uuid[20:32]))
        if magic != 0xEF53:
            bad += 1
            print('  ✗ 不是 ext2 —— /usr 文件系统已损坏，必然没有 UI')
            print('    ⇒ 下一步：重刷 /usr（先 --plan-only 演练）')
        elif free == 10999:
            print('  ⇒ 工厂原版 fs —— 我们的 v9 镜像【没有写进去】')
            print('    ⇒ 下一步：查刷写链路本身（不是 /usr 内容的问题）')
        elif free == 10223:
            print('  ⇒ 这就是我们的 v9（free=10223 对得上）')
            print('    ⇒ 刷写生效了；问题在 app 层 ⇒ 部署黑匣子拿运行时数据')
        else:
            print('  ⚠ free 既不是 10999 也不是 10223')
            print('    ⇒ fs 被别的东西改过（可能是 im.elf 的 e2fsck -y，或半截写入）')
            print('    ⇒ 建议：重刷 /usr 后再查一次')

        print()
        print('=' * 74)
        print('③ nflasha16（应为全零，黑匣子预留区）')
        print('=' * 74)
        b16 = upload_blocks(d, pb(0x40400000, 0), 1)
        nz = sum(1 for x in b16 if x)
        print('  首扇区非零字节 = %d  %s' % (nz, '✓ 干净' if nz == 0 else '已被写过'))

        print()
        print('=' * 74)
        print('判决：%s' % ('全部正常' if bad == 0 else '%d 项异常（见上）' % bad))
        print('=' * 74)
        return 0
    finally:
        d.close()

if __name__ == '__main__':
    sys.exit(main())
