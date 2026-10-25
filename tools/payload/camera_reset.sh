#!/usr/bin/env bash
# camera_reset.sh — 不用拔线，从主机侧强制相机重新枚举
#
# 三种手段，按侵入性从轻到重：
#   1) usbreset        软复位设备（设备还在枚举时可用）
#   2) authorized 翻转 等价于软件拔插（设备消失后也能强制重新枚举）
#   3) 等它自己回来     STOP_SENSER 后相机通常 5~90 秒自己回来
set -uo pipefail

VID=054c
PID=0d95

find_dev() {
  for p in /sys/bus/usb/devices/*/idProduct; do
    [ -f "$p" ] || continue
    [ "$(cat "$p" 2>/dev/null)" = "$PID" ] || continue
    d=$(dirname "$p")
    [ "$(cat "$d/idVendor" 2>/dev/null)" = "$VID" ] && { echo "$d"; return 0; }
  done
  return 1
}

echo "[*] 查找相机 0x${VID}:0x${PID} …"
for i in $(seq 1 "${1:-30}"); do
  if dev=$(find_dev); then
    echo "[✓] 已枚举: $dev"
    exit 0
  fi
  sleep 1
done

echo "[!] 未枚举。尝试 authorized 翻转（软件拔插）…"
# 设备消失后 1-5 目录可能还在，利用它强制重新枚举
for cand in /sys/bus/usb/devices/1-5 /sys/bus/usb/devices/1-4 /sys/bus/usb/devices/1-6; do
  [ -e "$cand/authorized" ] || continue
  echo "    操作 $cand: authorized 1→0→1"
  echo 0 > "$cand/authorized" 2>/dev/null || { echo "    写 0 失败"; continue; }
  sleep 1
  echo 1 > "$cand/authorized" 2>/dev/null || { echo "    写 1 失败"; continue; }
  for i in $(seq 1 20); do
    if dev=$(find_dev); then echo "[✓] 重新枚举成功: $dev"; exit 0; fi
    sleep 1
  done
done

echo "[!] authorized 翻转无效，尝试 usbreset…"
if command -v usbreset >/dev/null 2>&1; then
  usbreset "${VID}:${PID}" 2>&1 | sed 's/^/    /'
  sleep 3
  if dev=$(find_dev); then echo "[✓] usbreset 后已枚举: $dev"; exit 0; fi
fi

echo "[✗] 软复位失败 —— 需要物理拔插，或等相机自己回来"
exit 1
