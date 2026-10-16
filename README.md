# zve10-qemu — 让 Sony ZV-E10（一代）在 QEMU 里跑起来

把 **Sony ZV-E10（ILCE-ZV-E10，CXD90045 / "ASTRA" SoC）** 的**原厂固件**
搬进 QEMU 模拟器，让它完整启动、加载全部平台内核模块，并把机身 App
（`/usr/bin/im.elf`）跑起来。

> **本仓库只包含"胶水"与研究成果（补丁 / 脚本 / 文档 / 派生数据），
> 不含任何 Sony 固件、二进制或证书。** 你需要自备从自己设备导出的固件。

---

## 这是什么

Sony 的相机固件是一个相当封闭的 Linux 系统（内核 `3.0.27_nl-rt106`，
soft-float ARMv7，Sony 自研 SoC `CXD90045`）。本项目的工作是**把它移植到一个
通用的 ARM 模拟器里跑**，以便：

- 在不需要实机的情况下研究/调试固件行为
- 复现启动流程、逆向系统结构
- 为「给老机型移植新菜单」之类的改造提供实验环境

**当前进度：相机已经能在 QEMU 里完整启动，App 已运行到相机成像初始化阶段。**
详见 [`docs/QEMU_BOOT_PROGRESS.md`](docs/QEMU_BOOT_PROGRESS.md)（含执行摘要）。

---

## 当前状态

| # | 目标 | 状态 |
|---|------|------|
| 1 | 过掉 `backup.ko` 的 `file_load` 关卡；`/sbin/init` 完成挂载与 chroot；**约 20 个平台模块全部加载** | ✅ **完成**（实测 33 个模块） |
| 2 | 让 stella UI（`im.elf` + `appFw.so`）真正起来 | 🟡 **大部分**：App 持续运行、`MonDrv` 初始化成功、已进入相机成像初始化；剩余 1 处堆损坏 |
| 3 | 打通显示链路 `/dev/dmpgles2 → grm_gles.ko → Mali`，在 QEMU 窗口看到画面 | ❌ **未完成**：需要 Mali GPU 模型，或在 App 侧拦截 GLES 改软件渲染 |
| 4 | 打通输入链路（按键/触摸 → `input.ko`/`sircs`/`indctr`） | 🟡 模块已加载、串口双向交互已验证 |

**能观察到的（串口输出）**

```
eMMC: 1888MiB                     ← 真机整盘 eMMC 被正确识别
nflasha1 .. nflasha23             ← 16 个分区（与真机一致）
VFS: Mounted root (ext2)          ← 真机 initrd 挂成 root
Modules linked in: ...            ← 33 个平台模块全部加载
 FixedAreaTop  : 20098000         ← 机身 App 自己打印
 Mon Buf Size  : 00008000         ← 监视器子系统初始化成功
```

---

## 怎么用

### 1. 前置条件

| 依赖 | 说明 |
|------|------|
| **自备固件** | 你自己从 ZV-E10 导出的 `nflasha7`（rootfs）、`nflasha15`（`/usr`）等分区镜像，以及 initrd。**本仓库不提供。** |
| **Sony 内核源码** | 用于打内核补丁（`patches/kernel/`）。需自备对应版本源码树。 |
| **QEMU 源码 + Android/Mali 之类无关** | `patches/qemu/` 是针对一个 `qemu-sony` 分支（含 `hw/arm/bionz.c`）的补丁 |
| **ARM 交叉工具链** | 例如 `arm-buildroot-linux-gnueabihf-`（soft-float/v7） |

### 2. 打补丁

```sh
# 内核（在 Sony 内核源码树里）
patch -p1 < patches/kernel/adrs-conv-fallback.patch
patch -p1 < patches/kernel/kconfig-pcie-qemu.patch
patch -p1 < patches/kernel/tty-visible-qemu.patch

# QEMU（在 qemu 源码树里）
patch -p1 < patches/qemu/bionz.patch
patch -p1 < patches/qemu/sd-cid.patch
```

三个内核补丁分别解决：

| 补丁 | 作用 |
|------|------|
| `adrs-conv-fallback` | Sony 的物理↔虚拟地址换算在"无法判断"时返回 0，导致一串内核 oops；改为**正确换算** |
| `kconfig-pcie-qemu` | 打开 `CONFIG_PCI`（否则 `mmc_drv` 报一堆 `Unknown symbol pci_*`） |
| `tty-visible-qemu` | **让机身 App 的用户态输出可见** —— 这是本项目最重要的调试能力（见下） |

> 关于 `tty-visible`：Sony 把 console 的 `txrx_enable` 默认设为 0
> （真机 `console=ttynull`，用户态输出走 klog 而非串口）。QEMU 下强制打开，
> 于是机身 App 的每一层失败都会**自己"打字"出来**，调试效率提升一个量级。

