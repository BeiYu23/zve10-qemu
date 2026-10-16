#!/bin/bash
# ZV-E10 QEMU —— 交互式 root shell **在相机自己的 rootfs 里**
#
# 原理：
#   1. 用真机 CRAMFS initrd 启动（/sbin/init 是脚本，结尾自动开 /bin/ash）
#   2. 在 shell 里挂载真机分区到 /mnt
#   3. busybox chroot /mnt  →  进入相机 rootfs
#
# 用法: ./shell_camera.sh "命令1" "命令2" ...
set -e
IR=${IR:-${INITRD_REAL:-/path/to/your/nflasha1/boot/initrd.img}}
PREP=(
  "mount -t ext2 -o ro /dev/nflasha7 /mnt"
  "mount -t ext2 -o ro /dev/nflasha15 /mnt/usr"
  "mount -t vfat /dev/nflasha2 /mnt/setting"
  "mount -t proc proc /mnt/proc"
  "mount -t sysfs none /mnt/sys"
)
CMDS=("${PREP[@]}")
for c in "$@"; do CMDS+=("busybox chroot /mnt /bin/ash -c '$c'"); done
exec $(dirname "$0")/shell_zve10.sh "${CMDS[@]}"
