#!/bin/bash
# ZV-E10 QEMU 引导脚本
#
# ★ 使用前请按自己的环境设置以下变量（或用环境变量覆盖）：
#   QEMU     : qemu-system-arm 可执行文件路径
#   ZIMAGE   : 打好补丁后编译出的内核 zImage
#   EMMC     : 用 build_patched_emmc_safe.sh 生成的整盘 eMMC 镜像
#   INITRD   : 你自己从设备导出的 initrd
#   BACKUP   : 修正过的 BACKUP.BIN（见 docs/HANDOFF.md）
#   CMDLINE  : 内核 cmdline（含真机内存图/保留区等参数）
#
# 详见 ../docs/analysis/qemu/QEMU_BOOT_PROGRESS.md（含执行摘要）与 docs/HANDOFF.md
set -e
: "${QEMU:=/usr/local/bin/qemu-system-arm}"
: "${ZIMAGE:=/path/to/zImage}"
: "${EMMC:=/path/to/zve10_patched.img}"
: "${INITRD:=/path/to/initrd.img}"
: "${BACKUP:=/path/to/BACKUP.BIN.fixed}"
: "${CMDLINE_FILE:=/path/to/cmdline.txt}"
exec "$QEMU" -M cxd90045 -display none -serial stdio \
  -kernel "$ZIMAGE" -initrd "$INITRD" \
  -drive if=mtd,file="$EMMC",format=raw \
  -device loader,file="$BACKUP",addr=0x02E80000,force-raw=on \
  -append "earlyprintk $(cat "$CMDLINE_FILE")"
