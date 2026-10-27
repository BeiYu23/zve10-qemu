# 明天移植新菜单 —— 执行手册（每步只改一个变量）

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
/mnt/build/nflasha15_v6fix.ext2
  249,102,336 B   md5 7a325dc453bdd2548071879b141ace67
  修复：share/avsystemsound/beeplist.txt  5239(E10版) → 15586(M2版)
  e2fsck 通过；984 文件；228,649/243,264 blocks
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
       --image /mnt/build/nflasha15_v6fix.ext2 \
       --part-start 0x2D800000 --chunk-kb 768 --full --verify-every 30
     python3 host/flash_verify.py --part-start 0x2D800000 \
       --image /mnt/build/nflasha15_v6fix.ext2        # 全量逐字节校验（42 秒）

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
     /setting → /mnt/build/nflasha2_factory.img
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

## 四、刷写生效性的独立判据（与 beacon 互相印证）

```bash
# 在 fastboot 里（不依赖 /usr 挂载）
dd if=/dev/nflasha15 bs=1024 skip=1     count=1 | md5sum   # 超块
dd if=/dev/nflasha15 bs=1024 skip=16390 count=1 | md5sum   # inode 表（含 inode 4095）
# 原厂:  a24bfd7e… / dc8af9b9…
# v6fix: 需现场用 dd 从镜像算出（等刷完第一次后记录基准）
```

## 五、如果确认"刷写没生效"

```
症状：beacon 报 bootin_head: ELF，且超块 md5 = 原厂
⇒ 说明 fastboot 写入未真正落到 nflasha15 或启动读的是别处
⇒ 优先排查：eMMC 写缓存未 flush / 分区写保护 / 启动读的是解码视图
⇒ 这是与"移植内容"完全无关的独立问题，必须先解决
```
