# ZV-E10 QEMU 复现 —— 交接包

> 生成时间：本 session 第 54 轮（累计约 220 小时自主工作）
> 主文档：`ZVE10_QEMU_BOOT_PROGRESS.md`（8600+ 行 / 102 节，含执行摘要）

---

## 一、30 秒速览

**已做到**：把 ZV-E10 一代的真机固件**完整搬进 QEMU 并跑起来** ——
内核启动、真机 eMMC（16 分区）、真机 initrd 挂成 root、**29 个平台模块全部加载**、
相机 App（`/usr/bin/im.elf`）持续运行、**监视器子系统 `MonDrv` 初始化成功**、
**App 的用户态输出直接显示在串口上**，并能在串口上与 guest 双向交互。

**未做到**：
1. App 仍有一处**堆损坏**导致崩溃（机制已完全查明，见 §四）
2. **画面显示**需要 Mali GPU 模型或改渲染路径（见 §五）

---

## 二、一条命令复现

```sh
<repo>/run_qemu_zve10.sh
```

**预期串口输出（关键行）**：

```
eMMC: 1888MiB
nflasha1 .. nflasha23                       ← 16 个分区
VFS: Mounted root (ext2)
Modules linked in: devno dmac ldec emmc osal_utm osal_uipc utimer backup
  input i2c sircs stream stream2 usb_portMonitor usbg_sen usb_extcmd kikilog
  dmm upm grm_ma grm_gles accel mag compass mcmn_drv ms_drv mmc_drv
  dma330 codec_drv                          ← 29 个模块
 FixedAreaTop  : 20098000                   ← App 自己打印
 FixedAreaSize : 0007C000
 Physical Addr : 200EB400
 Logical  Addr : 0x481e400
 Mon Buf Size  : 00008000                   ← MonDrv 初始化完成
```

---

## 三、全部关键修复（逐条可复现）

### 内核侧（5 项）

| 补丁 | 内容 | 作用 |
| --- | --- | --- |
| `kernel_adrs_conv_fallback.patch` | `adrs_conv.c` 地址换算兜底（**正确换算 `pa + VA_DDR0`**，不是一律返回基址） | 消除一串 NULL 解引用 |
| `kernel_pci_enable.md` / `kernel_kconfig_pcie_qemu.patch` | 开启 `CONFIG_PCI` + Kconfig 放开 `CXD900X0_PCIE2_DME` | 修好 `mmc_drv` 的 `Unknown symbol pci_*` |
| `kernel_tty_visible_qemu.patch` | `amba2.c`：QEMU 下强制 console `txrx_enable=1` | ★ **让 App 的用户态输出可见** |
| （追加）`mmap_c_qmmap_trace.c` | `sys_mmap_pgoff` 里加大尺寸 mmap 追踪 printk | 诊断用（可移除） |

> 真机 `AMBAUART_CONSOLE_ENABLE_DEFAULT=0` 是**故意**的（真机 `console=ttynull`，
> 用户态输出走 klog/em_console）。QEMU 下强制打开只为调试。

### QEMU 模型侧（2 项）

| 补丁 | 内容 |
| --- | --- |
| `sony_qemu_bionz.patch` | `hw/arm/bionz.c`：boot_size=0x200000、WBI 7 段装载、DDR 1G+256M |
| `sony_qemu_sd_cid.patch` | `hw/sd/sd.c`：CID/CSD/EXT_CSD、CMD8/CMD6、**CMD23 块数掩码 `& 0xFFFF`** |

### 镜像侧

**`build_patched_emmc_safe.sh`** —— **安全构建脚本**（务必用它，不要手工 `debugfs rm/write`）：

```
1) debugfs -R "rdump / <WORK>/tree" 真机 nflasha15.img      → 908 个文件
2) 宿主机 Python 直接改字节打补丁：
     lib/libMWF.so            保持原版（★ 见 §六 教训）
     lib/libOnDemandLoader.so 0x5c2c / 0x5784 → movs r0,#0; bx lr
     lib/libInfraAccessLog.so 0x13a0 → 立即返回
     kmod/{usbg_sen,upm,mag,compass,accel}.ko  .init.text → 立即返回
     kmod/udate.ko 0xbb8、kmod/backup.ko 0x950
     kmod/backup.ko 0x31fc → ★ 惰性初始化 trampoline（含 ELF 重定位手术）
3) 逐项校验存在性与大小，任一缺失立即退出
4) mke2fs -d 重新打包 → 写回整盘镜像
```

### 数据侧

**`BACKUP.BIN.fixed`** —— **`MonDrv::Init` 成功的关键**：

