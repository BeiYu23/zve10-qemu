# 已排除的假设清单（堆损坏问题）

> 这份清单是为了**让后来者不必重复**我们已经做过并证伪的实验。
> 目标：定位 `im.elf` 里那处堆损坏（表现为 `free()/realloc(): invalid next size`，
> 最终 `abort`）。

---

## 一、现象定义

```
*** glibc detected *** /usr/bin/im.elf: free(): invalid next size (fast): 0x00020b80 ***
======= Backtrace: =========
libc.so.6(+0x57998)            ← free()
libc.so.6(cfree+0x55)
/usr/lib/libObj.so(+0x78ba2a)  ← 或 libJiritsu / libbackup / dlerror 路径
...
```

**关键：glibc 报的位置几乎总是「发现者」，不是「写者」。**
（`free`/`realloc` 只是第一个撞上坏 chunk 头的调用。）

---

## 二、❌ 已排除的假设（16 类，均实测证伪）

| # | 假设 | 反证方式 | 结论 |
|---|------|----------|------|
| 1 | 内存不足 | 把最大保留区从 680MB 缩到 64MB（可用 118MB → 734MB），崩溃地址**一字不变** | ❌ |
| 2 | `brk` 失败 | 追踪全部 13 次 `sys_brk`，堆顶停在 `0x37000`，行为完全正常 | ❌ |
| 3 | `mmap` 失败 | 内核 printk 追踪 24 次 >1MB 的 `mmap`，**全部成功**（含 3.16MB 的 MAP_FIXED） | ❌ |
| 4 | 缺库 / 缺符号 | 内核 `MISSSO` 追踪 96 条失败路径，逐条核对：全是 `//lib/*`（错目录）与 TLS/hwcap 变体，**无真正缺失**；228 库/1191 符号全量核对亦无缺失 | ❌ |
| 5 | 我们自己打的 `libMWF.so` 补丁 | A/B 对照：原版 ⇒ 崩溃指标全 0；故补丁已回退 | ❌ |
| 6 | `size` 计算错误 | `DefInh::GetFeatureIdMax()=3948` × `GetFactorTblMax()=200` × 4 = `0x303180`，**合法**，且正好等于静态表 `sm_refTbl` 的大小 | ❌ |
| 7 | jemalloc 覆盖了 `malloc` | `libjemalloc_ds.so.1` **不导出** `malloc/free/calloc/realloc` | ❌ |
| 8 | `MALLOC_OPTIONS` 影响 glibc | 该 libc **不解析** `MALLOC_OPTIONS`（`strings` 里没有） | ❌ |
| 9 | App 自己的 `memset(p,0,0x303180)` 写穿 | 把该 `memset` 长度改成 0，崩溃**一字不变** | ❌ |
| 10 | ✅ **大分配是触发点** | 把 `new[]` 尺寸从 3.16MB 改成 16 字节 ⇒ 原 `translation fault` **消失** | ✅ **确认** |
| 11 | ✅ **堆损坏独立且更早** | 小分配探针下 glibc **仍报** `free(): invalid next size` | ✅ **确认** |
| 12 | ✅ **`dlopen` 只是第一个检测者** | 回溯与另一处独立崩溃**逐帧一致** | ✅ **确认** |
| 13 | 用 `mcheck()` 定位 | 见 §三 —— **不可行** | ❌ |
| 14 | 崩溃点上游代码是写者 | `libObj.so+0x78ba2a` 上游循环逻辑是 `count=min(A,B)`，**数学上安全** | ❌ |
| 15 | 设备号不匹配 / 设备打不开 | 核对真机 devnode（`/dev/uipc` 251:207、`/dev/backup` 251:208、`/dev/dmpgles2` 248:29）+ 内核 `MISSDEV` 追踪 ⇒ **关键设备全部打开成功** | ❌ |
| 16 | 缺库导致 dlerror 溢出 | 补齐 110 个桩库后 `cannot open shared object` 归零，但损坏仍在 | ❌ |