### 3. 构建镜像与运行

```sh
# 按需修改脚本里的路径，指向你自己的固件
./scripts/build_patched_emmc_safe.sh
./scripts/run_qemu_zve10.sh
```

`build_patched_emmc_safe.sh` 的设计要点（**值得单独说明**）：

- **整棵目录树解包到宿主机 → 在宿主机上用 Python 直接改字节 → `mke2fs -d` 重新打包**
- **每一步都回读校验**（存在性 + 大小），任一缺失立即退出

> 为什么不直接在 ext2 镜像上 `debugfs rm/write`？
> 因为那样**不可靠**：`write` 可能失败，而一旦把 stderr 吞掉就会**静默丢文件**。
> 本项目曾因此丢掉 `libMWF.so`，害得后面追了十几轮才找到原因。

### 4. 桩库生成器

`scripts/gen_all_stubs.py` 从 `libIMDB.so` 里提取「机身 App 会 `dlopen` 的库 + 符号」表，
为所有缺失的库生成桩库。**这是消除"缺库 → 错误消息写穿堆"的关键工具。**

---

## 关键修复清单

### QEMU 模型侧

| 补丁 | 内容 |
|------|------|
| `bionz.patch` | `hw/arm/bionz.c`：`boot_size=0x200000`、WBI 7 段装载、DDR `1G + 256M` |
| `sd-cid.patch` | `hw/sd/sd.c`：CID/CSD/EXT_CSD、CMD8/CMD6，以及 **CMD23 块数掩码 `& 0xFFFF`**（这个 bug 让 eMMC 写超时 5 秒） |

### 数据/模块侧

| 项 | 说明 |
|----|------|
| `BACKUP.BIN` 修正 | 机身备份数据的两处运行态值（`FixedAreaTop`/`FixedAreaSize`）需从 `BACKUP.BAK` 补进 `BACKUP.BIN`，否则 `MonDrv` 监视器子系统起不来 |
| `backup.ko` 惰性初始化 | 为可重定位内核模块**新增 `R_ARM_ABS32` 重定位** + 注入 trampoline，让 `Data_SearchId` 在表为空时先调 `Data_init` |
| `libJiritsu.so` | `jiritsu_init`（"自立"日志子系统）会写坏堆；跳过它可让 App 大幅前进 |

---

## 调试方法论（本项目积累的观测手段）

1. **用户态输出可见** —— 让 App 自己报错（见上）
2. **物理内存快照 + 离线分析** —— QEMU monitor 的 `pmemsave`
   *（坑：文件名不能用绝对路径，`/` 会被当表达式解析）*
3. **内核 `printk` 追踪** —— 比 gdb 可靠得多
   （`patches/kernel/mmap-trace.c`、`fs-open-misso-trace.c` 是两个例子）
4. **"在快照里找笔迹"** —— 堆损坏时，去物理内存里读**被越界写进去的内容**，
   往往能直接认出写者（本项目多次靠这招破案）

### ⚠️ 踩过的坑（避免重走弯路）

1. **没有内部约定信息时，不要做字节级补丁**
   —— 曾凭猜测给 `libMWF.so` 打补丁，制造出一串更难查的崩溃
2. **每处改动都要 A/B 对照**，包括内核里"为消错而加的兜底"
3. **`debugfs` 原地替换文件不可靠**，必须回读校验（见上）
4. **大文件不要写 `/tmp`**（tmpfs 满会导致构建/调试静默失败）
5. **gdb 的 `commands` 块内 `dump` 在 target running 时失败**；
   绝对地址断点对**基址每次变化的用户态库无效**

---

---

## ★ 交互式调试（强烈推荐）

**不必每次等几分钟看日志 —— 可以直接在客机里敲命令。**

### 原理

真机 `nflasha1/boot/initrd.img`（4.9MB **CRAMFS**，md5 `bb50bfb5…`，与 `p1chksum` 吻合）
的 `/sbin/init` 是**脚本**，结尾为：

```sh
while true; do
    if [ -f /root/IAMLDUDWRT ]; then read -p LOGIN: hoge; ...
    else /bin/ash --login        # ← 该文件不存在时，自动开出 root shell
    fi
    sleep 1
done
```

> ⚠️ 注意区分：`nflasha1/boot/initrd.img` 是这份**脚本 init**（给出 shell）；
> 而正常启动链里第一段用的 initrd 的 `/sbin/init` 是 **ELF**。

### 用法

```sh
./scripts/shell_zve10.sh "cmd1" "cmd2" ...     # 客机 root shell
./scripts/shell_camera.sh "cmd1" "cmd2" ...    # 自动挂载 + chroot 进相机 rootfs
```

`shell_camera.sh` 实际执行：

