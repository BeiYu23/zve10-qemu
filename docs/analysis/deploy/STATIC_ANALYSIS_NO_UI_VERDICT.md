# 刷入新菜单 /usr 后无 UI —— 静态分析结论（2026-10-28）

> 目标：用纯静态手段回答「为什么刷入后没有 UI」。
> 方法：对 hybrid 镜像与 ZV-1M2 原厂逐项做结构、符号、依赖、资源、内存核对。

---

## 一、逐项排除表（全部通过，共 14 项）

| # | 检查项 | 结果 | 证据 |
|---|---|---|---|
| 1 | 文件系统完整性 | ✅ 通过 | 4 个镜像 `e2fsck -fn` 全无错误 |
| 2 | 几何 / UUID / magic / 特性位 | ✅ 一致 | blocks 243,264、inodes 60,960、inode_size 128、UUID c509f8ed、0xEF53、0x38/0x02/0x03 |
| 3 | `/usr` 是否挂载 | ✅ 一直挂上 | 真机实测 `/dev/nflasha15 /usr ext2 ro`；`s_mnt_count` 判据在 ro 下无效（已有专文） |
| 4 | `im.elf` 符号闭包 | ✅ 仅缺 2 个 CRT 弱符号 | `_Jv_RegisterClasses` / `__gmon_start__`，可缺 |
| 5 | `bootin.elf` / `sen.elf` 闭包 | ✅ 同样只缺那 2 个 | — |
| 6 | `im.elf` 的 15 个 DT_NEEDED | ✅ 全部可解析 | libIMDB/libbackup/libosal_*/libssl/libcrypto 在 hybrid，其余在 rootfs |
| 7 | `appFw.so` 的 14 个 DT_NEEDED | ✅ 全部可解析 | 含 libNetCommon/libGOConnectionManager/AdjChkConfig/libopus/libSoundTouch/libqrencode/libInfraSKD |
| 8 | `appFwBoot.so` 的 7 个 DT_NEEDED | ✅ 全部可解析 | 全为系统库 |
| 9 | glibc 版本需求 | ✅ ≤ GLIBC_2.4 | 相机 libc 2.11.2 |
| 10 | 浮点 ABI | ✅ 一致 | 双方均无 `Tag_ABI_VFP_args`（软浮点），与 `ld-linux.so.3` 匹配 |
| 11 | IMDB 注册 | ✅ 会装载 | M2 表 idx=145 `appFwBoot.so` `enable=0x01`；真机 argv 无 `target=` ⇒ 走 default(0x01) |
| 12 | 入口符号 | ✅ 导出 | `appFwBoot.so` 导出 `Application_System_Init@0x1270` |
| 13 | UXEngine 提供者 | ✅ 到位 | `uxengine_init2` / `sys_setCurrentDirectory` / `ArenaParam` 由 M2 `libObj.so`(3.9 MB) 提供；该库被 im.elf 硬编码 dlopen `/usr/lib/libObj.so` |
| 14 | Main() 所需资源 | ✅ M2 版到位 | `global.xdb` 16,112 / `style_cmn.uxc` 2,004 / `area_check_data.dat` 1,142,255 / `area_check_data_vertical.dat` 1,024,593 / `fontlist.dat` 360 |
| 15 | 内存 | ✅ 不是瓶颈 | kemco.txt：两机型 Linux 可用均为 **141.7 MB**（详见 MEMORY_HYPOTHESIS_REFUTED.md） |

**⇒ 静态层面找不到任何"必死"原因。**

---

## 二、发现的三处真实不一致（非致命，但要记录）

### 2.1 `share/avsystemsound/beeplist.txt` 没换成 M2 版

```
hybrid : 5,239 B   （E10 版）
M2 原厂: 15,586 B
⇒ 构建时这个文件漏换。只影响提示音列表，不应阻塞 UI，但说明 share 并非 100% M2
```
其余 `/share` 对比：hybrid 395 个文件 vs M2 359 个；仅 hybrid 多 36 个（E10 的 avsystemsound 音效）；
**仅 M2 有 = 0 个**，同名不同内容 = 1 个（就是 beeplist.txt）。

