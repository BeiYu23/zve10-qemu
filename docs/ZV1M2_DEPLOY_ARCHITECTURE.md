# ZV-1M2 菜单移植：部署架构（最终方案）

> 关键结论：**新菜单的 UI 逻辑在 `appFw.so`（61.3 MB）里，不在数据里**
> ⇒ 必须整套替换引擎，不是换资源。
> ⇒ 而引擎库全在 `/usr/lib`，glibc 在 `/root/lib`
> ⇒ **`mount --bind` 整个 `/usr` 就是最精确的做法。**

---

## 一、为什么必须整套换

```
ZV-E10  : libObj.so 21.3 MB（单体老引擎）+ 214 个 view*.uxc
ZV-1M2  : libObj.so 3.7 MB + appFw.so 61.3 MB（新引擎）+ 224 个资源，0 个 view*
```

**⇒ 换成 M2 的 `global.xdb` 后，老引擎会去索引里找那 215 个 view 屏，一个都找不到
⇒ 菜单空白或崩。所以引擎必须配套换。**

### 必须一起换的四件套

| 文件 | ZV-E10 | ZV-1M2 | 说明 |
|---|---|---|---|
| **`libIMDB.so`** | 注册 `appFw.so` | 注册 `appFwBoot.so` | **名字不同，混用直接断链** |
| `appFwBoot.so` | 无 | 8,312 B | 启动桥接 |
| `appFw.so` | 无 | **64,273,528 B** | 新 UI 全部逻辑 |
| `libObj.so` | 21,305,528 | 3,931,624 | 新架构精简版 |

**★ 好消息：`libIMDB.so` 在 ZV-1F 与 ZV-1M2 之间字节完全相同。**

### ⚠️ 硬不兼容点：`style_cmn.uxc`

```
        大小    偏移表项数   步长   +12 高16(schema)   +12 低16
ZV-E10  1888    60          9      3                  49212
ZV-1F   1940    62          9      3                  57406
ZV-1M2  2004    64          9      4  ← 升级！         64（变记录数）
```

**⇒ M2 的 `style_cmn` 是 schema 4，必须用 M2 的，不能用 ZV-E10/ZV-1F 的。**

---

## 二、★★★★★ 部署方案：loop mount + bind 整个 /usr

### 为什么可行

**① 内核**内置** loop 驱动（不是模块，不需要 modprobe）**
```
证据：vmlinux 里有 drivers/block/loop.c、loop.max_loop、loop.max_part
     以及 "loop: Write error at byte offset %llu" 等运行时消息
     固件里【没有】loop.ko ⇒ 是 built-in
```

**② 引擎库全在 `/usr/lib`，基础 ABI 在 `/root/lib`**

| 库 | 位置 | 换 /usr 后 |
|---|---|---|
| **libIMDB / libbackup / libosal_* / libssl / libcrypto / libjemalloc_ds** | **`/usr/lib`** | **★ 换成 M2 版（正是目的）** |
| **libc.so.6 / ld-linux.so.3** | **`/root/lib`** | **不变（基础安全）** |

### 流程

```
1. hybrid 镜像（300 MB）放到 /log/zvegisk/m2usr.img
   ★ 用 SD 卡传输（比 WiFi 快），首次拷到 /log 后就不需要 SD 了

2. zvegisk 构造函数（子进程）里：
   a. mknod /tmp/zdev/loop0 b 7 0
   b. fd = open("/log/zvegisk/m2usr.img", O_RDONLY)
   c. lfd = open("/tmp/zdev/loop0", O_RDWR)
   d. ioctl(lfd, LOOP_SET_FD /*0x4C00*/, fd)
   e. mount("/tmp/zdev/loop0", "/tmp/m2usr", "ext2", MS_RDONLY, 0)
   f. mount --bind /tmp/m2usr /usr        ← ★ 一次换掉整个 /usr

3. 重启即恢复（loop + bind 都不持久）
```

### 为什么不用担心"换掉 /usr 后我们自己也崩"

**zvegisk.so 已经加载在内存里，bind 之后不影响已映射的代码。
`im.elf` 的 NEEDED 里 glibc/ld.so 在 `/root/lib`，也不受影响。
只有 `/usr/lib` 下的 Sony 库在 bind 后被换成 M2 版 —— 这正是我们要的。**

---

## 三、资源现状

### 已建好的 hybrid 镜像

```
/mnt/build/fw/port_m2/out/nflasha15_hybrid.ext2
  300 MiB ext2（卷名 "usr"），内容 227.60 MiB，余量 72 MiB
  e2fsck 干净

/lib    208 文件 106.8 MB
  appFw.so        61.3 MB   ← ★ 新 UI
  libInfraCryptCore.so 7.4 MB
  libInfraSKD.so   6.9 MB
  libSysDef.so     5.8 MB
  libObj.so        3.7 MB
/share/app  ≈224 文件 81.6 MB（含 35 个 _vertical + 34 个 _g + 6 个 image_*_ASTRA）
/bin     53 文件   8.9 MB
/kmod   103 文件   4.8 MB   ← ★ 保留 E10 的（必须匹配 E10 内核）
```