```
真机 BACKUP.BIN : key 0x00fa0028 = 0            key 0x00fa005d = 0
真机 BACKUP.BAK : key 0x00fa0028 = 0x20098000   key 0x00fa005d = 0x0007C000
⇒ 把 BAK 的运行态值补进 BIN（文件偏移 0x0bda40 / 0x0bdb14）
```

### 模块侧

**`backup_lazy_final.ko`** —— 在 `backup.ko` 里：
1. 新增一条 `R_ARM_ABS32 .bss` 重定位（把 `.rel.text` 搬到文件末尾并追加）
2. 在 `.text` 空闲区（VMA `0x9700`）注入 trampoline：
   `Data_SearchId` 入口跳过去，若 `bss+4 == 0` 则先调 `Data_init(0x62E80000)`
3. 叠加 `0x950` 空写 → `nop`

---

## 四、剩余阻塞 A：App 堆损坏（机制完全查明）

**调用链**：

```
im.elf 的 dlopen 封装 (0xb4fc)
 └─ dlopen("libMWF.so") → dlsym → MWF::InhInfo::Initialize()   (libMWF+0x1f498)
     ├─ DefInh::GetFeatureIdMax() = 3948      (libSysDef.so 常量, 0xd82)
     ├─ DefInh::GetFactorTblMax() = 200       (libSysDef.so 常量, 0xd7a)
     ├─ size = 3948×200×4 = 0x303180 = 3.16MB  ← 正好 = 静态表 sm_refTbl 的大小
     └─ operator new[](0x303180) → libstdc++ → libc malloc
        └─ __libc_malloc → _int_malloc（先走 arena）
             └─ 用**size 字段被夸大到 ≈3MB 的块**"成功"满足请求
                  └─ chunk split：把新块头写到 0x20820 + 0x303188 = 0x3239a8
                       └─ ★ 堆外（堆仅 0x16000–0x37000）⇒ section translation fault
```

**数值验算**：`0x3239a8 − 0x20820 = 0x303188` ✓（正是该请求的对齐尺寸）

**已排除**（逐条实测）：

| 假设 | 反证 |
| --- | --- |
| 内存压力 | 734MB 可用下崩溃地址一字不变 |
| `brk` 失败 | 13 次 brk 全部正常（堆顶 0x37000） |
| mmap 失败 | **24 次 >1MB 映射全部成功**（含 3.16MB 的 MAP_FIXED） |
| dlopen 失败 | 目标库都在 |
| `libMWF` 补丁 | 已回退原版 |
| size 计算错误 | 3948×200×4 合法 |
| jemalloc 覆盖 malloc | `libjemalloc_ds.so.1` 不导出 `malloc` 族 |
| `MALLOC_OPTIONS` | `libc` 不解析该变量 |
| `memset(p,0,0x303180)` 写穿 | **长度置 0，崩溃一字不变** |

**⇒ 收敛结论**：堆在 `InhInfo::Initialize` **之前**就已被某处越界写破坏，
使 arena 出现 ≈3MB 尺寸的块。

**⇒ 下一步建议**：
1. **多时间点物理快照对比**（QMP `pmemsave`，**文件名不能用绝对路径**），
   找第一个 size 字段异常的块；**该块的前一个块**即越界写的源头
2. 或**补丁 `libc+0x59c44`**：在 `r0 = r4 + r2` 后加边界检查，越界则跳回 `sysmalloc`
   （最小侵入的绕过，可立刻验证"是否就是这条路径"）

---

## 五、剩余阻塞 B：显示需要 Mali（目标 3 的真正瓶颈）

```
App → /dev/dmpgles2 (248,29) → grm_gles.ko「SUGILITE Driver」(DMP/Sony)
     → grm_ma.ko（显存） → Mali GPU 硬件
```

**QEMU 的 `cxd90045` 模型没有 Mali/Sugilite。**
⇒ 两条路：① 在 QEMU 里建模 Mali（研究级工作量）
② 在 App 侧拦截 GLES 调用（`libObj.so` 里 196 个入口）改用软件渲染

---

## 六、⚠️ 六条重要教训（避免重走弯路）

1. **没有内部约定信息时不要做字节级补丁**
   —— 曾在 `libMWF.so` 上打补丁，制造出更难查的一串崩溃，白追十几轮
2. **每处改动都要 A/B 对照**（包括内核里"为消错而加的兜底"）
3. **`debugfs` 原地 `rm`+`write` 替换文件不可靠**
   —— 必须回读校验；曾因吞掉 stderr 而**静默丢掉 `libMWF.so`**
