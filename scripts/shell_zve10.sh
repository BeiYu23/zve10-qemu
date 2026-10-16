#!/bin/bash
# ZV-E10 QEMU 交互式 shell —— 真机 CRAMFS initrd（nflasha1/boot/initrd.img）自带 shell
#
# ★ 原理：该 initrd 的 /sbin/init 是**脚本**，结尾为：
#     while true; do
#        if [ -f /root/IAMLDUDWRT ]; then read -p LOGIN: hoge; ... else /bin/ash --login; fi
#        sleep 1
#     done
#   只要 /root/IAMLDUDWRT 不存在，就自动开出 /bin/ash（root）
#
# 用法:
#   ./shell_zve10.sh "cmd1" "cmd2" ...     # 批量执行并打印输出
#   ./shell_zve10.sh -f cmds.txt
#
# 环境变量: WAIT(等 shell 秒数) TAILTIME TIMEOUT EMMC OUT
set -e
IR=${IR:-/root/fw/unpacked_zve10/firmware.tar_unpacked/0400_updater/dev/nflasha1_unpacked/boot/initrd.img}
Q=${QEMU:-/mnt/build/qemu-build/arm-softmmu/qemu-system-arm}
Z=${ZIMAGE:-/mnt/build/src/a7c-kernel/linux/arch/arm/boot/zImage}
IMG=${EMMC:-/mnt/build/ab_sym.img}
BK=${BACKUP:-/root/fw/BACKUP.BIN.fixed}
RAMDISK_SIZE=${RAMDISK_SIZE:-8192}
WAIT=${WAIT:-50}
PER=${PER:-2}            # 每条命令间隔
TAILTIME=${TAILTIME:-20}
OUT=${OUT:-/mnt/build/shellout.log}
CMD="mem=1G@0 memrsv=680M@0x15800000:uc mem=256M@0x80000000@1 root=/dev/ram0 init=/sbin/init ramdisk_size=${RAMDISK_SIZE:-8192} console=ttyAM0,115200n8 cfg.uart=9"

CMDS=()
if [ "$1" = "-f" ]; then mapfile -t CMDS < "$2"; else CMDS=("$@"); fi

LOADER=()
[ -f "$BK" ] && LOADER=(-device loader,file="$BK",addr=0x02E80000,force-raw=on)

(
  sleep "$WAIT"
  for c in "${CMDS[@]}"; do
    printf '%s\r' "$c"; sleep "$PER"
    printf 'echo "___DONE___"\r'; sleep "$PER"
  done
  sleep "$TAILTIME"
) | timeout ${TIMEOUT:-150} "$Q" -M cxd90045 -display none -serial stdio \
      -kernel "$Z" -initrd "$IR" \
      -drive if=mtd,file="$IMG",format=raw "${LOADER[@]}" \
      -append "$CMD" > "$OUT" 2>&1 || true

# 过滤：只留 shell 的输入回显与命令输出
sed -i 's/\r/\n/g' "$OUT" 2>/dev/null || true
grep -avE '^\[[[:space:]]*[0-9]+\.[0-9]+\]|^<[0-9]>|QMMAP|MISSSO|MISSDEV|EXECVE|execute_tuning|AL_EMMC|^ZZ|^ *$' "$OUT"