---

## 三、❌ 为什么 `mcheck()` 这条路走不通

这份 `libc-2.11.2.so` **导出** `mcheck` / `mcheck_check_all` / `mcheck_pedantic`，
理论上能让 glibc 在**每次 malloc/free** 做一致性检查，从而在**越界写发生的那一刻**报错。

**试过的注入方式：**

| 方式 | 结果 |
|------|------|
| `/etc/ld.so.preload` 全局预加载 | 引导阶段的 `bootin.elf` 也 abort ⇒ **中断引导** |
| 预加载库用 `__progname` **只对 `im.elf` 生效** | 库确实被加载（App maps 里可见），App 也跑到 `MonDrv::Init`，**但 `mcheck` 没生效** |
| 同上，去掉 `mcheck_check_all()` | 仍无效 |

**⇒ 根本原因：`mcheck()` 必须在**任何 malloc 之前**调用。
而动态库的 `.init_array` 构造函数跑在 `ld.so` 完成重定位**之后**
（ld.so 早已 malloc 过）⇒ **`mcheck()` 静默失效。**

**⇒ 真要生效只能改 ELF 入口点（`_start`）——而 `im.elf` 已 strip、没有 `_start` 符号。**

**另外**：本 libc **没有编译进 `MALLOC_CHECK_` 环境变量支持**
（`nm` 无 `__malloc_check_init`、字符串表无 `MALLOC_CHECK_`），那条常见路也不通。

---

## 四、✓ 唯一验证有效的定位手段

**「在物理内存快照里找笔迹」** —— 本项目多次靠它破案：

1. 崩溃瞬间用 QEMU monitor 的 `pmemsave` 抓物理内存
   > ⚠️ 坑：**文件名不能用绝对路径**，`/` 会被 monitor 的表达式解析器吃掉
2. 在宿主机上搜**损坏区域附近的残留内容** —— 被越界写进去的东西常能直接认出写者
3. 实例：本项目靠这招在堆里读到一批 `dlopen` 错误消息
   （`cannot open shared object file` / `No such file or directory` / `undefined symbol`），
   从而定位到 **`libJiritsu.so` 的 `jiritsu_init` 会写坏堆**（跳过它后 App 大幅前进）

其他有效手段：
- **内核 `printk` 追踪**（比 gdb 可靠得多）——如 `mmap` 参数/返回值、失败的 `/dev/*` 与 `.so` 打开
- **A/B 对照**：每处改动都要有对照，包括内核里"为消错而加的兜底"

---

## 五、已经确认的「真凶」（逐层剥出来的）

| 层 | 真凶 | 处理 |
|---|------|------|
| 1 | `backup.ko` 取数据表指针为 NULL | 为可重定位模块**新增 `R_ARM_ABS32` 重定位** + 注入惰性初始化 trampoline |
| 2 | `MonDrv::Init` 的 `mmap` 因 `FixedAreaSize=0` 失败 | 从 `BACKUP.BAK` 把运行态值补进 `BACKUP.BIN` |
| 3 | **`libJiritsu.so` : `jiritsu_init` 写坏堆** | 跳过该函数 ⇒ **App 大幅前进到多线程 / 相机成像阶段** |
| 4 | 8 个缺失的 `dlopen` 目标（含 `libDMPGlslc.so` = **GLSL 着色器编译器**） | 造带正确符号的桩库 |

**⇒ 现象：每修掉一层，崩溃点就深入到下一层。** 这是本项目判断"可能还有若干层"的依据。

---

## 六、给后来者的建议

1. **先看 §五 的"已确认真凶"** —— 这些是实打实的进展，复现即可获益
2. **不要重复 §二 的 16 类实验** —— 直接跳过
3. **新思路优先**：
   - 在**写者侧**下断（而不是 `free`/`realloc` 侧）
   - 用**硬件 watchpoint** 监控可疑 chunk 的头部
   - 既然 App 已进入**多线程**阶段，注意**线程间共享结构**的竞争
   - `libosal_uipc.so` / `libosal_utm.so`（UIPC 消息栈）是当前最可疑的方向

