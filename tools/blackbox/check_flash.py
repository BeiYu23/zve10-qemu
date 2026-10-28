#!/usr/bin/env python3
"""不用开机、不用 UI —— 直接读 /usr 里三个文件的扇区，判断刷写到底生效没有。

原理：这三个文件在我们镜像里和工厂里【首扇区内容不同】，md5 一比对就知道
相机盘上实际是哪个版本。

  文件                 LBA         我们的(v9)         工厂(E10)
  lib/libInfraSKD.so   0x19D402    2b05424ef1f9f5c2   2e7e9535a194eaa9
  share/app/global.xdb 0x1B24A2    1194d334d650657c   373e93678ebaf1de
  lib/libObj.so        0x1A03EA    04b8e1af323b4544   5343f6ca3e8c76e7

用法：插 Pico → 相机进 bootloader（或先跑 inject_payload 注入 stage2）→
      python3 check_flash.py
"""
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, '/root/fw/oss/fastboot/host')
from bootstrap import Device, wait_for_device     # noqa: E402
from read_updater_log import upload_blocks        # noqa: E402

CASES = [
    ("lib/libInfraSKD.so",   0x19D402,
     "2b05424ef1f9f5c2", "2e7e9535a194eaa9"),
    ("share/app/global.xdb", 0x1B24A2,
     "1194d334d650657c", "373e93678ebaf1de"),
    ("lib/libObj.so",        0x1A03EA,
     "04b8e1af323b4544", "5343f6ca3e8c76e7"),
]


def main():
    dev = wait_for_device(60)
    if dev is None:
        raise SystemExit('✗ 没有 fastboot 设备（Pico 没插 / 相机没进 bootloader）')
    d = Device(dev)
    verdict = []
    try:
        d.require('oem emmc-init')
        print('%-24s %-18s %s' % ('文件', '相机上的 md5(前16)', '结论'))
        print('-' * 62)
        for name, lba, ours, factory in CASES:
            blk = upload_blocks(d, lba, 1)
            got = hashlib.md5(blk).hexdigest()[:16]
            if got == ours:
                v = '★ 我们的（刷写生效）'
                verdict.append('ours')
            elif got == factory:
                v = '工厂原版（没刷进去/被还原）'
                verdict.append('factory')
            else:
                v = '?! 既不是我们的也不是工厂的'
                verdict.append('other')
            print('%-24s %-18s %s' % (name, got, v))

        print()
        print('=' * 62)
        if verdict and all(v == 'ours' for v in verdict):
            print('判决：刷写【生效】。问题在 app 层（libIMDB/appFwBoot/appFw），')
            print('      下一步部署黑匣子拿运行时数据：python3 deploy_bb.py')
        elif verdict and all(v == 'factory' for v in verdict):
            print('判决：刷写【从未生效】。问题在刷写环节。')
            print('      下一步：先 --plan-only 演练，再用 verify-every 刷一次并抓返回')
        elif 'ours' in verdict:
            print('判决：部分生效 —— 说明刷写链路能通，但有扇区没写对。')
            print('      下一步：全量 verify_partition.py 找出偏差范围')
        else:
            print('判决：内容既非我们的也非工厂的 —— 盘上被别的东西改过。')
            print('      下一步：读 ext2 超级块（偏移 1024）确认文件系统完整性')
        return 0
    finally:
        d.close()


if __name__ == '__main__':
    sys.exit(main())
