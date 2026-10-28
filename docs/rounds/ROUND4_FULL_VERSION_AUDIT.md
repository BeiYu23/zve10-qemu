# 轮次 4：全镜像版本审计（97 个 E10/E2 版本错配项的分类处理）

## 一、审计方法

对 v8 里每一个文件，同时与 M2 原版、E10 原版比较 md5：
```
文件在 v8 的 md5 == M2 版  → 来源 M2
文件在 v8 的 md5 == E10 版 → 来源 E10   ← 重点关注
两边都有但内容不同           → 潜在错配
```

结果：**97 个文件是 E10 版而 M2 有不同版本**，分布：
```
kmod/     49 个   硬件绑定 → 保留 E10【正确，不动】
sabin/    23 个   CP/DSP 固件 → 保留 E10【正确，不动】
lib/      14 个   需逐个判断
bin/      10 个   部分需换
scenario/  1 个   需换
```

## 二、分类判据（本轮建立）

对"同名同大小但内容不同"的库，先量**字节差异率**，再按调用方归属决策：

| 差异率 | 典型库 | 处理 |
|---|---|---|
| <1% | libNetContUtil 0.01% / libgps_rsrc 0.01% / libtestcmd 0.11% / libInfraExtBackupCommunicator 0.6% / libInfraExtCmdSvc 0.7% / libosal_ulogio 0.9% | 本质相同 ⇒ **换 M2 版，零风险** |
| 3~9% | libInfraNetworkServiceInfo 3.0% / libWlanAdjust 6.8% / libWlanResource 8.4% / **libosal_uipc 6.4%** | 需看调用方 |
| 45~57% | libptp 46.7% / libInfraMscServer 45.8% / libdig_level 56.9% | 完全不同实现 ⇒ **换 M2 版**（新 UI 的 IMDB 期望 M2 版） |

## 三、libosal_uipc.so 的特殊处置（唯一保留 E10 版的库）

```
字节差异 6.44%（1,719 / 26,692 B），导出符号 69 个完全相同
调用方：
   E10 二进制: im.elf, bootin.elf, sen.elf  ← 启动必经路径
   M2 库:     libInfraNetworkCommon, libEMG, libsencore, libtestcmd_scn,
              libInfraBluetoothCore, libInfraPtpExtCmd, libGetLog, libBTUtils,
              libgenuinetime, ludpadrufunc, libInfraHttpClientUtil …
决策：保留 E10 版
理由：启动锚点（im.elf/bootin/sen）是 make-or-break；
      导出符号一致 ⇒ 链接无碍；若 v9 失败，v8 里就是同一份，
      可把 libosal_uipc 换成 M2 版作为下一个单变量实验
```

## 四、v9 最终配置

```
换成 M2 版：
  13 个库（见上表）+ bin/startBTCore.sh + scenario/CAMERA_SCN_FACE_REC_DATA.so

保留 E10 版（有意）：
  lib/libosal_uipc.so
  全部 kmod/（49 个，硬件绑定）
  全部 sabin/（23 个，CP/DSP 固件，必须配 E10 的 av-cam.bin）
  bin/im.elf  bin/bootin.elf  bin/sen.elf  bin/launch_shell.elf（启动锚点）
```

## 五、v9 复验（三项全过）

```
① 传递依赖闭包：224 根 / 185 库 / 缺失 0
② M2 /usr/lib 覆盖：M2 201 / v9 209 / 缺 0
③ 17 个关键组件符号闭包：
     libInfraSKD 缺0  libSysDef 缺0  libNetCommon 缺0  libMWF 缺0  libmintx 缺0
     libNetContUtil 缺0  libptp 缺0  libtestcmd 缺0  libWlanAdjust 缺0
     libWlanResource 缺0  libosal_uipc 缺0  libsencore 缺0  libIMDB 缺0  appFwBoot 缺0
     appFw.so 缺2 / AdjChkConfig 缺1 / libObj 缺1  ← 与 M2 原版完全相同
```

## 六、镜像演进总表

| 镜像 | 块数 | md5(前16) | 关键变化 |
|---|---:|---|---|
| v5 | 228,638 | — | 起点（M2 lib+share，E10 其余） |
| v6fix | 228,649 | 7a325dc453bdd254 | beeplist 换 M2 版 |
| v7 | 228,908 | c6f3b4ea46f9cdb7 | +libmprctrl.so（补第二级依赖） |
| v8 | 233,034 | f09c7e96f937ecec | 8 个新 UI 依赖库改回 M2 版 |
| **v9 ★** | **233,041** | **f6ae9f576821b35c** | **+13 库 +startBTCore.sh +人脸识别数据** |

## 七、若 v9 仍无 UI 的下一步单变量清单（按优先级）

```
① libosal_uipc.so 换 M2 版（唯一还保留 E10 的业务库）
② bin/im.elf 换 M2 版（两版仅 4 处字符串不同，风险极低）
③ 刷写生效性排查（beacon 的 bootin_head 字段 / global.xdb 大小判据）
④ 运行时取证（/proc/<im.elf>/maps 看 libObj/appFwBoot/appFw.so 是否被映射）
```