---

## 七、后续补充（2026-10 更新）

### 7.1 ❌ 排除：备份数据缺 key 导致堆损坏

`IMCMN` 会打印大量 `not exit bkid` / `invaid data size`，一度被怀疑是堆损坏源头。
**把真机 `/setting/Backup.bin` 与我们的 `BACKUP.BIN.real`/`fixed` 做键级对比后确认：
那些 key（如 `0x00d50099`、`0x00fa0028`）在真机数据里本来就不存在**
⇒ 那些日志是**正常行为**（可选键缺失），不是损坏源头。

### 7.2 ⚠️ 一个容易踩的误判：内核回溯的符号大小可能错

实测回溯给出 `(osal_printf+0x98c/0xdd8 [backup])`，但查 `backup.ko` 符号表发现：

```
0000038c 00000030 t osal_printf     ← 只有 48 字节！
```

**⇒ `osal_printf` 不可能有 `+0x98c`。内核把符号大小算成了 `0xdd8`（错），
真正落点是模块偏移 `0xd18` 处**另一个函数**。
⇒ **不要盲信回溯里的 `+offset/size`，要用符号表核实。**

### 7.3 ⚠️ 两条启动路径的模块**不是同一份**

| 路径 | `backup.ko` | 大小 |
|------|-------------|------|
| 正常启动（eMMC 的 `/usr/kmod`） | 我们打过补丁的那份 | 73,764 B |
| 真机 CRAMFS initrd（给交互 shell 的那份） | initrd 自带 | 110,248 B |

**⇒ 用交互 shell 复现的崩溃**不等于**正常路径的崩溃。做功能验证要以正常路径为准。**
（运行时 `rmmod` 换模块实测会触发内核异常，不可行。）

### 7.4 重建 initrd 的坑（想改 initrd 的人注意）

- 解包会**丢失可执行位与符号链接** ⇒ 必须 `chmod 755` + 为 busybox 重建 `sh`/`mount`/… 链接
- 老内核（3.0.27）的 ext2-over-ramdisk 对 **新 e2fsprogs(1.47)** 生成的镜像
  **能挂载但无法 exec**（`Failed to execute /sbin/init` → `No init found`）
  ⇒ 即便权限/链接/特性（试过 `-O none`）都正确也如此；这条路易走死
- `ramdisk_size` 必须 ≥ 镜像大小（13MB 镜像需 `ramdisk_size=16384`）
- busybox 是精简版：没有 `head`/`id`/`df`；`chroot` 用 `busybox chroot`

### 7.5 ★ 重要区分：主崩溃 **不是** malloc 堆越界写（哨兵分配器实证）

用 interposition 写了个哨兵字节分配器（`guard/libguard.c`）：
每个分配多要 `16B 头 + 32B 尾哨兵`，释放时校验。

**结果：哨兵完好无损，而崩溃现象与不加哨兵时**完全一致**。**

⇒ **主崩溃（`pc=0x106`，跳到劣质函数指针）不是堆越界写。**

**⇒ 必须把两个问题分开：**
| 编号 | 现象 | 性质 |
|------|------|------|
| A | `pc=0x106`，线程内跳到坏函数指针 | **不是堆越界**（已证） |
| B | `libObj.so` 线程内 `free(): invalid next size` | 堆损坏（另一时机） |

**实现哨兵分配器的两个坑**（都踩过）：
- **绝不要在 `malloc` 里调 `dlsym`** —— `dlsym` 自身会分配内存 ⇒ 递归；
  实测会让 `bootin.elf` 崩在 `page translation fault`。改用 `__libc_malloc` 系列。
- 哨兵常量要用 **32 位**（ARM 32 位下 64 位常量被截断）。
