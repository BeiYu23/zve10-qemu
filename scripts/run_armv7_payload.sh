#!/bin/bash
# 在 cxd90045 QEMU 模型里跑裸 ARMv7 payload（gdb 设 PC + 抓 UART）
# 用法: ./run_armv7_payload.sh <payload.bin> [入口地址] [栈地址] [超时秒]
set -e
P=${1:?用法: $0 <payload.bin> [entry=0xFE020000] [stack=0xFE100000] [timeout=40]}
ENTRY=${2:-0xFE020000}
STACK=${3:-0xFE100000}
TMO=${4:-40}
Q=${QEMU:-/mnt/build/qemu-build/arm-softmmu/qemu-system-arm}
OUT=${OUT:-/tmp/payload_uart.log}
GDBPORT=${GDBPORT:-1235}
rm -f "$OUT"
timeout "$TMO" $Q -M cxd90045 -display none -serial file:"$OUT" -monitor none \
  -S -gdb tcp::${GDBPORT} -device loader,file="$P",addr="$ENTRY",force-raw=on \
  >/tmp/payload_qemu.log 2>&1 &
QPID=$!
sleep 3
timeout $((TMO-5)) gdb-multiarch -batch -q -ex "target remote :${GDBPORT}" \
  -ex "set confirm off" -ex "set \$pc = $ENTRY" -ex "set \$sp = $STACK" \
  -ex "continue" >/dev/null 2>&1 || true
wait $QPID 2>/dev/null || true
echo "======== UART (from payload) ========"
cat "$OUT" 2>/dev/null || echo "(无输出)"
