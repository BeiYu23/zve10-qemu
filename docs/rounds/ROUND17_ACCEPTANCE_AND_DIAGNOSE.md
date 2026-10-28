# 轮次 17：交付物验收 + 自动诊断器

## 一、交付物验收（v9 逐项核对）

在只读挂载 v9 后，用"期望尺寸"逐条核对 21 个关键项，全部通过：

```
启动锚点（保持 E10 版）:
  bin/im.elf              23,240  ✓
  bin/bootin.elf          15,336  ✓
  bin/sen.elf             19,208  ✓
  bin/launch_shell.elf     5,140  ✓
  lib/libosal_uipc.so     26,692  ✓（锚点保护）

新架构（M2 版）:
  lib/libIMDB.so          40,596  ✓
  lib/appFwBoot.so         8,312  ✓
  lib/appFw.so        64,273,528  ✓
  lib/libObj.so        3,931,624  ✓
  lib/libInfraSKD.so   7,259,708  ✓（原来错配成 10 KB）
  lib/libSysDef.so     6,033,848  ✓（原来错配成 3.3 MB）
  lib/libNetCommon.so    122,876  ✓
  lib/libmintx.so      1,532,236  ✓
  lib/libMWF.so          247,864  ✓
  lib/libEMG.so            5,872  ✓
  lib/libmprctrl.so      263,812  ✓（补的第二级依赖）

资源（M2 版）:
  share/app/global.xdb    16,112  ✓
  share/app/style_cmn.uxc  2,004  ✓
  share/app/area_check_data.dat 1,142,255  ✓
  share/avsystemsound/beeplist.txt 15,586  ✓

硬件层（保持 E10 版）:
  kmod/bcmdhd.ko         522,740  ✓

老架构残留（应已清除）:
  lib/libBizFw.so  不存在 ✓
  lib/libBizFw2.so 不存在 ✓
  viewUnified2.so  不存在 ✓

fs: 233,041/243,264 blocks（空闲 10,223 ≈ 10 MB），e2fsck 干净
```

## 二、自动诊断器 `diagnose.py`

把真机取证数据直接翻译成结论。它依次读六类信息并输出判决：

```
① 基本状态   uptime / dmode / preload / 镜像与工具是否存在
② bindm2 日志（/log 或 /tmp 兜底）
③ 挂载状态   /proc/mounts 里的 m2root 与 /usr
④ im.elf 映射了哪些 .so
⑤ 判决 + 下一步建议
```

判决逻辑（关键几条）：

| 现象 | 判决 |
|---|---|
| bindm2 完全无输出 | im.elf 没跑到 preload ⇒ 问题在 exec 之前 |
| `LOOP_SET_FD=-N` | loop 设备问题 |
| `mount ext2=-N` + `ext2_magic 不是 ext2` | 镜像不完整，重新上传 |
| 6 个关键文件尺寸都等于 M2 值 | **bind 完全生效** |
| 看到的是工厂尺寸 | bind 没生效，查 mount/bind 返回值 |
| bind 生效 + maps 里有 appFw.so | **UI 代码已装载**，若仍黑屏则问题在绘制/CP 层 |
| bind 生效 + 有 appFwBoot 无 appFw | 卡在 Application_System_Init |
| bind 生效 + 无 appFwBoot | M2 libIMDB 没走装载流程，查 preload |

## 三、明天的完整动作链（三条命令）

```bash
cd /root/fw/oss/tools/bindm2
python3 deploy_bindm2.py --image /mnt/build/nflasha15_v9.ext2   # 部署
# —— 重启相机 ——
python3 diagnose.py                                              # 自动判决
```

失败时：`python3 deploy_bindm2.py --rollback`（或直接重启）
