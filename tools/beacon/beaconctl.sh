#!/bin/sh
# beacon 部署/读取/回滚 —— 只动 /setting 的一个文件和 /log 的一个 .so
# 用法: ./beaconctl.sh deploy | read | clean | stock
set -u
CAM="python3 /root/fw/oss/tools/payload/cam.py"
SO="${1:-}"

case "${1:-}" in
  deploy)
    $CAM push /log/beacon.so --file /root/fw/oss/tools/beacon/beacon.so
    $CAM run "printf '3e' > /setting/mode/dmode; printf '/usr/tool/libefence.so:/log/beacon.so' > /setting/mode/preload; sync; echo 'dmode='; cat /setting/mode/dmode; echo; echo 'preload='; cat /setting/mode/preload; echo"
    echo "⇒ 现在重启相机，然后跑: $0 read"
    ;;
  read)
    $CAM run "echo -n 'beacon.txt: '; cat /log/.beacon.txt 2>/dev/null | /bin/busybox tail -20; echo '--- count ---'; cat /log/.beacon.count 2>/dev/null | /bin/busybox wc -c 2>/dev/null || echo '(无 count)'"
    ;;
  clean)
    $CAM run "/bin/busybox rm -f /log/beacon.so /log/.beacon.txt /log/.beacon.count /tmp/.beacon.txt; echo cleaned"
    ;;
  stock)
    # 恢复成"老菜单 + agent 自启"的已验证配置
    $CAM run "printf '3e' > /setting/mode/dmode; printf '/usr/tool/libefence.so:/log/agent_v14.so' > /setting/mode/preload; sync; echo -n 'dmode='; cat /setting/mode/dmode; echo; echo -n 'preload='; cat /setting/mode/preload; echo"
    ;;
  *) echo "用法: $0 deploy|read|clean|stock"; exit 1;;
esac