4. **大文件不要写 `/tmp`** —— tmpfs 会满，导致构建/调试静默失败
5. **QEMU monitor 的 `pmemsave` 文件名不能用绝对路径**（`/` 会被当表达式）
6. **gdb 的限制**：`commands` 块内 `dump` 在 target running 时失败；
   绝对地址断点对**基址每次变化的用户态库**无效；
   长会话（>200s）会失效 ⇒ **优先用内核 printk 追踪**

---

## 七、建立的四项观测能力（本项目的方法论核心）

| 能力 | 说明 |
| --- | --- |
| **用户态输出可见** | `amba2.c` QEMU 分支 ⇒ App 每层失败自己"打字"出来 |
| **物理内存快照 + 离线分析** | QMP `pmemsave` + 宿主机 chunk 链遍历 |
| **内核 printk 追踪** | 比 gdb 可靠（用它证实了 mmap 全部成功） |
| **完整逆向知识** | 备份子系统完全透明（格式/算法/长度语义/两条调用路径） |

---

---

## 九、★ 最新进展（Round 76，本 session 最新）

### 9.1 已固化到构建脚本的关键补丁

| 补丁 | 作用 |
| --- | --- |
| `libJiritsu.so` 的 `jiritsu_init`（`0x3be4`）跳过 | **去掉 App 早期最大的堆损坏源** |
| **110 个桩库**（含 **dlsym 符号**） | 消除 `dlopen`/`dlsym` 失败 ⇒ 消除 dlerror 溢出 |
| `backup.ko` 惰性初始化 trampoline + `0x950` nop | 打通模块初始化链 |
| `BACKUP.BIN.fixed` | `MonDrv::Init` 完整成功 |

**⇒ 一条命令复现：`<repo>/run_qemu_zve10.sh`（配合 `build_patched_emmc_safe.sh` 生成的镜像）**

### 9.2 崩溃点后移轨迹（"剥洋葱"进度）

| 阶段 | 崩溃点 | 层次 |
| --- | --- | --- |
| R1–R50 | `MonQueue` / `backup_suspend` / `MonDrv` / `malloc` | 内核 + App 极早期 |
| R62 | `libMWF.so` dlerror 溢出 | App 早期 |
| R72 | **`libJiritsu.so : jiritsu_init`** | App 早期日志 |
| **R76** | **`libObj.so + 0x78ba2a`（`THREAD_INFRA2` 线程内）** | **App 主框架 · 多线程** |

**⇒ 当前状态：**
- **33 个模块加载**（含 `liro`/`hdmi`/`cec`）
- **App 已进入多线程阶段**（`THREAD_INFRA2` pid 133）
- **加载 161 个库**
- 剩余：`libObj.so` 线程内的一处 `free(): invalid next size`（`0x2c5e8`）

### 9.3 下一步（给接手者）

1. **反汇编 `libObj.so+0x78ba2a`**（21MB 库，有符号表），看它释放的缓冲由谁分配
2. **定位线程入口**：`libpthread+0x509c` 是线程启动桩 ⇒ 找对应的 `pthread_create`
3. **"物理快照找笔迹"**：对损坏地址 `0x2c5e8` 附近找残留内容
   （§62 与 §104 都用这招破过案）
4. **显示链路**：`libDMPGlslc.so`（GLSL 编译器）与 `libdmpNative.so` 已确认是 DMP/GPU 体系
   ⇒ 目标 3 必须建 Mali 模型或拦截 GLES（见 §五）

---

## 八、文件清单

| 文件 | 说明 |
| --- | --- |
| `ZVE10_QEMU_BOOT_PROGRESS.md` | **主文档**（8600+ 行，102 节，含执行摘要） |
| `ZVE10_QEMU_HANDOFF.md` | 本交接包 |
| `run_qemu_zve10.sh` | **一条命令复现** |
| `build_patched_emmc_safe.sh` | 安全镜像构建（务必用它） |
| `BACKUP.BIN.fixed` | 关键数据修复 |
| `backup_lazy_final.ko` | 惰性初始化 trampoline |
| `boot_mondrv_ok.log` | 最佳引导日志 |
| `ab_pristine_libMWF.log` | A/B 对照（原版 libMWF 下崩溃指标全 0） |
| `qmmap_trace.log` | 24 次大尺寸 mmap 全记录 |
| `app_backup_keys.txt` | App 查询的 553 个 backup key |
| `datasaerchid_calls_553.txt` | 553 次 Data_SearchId 调用（含调用者） |
| `kernel_*.patch` / `adrs_conv_qemu.c` | 内核补丁（5 项） |
| `sony_qemu_*.patch` | QEMU 补丁（2 项） |
| `stubs/gen_all_stubs.py` + `stubs/generated/` | 批量桩库生成器（102 个） |
| `<build>/snp/` | 崩溃瞬间物理快照（192MB） |