### 2.2 WLAN 栈混搭（可能导致 WiFi 起不来，但不影响 UI）

```
libWlanControlCommon.so           hybrid = M2
libInfraNetConnectionManager.so   hybrid = M2
libWlanAdjust.so                  hybrid = E10   ← 跨代混搭
libWlanResource.so                hybrid = E10
kmod/bcmdhd.ko                    hybrid = E10   ✓
/usr/wlan/bcm/bcm4339*.bin        两版字节相同   ✓
```
**⇒ 这解释了"agent 不可达"，但不解释"无 UI"。**

### 2.3 相机 `/usr` 超块实测仍是原厂值（与"刷写生效"矛盾）

```
块 1（超块）  = a24bfd7e = 原厂
块 16390（inode 表，含 inode 4095）= 115d4396 = 我们镜像
块 131599（bootin 数据块）        = 32616be8 = 我们镜像
而两份镜像里 inode 4095 的位置完全相同（块 16390 / 偏移 0x300）
⇒ 数据块上是我们的脚本，文件系统却报 15,336 B 原厂 ELF
```
**⇒ 这是唯一真正的"物理矛盾"，它意味着"刷写是否真正生效"本身还没被证实。**

---

## 三、三级结论

```
【已证实】文件系统、符号、依赖、资源、内存、ABI 全部无碍
        ⇒ 静态分析无法解释"无 UI"

【最可疑】2.3 那个矛盾：刷进去的内容可能根本没被启动流程读到
        ⇒ 优先级最高，因为它会让"后续所有实验都变成在测空气"

【次可疑】im.elf 对 /usr/lib/libObj.so 的 dlopen 时机与 RTLD 标志
        appFwBoot.so 的 UXEngine 符号靠【全局符号域】解析，
        若 libObj.so 未以 RTLD_GLOBAL 先加载，Main() 第一条 uxengine_init2() 就崩
        （真机 argv 含 lazy-global，理论上 OK，但 libObj 是 im.elf 单独 dlopen 的，
          其标志不受 argv 控制）
```

---

## 四、明天刷入后无 UI 时的第一手判据（按顺序，全部只读）

```bash
# 0. 确认本次开机
cat /proc/uptime

# 1. 刷写到底有没有生效（解决 2.3 的矛盾）
dd if=/dev/nflasha15 bs=1024 skip=1     count=1 | busybox md5sum
dd if=/dev/nflasha15 bs=1024 skip=16390 count=1 | busybox md5sum
#   期望：= 我们镜像的值 ⇒ 刷写生效；= 原厂值 ⇒ 刷写未生效

# 2. im.elf 有没有起来 + 它映射了哪些库
for p in /proc/[0-9]*; do c=$(cat $p/comm 2>/dev/null);
  [ "$c" = "im.elf" ] && { echo "PID ${p#/proc/}"; busybox cat $p/maps 2>/dev/null | busybox grep -E "libObj|appFwBoot|appFw|libIMDB|libuxengine"; }; done
#   libObj.so 在 ⇒ 第一步 dlopen 成功
#   appFwBoot.so 在 ⇒ IMDB 装载成功
#   appFw.so 在 ⇒ Application_System_Init 跑通并 dlopen 成功
#   appFw.so 不在 ⇒ 卡在 Application_System_Init（那条同步消息或符号解析）

# 3. 有没有 OOM / 缺页失败
dmesg | busybox grep -iE "oom|out of memory|killed process|segfault|undefined symbol"

# 4. dmesg 里的 ext2/VFS 错误（会有挂载问题的话在这里）
dmesg | busybox grep -iE "EXT2|VFS|mount"
```

**⇒ 第 2 步的三行输出能把故障点从"整个应用层"收窄到"四个具体阶段之一"，
这是纯静态分析做不到、但代价最低的一步。**
