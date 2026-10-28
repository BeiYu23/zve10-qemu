#!/bin/sh
# beacon 部署/读取/回滚。只动 /log 文件 + /setting/mode 两个文件
# 用法: ./beaconctl.sh prep | read | clean | stock
CAM="python3 /root/fw/oss/tools/payload/cam.py"
B=/root/fw/oss/tools/beacon/beacon2.so
case "${1:-}" in
  prep)
    $CAM run "/bin/busybox rm -f /log/beacon2.so /tmp/.beacon.txt /log/.beacon.txt" >/dev/null 2>&1
    $CAM push /log/beacon2.so --file "$B"
    $CAM run "printf '3e' > /setting/mode/dmode; printf '/usr/tool/libefence.so:/log/beacon2.so' > /setting/mode/preload; sync; echo -n 'dmode='; cat /setting/mode/dmode; echo; echo -n 'preload='; cat /setting/mode/preload; echo"
    echo "⇒ 现在去 fastboot 刷 /usr 镜像，然后正常开机，看屏幕"
    ;;
  read)
    echo "=== /tmp/.beacon.txt（/tmp 在 /usr 之前挂载，最可靠）==="
    $CAM run "cat /tmp/.beacon.txt 2>/dev/null || echo '(不存在)'"
    echo "=== /log/.beacon.txt ==="
    $CAM run "cat /log/.beacon.txt 2>/dev/null || echo '(不存在)'"
    ;;
  clean)
    $CAM run "/bin/busybox rm -f /log/beacon2.so /tmp/.beacon.txt /log/.beacon.txt; echo cleaned"
    ;;
  stock)
    $CAM run "printf '3e' > /setting/mode/dmode; printf '/usr/tool/libefence.so:/log/agent_v14.so' > /setting/mode/preload; sync; echo -n 'preload='; cat /setting/mode/preload; echo"
    ;;
  *) echo "用法: $0 prep|read|clean|stock"; exit 1;;
esac
