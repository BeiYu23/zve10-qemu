## 〇、2026-10-24 21:40 最新（覆盖下方历史内容）

### 当前最稳定组合（新菜单）
```
镜像   /mnt/build/tmp/hb/v52.img 或 v53.img
cmdline /root/fw/real_cmdline_lowlog.txt（run_quiet.sh）或 loglevel=7 版（run_verbose.sh）
preload libmmapad.so（匿名 mmap +0x2000）+ libmprotsh.so（忽略 PROT_NONE）
        [+ libgpushim.so / libinputinj.so 可选]
kmod   真 backup/grm_ma/dmm + fake_gles（忠实 128KB 环契约，ioctl 未实现→-EINVAL，事件值 0）
       + stub upm/accel/mag/compass
```
- 状态：36 模块、无 oops、无 data abort、无 SIGABRT（连续 190s，两轮复现）
- App：主循环在跑（futex ×44、GLESKICK ×4、反复读 /proc/self/status 与 /proc/1/cmdline）
- **尚未**：打开 /dev/stream、/dev/dmpgles2；未调用 GraphicsImage/gperm；屏幕全黑

### 关键调试钩子（内核，全部已生效）
```
ZZREG      寄存器
ZZMOVMAP   用户映射（600 段）
ZZR7       r7 所属 vma 及大小
ZZSIG      信号投递（含真实 si_code/kpid/kuid）
ZZTGKILL   tgkill 调用
ZZINSN     故障指令原始编码
ZZMWF      libMWF 的 mp_mon
```

### 老菜单（保持基线，勿动）
```
/mnt/build/ab_v603.img —— 启动链稳定、backup 同步队列已实现、initializeConfig 仍未返回
```