**★ 23 个保留 ZV-E10 的项全部合理：20 个 `kmod/*.ko` + 蓝牙固件等。**

### 空间

```
/log (nflasha11) = 500 MB
hybrid 镜像      = 300 MB
⇒ 剩余 200 MB   ✅ 够
```

---

## 四、回退（四层，都不需要外部工具）

```
① boot_mode="old" 或拔 SD        → 不执行 loop/bind
② watchdog（armed）              → UI 90s 没起来 ⇒ umount + reboot
③ boot_count > 3                 → 自动删 /setting/mode/preload + safemode
④ phase 标记连续 2 次没走完       → 立刻删 preload
⑤ 彻底救不回                     → BootROM fastboot 改 1 字节（已实机验证）
```

**★ 因为 bind 不持久，**最简单的回退就是断电重启**（配置仍是 old 就完全原厂）。**

---

## 五、实施顺序

```
第 0 步  验证 loop 可用（**在相机上跑一个只读探测**，不部署任何东西）
          · mknod /dev/loop0 b 7 0
          · ioctl LOOP_SET_FD 到一个小的测试镜像
          · mount 到 /tmp 读一下
          ⇒ 这一步决定整个方案成立与否

第 1 步  把 hybrid 镜像传到相机 /log（300 MB，SD 卡）
          + 校验 md5

第 2 步  zvegisk 加 loop+bind 逻辑（约 80 行代码）
          先用 agent 隔离测试（不设 dmode）

第 3 步  确认无误 → dmode=3p + boot_mode="new" → 重启真机实测

第 4 步  如果成功 → 再加 screenreader（207 MB，看空间够不够）
```

**★★★ 第 0 步是**唯一的前置风险**：如果 loop 不可用，整套方案要退回
"目录级符号链接农场"（432 个链接），复杂度高一个数量级。**

---

## 八、自检机制（让一次开机拿到两个关键答案）

**在两处未知上用"自检 + 日志"替代"反复试错"：**

### ① `check_preload()` —— libIMDB 从哪加载的

```c
读 /proc/self/maps
找含 "libIMDB" 的行 → 打印整行（含路径）
再检查 libObj / appFw 是否已加载（启动早期应为"未加载"）
```

**判读：**
- **路径是 `/log/zvegisk/m2/libIMDB.so` ⇒ preload 三条目生效 ✓**
- **路径是 `/usr/lib/libIMDB.so` ⇒ preload 没生效（仍是老引擎）**
- **找不到 ⇒ 根本没加载**

**★ 这个判读比"数 ld.so 报错条数"可靠 —— 因为 stderr 走 `console=ttynull` 被丢弃，看不到错误信息。**

### ② `check_loop()` —— loop 设备是否可用

```c
mkdir /tmp/zdev
mount tmpfs → /tmp/zdev           (因为 /tmp 是 vfat，不能 mknod)
mknod /tmp/zdev/loop0 b 7 0
open(loop0, O_RDWR)
if (/log/zvegisk/m2usr.img 存在):
    open image O_RDONLY
    ioctl(loop0, LOOP_SET_FD=0x4C00, image_fd)
    mount(loop0, /tmp/m2usr, "ext2", MS_RDONLY, 0)
```

**判读：**
- **`loop_chk: mount ext2 OK => loop USABLE` ⇒ 整个方案成立**
- **`mknod FAIL` ⇒ 权限问题**
- **`LOOP_SET_FD FAIL` ⇒ loop 驱动不可用 ⇒ 退回符号链接农场方案**
- **`m2usr.img absent -> basic test only` ⇒ 镜像还没传，只测了基础**

### ⚠️ 顺带修掉一个自己引入的 bug

**`check_loop()` 挂了 tmpfs 到 `/tmp/zdev`，而 `mount_sd()` 后面还要挂一次
⇒ 第二次失败 ⇒ 误报 `sd_unavailable`。**

**⇒ `mount_sd()` 改成容忍 tmpfs 已挂载：**

```c
if (sy5(SYS_mount, "tmpfs", "/tmp/zdev", "tmpfs", 0, 0)) {
    lg("tmpfs already? -> continue\n");   // 不是错误
}
```

### ⚠️ 部署时的顺序注意

**`unmount_sd()` 会卸 `/tmp/zdev` ⇒ loop0 设备节点消失（但 loop 设备本身仍有效）。
⇒ **实际的 bind 必须在 `unmount_sd()` 之前完成**，或者部署时不要卸 `/tmp/zdev`。**

---

## 九、内存影响（128MB 设备）

```
.text    30,116 B
.rodata   2,684 B
.data        48 B
.bss     63,680 B   ← 自检的 32KB maps 缓冲
─────────────────────
RW 总计  ~64 KB = 128MB 的 0.05%   ✅ 可接受
```

**★ `.bss` 从 30KB 涨到 63KB（maps 缓冲 32KB）。
`/proc/self/maps` 对 im.elf（97 个库）实测约 10KB，32KB 有充足余量。**
