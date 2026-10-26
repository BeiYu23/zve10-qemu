#!/bin/bash
# Fixed observation interval. No idle-based stability claims.
set -euo pipefail
IMAGE=${1:?image}; OUT=${2:?output}; SECONDS_MAX=${SECONDS_MAX:-120}
QEMU=${QEMU:-/mnt/build/src/qemu-sony/arm-softmmu/qemu-system-arm}
KERNEL=${KERNEL:-/mnt/build/src/a7c-kernel/linux/arch/arm/boot/zImage}
INITRD=${INITRD:-/root/fw/handoff_p0/initrd_q4.img}
if pgrep -x qemu-system-arm >/dev/null; then echo 'Refusing concurrent QEMU' >&2; exit 2; fi
if [ -e "$OUT/serial.log" ]; then echo 'Refusing to overwrite previous run' >&2; exit 2; fi
mkdir -p "$OUT"
CMD="$(< /root/fw/real_cmdline_lowlog.txt) norandmaps"
sha256sum "$KERNEL" "$INITRD" > "$OUT/inputs.sha256"
printf 'image=%s\ncmdline=%s\n' "$IMAGE" "$CMD" > "$OUT/launch.txt"
cleanup() { pkill -x qemu-system-arm 2>/dev/null || :; }
trap cleanup EXIT INT TERM
set +e
timeout --signal=TERM --kill-after=5 "$SECONDS_MAX" "$QEMU" -M cxd90045,graphics=on -display none \
 -kernel "$KERNEL" -initrd "$INITRD" -drive "if=mtd,file=$IMAGE,format=raw" \
 -device loader,file=/root/fw/BACKUP.BIN.fixed,addr=0x02E80000,force-raw=on \
 -serial "file:$OUT/serial.log" -qmp "unix:$OUT/qmp,server=on,wait=off" \
 -global driver=bionz-streamfb,property=blank-pattern,value=off \
 -global driver=bionz-streamfb,property=width,value=640 \
 -global driver=bionz-streamfb,property=height,value=480 \
 -global driver=bionz-streamfb,property=stride,value=1280 \
 -global driver=bionz-streamfb,property=format,value=1 \
 -append "$CMD" > "$OUT/qemu.log" 2>&1
rc=$?
set -e
printf 'QEMU_EXIT=%s timeout124_is_expected=1\n' "$rc" | tee "$OUT/result.txt"
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then exit "$rc"; fi
