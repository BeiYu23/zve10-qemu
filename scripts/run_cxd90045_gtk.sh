#!/bin/sh
# QEMU-only CXD90045 GTK/QMP runner.
# Usage: run_cxd90045_gtk.sh IMAGE [SECONDS]
set -eu
IMAGE=${1:?image path required}
SECONDS=${2:-120}
QEMU=${QEMU:-/mnt/build/src/qemu-sony/arm-softmmu/qemu-system-arm}
KERNEL=${KERNEL:-/mnt/build/src/a7c-kernel/linux/arch/arm/boot/zImage}
INITRD=${INITRD:-/root/fw/handoff_p0/initrd.img}
BACKUP=${BACKUP:-/root/fw/BACKUP.BIN.fixed}
OUT=${OUT:-/tmp/cxd90045-run}
mkdir -p "$OUT"
CMD="$(cat /root/fw/real_cmdline.txt) norandmaps"
rm -f "$OUT/qmp" "$OUT/screen.ppm" "$OUT/screen.png" "$OUT/serial.log"
nohup xvfb-run -a "$QEMU" \
  -M cxd90045,graphics=on -display gtk \
  -kernel "$KERNEL" -initrd "$INITRD" \
  -drive if=mtd,file="$IMAGE",format=raw \
  -device loader,file="$BACKUP",addr=0x02E80000,force-raw=on \
  -serial "file:$OUT/serial.log" \
  -qmp "unix:$OUT/qmp,server=on,wait=off" \
  -append "$CMD" >/dev/null 2>&1 &
QPID=$!
cleanup() { kill "$QPID" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
for i in $(seq 1 60); do [ -S "$OUT/qmp" ] && break; sleep 1; done
sleep "$SECONDS"
python3 - "$OUT/qmp" "$OUT/screen.ppm" <<'PY'
import json, socket, sys, time
sock=socket.socket(socket.AF_UNIX); sock.connect(sys.argv[1])
sock.recv(4096)
def cmd(x):
    sock.send((json.dumps(x)+'\r\n').encode()); time.sleep(.2); return sock.recv(4096).decode(errors='replace')
cmd({'execute':'qmp_capabilities'})
print(cmd({'execute':'screendump','arguments':{'filename':sys.argv[2]}}))
sock.close()
PY
if command -v convert >/dev/null 2>&1; then convert "$OUT/screen.ppm" "$OUT/screen.png"; fi
echo "serial=$OUT/serial.log"
echo "screen=$OUT/screen.ppm"
[ -f "$OUT/screen.png" ] && echo "png=$OUT/screen.png"
