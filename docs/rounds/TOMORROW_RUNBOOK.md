# 明天移植新菜单 —— 执行手册（每步只改一个变量）

## ★ 首选路径：loop + bind（不刷 /usr，重启即恢复）

```bash
# 相机正常开机 + WiFi + agent 在跑
python3 /root/fw/oss/tools/bindm2/deploy_bindm2.py --image /mnt/build/nflasha15_v9.ext2
# 然后重启 → 应看到 M2 新菜单；不成功再重启即完全恢复
# 细节见 ../analysis/deploy/BIND_M2_NOFLASH_APPROACH.md

# 它做了三件事：
#   ① 推 /log/libIMDB_m2.so（M2 的 libIMDB 原版，40,596 B）
#   ② 推 /log/bindm2.so（loop+bind，5,812 B）与 /log/m2usr.img（237 MB，分块上传）
#   ③ preload 设为三项：
#      /usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so
# 三者缺一不可（详见文档"三者缺一不可"一节）
```

**理由：改动只落在 `/log`（可写 500 MB）和 `/setting`（两个小文件），
bind 构造函数在 `im.elf` 装载时执行，早于 `libIMDB`/`appFw` 的 dlopen。
失败零代价，重启即恢复 —— 不需要碰 `/usr` 分区。**

## 备选路径：fastboot 刷 /usr（本文件以下内容）

## 目标
真机出现 ZV-1M2 新菜单 UI。若刷入后无 UI，用 beacon 取证定位断点。

## 零、当前静态分析状态（已完成，无需重做）

```
15 项检查全部通过：
  e2fsck 干净 / 几何·UUID·特征位与原厂一致 / /usr 一直 ro 挂载成功
  im.elf·bootin·sen 符号闭包只缺 2 个 CRT 弱符号
  im.elf 15 个 · appFw.so 14 个 · appFwBoot.so 7 个 DT_NEEDED 全部可解析
  glibc ≤2.4 / 软浮点 ABI 一致 / IMDB enable=0x01 会装载
  UXEngine 全部符号由 M2 的 libObj.so 定义（uxengine_init2@0xe2814 等）
  Main() 需要的 7 个资源均为 M2 版
  内存：两机型 Linux 可用均 141.7 MB（内存假说已证伪）
  hybrid 的符号缺失 == M2 原版（无新增缺口）
```

## 一、待刷镜像（已修复一处构建缺陷）

```
/mnt/build/nflasha15_v9.ext2          ← ★ 用这个
  249,102,336 B   md5 f6ae9f576821b35ce33d172a7cab2859
  e2fsck 通过；985 文件；233,041/243,264 blocks（空闲 ~10 MB）

  v9 = v8 + 13 个库 + 2 个文件改成 M2 版（完整版本审计的结果）:
    libptp(46.7%差异) libInfraMscServer(45.8%) libdig_level(56.9%)  ← 完全不同实现
    libWlanAdjust(6.8%) libWlanResource(8.4%) libInfraNetworkServiceInfo(3.0%)
    libInfraKikiLogSender(1.9%) libNetContUtil(0.01%) libtestcmd(0.11%)
    libgps_rsrc(0.01%) libInfraExtBackupCommunicator(0.6%) libInfraExtCmdSvc(0.7%)
    libosal_ulogio(0.9%)
    bin/startBTCore.sh  1,283 → 1,936
    scenario/CAMERA_SCN_FACE_REC_DATA.so
  仍保留 E10 版（有意为之）:
    lib/libosal_uipc.so    ← im.elf/bootin/sen 的锚点（6.44% 差异但导出完全相同）
    全部 kmod/(49) 全部 sabin/(23) bin/{im,bootin,sen,launch_shell}.elf

  已验证：传递依赖零缺失；M2 /usr/lib 全覆盖；17 个关键组件符号闭包与 M2 原版等价

  相对 v6fix 的关键修复：
    + lib/libmprctrl.so (263,812 B)
      ← 传递依赖缺口：E10 版 libNetCommon.so 需要它，
        M2 没有、原 hybrid 也没有 ⇒ dlopen("appFw.so") 必失败
  另外已修：share/avsystemsound/beeplist.txt 5239(E10) → 15586(M2)

  已验证：镜像内 224 个 .so/.elf 的【传递依赖闭包零缺失】；
         完整覆盖 M2 的 /usr/lib（190/190）；e2fsck 干净
```

**⚠️ 注意：这个镜像的 block_count 是 243,264（= 原厂），不是 307,200。**
刷写时分区 300 MB，镜像只有 237.6 MB ⇒ 用 `--full` 只会写 3801 个 64KiB 块（237.6 MiB），
分区尾部的 62 MB 保持原样（不影响挂载）。

## 二、执行顺序