```sh
mount -t ext2 -o ro /dev/nflasha7  /mnt
mount -t ext2 -o ro /dev/nflasha15 /mnt/usr
mount -t vfat       /dev/nflasha2  /mnt/setting
mount -t proc proc /mnt/proc ; mount -t sysfs none /mnt/sys
busybox chroot /mnt /bin/ash
```

### 实测能做什么

```sh
/ # lsmod                                   # 看模块加载与依赖
/ # cat /sys/module/dmm/parameters/DmmConfig   # 看模块实参
/ # cat /proc/partitions                    # 看分区
/ # busybox chroot /mnt /bin/ash -c 'cd /; export LD_LIBRARY_PATH=/lib:/usr/lib; \
       /usr/bin/im.elf debug s g r u l i a lazy-global'   # 手动跑相机 App
```

**⇒ 这是本项目最强的调试手段：可现场复现崩溃、看内核栈回溯、逐条验证假设。**

### 踩坑提醒

- 解包 CRAMFS 会**丢失可执行位与符号链接**；重建镜像时要 `chmod 755`
  并为 busybox 重建 `sh`/`ash`/`mount`/... 链接，否则内核报 `No init found`
- 老内核（3.0.27）对现代 e2fsprogs 的特性敏感，必要时 `mke2fs -O none`
- `ramdisk_size` 必须 ≥ 镜像大小（13MB 镜像需 `ramdisk_size=16384`）
- busybox 是精简版：没有 `head`/`id`/`df`；`chroot` 要用 `busybox chroot`

---

## 目录结构

```
docs/                    研究文档（启动进展、交接包、系统结构、逆向报告…）
patches/kernel/          内核补丁 + 复用源码片段
patches/qemu/            QEMU 机器模型 / SD 卡补丁
scripts/                 构建脚本、引导脚本、桩库生成器
data/                    派生技术数据（分区表、IMDB 表、机型对照）
```

---

## 已知限制 / 下一步

**A. 机身 App 仍有一处堆损坏**
- 现象：`libObj.so` 某线程内 `free(): invalid next size`
- 已定位到机制：glibc `_int_malloc` 用"尺寸被夸大的块"满足了 3.16MB 请求 ⇒ chunk split 越界
- 已排除：内存压力、`brk`、`mmap`、dlopen 缺库、size 计算、jemalloc、`MALLOC_OPTIONS`

**已试过但行不通的路（详见 [docs/EXCLUDED_HYPOTHESES.md](docs/EXCLUDED_HYPOTHESES.md)，内含 16 类已排除假设的完整清单）：**

- ✗ **`mcheck()` 注入** —— `mcheck` 必须在任何 `malloc` 之前调用，
  而动态库构造函数跑在 `ld.so` 重定位之后 ⇒ **静默失效**。
  （另：本 libc 未编译进 `MALLOC_CHECK_` 支持。）真要生效只能重写 ELF 入口点。
- ✗ **逐层分析崩溃点上游** —— glibc 报的 `free/realloc` 位置几乎总是**发现者**，
  不是写者。例如 `libObj.so+0x78ba2a` 上游的循环经分析逻辑是安全的。

**目前唯一验证有效的定位手段**：在物理内存快照里找**被越界写进去的残留内容**
（本项目靠这招在堆里读到一批 `dlopen` 错误消息，从而定位到 `libJiritsu.so`）。

**另外一个值得注意的观察**：启用全局 `mcheck` 时，连引导阶段的 `bootin.elf`
也报了 `munmap_chunk(): invalid pointer` ⇒ **堆损坏不止发生在 `im.elf`，
而是多个程序都可能中招** ⇒ 提示根因可能是**共享的初始化逻辑或环境差异**。

**B. 显示链路需要 Mali**
```
App → /dev/dmpgles2 → grm_gles.ko「SUGILITE Driver」(DMP/Sony) → Mali GPU
```
两条路：① 在 QEMU 里建模 Mali（研究级工作量）
② 在 App 侧拦截 GLES 调用、改软件渲染

---

## 法律与免责

- 本仓库**不包含**任何 Sony 固件、二进制、证书或其派生副本。使用者需**自行导出**
  自己设备的固件。
- 本项目目的是**互操作性研究与个人设备改造**。请遵守你所在地区的法律，
  以及 Sony 的软件许可条款。**风险自负。**
- 文档中出现的商标、产品名归其各自所有者所有。本项目与 Sony 无关联。

---

## 参与

欢迎提交 issue / PR。特别欢迎：

- **Mali / SUGLITE 建模**相关的经验
- **堆损坏定位**的新思路（见「已知限制 A」）
- 其他 **CXD90045 / BIONZ 平台**设备的移植反馈

---

## 致谢

- `YuuLuo/a7c2-service-preload` —— 服务模式/`ld.so.preload` 思路的先行工作
- QEMU 社区
- 所有在相机逆向领域公开过笔记的人
