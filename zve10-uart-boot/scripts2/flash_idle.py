#!/usr/bin/env python3
"""第2步：把 RP2040 刷成「空闲载波」固件"""
import os,time,termios,glob,subprocess
try:
    fd=os.open('/dev/ttyACM0', os.O_RDWR|os.O_NOCTTY)
    a=termios.tcgetattr(fd); a[4]=termios.B1200; a[5]=termios.B1200
    termios.tcsetattr(fd, termios.TCSANOW, a); time.sleep(0.3); os.close(fd)
    print('已触发 BOOTSEL…')
except Exception as e: print('（设备可能已不在串口模式）', e)
dev=None
for _ in range(10):
    time.sleep(1)
    out=subprocess.run(['lsblk','-lno','NAME,LABEL'],capture_output=True,text=True).stdout
    for ln in out.splitlines():
        if 'RPI-RP2' in ln: dev='/dev/'+ln.split()[0]; break
    if dev: break
if not dev: print('未找到 RPI-RP2，请手动按住 BOOTSEL 插 USB'); raise SystemExit(1)
os.makedirs('/mnt/rpi',exist_ok=True)
subprocess.run(['mount',dev,'/mnt/rpi'],check=False)
subprocess.run(['cp','/root/fw/aux_fix/rp2040/uart_boot_idle2.uf2','/mnt/rpi/'],check=True)
subprocess.run(['sync']); time.sleep(2); subprocess.run(['umount','/mnt/rpi'])
print('★ 已刷入空闲载波固件，等板子重启（约 5 秒）')