```
① 相机正常开机 → WiFi → 部署 beacon
     /root/fw/oss/tools/beacon/beaconctl.sh prep

② 插 Pico → 进 BootROM → 刷 /usr
     cd /root/fw/oss/fastboot
     python3 pico/inject_payload.py --port /dev/ttyACM0 --file prebuilt/stage1.bin --duration 600
     python3 host/flash_partition.py --part nflasha15 \
       --image /mnt/build/nflasha15_v9.ext2 \
       --part-start 0x2D800000 --chunk-kb 768 --full --verify-every 30
     python3 host/flash_verify.py --part-start 0x2D800000 \
       --image /mnt/build/nflasha15_v9.ext2        # 全量逐字节校验（42 秒）

③ 断电 → 正常开机 → 看屏幕
     有新菜单 ⇒ 成功，进入功能验证
     无 UI    ⇒ 进入第 ④ 步取证

④ 取证（不需要 agent！）
   断电 → 插 Pico → 进 BootROM → 直接读两个位置：
     /tmp 分区里的 .beacon.txt      （偏移 0x0AC00000）
     /log 分区里的 .beacon.txt      （偏移 0x0B800000）
   ⇒ beacon 是 im.elf 的 preload，它跑了就说明 im.elf 被 exec 了
   ⇒ /tmp 在 /usr 之前挂载，所以即使 /usr 挂不上也能留证据

⑤ 恢复（无论成功失败）
     /usr  → /root/fw/zve10_backup/nflasha15.img
     /setting → /root/fw/zve10_backup/nflasha2_current.img
     然后 beaconctl.sh stock
```

## 三、取证结果怎么读

| `/tmp/.beacon.txt` | 结论 | 下一步 |
|---|---|---|
| 多条记录，`bootin_head: SCRIPT` 或 `ELF` | `im.elf` **启动了** ⇒ 断点在 appFwBoot/appFw/CP | 看 `usr_mount` 行确认 /usr 挂载；然后查 appFwBoot 的同步消息 |
| 只有 1~2 条 | 部分 im.elf 实例起来了 | 同上 |
| **文件不存在** | `im.elf` **从未执行** ⇒ 断在 `init → launch_shell → bootin → im.elf` | 查 /usr 是否挂载（读超块/inode 表）；查 bootin.elf 是否被还原 |

**beacon 每条记录包含：**
```
=== BEACON pid=NNN
argv: /usr/bin/im.elf debug s g r u l i a lazy-global
usr_mount: /dev/nflasha15 /usr ext2 ro,...        ← /usr 挂载状态原文
bootin_head: ELF(原厂二进制) / SCRIPT(我们的包装)  ← ★ 刷写到底生效没有的直接证据
memfree: MemFree: 3234 kB
```

**⇒ `bootin_head` 这一行能一举解决"刷写是否生效"的悬案：**
- 显示 `SCRIPT` ⇒ 我们的镜像生效了
- 显示 `ELF` ⇒ 刷写没生效，先解决刷写

## 四、刷写生效性的【可靠判据】（不依赖 inode 编号）

**⚠️ 重要：我们重建的镜像（mke2fs -d）inode 编号与工厂镜像不同**
```
工厂镜像  /bin/bootin.elf = inode 4095
v6fix     /bin/bootin.elf = inode 22
```
**⇒ 所以「比块 16390 的 md5」这种方法会在 v5/v6fix 上给出误导结果。改用文件内容判据。**

### 4.1 最简判据（一条命令）

```bash
ls -l /usr/share/app/global.xdb
#   12252 B ⇒ 刷写【没】生效（还是工厂 /usr）
#   16112 B ⇒ 刷写生效了（M2 资源已就位）
```

### 4.2 完整判据表

| 文件 | 工厂（E10 老菜单） | v6fix（新菜单） |
|---|---:|---:|
| `share/app/global.xdb` | 12,252 | **16,112** |
| `share/app/style_cmn.uxc` | 1,888 | **2,004** |
| `share/avsystemsound/beeplist.txt` | 5,239 | **15,586** |
| `share/app/area_check_data.dat` | 1,174,103 | **1,142,255** |
| `share/app/area_check_data_vertical.dat` | 不存在 | **1,024,593** |
| `lib/libObj.so` | 21,305,528 | **3,931,624** |
| `lib/appFw.so` | 不存在 | **64,273,528** |
| `lib/appFwBoot.so` | 不存在 | **8,312** |
| `lib/libIMDB.so` | 37,204 | **40,596** |

### 4.3 ⚠️ 这个镜像是"全有或全无"

```
v6fix 把 libObj.so 从 21 MB（E10 老引擎）换成 3.9 MB（M2 图形引擎）
⇒ 一旦刷写生效，就【没有老引擎可回退】
⇒ 老菜单不会再出现；要么新菜单起来，要么没 UI
⇒ 所以判据 4.1 的 global.xdb 大小 = 判断"有没有刷进去"的唯一开关，
   必须在刷完后第一时间确认它变成 16112
```

### 4.4 beacon 里的对应字段

```
bootin_head: ELF(原厂二进制)  / SCRIPT(我们的包装)
memfree: MemFree: NNNN kB
usr_mount: /dev/nflasha15 /usr ext2 ro,...
```
**⇒ 若 beacon 有记录但 `usr_mount` 行缺失 ⇒ /proc/mounts 里查不到 /usr ⇒ 挂载真的失败**
**⇒ 若 beacon 一条都没有 ⇒ im.elf 没起来 ⇒ 断在 bootin 之前**

## 五、如果确认"刷写没生效"

```
症状：beacon 报 bootin_head: ELF，且超块 md5 = 原厂
⇒ 说明 fastboot 写入未真正落到 nflasha15 或启动读的是别处
⇒ 优先排查：eMMC 写缓存未 flush / 分区写保护 / 启动读的是解码视图
⇒ 这是与"移植内容"完全无关的独立问题，必须先解决
```
