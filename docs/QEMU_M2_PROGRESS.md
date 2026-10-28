# QEMU M2 移植实验 —— 进度台账（跨轮记忆锚点）

> **每次实验后必须更新此文件**：镜像 md5、串口关键行、结论、下一步。
> 目标：在 QEMU 里抓帧确认 **新菜单画面出现**。

## 0. 环境（固定，勿改）

```
工作区: /mnt/build/m2qemu/
QEMU  : /mnt/build/m2qemu/qemu-system-arm   md5 320155640ef7
        (= /mnt/build/work-q/qemu-11.1.2/build-zve10/qemu-system-arm，同一二进制)
启动  : ./start-m2.sh   （-M zve10 -cpu cortex-a9 -smp 4 -m 1024
                        cfg.uart=3，双 serial → out/<名>/serial0.log
                        无 -initrd，cpio 内嵌在 Image 里）
工具链: /mnt/build/ref-chocotan-zve10/work/armv7-eabihf--glibc--stable-2020.08-1/bin/arm-linux-
        ⚠️ 编译前必须 export LD_LIBRARY_PATH=<toolchain>/lib（否则 cc1 找不到 libmpfr.so.4）
内核源: /mnt/build/ref-chocotan-zve10/work/linux-kernel   （只读引用）
构建体: /mnt/build/m2qemu/work/kbuild-ui2/               （2.1 GB，可增量）
结构化: CONFIG_INITRAMFS_SOURCE=<指向我的 ui.cpio>
```

**复现命令：**
```bash
cd /mnt/build/m2qemu
IMAGE=work/Image.xxx SECONDS_MAX=300 SMP=4 OUT=out/xxx ./start-m2.sh
# 一次只能一台 QEMU；跑前 pgrep -x qemu-system-arm 确认
```

## 1. ★ 硬规则（踩过的坑）

| # | 规则 |
|---|---|
| 1 | **改 cpio 后必须 `rm -f kbuild-ui2/usr/initramfs_data.o` 再 make Image**，否则跑的是旧代码 |
| 2 | 校验 `strings Image \| grep -c <标志>` 确认改动进了镜像 |
| 3 | **cpio 打包必须排除备份目录**（`usr.e10bak` 曾被打进去，cpio 从 240 MB 涨到 478 MB） |
| 4 | **一次只跑一台 QEMU**（双实例会抢 CPU，时序敏感的服务初始化会超时） |
| 5 | 抓帧地址 = `guest_phys 0x22c48000`（640×480×2，RGBA4444）；宿主地址 = QEMU 进程里 **≥900 MB 的 rw-p 映射基址 + 0x22c48000** |
| 6 | 判"有画面"用**采样非零字节数**：E10 基线 ≈ 3855，全黑 = 0 |
| 7 | **`libkeyinject.so` 必须移除**（拦截 `osal_rcv_msg_tmo` 改变行为，导致到不了菜单） |

## 2. 已构建的镜像

| 镜像 | 大小 | md5(前12) | 内容 | 结果 |
|---|---|---|---|---|
| `work/Image.e10clean` | 242,898,308 | `8ce8992310a3` | E10 原始 usr + libuiobserve，无 keyinject | ✅ **61/61 init + 帧非零 3855（菜单出来了）** |
| `work/Image.m2obs` | 255,473,028 | `aad0287313e2` | **M2 混合 usr** + libuiobserve | ⚠️ 59/59 init，`Application_System_Init` 返回 0，**帧全零** |
| `work/Image.m2arg2` | 255,481,220 | — | M2 混合 usr + **我的 argtrace（dlopen 日志）** | ❌ 那次 QEMU 卡在内核初始化，未取得数据 |

## 3. ★ 已确证的事实

### 3.1 M2 栈比真机症状走的远得多
```
UI_INIT_ENTER=59  UI_INIT_RETURN=59              ← 59 个服务全部初始化成功
UI_RESOLVE  Application_System_Init = 0x119a9271
UI_INIT_ENTER Application_System_Init arg=0x4102601
UI_INIT_RETURN Application_System_Init result=0  ← 成功返回
```
**⇒ 加载链通、绘制链断。**

### 3.2 `arg` 实测值（argtrace 直接打印）
```
ARGTRACE enter arg=0x4005601 str="AppConfig.so"
ARGTRACE return 0x0
```

### 3.3 libIMDB 注册表项（字节位置）
```
appFwBoot.so                    @0x55DC
Application_System_Init         @0x55E9
AppConfig.so                    @0x5601   ← 传给 init 的配置串
/usr/local/lib/AdjChkConfig.so  @0x5673
```

### 3.4 ★ 更正：`arg` 不是分支判据
**`appFwBoot.so` 的 `.rodata` 顺序：**
```
AppConfig.so → appFw.so → Application_Start → AdjChkConfig.so → app_adjchk_ui::main_loop → AppFw
```
**正确理解（待反汇编最终确认）：**
```c
if (arg == "AppConfig.so")   // 正常
    dlopen("AppConfig.so") → dlopen("appFw.so") → dlsym("Application_Start")
else                          // 调整模式
    dlopen("/usr/local/lib/AdjChkConfig.so") → dlsym("app_adjchk_ui::main_loop")
```
**⇒ 昨天"传错参数导致加载错 UI"的假设【撤回】。`AppConfig.so` 是正常配置。**

### 3.5 线程状态对比（判据）
| | E10 基线 | M2 |
|---|---|---|
| `im.elf` | 有 8 秒 CPU 时间 | 停在 `osal_rcv_msg_tmo` |
| 线程 wchan 分布 | **875 × `cond_wait`**（菜单空闲态） | 43 × `osal_rcv_msg_tmo` + 34 × `futex`，**零 `cond_wait`** |
| 帧缓冲 | 非零 3855 | **0** |

## 4. 当前断点范围

`Application_System_Init` 返回 0 之后，线程执行 `Main()`：
```
uxengine_init2()                                   ← libObj 提供
initializeArena()
Kernel::sys_setCurrentDirectory("/usr/share/app/")
AreaCheckExtensionManager::setCheckDataPath("/usr/share/app/area_check_data.dat")
AreaCheckExtensionManager::setLogPath("/tmp/area_check_log.dat")
GraphicsEngine::setOverflowResizeFontAttribute(28, 21)
GraphicsEngine::setOverflowResizeLayoutAttribute(0x22)
ResourceManager::setDatabase("global.xdb")
ResourceManager::sys_setFontNameWithNamespace(0x33)
entry()    ← appFw.so 的 Application_Start
```

**四种可能（未区分）：**
```
① appFw.so dlopen 失败
② uxengine_init2() 内部失败（依赖 /dev/dmpgles2、dmm.ko）
③ setDatabase("global.xdb") 失败（cwd 问题）
④ Application_Start 内部早退
```

## 5. 下一步（一次运行即可区分 ①②）

**跑 `Image.m2arg2`，看 `ARGTRACE dlopen` 行：**
```
出现 dlopen path=/usr/lib/appFw.so -> 0xNNNN   ⇒ ① 通过，断点在 Main() 后面
出现 dlopen path=/usr/lib/appFw.so -> (nil)    ⇒ ① 失败，查 errno/符号
完全没有 appFw.so 这行                          ⇒ 卡在 AppConfig.so 之前
```

**tracer 源码：`/mnt/build/m2qemu/src/argtrace.c`（导出 `dlopen`+`dlsym`，只记录 appFw/libObj/AdjChk/AppConfig 相关）**

## 6. 其他资源

- 混合 usr 树：`/mnt/build/fw/port_m2/out/usr`（238,652,994 B / 858 文件）
- E10 原始 usr 备份：`/mnt/build/m2qemu/usr.e10bak`
- 对方工作区（**只读，勿改**）：`/mnt/build/ref-chocotan-zve10/`

---

## ★ 2026-10-28 17:20 重大进展：找到并修复 appFw.so 加载失败

### 根因（链式）
```
appFw.so
  └─ DT_NEEDED libNetCommon.so        ← 混合树保留了 E10 版 (md5 c3f505a0)
       └─ DT_NEEDED libmprctrl.so     ← 只存在于 ZV-E10，ZV-1M2 根本没有
            ⇒ 混合树没带 libmprctrl.so
  ⇒ dlopen("appFw.so") 返回 NULL
  ⇒ Application_System_Init 仍返回 0（不检查失败）→ 静默
  ⇒ 真机现象：无 UI、无日志、无报错
```

### 修复
```
把 initramfs-ui/usr/lib/libNetCommon.so 换成 ZV-1M2 版 (md5 1444fb66348e)
⇒ 其 DT_NEEDED 不含 libmprctrl，全部可解析
```

### 验证（itraces 实测）
| 阶段 | 结果 |
|---|---|
| 修复前 | `ARGTRACE dlopen FAILED path=appFw.so err=libmprctrl.so: cannot open shared object file` |
| **修复后** | **`ARGTRACE dlopen path=appFw.so flags=0x101 -> 0xa9c48` ✅ 加载成功** |

### 当前状态
```
✓ 59/59 服务初始化
✓ appFwBoot.so 加载、Application_System_Init 调用返回 0
✓ appFw.so 加载成功（修复后）
✗ 帧缓冲仍全零（采样非零=0）
✗ 线程 cond_wait=0 / osal_rcv_msg_tmo=41（未进入菜单空闲态）
⇒ 第二层问题：加载成功但未渲染
```

### 镜像
| 镜像 | 说明 |
|---|---|
| `work/Image.m2fix2` | **当前最好版本**：M2 混合 usr + libNetCommon 修复 + argtrace |

### 踩坑记录（新增规则 8）
**`CONFIG_INITRAMFS_SOURCE` 必须指向当次打包的 cpio！**
我曾打包了 `ui.cpio.fix` 但配置仍指向 `ui.cpio.arg`，导致镜像里是旧内容、白跑一轮。
**⇒ 每次重编前先 `grep CONFIG_INITRAMFS_SOURCE kbuild-ui2/.config` 确认。**

### 下一步
1. 在 argtrace 中再 hook `Application_Start`，确认 appFw 的入口是否被调用
2. 若调用：查 `uxengine_init2` / `setDatabase` / 渲染循环
3. 若未调用：查 appFwBoot 建线程那一步

---

## ★ 2026-10-28 17:35 第二层：Application_Start 进入但阻塞在 futex

### 实测（Image.m2s2，加 Application_Start / uxengine_init2 探针）
```
ARGTRACE dlopen path=appFwBoot.so      -> 0xcacb0    ✓
ARGTRACE hooked Application_System_Init = 0xeff8271
ARGTRACE init enter arg="AppConfig.so"
ARGTRACE dlopen path=appFw.so          -> 0xcb010    ✓ 已修复
ARGTRACE hooked Application_Start      = 0x2e27b581
ARGTRACE init return 0x0
ARGTRACE >>> Application_Start ENTER                ← 入口被调用
（之后无输出，无 RETURN —— 在事件循环里，符合预期）
```

### 状态
```
im.elf 存活，wchan = futex_wait_queue_me      ← ★ 阻塞在 futex（pthread 互斥/条件变量）
帧缓冲采样非零 = 0                             ← 仍未有画面
无 error/fail/panic
```
**⇒ `Application_Start` 跑起来后立刻阻塞在一个 futex 上，等一个永远不来的东西。**

### 与 E10 基线对比
| | E10 基线 | M2 |
|---|---|---|
| im.elf CPU 时间 | 8 秒 | 阻塞于 futex |
| 线程 wchan | 875 × `cond_wait` | 41 × `osal_rcv_msg_tmo`，0 × `cond_wait` |
| 帧 | 非零 3855 | 0 |

### ★ 新的重要疑问
**E10 基线是**带 `libuiobserve`**跑的，而 `libuiobserve` 里有 `try_original_menu()`、
`software_lcd_init()`、`observe_draw/notify` 等主动驱动逻辑。
**⇒ 有可能"画面出现"本身就依赖 libuiobserve 的驱动，而不是 M2 真的在画。**

**⇒ 必须做对照：E10 基线**去掉 libuiobserve** 再跑一次。**
- **若也出画** ⇒ harness 不依赖插桩，M2 的 futex 阻塞是真实问题
- **若不出画** ⇒ E10 的画面是插桩驱动的，M2 的结论需要重新解释

### 下一步（按优先级）
1. **对照实验：E10 usr + 无 libuiobserve → 抓帧**（决定后续所有结论的有效性）
2. 若 1 显示 harness 能自绘：查 `Application_Start` 等的那个 futex 是谁该释放
3. 探针扩展：hook `GraphicsEngine::drawFramebuffer` / `WidgetSystem::updateFrame` 看是否被调用

### 镜像
| 镜像 | 说明 |
|---|---|
| `work/Image.m2fix2` | M2 + libNetCommon 修复（无入口探针） |
| `work/Image.m2s2` | **当前最新**：+ Application_Start / uxengine_init2 探针 |
| `work/Image.e10clean` | E10 基线（带 libuiobserve），61/61 + 有画面 |

---

## ★★★ 2026-10-28 17:45 对照实验：harness 有效性已证明

### 实验
**E10 原始 usr + 完全无插桩（`/init` 无 LD_PRELOAD，usr/lib 无 libuiobserve/argtrace）**

### 结果
| 配置 | 帧采样非零 |
|---|---|
| E10 + libuiobserve | 3855 |
| **E10 无任何插桩** | **3879** ✅ |
| M2 混合（已修 libNetCommon） | **0** ❌ |

**⇒ harness 不依赖 libuiobserve 的驱动逻辑（`try_original_menu`/`software_lcd_init` 等）。
⇒ **M2 的"帧全零"是真实问题，不是插桩伪影。**
⇒ 前面关于 libNetCommon/libmprctrl 的修复是**真实有效的**，且后续结论可信。**

### 当前确定的状态
```
✓ 59/59 服务初始化
✓ appFwBoot.so 加载、Application_System_Init 调用返回 0
✓ appFw.so 加载成功（libNetCommon 修复后）
✓ Application_Start 被调用并进入
✗ 帧全零
✗ im.elf 阻塞于 futex_wait_queue_me
✗ 无任何 error/fail/panic 输出
```

### 剩余问题（唯一）
**`Application_Start` 在入口后立刻阻塞在一个 futex 上，等一个永远不来的释放者。**

### 下一步（下一轮从这里开始）
1. **确认 `Application_Start` 里第一条阻塞调用是什么**
   - 扩展 argtrace：hook `pthread_mutex_lock` / `pthread_cond_wait` / `osal_wai_sem_tmo` / `osal_rcv_msg_tmo`
   - 或 hook `GraphicsEngine::drawFramebuffer` 看是否被调用过
2. **对比 E10 里谁释放那个 futex**
3. **检查是否缺少某个 `/dev` 或资源导致等待**

### 镜像清单（当前）
| 镜像 | 内容 | 帧 |
|---|---|---|
| `Image.e10clean` | E10 + libuiobserve | 3855 ✅ |
| `Image.e10ctl` | **E10 无插桩** | **3879 ✅（对照基准）** |
| `Image.m2fix2` | M2 + libNetCommon 修复 | 0 ❌ |
| `Image.m2s2` | 同上 + 入口探针（当前最有信息量） | 0 ❌ |

### 树状态（重要，下一轮别搞混）
```
initramfs-ui/usr        = E10 原始（对照实验用）
/mnt/build/m2qemu/usr.m2_v2     = M2 修复版 usr（含 libNetCommon 1444fb66）
/mnt/build/m2qemu/usr.e10bak    = E10 原始 usr 备份
```
**恢复 M2 树：`rm -rf initramfs-ui/usr && cp -a /mnt/build/m2qemu/usr.m2_v2 initramfs-ui/usr`**

---

## ★ 2026-10-28 17:40 第三层：iim.elf 无关；发现 CPU 亲和性可能是 harness 限制

### 实验 A：换 M2 版 im.elf
```
initramfs-ui/usr/bin/im.elf = ZV-1M2 版 (d2f47a82, 23,240 B)
依赖全部可解析（libjemalloc_ds/libIMDB/libbackup/libosal_*/libSTMcmdexecute/libssl3/libcrypto3…）
结果：与 E10 版 im.elf 完全相同 —— Application_Start ENTER，无后续输出，帧仍全零
⇒ im.elf 版本不是第二层原因（撤回"E10 im.elf 不发 M2 需要的消息"假设）
```

### 实验 B：线程状态分布（M2）
```
futex_wait_queue_me : 35
osal_rcv_msg_tmo    : 41
do_wait             :  4
worker_thread       : 10
（E10 基线对照：cond_wait 875）
⇒ M2 有 35 个线程阻塞在 futex，结构上与 E10 的"菜单空闲态"完全不同
```

### ★★ 新发现：`osal_set_cpu(thread, 15)`
**反汇编 `appFwBoot.Application_System_Init` 得知：建线程后执行 `osal_set_cpu(thread, 15)`——绑到 **CPU 15**。**

**而 QEMU `-M zve10` 的 CPU 上限：**
```
$ qemu-system-arm -M zve10 -smp 16 → Invalid SMP CPUs 16. The max CPUs supported by machine 'zve10' is 4
$ qemu-system-arm -M zve10 -smp 8  → Invalid SMP CPUs 8.  The max CPUs supported by machine 'zve10' is 4
```

**⇒ 该机型最多 4 核（0-3）。`osal_set_cpu(...,15)` 在此环境下必然失败。**

### ★★★ 推论（重要，可能改变整个判断）
**如果 `osal_set_cpu(15)` 失败会影响线程调度/同步，那么 M2 的 futex 阻塞**可能是 harness 的 CPU 拓扑限制导致的，而不是移植本身的问题**。**

**⇒ 这意味着：当前 QEMU 环境可能**无法**验证 M2 栈的渲染能力。**

### 待办（下一轮）
1. **确认 `osal_set_cpu` 失败是否致命** —— 反汇编 `osal_set_cpu` 实现，看返回值是否被检查
2. **若失败被忽略** ⇒ futex 阻塞另有原因，继续查锁
3. **若失败致命** ⇒ 需要给 `zve10` 机型加 CPU 数（改 QEMU device 定义），或用 `-M cxd90045`（对方另一套，可能支持更多核）
4. 备选：interpose `osal_set_cpu` 让它始终返回成功，看是否能解开 futex 阻塞

### 镜像
| 镜像 | 内容 | 帧 |
|---|---|---|
| `Image.e10ctl` | E10 无插桩（**对照基准**） | 3879 ✅ |
| `Image.m2s2` | M2 + 入口探针 | 0 |
| `Image.m2im` | 同上 + M2 版 im.elf | 0 |
| `Image.m2smp` | 同上但 E10 im.elf，准备测 -smp（未跑成） | — |

### ★ 2026-10-28 17:50 排除 CPU 亲和性
**反汇编确认 `Application_System_Init` 尾段：**
```asm
1344: blx osal_create_waitable_thread
1348: movs r1, #15
134a: blx osal_set_cpu          ; 调用
134e: add.w r7, r7, #64         ; 直接恢复栈
1352: mov sp, r7
1354: ldmia.w sp!, {r4,r5,r6,r7,r8,pc}   ; 返回，无 cmp/bne
```
**⇒ `osal_set_cpu` 的返回值**未被检查，亲和性失败被忽略。
⇒ **CPU 拓扑（4 核 vs 期望的 CPU15）不是 futex 阻塞的原因。**
（上一节把它列为"可能改变判断"的怀疑，现已排除。）

### 下一轮起点（明确）
**M2 的 `Application_Start` 进入后，35 个线程阻塞在 `futex_wait_queue_me`。
需要定位它等的具体锁/资源。**

**建议手段（按代价排序）：**
1. **interpose `pthread_cond_wait` / `pthread_mutex_lock` / `osal_wai_sem_tmo`，
   打印调用者返回地址（`__builtin_return_address(0)`）**，与固件符号表对照定位
2. **interpose `GraphicsEngine::drawFramebuffer`（同名导出符号，LD_PRELOAD 可拦截）**，
   确认是否被调用过
3. **对比 E10 基线里同位置的行为**（E10 走 libObj 的 Application_System_Init，路径不同）
4. **检查 `/dev/dmpgles2`、`/dev/dmm` 是否可打开**（M2 的引擎更依赖 GPU）

### 已排除的假设（累计）
| 假设 | 状态 |
|---|---|
| appFw.so 加载失败 | ✅ 已修（libNetCommon→M2 版） |
| im.elf 版本不对 | ❌ 已排除（M2 版结果相同） |
| CPU 亲和性 CPU15 | ❌ 已排除（返回值未检查） |
| harness 依赖插桩 | ❌ 已排除（E10 无插桩也能出画 3879） |
| 参数加载错 UI | ❌ 已排除（"AppConfig.so" 是正常配置） |

---

## ★ 2026-10-28 18:05 第四层：画出阻塞信号量

### 探针工具（新建）
`/mnt/build/m2qemu/src/probe.c` → `probe.so`（7.7 KB），LD_PRELOAD interpose：
```
_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv     (转调 RTLD_NEXT)
_ZN2ux6wgtsys12WidgetSystem11updateFrameEv           (转调 RTLD_NEXT)
osal_wai_sem_tmo                                      (带 __builtin_return_address(0))
```
**加载方式：`LD_PRELOAD=/usr/lib/argtrace.so:/usr/lib/probe.so`**

### 实测输出（Image.m2pr）
```
PROBE osal_wai_sem_tmo #1 id=0x9401c3 tmo=-1 caller=0xb80d
PROBE osal_wai_sem_tmo #2 id=0x9401c3 tmo=-1 caller=0xb80d
...（连续 8 次，同一个 id、同一个 caller）
（无任何 PROBE drawFramebuffer / updateFrame 输出）
```

### 判读
```
★ 反复等待同一个信号量 0x9401c3，超时 -1（无限等）
★ caller 恒为 0xb80d
★ drawFramebuffer / updateFrame 一次都没被调用
```

**⚠️ 两个待验证点：**
1. **interpose 是否真的生效**（若 appFw 用非 dlsym 方式解析，或符号被本地绑定，则"没调用"可能是假阴性）
2. **caller=0xb80d 属于哪个模块**（低位地址，像 bootin.elf/im.elf 的静态区；需按加载基址换算）

### ⚠️ 本轮对照受限
**E10 基线（`Image.e10ctl`）是**无插桩**版本，日志里没有 `osal_sem.id` 记录，
所以无法直接对比 0x9401c3 在 E10 里的行为。**
**⇒ 下一轮需要：E10 + 仅 probe.so（不带 libuiobserve/argtrace），取得同口径对照。**

### 本轮另一个观察
**加了 probe.so 后，这次运行**没有**到达 `Application_Start`（无 ARGTRACE 输出），
说明 probe.so 拖慢了启动或干扰了流程。**
**⇒ 下一轮要么只挂 probe.so（不挂 argtrace），要么延长 SECONDS_MAX。**

### 下一轮起点
1. **E10 + 仅 probe.so** → 取 0x9401c3 的同口径对照（E10 是否也等它？等多久？）
2. **验证 interpose 有效性**：在 probe 里加一个"构造函数打印已安装"；
   或选一个必定被调用的符号（如 `malloc`）验证 interpose 机制本身
3. **换算 caller=0xb80d** → 定位到具体库/函数
4. 若确认 0x9401c3 是 UI 就绪握手信号量 → 找谁该 signal 它

### 镜像
| 镜像 | 内容 | 结果 |
|---|---|---|
| `Image.e10ctl` | E10 无插桩（对照基准） | 帧 3879 ✅ |
| `Image.m2s2` | M2 + argtrace | Application_Start ENTER，帧 0 |
| `Image.m2pr` | **M2 + argtrace + probe.so** | 卡在 0x9401c3 等信号量，未到 Application_Start |

---

## ★★ 2026-10-28 18:15 撤回：probe.so 破坏渲染，上一轮结论作废

### 静态验证（有效）
```
appFw.so 重定位类型：
  _ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv   R_ARM_JUMP_SLOT  ← 走 PLT
  _ZN2ux6wgtsys12WidgetSystem11updateFrameEv          R_ARM_JUMP_SLOT
  osal_wai_sem_tmo                                    R_ARM_JUMP_SLOT
⇒ LD_PRELOAD interpose 机制【原理上有效】，且 osal_wai_sem_tmo 探针确实触发过
```

### ❌ 但对照实验推翻了上一轮的解读
| 配置 | 串口 | 帧采样非零 |
|---|---|---|
| E10 无插桩（`Image.e10ctl`） | 865 KB | **3879 ✅** |
| **E10 + 仅 probe.so（`Image.e10pr`）** | **11.6 KB** | **0 ❌** |

**⇒ `probe.so` **本身把 E10 的渲染打死了**（串口从 865 KB 掉到 11.6 KB）。**

### ⇒ 因此【撤回】上一轮结论
**~~"M2 里 drawFramebuffer 从未被调用"~~—— 那是在 probe.so 破坏过的配置上测的，不成立。**
**~~"反复等待信号量 0x9401c3 是阻塞点"~~ —— 同样可能由 probe.so 引入。**

**⇒ 教训（第 10 条规则）：任何 interpose 探针都必须先在【能出画的 E10 基线】上验证"加了探针仍能出画"，
否则测量结果无效。这和"改 cpio 没重编"是同一类错误。**

### 当前可信的状态（未受探针污染）
```
✓ 59/59 服务初始化
✓ appFwBoot.so 加载、Application_System_Init 返回 0
✓ appFw.so 加载成功（libNetCommon 修复）
✓ Application_Start ENTER（Image.m2s2，仅 argtrace，未观察到行为异常）
✗ 帧全零
✗ 35 个线程 futex_wait_queue_me
```
**⇒ 目前唯一"干净"的观察工具只剩 `argtrace.so`（它只打印不转调改变行为），
但它只能看到 dlsym/dlopen 层，看不到绘制/锁。**

### 下一轮起点
1. **不要再用 probe.so**（除非先证明它在 E10 上不破坏渲染）
2. **找非侵入式观测手段**：
   - `oled`/`gdb` 附加到 QEMU 里读内存/寄存器（QEMU 支持 `-s` 或 gdbstub）
   - 或只在**已确认 AppStart 已进入之后**注入探针（延迟安装）
3. **回到最基础的判据**：E10 能画出帧，M2 不能 —— 差异在 M2 栈内部
4. 可考虑：把 M2 的 `libObj`/`appFw` 逐个换回 E10 版本做二分（定位是哪个库的问题）

### 镜像状态
| 镜像 | 内容 | 帧 |
|---|---|---|
| `Image.e10ctl` | E10 无插桩 | 3879 ✅（**唯一可信基准**） |
| `Image.e10pr` | E10 + probe.so | 0 ❌（证明 probe 有害） |
| `Image.m2s2` | M2 + argtrace（**行为未见异常**） | 0 |
| `Image.m2pr` | M2 + probe.so | 结果作废 |

---

## ★ 2026-10-28 18:35 第五层：排除"只是慢"，但暴露运行不稳定

### 实验：延长到 300 秒再抓帧
**Image：`Image.m2long`（M2 混合 + 仅 argtrace），SECONDS_MAX=420，300 秒时抓帧**

### 结果
```
帧采样非零 = 0                          ← 不是"慢"，300 秒仍无画面
但本次运行串口只有 11,712 B、模块 14 个、无任何 ARGTRACE / appFwBoot 记录
尾部是内核 Oops：
  do_exit → do_group_exit → __wake_up_parent → sys_exit_group
  Code: e1a01000 e59f000c eb0003f8 e3a03000 (e5833000)
  共 6 处 exception/panic/Oops 标记
```

### ★★ 关键观察：同一镜像行为不一致
| 运行 | 串口 | 到达阶段 | 结果 |
|---|---|---|---|
| `Image.m2s2`（更早） | 23,935 B | `Application_Start` ENTER | 帧 0 |
| `Image.m2long`（本次，同内容） | 11,712 B | 内核 Oops，未到应用层 | 帧 0 |

**⇒ 两次同样是 M2 混合 + argtrace，一次到应用层、一次内核崩。**
**⇒ **运行间不稳定**，可能是：**
1. **残留 QEMU 实例 / 并发编译干扰**
2. **内核 initramfs 大小接近某个界限（M2 树比 E10 大 ~12 MB）**
3. **QEMU 机型对 initramfs 大小/内存布局敏感**

**⇒ 这解释了为什么之前 m2arg2 那次也"卡在内核初始化"。**

### 已排除
| 假设 | 状态 |
|---|---|
| M2 只是渲染更慢 | ❌ 排除（300 秒仍 0） |

### 下一轮起点（做好稳定性再看功能）
1. **先解决可复现性**：每次跑前确认 `pgrep -x qemu-system-arm` 为空；
   跑一次基线 E10 确认仍 3879 帧，再跑 M2
2. **检查内核 Oops 的具体位置**（`Code:` 行 + 调用栈），判断是否与 initramfs 大小/内存有关
3. **若不稳定与 initramfs 大小相关**：尝试精简 M2 树（去掉 screenreader 等无关大文件）缩小 cpio
4. 稳定后再回到功能定位

### 镜像
| 镜像 | 结果 |
|---|---|
| `Image.e10ctl` | 3879 帧 ✅（基准） |
| `Image.m2s2` | Application_Start ENTER，帧 0（**最能说明问题的一版**） |
| `Image.m2long` | 内核 Oops，未到应用层（同内容，不稳定证据） |

### ★ Oops 精确定位（补充）
```
[<604622a8>] (sched_init_smp+0x0/0x110) from [<6045aa78>] (kernel_init+0xa8/0x1dc)
[<6045a9d0>] (kernel_init+0x0/0x1dc)  from [<6016242c>] (do_exit+0x0/0x6f4)
→ do_basic_setup
```
**⇒ 崩在 `sched_init_smp`（SMP 调度初始化），与日志里的 `CPU1/2/3: failed to boot: -38` 对应。**

**⇒ 这是**副 CPU 启不起来的间歇性失败**，属 QEMU 仿真时序/争用问题，不是 M2 移植问题。**

### 应对（下一轮验证）
1. **跑前确保 `pgrep -x qemu-system-arm` 为空**，并检查宿主机负载（`uptime`）
2. **改试 `-smp 1`**（单核，无 SMP 初始化 ⇒ 绕开这个崩溃点；代价是慢，但能先拿到功能层结果）
3. 每次跑完都跑一次 **E10 基准** 校验环境仍健康（应 3879 帧）

---

## ★★ 2026-10-28 18:45 第六层：找到干扰源 + 崩溃精确到 build_sched_domains

### ① 干扰源确认
**跑之前/运行中，**对方的 QEMU 正在同时运行**：**
```
pid=1088043 [02:39 98.0% CPU] /mnt/build/work-q/qemu-11.1.2/build-zve10/qemu-system-arm -M zve10 -smp 4 ...
  ⇒ 【对方的会话】，不是我自己的
pid=1088060 [02:39 90.5% CPU] /mnt/build/m2qemu/qemu-system-arm -M zve10 -smp 1 ...
  ⇒ 我的
```
**⇒ 两个 QEMU 各占满一核，宿主 4 核 ⇒ 严重争用。**

### ② 崩溃精确位置（`-smp 1` 也会崩！）
```
[<6015a7a0>] (build_sched_domains+0x0/0x818) from [<60462310>] (sched_init_smp+0x68/0x110)
[<604622a8>] (sched_init_smp+0x0/0x110)     from [<6045aa78>] (kernel_init+0xa8/0x1dc)
```
**⇒ 崩在 `build_sched_domains`（sched_init_smp 内部的分域构建）。**
**⇒ **即使单核 `-smp 1` 也崩** ⇒ 与 CPU 数量无关，是内核初始化期的内存/资源分配失败。**

### ★★★ 新假设（最有解释力）
**initramfs 是**解压到 ramfs** 的，**占用真实 RAM**：**
```
E10 树: 226 MB usr + 其余 ≈ 228 MB cpio   → 正常启动，帧 3879
M2  树: 238 MB usr + 其余 ≈ 240 MB cpio   → 崩在 build_sched_domains
```
**⇒ M2 的 initramfs 比 E10 大约 12 MB，可能把内核早期初始化（`build_sched_domains` 需要分配
per-CPU 数据与调度域）的内存压到了阈值以下。**

**⚠️ 另有旁证：对方也说 `-smp 2` 常卡、`-smp 4` 才快 —— 说明这套配置对内存/时序敏感。**

### 下一轮起点（按优先级）
1. **等对方 QEMU 停下再跑**（`pgrep` 确认只有我一台），先排除争用
2. **缩减 M2 的 initramfs 体积**验证内存假设：
   - 删掉混合树里 E10 专属的 4 个库（`libInfraCameraAllSetting/Profile`、`libInfraExtTransferApp`、`libclema`）
   - 或删掉确认不用的 `share/` 大文件
   - 目标：让 cpio ≤ E10 的 228 MB
3. **若缩减后能稳定启动** ⇒ 内存假设成立，再逐步加回必需文件
4. 备选：给 QEMU 加 `-m 2048`（若机型支持），直接排除内存不足

### 镜像
| 镜像 | 结果 |
|---|---|
| `Image.e10ctl` | 3879 帧 ✅（基准，228 MB cpio） |
| `Image.m2s2` | Application_Start ENTER，帧 0（**曾成功启动过**） |
| `Image.m2long` | build_sched_domains 崩溃（240 MB cpio） |

---

## ★★★★ 2026-10-28 19:00 第七层：不稳定的真正原因 —— 对方的 pkill 误杀

### 决定性证据
```
qemu.log: qemu-system-arm: terminating on signal 15 from pid 1088925 (pkill)
```
**⇒ 我的 QEMU 进程被一个 `pkill` 终止，不是崩溃、不是 Oops、不是内存不足。**

### 现状
```
我的 qemu: 2 个     对方 qemu: 3 个      ← 同时存在多个实例
```

### ★ 重新解释之前所有"不一致"
| 现象 | 原解释 | 真正原因 |
|---|---|---|
| `m2long` 崩 `build_sched_domains` | 内存不足假设 | **可能被 pkill 打断的假象** |
| `-smp 1` 也崩 | 与 CPU 数无关 | 同上 |
| 同镜像两次结果不同 | 运行不稳定 | **对方的 pkill / 多实例争用** |

**⇒ 之前第 5、6 层的"内存假设"**暂无证据支持**，应标记为未验证。**
**⇒ 在拿到一次**没有被外部 pkill 打断**的干净运行之前，所有关于 M2 栈的结论都要打折扣。**

### 必须做的（下一轮）
1. **和对方协调**：请他们把 `pkill` 收窄（只杀自己路径的进程），
   或约定"我在跑时他们不跑"
2. **我这边改成精确定位自己的进程**：
   `pgrep -f '/mnt/build/m2qemu/qemu-system-arm'`（而不是 `-x qemu-system-arm`）
3. **跑一次"干净运行"作为基准**：
   - 开始前：`pgrep -af qemu-system-arm` 记录所有实例
   - 运行中：每 30 秒记录一次进程列表，确认我的实例没被杀
   - 抓帧后再确认

### ⚠️ 方法论教训（第 11 条）
**当同一输入产生不同输出时，先检查"外部干扰"，再怀疑被测对象。**
**我之前直接跳到"内存不足"的解释，是因为没有先检查进程是否被外部终止。**

---

## ★★ 2026-10-28 19:15 第八层：那不是崩溃，是 WARNING；指向副 CPU 启不来

### 精确识别
```
<4>WARNING: at .../kernel/sched.c:8107 build_sched_domains+0x6c0/0x818()
[<6015e24c>] (warn_slowpath_null+0x0/0x2c) from [<6015ae60>] (build_sched_domains+0x6c0/0x818)
```
**⇒ 这是 `WARN_ON`（`<4>` 级别 + `warn_slowpath_null`），**不是 panic、不是 Oops 致命错误**。
**⇒ 内核会继续跑，之前的 `do_exit` 栈只是进程正常退出时的 dump，被我误读成崩溃。**

### 与已知线索串起来
```
日志开头:  CPU1: failed to boot: -38
           CPU2: failed to boot: -38
           CPU3: failed to boot: -38        ← 三个副核全部启不来
之后:      build_sched_domains 在 sched.c:8107 触发 WARN
```
**⇒ `build_sched_domains` 的这条 WARN 通常与**CPU 拓扑/cpumask 不完整**相关。
**⇒ 副核全失败 ⇒ 调度域只覆盖 CPU0 ⇒ WARN。**

### ★★★ 新的最强假设（机理清楚）
**OSAL/RTOS 层（`libosal_utm` / `libosal_uipc`）与 M2 的 `appFwBoot` 建线程机制，
可能依赖**多核调度**（`osal_create_waitable_thread` + `osal_set_cpu(15)`）。
**⇒ 仿真环境里副核启不来 ⇒ 调度域不全 ⇒ 那些线程建了但不被调度/同步异常
⇒ M2 应用起来但**永远不渲染**；E10 的线程模型容错，所以能出画。**

**⚠️ 这与"CPU 亲和性返回值未检查"不矛盾：返回值没检查不代表调度本身正常。**

### 待验证（下一轮）
1. **确认副核为什么 -38**（`-38 = ENOSYS`；可能 QEMU 机型没实现副核启动路径，或需要特定 boot 协议）
2. **看 `build_sched_domains` 之后内核是否继续**（serial 停在 11,712 B，说明后续卡住 —— 但可能在等别的东西）
3. **检查 `m2s2` 与 `m2long` 的差异**（s2 曾到 Application_Start，long 没有）：
   ```
   Image.m2s2    255,481,220 B   ui.cpio.s2    251,843,072 B
   Image.m2long  255,485,828 B   ui.cpio.long  251,847,680 B
   ⇒ 差 4,608 B，内容几乎相同，**行为却不同**
   ⇒ 更可能是"跑的时候环境不同"，而非镜像差异
   ```

### ⚠️ 方法论（第 12 条）
**看到调用栈 + `do_exit` 不要直接判定"崩溃"。
先看日志级别（`<0>` panic / `<3>` err / `<4>` warning）和是否有 `end trace`。
我把 `<4>WARNING` 误读成崩溃，白走了两轮。**

---

## ★★★★ 2026-10-28 19:30 第九层：Application_Start 是 6 次调用后返回的初始化函数

### 反汇编（72 B 全貌）
```asm
0054d580 <Application_Start@@Base>:
 54d580: ldr r1,[pc,#64]        ; 取一个字符串指针
 54d582: movs r2,#1
 54d584: push {r7,lr}
 54d586: sub.w sp,sp,#2208      ; 2.2 KB 栈帧
 54d58e: add r1,pc
 54d590: blx 0x92559c           ; ★ 调用 1
 54d59a: bl  0x54d154           ; ★ 调用 2
 54d5a6: bl  0x54d2b0           ; ★ 调用 3
 54d5ac: bl  0x54f664           ; ★ 调用 4
 54d5b0: blx 0x923c84           ; ★ 调用 5
 54d5b6: bl  0x54d538           ; ★ 调用 6
 54d5c0: pop {r7,pc}            ; ★ 返回
```

### ★ 重大推论
**`Application_Start` **不是事件循环**，是**短初始化序列：6 次调用后返回**。**

**而实测：**
```
ARGTRACE >>> Application_Start ENTER        ← 进入了
（无 <<< Application_Start RETURN）          ← 永远没返回
```
**⇒ **阻塞发生在这 6 个调用之一里面。** 而不是"Application_Start 自己进了循环"。

### 这也修正了之前的一个理解
**之前以为"Application_Start 是入口，进去就是主循环，所以没有 RETURN 正常"——错。
正常运行它应当**返回**，然后由 Main 返回、线程结束或转入别处。**

### 下一轮：定位阻塞在哪个调用
**6 个调用点已知（0x92559c / 0x54d154 / 0x54d2b0 / 0x54f664 / 0x923c84 / 0x54d538）。**

**方法（从便宜到贵）：**
1. **argtrace 里按返回地址打印**：hook 一个必定被调用的底层函数（如 `malloc`/`write`），
   记录 `__builtin_return_address(0)`，看最后落在哪个区间
2. **对 6 个调用点逐个反汇编**，看哪个是"等待"性质（调用 osal_wai_sem / pthread_cond_wait）
3. **用 QEMU gdbstub 附加**，卡住时读 PC 直接定位

**建议先做 2（纯静态，最便宜）：反汇编 0x92559c，看它是不是等锁/等消息。**

### 方法论（第 13 条）
**"函数没有返回 ⇒ 它是事件循环"是一个未经验证的假设。
反汇编只需要 72 字节就能证伪 —— 先看目标函数多大、是否含 `pop {...,pc}`，
比猜测便宜得多。**

---

## ★ 2026-10-28 19:45 第十层：6 个被调函数已分类

| 地址 | 形态 | 判读 |
|---|---|---|
| **0x92559c** | 反汇编乱码，`stmia r0!,{r2}` + `bx pc` —— 是 **ARM 态代码**（`blx` 切到 ARM） | 静态链接的 ARM 例程（可能是编译器内建/memcpy 类） |
| **0x54d154** | 小函数（~20 条），写 vtable 指针 `str r3,[r5,#0]` | 构造 / vtable 初始化 |
| **0x54d2b0** | 中等，连续 3 个子调用 `0x54d094/0x54d0b0/0x54d0cc` | 初始化序列 |
| **0x54f664** | ★ **栈帧 2048+28 字节**（`sub.w sp,sp,#2048`），大函数 | **最可疑：UI 主体初始化** |
| **0x923c84** | ARM 态代码（同 92559c 形态） | 静态链接 ARM 例程 |
| **0x54d538** | 小函数，多个 vtable 写入 | 构造 / vtable 初始化 |

### ★ 结论
**6 个调用里，`0x54f664` 是唯一的大函数（2 KB 栈帧），最可能是 UI 初始化主体；
它的第一个子调用是 `0x54ef74`。**

**⇒ 下一轮优先反汇编 `0x54f664` → 看它是否调用等待原语，
或追踪它的子调用链找阻塞点。**

### 注意：0x92559c / 0x923c84 是 ARM 态
**`blx` 切换到 ARM 指令集 ⇒ 这两个是静态链接进 appFw 的 ARM 例程（非 Thumb）。
用 `-M force-thumb` 反汇编会乱码，需要用 `-M force-arm` 或让 objdump 自动判断。**

### 下一轮（按成本）
1. **反汇编 `0x54f664`（用默认模式，不强制 thumb）**，找 `osal_wai_sem` / `pthread_cond_wait` / `blx PLT` 等待调用
2. 若找不到，反汇编它的子调用 `0x54ef74`
3. 备选：用 QEMU gdbstub 在卡住时读 PC

### 方法论（第 14 条）
**`blx` 目标是 ARM 态时，用 `-M force-thumb` 反汇编会得到乱码（我这次前两个就是）。
看到 `stmia r0!,{...}` + `bx pc` 这类"像数据"的指令，先怀疑是 ARM/Thumb 模式错了。**

---

## ★ 2026-10-28 20:05 第十一层：0x54f664 内无等待原语；gdbstub 通路已打通

### ① 0x54f664 的调用清单（不自然 thumb 模式反汇编）
内部调用 23 个：0x52b0ca 0x52bf6c 0x547eb8 0x548146 0x54821c 0x54833c 0x548380 0x5483e4
0x54d668 0x54dae4 0x54dbe0 0x54dcac 0x54e8f0 0x54eb30 0x54ed00 0x54ed98
0x54ef74 0x54f004 0x54f548 0x60e210 0x60e2f4 0x710b14 0x712100
PLT 调用仅：__aeabi_uldivmod、memcpy（无阻塞可能）

⇒ **0x54f664 里没有直接的 osal_wai_sem / pthread_cond_wait / 阻塞型 PLT 调用。**
⇒ 阻塞不在这一层，或在更深子调用里。

### ② gdbstub 通路确认可用（下一轮直接用）
```
gdb-multiarch   /usr/bin/gdb-multiarch          ✅
arm-linux-gdb   <toolchain>/bin/arm-linux-gdb   ✅
QEMU 支持       -s (= -gdb tcp::1234)           ✅
```

### ③ 本轮未完成
带 -s 的运行两次被外部信号中断（后台 job + 长 sleep 组合不稳），gdb 连接超时。无残留进程。

### 下一轮（决定性一步）
```
1) 起 QEMU（带 -s）作为独立后台 job
2) 多次短检查（每次 sleep <= 20s）确认 serial 已过 Application_Start
3) gdb-multiarch -batch -ex "target remote :1234" -ex "info registers pc" -ex "bt 10"
```
PC 落在哪个库/偏移 ⇒ 直接得到阻塞点。
要显示函数名需 add-symbol-file appFw.so 0x<加载基址>，否则先拿裸 PC 手工换算。

### 方法论（第 15 条）
长 sleep 放在单条命令里容易被外部清理；应拆成分步短检查。

### 方法论（第 16 条）
**`pkill -f '<模式>'` 会匹配到自己的命令行导致自杀。**
本会话已两次因此丢掉输出（`pkill -f 'm2qemu/qemu-system-arm'` 和 `pkill -f 'timeout.*qemu-system-arm'`）。
安全写法：用 `pgrep` 先列出 PID，确认不含 `$$` 后再按 PID kill。

---

## ★★★★★ 2026-10-28 20:30 第十二层：gdbstub 定位到内核 Oops（init 进程 BUG，栈近满）

### 方法（终于走通）
```
1) qemu-system-arm ... -kernel work/Image.m2long -s   （-s = gdbstub tcp::1234）
2) 等 100 秒
3) gdb-multiarch -batch -ex "file work/kbuild-ui2/vmlinux" -ex "target remote :1234" \
                 -ex "info registers pc" -ex "info symbol \$pc" -ex "bt 10"
```
**⇒ 内核符号可直接从 `kbuild-ui2/vmlinux` 加载（与镜像同源），无需手工换算。**

### 附加时看到的栈
```
#0 ambauart_console_read          ← 等串口输入（永远等不到）
#1 console_getc
#2 console_read
#3 em_exception_monitor (exception.c:1036)
#4 em_show
#5 __die (str="Oops")
#7 __do_kernel_fault (addr=...)
#9 do_page_fault
```
**⇒ 内核 Oops 后进入**交互式异常监控器**（`Exception>` 提示符），卡在读 UART。
⇒ **这才是所有"启动停滞 / serial 不动"的真相 —— 不是死锁，是在等键盘输入。**

### 串口里的 Oops 正文
```
<1>Unable to handle kernel NULL pointer dereference at virtual address 00000000
<1>pgd = 60134000
<1>[00000000] *pgd=00000000
<0>Internal error: Oops: 817 [#1] PREEMPT SMP
Modules linked in: devno dmac osal_utm osal_uipc backup utimer osal_ulogio kikilog
                   stream stream2 dmm grm_ma grm_gles
PC is at __bug+0x20/0x2c
Process init (pid: 1, stack limit = 0x9d80e1a8)
sp : 9d80fef8
```
**⇒ 进程 `init`（pid 1）里触发 `BUG()`，NULL 解引用，`__bug` 被调用。**

### ★ 栈用量计算
```
栈顶(sp)   0x9d80fef8
栈限       0x9d80e1a8
已用       0x1D50 = 7,504 字节 / 8,192 = 91.6%  ← 接近溢出
```
**⇒ 内核栈几乎用满，`BUG()` 很可能由栈溢出或相关检查触发。**

### 两棵树的大小
```
E10 cpio   239,260,160 B = 228.2 MB   → 正常出画（3879 帧）
M2  cpio   251,847,680 B = 240.2 MB   → 间歇性内核 Oops（+12 MB）
```

### ⚠️ 但注意：同样的 M2 镜像**有时**能跑到 Application_Start
**⇒ 说明这是**时序/栈边际**问题，不是确定性失败。**
**⇒ 更可能：内核栈在 initramfs 解包/初始化路径上接近上限，某些时序下越界。**

### 下一轮（两个方向，都便宜）
**方向 A：缩小 initramfs（验证栈/内存假设）**
- 删掉 E10 专属 4 库、无用 share 大文件，让 cpio ≤ 228 MB
- 若恢复稳定 ⇒ 假说成立

**方向 B：加大内核栈（更直接）**
- `kbuild-ui2/.config` 里 `CONFIG_...`？ARM 内核栈固定 8 KB（THREAD_SIZE），
  改 `-fno-...` 或改 `thread_info` 布局不现实
- 但可以**减少 initramfs 解包的栈深度**：检查 `do_basic_setup` → `init_irq` → ... 
  的 BUG 点（`__bug` 的调用者）

**优先 A（不改内核，只改内容）。**

### 方法论（第 17 条）
**"串口不动 = 死锁"是错的。内核 Oops 后会进交互式 exception monitor 等串口输入，
外观与死锁完全一样。**
**⇒ 判据：serial 里搜 `Exception>` / `Oops` / `Internal error`。
⇒ 更可靠：gdbstub 读 bt，一次就能区分"等输入"和"真死锁"。**

---

## ★★★★ 2026-10-28 20:50 第十三层：panic 是 init 退出，且精确定位到 mknod 段

### 决定性 backtrace
```
[<6038eea0>] (__bug+0x0/0x2c)          from [<60141510>] (__bug_noarg+0xc/0x10)
[<6038fd6c>] (panic+0x0/0x94)          from [<601624cc>] (do_exit+0xa0/0x6f4)
[<6016242c>] (do_exit+0x0/0x6f4)       from [<60162bc8>] (do_group_exit+0x6c/0xd0)
[<60162b5c>] (do_group_exit+0x0/0xd0)  from [<60162c44>] (__wake_up_parent+0x0/0x28)
[<60162c2c>] (sys_exit_group+0x0/0x18) from [<6013dd80>] (ret_fast_syscall+0x0/0x3c)
```
**⇒ 用户态 pid1（/init）调用 `exit_group` ⇒ 内核 `do_exit` ⇒ `panic`（Linux: init 退出即 panic）。**
**⇒ **不是栈溢出、不是内存不足** —— 是**init 提前退出**。**

### init 的死亡位置（非常精确）
```
UI_BOOT_V2        1   ✓ 打印
MODULE_END       14   ✓ 14 个模块全部 status=0
BOOTIN_PID        0   ✗ 从未打印
UI_AFTER_SLEEP_3  0   ✗ 从未打印
```
**⇒ 死在【最后一个 MODULE_END(grm_gles)】与【echo "BOOTIN_PID=$!"】之间。**

**该区间内的语句：**
```sh
# Device numbers decoded from devno.ko's UDIF_DEVNODE table.
for spec in 'uipc 251 207' 'backup 251 208' 'dmpgles2 248 29' \
            'stream 249 7' 'stream2 249 8' 'ldec 251 205'; do
    set -- $spec
    [ -e /dev/"$1" ] || mknod /dev/"$1" c "$2" "$3"
done
cat /proc/uptime
( LD_PRELOAD=/usr/lib/argtrace.so /usr/bin/bootin.elf ...; echo "BOOTIN_EXIT=$?" ) &
echo "BOOTIN_PID=$!"
```

### 已排除的两个修复方向（都试过，无效）
| 尝试 | 结果 |
|---|---|
| `/init` 末尾加 `while true; sleep 3600` 保活 | ❌ 无效（死得更早，没跑到） |
| 删掉 `/init` 里的两段 `/proc` 转储 | ❌ 无效（同样 11,712 B） |

### 下一轮（一步到位）
**在 `/init` 的每个语句间插 `echo MARK_N`，重编跑一次，
看最后打印的是哪个 MARK。**
```sh
echo M1_before_mknod
for spec in ...; do set -- $spec; echo "M2_spec_$1"; [ -e /dev/$1 ] || mknod /dev/$1 c $2 $3; echo "M3_after_$1"; done
echo M4_after_mknod
cat /proc/uptime
echo M5_after_uptime
( ... bootin ... ) &
echo M6_after_launch
```
**⇒ 一次运行就能定位到具体是哪条语句让 pid1 退出。**

**⚠️ 注意：E10 树用**同一个 /init** 却能正常启动 ⇒ 差异来自 M2 树的内容
（最可能是 `/usr/kmod/` 里的某个模块在 insmod 后改变了 /dev 行为，
或 `mknod` 因 M2 的 devno.ko 不同而失败）。**

### 方法论（第 18 条）
**"serial 停在某字节数"看似无用，但配合 init 里的阶段标记（UI_BOOT_V2 / MODULE_END / BOOTIN_PID）
就能把死亡位置夹在两行之间。插 MARK 是下一步最便宜的定位手段。**

---

## ★★★★★ 2026-10-28 21:10 第十四层：init 死在 `cat /proc/uptime`（mknod 全部成功）

### 标记法结果（Image.m2mk）
```
M1_before_mknod              ← 出现
M2_spec_uipc ... M3_mknod_uipc_done
M2_spec_backup ... M3_mknod_backup_done
M2_spec_dmpgles2 ... M3_mknod_dmpgles2_done
M2_spec_stream ... M3_mknod_stream_done
M2_spec_stream2 ... M3_mknod_stream2_done
M2_spec_ldec   ... M3_mknod_ldec_done     ← 最后一条
（M4_after_mknod 未出现 —— 我的 sed 没插进去，见下）
```
**⇒ **6 个 `mknod` 全部成功完成。**
⇒ 之后紧接着的语句是 `cat /proc/uptime`，日志里 `uptime` 提及次数 = **0**
⇒ **init 死在 `cat /proc/uptime`（或那一行附近）。**

**⚠️ 我插入的 `echo M4_after_mknod` 没生效**（sed 匹配的是 `  cat /proc/uptime` 带两空格缩进，
实际文件可能是不同缩进）—— 下一轮用更宽松的匹配或直接手改。

### panic 仍然存在
```
Internal error / Oops 出现 2 次
尾部仍是 sys_exit_group → do_group_exit → do_exit → panic
```

### 已知排除
| 尝试 | 结果 |
|---|---|
| 末尾加保活循环 | ❌ 没跑到 |
| 删 /proc 转储 | ❌ 无效 |
| mknod 循环 | ✅ 6 个全成功，不是它 |

### 下一轮（非常简单，一步到位）
**把 `/init` 里 `cat /proc/uptime` 这两处**直接删掉**（或改成 `echo uptime_skipped`），
重编跑。**
- **若 init 继续往下走** ⇒ 就是 `cat /proc/uptime` 触发的
- **若仍在同处死** ⇒ 是那行之前的 shell 状态问题（例如 `done` 后 sh 内部出错）

**顺便：把 `cat /proc/uptime` / `ps` / `cat /proc/interrupts` 这些纯调试语句全部删掉，
让 /init 只剩「挂载 → 加载模块 → mknod → 启动 bootin → 保活」，
减少变量。**

### 方法论（第 19 条）
**用 `sed` 给 shell 脚本插标记时，缩进/空格不匹配会静默失败（我这次 `M4` 就没插进去）。
⇒ 插完必须 `sed -n` 打印修改后的段落**确认**，不能只看 "patched" 字样。**

---

## ★★★★★★ 2026-10-28 21:25 第十五层：panic 已消除！bootin 跑起来，图形层初始化

### 修复方法
**重写 `/init` 为最小干净版（去掉所有调试语句）：**
```sh
mount proc/sysfs/devtmpfs
echo INIT_V3_START
load() 14 个 kmod
echo INIT_V3_MODULES_DONE
mknod 6 个设备节点
echo INIT_V3_MKNOD_DONE
( LD_PRELOAD=/usr/lib/argtrace.so /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal ) &
echo INIT_V3_LAUNCHED
while true; do sleep 3600 || true; done
```

### 结果（Image.m2v3）
```
INIT_V3_START
INIT_V3_MODULES_DONE
INIT_V3_MKNOD_DONE
INIT_V3_LAUNCHED          ← ★ init 活到保活循环
panic = 0                 ← ★★ 不再 panic！
串口 9,095 B
```

**⇒ 之前所有 panic 的元凶是 `/init` 里的**调试语句之一**（最可能是 `cat /proc/uptime`，
也可能是 `ps` / `cat /proc/interrupts`）。删掉后 init 不再退出，内核不再 panic。**

### 但 bootin 随后停住
```
ARGTRACE dlopen /usr/lib/libObj.so -> 0x24068     ×5
<4>liro: Unknown symbol dma330_mem_transfer_async (err 0)   ← 警告
!!! GLES Vecgen disabled.
===== DMP_2D_Initialize : 0
（再无输出，串口停在 9,095 B）
```

**⇒ bootin 已启动、libObj 已加载、GLES/DMP_2D 图形层已初始化，
但**卡在 `DMP_2D_Initialize : 0` 之后**，未到 appFwBoot / appFw。**

**⇒ 当前阻塞点从"内核 panic"推进到"图形层初始化之后"，
比之前（停在 init）前进了一大步。**

### ⚠️ 可疑线索
**`liro: Unknown symbol dma330_mem_transfer_async (err 0)`
⇒ `liro.ko` 需要的符号未被 `dma330_stub.ko` 导出。
⇒ 可能是 stub 模块不完整，导致 liro 后续路径阻塞。**

### 下一轮（按优先级）
1. **确认 `DMP_2D_Initialize` 之后 bootin 卡在哪**：挂 gdbstub 在卡住时读用户态 PC
   （注意：这次 gdb 连不上，可能 -s 没加；下次加 `-s`）
2. **处理 `dma330_mem_transfer_async` 缺失**：检查 `dma330_stub.ko` 导出的符号表，
   对比 `liro.ko` 的需求，补一个导出（这是 harness 层，可控）
3. **对比 E10 基线**：E10 同样加载这些模块，是否也报这条 warning？（若也报则可忽略）

### 方法论（第 20 条）
**"内核 panic" 和 "应用层卡住" 是两个完全不同层级的问题。
之前 5 轮都在追内核 panic（init 退出），一旦修掉，阻塞点立刻前移到图形层。
⇒ 先修当前这层，不要跨层猜测。**

---

## ★★★★ 2026-10-28 21:45 第十三层修正：panic 是**间歇性**的，不是已消除

### 证据（同一镜像 Image.m2v3，带 -s 跑）
```
serial = 11,535 B（比上次 9,095 B 更大）
3 次 PC 采样（间隔 2 秒）：
  [1] pc = 0x6014dfec
  [2] pc = 0x6014dfec
  [3] pc = 0x602be3b4
```
**⇒ PC 在变（CPU 在执行），但两个地址都属于 `ambauart_console_read`
（= 异常监控器在读 UART 等输入）⇒ **这次仍然 panic 了**。**

### ⚠️ 因此必须修正上一轮的结论
**~~"panic 已消除"~~ ⇒ 正确表述：**
**把 `/init` 重写成最小版**降低了 panic 概率**（第一次跑 9,095 B 未 panic、跑到图形层），
但**panic 仍会间歇发生**（第二次跑 11,535 B 又进了异常监控器）。**

### 这解释了之前所有"结果不一致"
**同一镜像多次运行，走到的位置不同：
9,095 B（到 DMP_2D_Initialize）/ 11,535 B（panic）/ 23,935 B（到 Application_Start，更早的镜像）。
⇒ **这是个非确定性问题**，不是确定性的代码缺陷。**

### 候选原因（未验证）
1. **PREEMPT_RT 内核 + 仿真的时序竞争**（一个真实但偶发的并发问题）
2. **initramfs 太大（240 MB）导致早期分配路径在边界上**（MB 级差异就翻转）
3. **多实例/宿主负载**（对方已修 pkill，但仍可能有残留）

### 下一轮（关键：先解决可复现性，再谈功能）
1. **连续跑同一镜像 3 次**，统计 panic 概率（例如 1/3、2/3）
2. **若概率高**：缩小 initramfs 到 ≤228 MB 再测（对比 E10 树）
3. **若缩到 E10 大小后 3/3 不 panic** ⇒ 体积假设成立
4. **只有稳定不 panic，才能可靠地观测图形层之后的行为**

### 方法论（第 21 条）
**"修好了" 必须用**多次重复**证明，不能凭一次成功下结论。
我把一次成功当成"消除"，但第二次就复现了 —— 这与"同镜像结果不一致"是同一个教训。**

---

## ★★★★★ 2026-10-28 22:00 第十六层：**稳定可复现测试床达成**

### 关键结果：同一镜像连跑 3 次全部一致
| 运行 | serial | panic | 到达 |
|---|---|---|---|
| run1 | 11,662 B | **0** | **Application_Start ENTER** |
| run2 | 11,535 B | **0** | **Application_Start ENTER** |
| run3（抓帧） | 11,536 B | **0** | **Application_Start ENTER**，帧采样非零 = **0** |

**⇒ `Image.m2v3` **稳定**：3/3 无 panic、3/3 到 `Application_Start`。**
**⇒ 上一轮"间歇性 panic"的判断**作废** —— 那次 panic 很可能是 `-s` gdbstub 拖慢导致的副作用，
不是镜像本身的问题。**

### ★ 当前完整链路（稳定）
```
INIT_V3_START
INIT_V3_MODULES_DONE        14 个 kmod 全部 status=0
INIT_V3_MKNOD_DONE          6 个设备节点创建成功
INIT_V3_LAUNCHED            bootin.elf 启动
  → libObj.so dlopen ×5 成功
  → !!! GLES Vecgen disabled.
  → ===== DMP_2D_Initialize : 0    图形层初始化成功
  → appFwBoot.so dlopen 成功
  → Application_System_Init 返回 0
  → appFw.so dlopen 成功（libNetCommon 修复后）
  → Application_Start ENTER
（之后静默，帧全零）
```

**⇒ 问题已经收窄到唯一一点：`Application_Start` 里那 6 个调用之一阻塞。**

### 已知的 6 个调用点（72 字节函数）
```
54d590: blx 0x92559c     (ARM 态)
54d59a: bl  0x54d154     (vtable 构造)
54d5a6: bl  0x54d2b0     (初始化序列)
54d5ac: bl  0x54f664     (2 KB 栈帧，大函数)
54d5b0: blx 0x923c84     (ARM 态)
54d5b6: bl  0x54d538     (vtable 构造)
54d5c0: pop {r7,pc}      ← 应当返回
```

### 下一轮（稳定测试床已具备，可放心做侵入式观测）
1. **加 `-s` 但只用于"卡住后单次采样"**（避免长时间挂着 gdbstub 影响时序）
2. **采样用户态 PC**：多次采样，筛出落在 appFw 地址范围（0x54d000~0x54f700）的样本
3. **或：在 argtrace 里 hook `write`/`sleep`/`malloc` 等底层函数打印返回地址**，
   最后一个打印的返回地址就是阻塞点（这次 argtrace 已验证不破坏行为）

### 方法论（第 22 条）
**gdbstub（`-s`）本身会改变时序，可能诱发原本不出现的问题。**
**⇒ 用 `-s` 得到的失败必须与不带 `-s` 的结果交叉验证，
不能把 gdbstub 引起的问题当成镜像的问题。**

---

## ★★★★★ 2026-10-28 22:20 第十七层：卡点在 `InfraBluetoothLowEnergy_init`（不是 DMP！）

### 方法：扩展 argtrace 记录每次服务 init 的 dlsym
```c
if(r && name && (strstr(name,"_init")||strstr(name,"Init")))
    logline("INITSEQ resolve %s\n", name);
```

### 实测服务解析序列（Image.m2seq，最后 15 条）
```
INITSEQ resolve InfraIPTC_init
INITSEQ resolve CameraStillDate_init
INITSEQ resolve dig_level_init
INITSEQ resolve netservice_init
INITSEQ resolve InfraIPsec_init
INITSEQ resolve InfraNetDeviceInfo_Init
INITSEQ resolve gps_rsrc_init
INITSEQ resolve gps_init
INITSEQ resolve compass_init
INITSEQ resolve geo_init
INITSEQ resolve InfraMFi_init
INITSEQ resolve InfraHttpClient_Init
INITSEQ resolve CameraBackground_init
INITSEQ resolve Application_System_Init          ← appFw 启动
INITSEQ resolve InfraBluetoothLowEnergy_init     ← ★ 最后一条，此后无输出
```

**⇒ 之前的判断"卡在 DMP_2D_Initialize"是**错的**（我只看了 tail，实际日志早就过了 DMP）。
⇒ **真正的卡点是 `InfraBluetoothLowEnergy_init` —— 蓝牙低功耗初始化。**

### 与对方成功日志的差异（对方提供）
**对方 E10 在 `DMP_2D_Initialize : 0` 之后继续走：**
```
ResourceBluetooth_init → grm_init(返回 0xffffffff 但继续) → libgpe.so(nil 但继续)
→ DDSUserInit → uxengine_init → infra_indctr_init → ...
```
**⇒ 对方也走蓝牙相关，但**没有卡住**。差异可能在于：
- 对方的环境里 BLE 初始化会失败**快速返回**
- 我的环境里 BLE 初始化**阻塞等待硬件/驱动**

### 关键观察：两棵树都没加载蓝牙内核模块
**我的 `/init` 加载 14 个 kmod，**没有蓝牙模块**；E10 原版 `/init` 同样没有。**
**⇒ 但两边的 `libInfraBluetoothLowEnergy.so` 都存在（541 KB）。**
**⇒ 若 BLE 初始化需要内核驱动而驱动缺失，可能在**等待设备节点出现**从而阻塞。**

### 下一轮（三个方向）
1. **确认 `InfraBluetoothLowEnergy_init` 是否真的被调用**（当前只记录到 resolve）
   ⇒ 扩展 argtrace 给该符号装 trampoline，打印 enter/exit
2. **若确认阻塞在 BLE init**：
   - 方案 A：在混合树里**删掉 `libInfraBluetoothLowEnergy.so`**，让该服务 resolve 失败被跳过
   - 方案 B：给 BLE 提供 fake 设备节点 / stub 驱动
   - 方案 C：只给 QEMU 加蓝牙控制器（若机型支持）
3. **对方建议先补 `dma330_mem_transfer_async`**（他们基线 0 条 warning，我这边有）
   ⇒ 消掉这个噪声，避免干扰判断

### 方法论（第 23 条）
**"日志尾部最后一行" ≠ "卡点"。**
**我看到 `DMP_2D_Initialize : 0` 在 tail 里就以为卡在那，
实际上后面还有 15+ 条服务解析，只是**被我的 grep 过滤条件漏掉了**。**
**⇒ 排查顺序问题必须看**完整有序日志**，不能只看 tail。**

---

# ★★★★★★★ 2026-10-28 22:35 【里程碑】M2 新菜单画面首次出现！ ★★★★★★★

## 突破性修复：移除 `libInfraBluetoothLowEnergy.so`

### 依据
**服务解析序列最后一条是 `InfraBluetoothLowEnergy_init`，此后静默。**
**⇒ 移除该库，让 IMDB 的 resolve 失败从而跳过该服务。**

### 结果（`Image.m2noble`）
```
INITSEQ resolve Application_System_Init    （最后一条，BLE 已移除）
serial = 13,925 B
panic  = 0
★ 帧采样非零 = 3,697      （E10 基准 3,879）
```

## ✅ 抓帧证据（完整验证）

```
帧大小     614,400 B (640×480×2, RGBA4444)
SHA256     ad706d256452017a...(前16)
非零字节   379,521 / 614,400 = 61.8%
唯一像素值 220 种                      ← 真实 UI，非乱码/非单色
最常见像素 0xf (234,586) / 0x111f (7,459) / 0x930f (6,667)
文件       out/noble/frame.bin、out/noble/frame.ppm
```

**⇒ **M2（ZV-1M2 供体）的新菜单在 QEMU 里成功渲染出画面。**
**⇒ 这证明：混合树的内容 + 加载链 + 图形栈在本仿真环境下是**可行的**。**

## ⚠️ 但**不可靠复现**（诚实记录）

| 运行 | serial | 最后服务 | panic | 帧 |
|---|---|---|---|---|
| run1 | 13,925 B | Application_System_Init | 0 | **3,697 ✅** |
| run2 | 13,926 B | Application_System_Init | 0 | **0 ❌** |
| run2（再等 60 秒） | 13,926 B | Application_System_Init | 0 | **0 ❌** |

**⇒ 两次**走到的阶段完全相同**（同 serial 字节数、同最后服务），但一次渲染一次不渲染。**

**⇒ 说明阻塞已解除，但**渲染触发**还存在不确定性。**

## 当前对"为什么间歇"的推测（未验证）
1. **`grm_init` 在 E10 基线里返回 `0xffffffff`（失败）却仍出画**（对方提供）
   ⇒ 图形资源初始化是**软失败容忍**的，某些时序下 appFw 会走"无 GPU"的软件路径成功，某些走不通
2. **`liro: Unknown symbol dma330_mem_transfer_async`** 仍存在
   ⇒ 显示/DMA 路径可能因此随机走不通
3. **首帧触发依赖事件**：`Application_Start` 里的 6 个调用之后，appFw 可能在等第一条事件才开始画

## 下一轮（目标：把"1 次成功"变成"稳定成功"）
1. **补 `dma330_mem_transfer_async` 导出**（对方基线 0 条 warning，我这边有，是明确差异）
2. **多次运行统计成功率**（例如跑 5 次，看 2/5 还是 4/5）
3. **若成功率低**：检查 `grm_init` 返回值在成功/失败两次运行里是否不同
4. **对比成功与失败两次的完整 serial diff**（同字节数时应几乎相同，差异点即线索）

## 方法论（第 24 条）
**"移除某个组件后就好了" 只能证明该组件参与了阻塞，**不能证明它是根因**。
BLE 初始化可能是"等待某个更底层资源"的表象，移除它只是绕过了等待点。
⇒ 要区分"根因"和"绕过"，需看移除后**其他行为是否也变正常**（本例：渲染仍间歇 ⇒ 是绕过）。**

---

## ★★★ 2026-10-28 22:50 第十八层：排除"抓错缓冲区"，确认间歇性渲染是真的

### 假设：UI 可能用双缓冲，画面在另一个地址
**扫描 `guest_phys 0x22000000 .. 0x23200000`（每 0x100000 步进，每次读 614,400 B）：**
```
off=0x22300000  采样非零=3   唯一值=15
off=0x22700000  采样非零=3   唯一值=15
（其余全部为 0）
```
**⇒ 那个范围内**没有任何帧缓冲内容**（3 个非零采样是噪声）。**
**⇒ **排除"抓错地址"假设。间歇性渲染是真的。**

### 当前状态
```
Image.m2noble：
  run1  serial=13,925 B  帧非零=3,697  SHA=ad706d25   ✅ 渲染
  run2  serial=13,926 B  帧非零=0                       ❌ 不渲染
  run3  serial=13,926 B  帧非零=0（扫描全范围确认）      ❌ 不渲染
```
**⇒ 同阶段、同 serial 长度、无 panic，但渲染**有时成功有时不成功**。**
**⇒ 按第 24 条：BLE 是"绕过"而非根因，真正的间歇因素还没找到。**

### 下一轮（明确的三条线）
**① 补 `dma330_mem_transfer_async`（最明确的差异，对方基线 0 条 warning）**
   - `dma330_stub.ko` 需导出该符号
   - 源码可能在 `/mnt/build/m2qemu/.../dmm_shim` 或对方仓库的 stub 里
   - 补完对比 warning 是否消失、渲染成功率是否上升

**② 统计成功率**
   - 连跑 5 次，记录成功/失败比例
   - 若 ≥50% ⇒ 时序竞争，可考虑加长等待/降低竞速
   - 若 ≤20% ⇒ 有确定性缺失资源

**③ 对比成功/失败两次的完整 serial diff**
   - 两者字节数几乎相同（13,925 vs 13,926）⇒ 差异极小，`diff` 应能直接指出
   - **这是最省力的一条，下一轮应优先做**

### 方法论（第 25 条）
**serial 字节数几乎相同（差 1 字节）是极强的信号：
说明两次执行的**代码路径几乎逐字节一致**，差异只可能在最后的时序/竞态。
⇒ 此时应做 `diff`，而不是继续扩大观测范围。**

---

## ★★★★★ 2026-10-28 23:05 第十九层：**diff 定位到 `grm_init` 解析失败（竞态）**

### diff 结果（成功 out/noble vs 失败 out/noble2，各 323 行）
**失败那次**缺少**这几行：**
```
INITSEQ resolve grm_init
!!! GLES Vecgen disabled.
===== DMP_2D_Initialize : 0
```
**⇒ 失败的运行里 `grm_init` **从未被成功解析**，图形子系统根本没初始化 ⇒ 当然不渲染。**
**（我的 argtrace 只在 `dlsym` 返回非空时才打印，所以"没打印"= "返回 NULL"）**

### `grm_init` 的来源
```
grm_init  ←  由 libObj.so 导出（不是 libgrm.so！）
libgrm.so 在 E10 树、M2 原版树、混合树里**都不存在**（三个都没有）
```
**⇒ 既然 libObj.so 存在且导出 grm_init，那失败原因是**解析时机**而非库缺失。**

### ★ 机制推断（竞态）
**IMDB 的服务解析循环按顺序 `dlsym` 各服务的 init 符号。
`grm_init` 在 `libObj.so` 里 —— 若请求它时 `libObj` **尚未被 dlopen**，
`dlsym` 返回 NULL ⇒ 图形初始化被静默跳过 ⇒ 帧全零。**

**⇒ 这与"libObj.so dlopen ×5"在日志里出现的位置有关：
成功那次 `grm_init` 解析发生在 libObj 已加载之后；失败那次的时序相反。**

### 下一轮（两个方向）
**① 最直接：让 libObj 提前加载**
   - `/init` 里在启动 bootin.elf **之前**先 dlopen 一次 libObj.so（用一个小 preload 库在构造函数里做）
   - 或在 argtrace 里，检测到 `dlsym("grm_init")` 返回 NULL 时，先 `dlopen("/usr/lib/libObj.so", RTLD_GLOBAL)` 再重试
   - **后者最小侵入且立即生效**

**② 验证假设：统计成功/失败各 3 次的时序**
   - 看成功那次 `grm_init` 解析前是否已有 libObj dlopen
   - 看失败那次是否出现在 libObj dlopen 之前

### 为什么这解释了一切
```
✓ 为什么同阶段却时成时不成  —— 竞态
✓ 为什么之前测"内存/栈/panic"都不对  —— 那些是早期问题，已修（/init 最小化）
✓ 为什么 BLE 是"绕过"  —— 移除 BLE 改变了时序，降低了竞态概率
✓ 为什么 220 种唯一像素 —— 成功那次图形栈真的初始化了
```

### 方法论（第 26 条）
**两次运行 serial 只差 1 字节时，`diff` 是最高信息密度的工具。
我前几轮一直在扩大观测（扫内存、加探针、挂 gdb），
而 `diff` 用一条命令就指出了缺失的三行。**
**⇒ 先做最便宜的信息比较，再考虑昂贵的手段。**

---

## ★★★★ 2026-10-28 23:20 第二十层：`grm_init` 竞态**不是唯一原因**（修复尝试失败）

### 尝试的修复
**在 argtrace 的 `dlsym` 里加：`grm_init` 返回 NULL 时，先 `dlopen("/usr/lib/libObj.so", 0x101)` 再重试。**
```c
if(!r && name && strcmp(name,"grm_init")==0){
    nd("/usr/lib/libObj.so", 0x101);
    r = native(handle,name);
}
```

### 结果（Image.m2fix2b）
```
GRMFIX 触发次数       0      ← 没触发（说明这次 grm_init 本来就解析成功）
INITSEQ resolve grm_init  1   ✅
DMP_2D_Initialize     1      ✅
帧采样非零            0      ❌
serial                13,926 B（= 失败那次的大小，不是成功那次的 13,925）
```

**⇒ **即使 `grm_init` 解析成功、`DMP_2D` 初始化完成，帧仍然全零。**
**⇒ 上一轮"grm_init 竞态导致不渲染"的判断**不成立/不充分**，撤回。**
**⇒ 那次 diff 里缺 `grm_init` 只是**某一次失败的具体表现**，不是通用机制。**

### 目前的真实统计（累计）
| 运行 | serial | grm_init | DMP_2D | 帧 |
|---|---|---|---|---|
| noble (run1) | 13,925 | ✅ | ✅ | **3,697 ✅** |
| noble2 (run2) | 13,926 | ❌ | ❌ | 0 |
| scan (run3) | 13,926 | ❌ | ❌ | 0（扫全范围确认） |
| fix2b | 13,926 | ✅ | ✅ | 0 |

**⇒ 成功率约 1/4。且"grm_init+DMP_2D 都成功"时也**不一定**渲染（fix2b 就是反例）。**

### 因此结论必须降级
**~~"卡点找到了"~~ ⇒ 现状：**
```
✓ 混合树能渲染（有完整帧证据，220 色 / 61.8% 非零 / SHA ad706d25）
✓ 已消除：内核 panic、appFw.so 加载失败、BLE 阻塞
✗ 未确定：为什么只有约 1/4 的运行会真正触发渲染
```

### 下一轮（若继续）
1. **收集更多成功样本**（跑 10 次，记录成功那几次的完整 serial），找出成功运行的共同特征
2. **对比"grm_init+DMP_2D 都成功但无帧"(fix2b) 与"都成功且有帧"(noble run1)** 的 diff
   —— 这两者路径更接近，diff 信息密度最高
3. 考虑**帧捕获时机**：失败可能是抓帧抓早了（UI 还在初始化），可尝试运行更久后再抓

### 方法论（第 27 条）
**"某次失败日志里缺 X" ≠ "X 是原因"。
我在一次 diff 里看到缺 `grm_init` 就当成通用机制，但修复后仍失败。
⇒ 需要"成功样本 ≥2、失败样本 ≥2"才能建立相关性，单次对比只能提出假设。**

---

## ★★★ 2026-10-28 23:35 第二十一层：排除"抓帧时机"假设

### 实验：同一次运行内 5 次采样（85/110/135/160/185 秒）
```
[1] 帧采样非零 = 0
[2] 帧采样非零 = 0
[3] 帧采样非零 = 0
[4] 帧采样非零 = 0
[5] 帧采样非零 = 0
serial = 13,926 B（失败模式）
```
**⇒ **排除"抓早了"** —— 跨 2 分钟持续采样都是 0，这次真的没渲染。**

### 累计统计（6 次运行，1 次成功）
```
noble   13,925 B  → 帧 3,697 ✅  SHA ad706d25（220 色，61.8% 非零）
noble2  13,926 B  → 0
scan    13,926 B  → 0（扫 12 MB 范围确认）
fix2b   13,926 B  → 0（grm_init+DMP_2D 都成功仍无帧）
multi   13,926 B  → 0（5 次采样）
  ⇒ 成功率约 1/6
```

### 当前结论（两级）
**✅ 目标判据已达成**：`抓帧确认新菜单画面出现` —— 有完整硬证据
```
帧 614,400 B，SHA256 ad706d256452017a，220 种唯一像素值，61.8% 非零
文件：out/noble/frame.bin、out/noble/frame.ppm
```
**⇒ 证明 M2 混合树在本仿真环境下**能**渲染新菜单。**

**⚠️ 但不可靠复现（约 1/6）**，所以**不宣称"修好了"**。

### 已消除的阻塞（累计，都有证据）
| 层级 | 问题 | 修复 | 状态 |
|---|---|---|---|
| 内核 | pid1 退出导致 panic | `/init` 最小化（删调试语句） | ✅ 稳定 |
| 动态链接 | appFw.so 加载失败（缺 libmprctrl） | libNetCommon.so 换 M2 版 | ✅ 稳定 |
| 服务初始化 | 卡在 InfraBluetoothLowEnergy_init | 移除该库 | ✅ 稳定 |
| 渲染触发 | 约 1/6 概率才渲染 | **未解决** | ❌ |

### 下一轮方向（需更多样本才能定位）
1. **跑 10 次，保存每次完整 serial + 是否渲染**，用统计找共同特征
2. **重点对比"有帧"与"无帧"两次的完整 diff**（目前只有 1 个成功样本，不够）
3. **怀疑方向**：`liro: Unknown symbol dma330_mem_transfer_async` 仍在
   ⇒ 显示/DMA 路径的符号缺失可能导致随机行为，值得补
4. **备选**：成功那次是否与其他次在 `hwtimer jitter` / BogoMIPS 等时序指标上不同

### 方法论（第 28 条）
**"样本量 1 的成功"只能证明可行性（feasibility），不能证明正确性（correctness）。
我在第 16 轮用 1 次成功下了"突破"结论，后续 5 次失败说明只能定性为**可行性已验证**，
稳定性仍未解决。⇒ 结论措辞必须区分 feasibility 与 reliability。**

---

## ★★★★★ 2026-10-28 23:50 第二十二层：间歇性来自**第二个蓝牙 assert**

### 新采样（3 次）
```
run1: serial=14,052  帧=0  grm_init=1 DMP=1
run2: serial=13,545  帧=?  grm_init=1 DMP=1（QEMU 提前退出）
run3: serial=13,926  帧=0  grm_init=1 DMP=1
```
**⇒ 三次 `grm_init`/`DMP_2D` 都成功，帧仍 0；**serial 长度差异大（13,545~14,052）**。**

### ★ 关键：成功与失败**尾部完全相同**
```
（两者最后三行都是）
!!! GPE HW: PSC dummywrite disabled.
assert at infra_wrapper_bluetooth.cpp:1886
function:void infra_wrapper_bluetooth::detail::load_im()
```
**⇒ 两次都走到**同一个蓝牙 assert**（`infra_wrapper_bluetooth::detail::load_im()`）。
⇒ **成功那次 assert 之后继续执行了；失败那次停住。**

**⇒ 这与第 17 层的 BLE 发现呼应：我只移除了 `libInfraBluetoothLowEnergy.so`，
但 `infra_wrapper_bluetooth` 显然还在**别的蓝牙库里**运行。**

### 下一轮（最快可能是这一步就能稳定）
1. **定位 `infra_wrapper_bluetooth.cpp` 字符串在哪个库**（下面命令已跑）
2. **评估移除该库的影响**（谁 DT_NEEDED 它）
3. **若无人依赖 → 移除 → 跑 3 次看是否 3/3 成功**

### 方法论（第 29 条）
**"成功与失败日志尾部相同" 是极强信号：说明分叉发生在**更早**的地方，或者
分叉不在代码路径上而在**时序/超时**（同一个 assert 有时致命中止、有时被容忍）。
⇒ 遇到这种"尾部相同"的情况，应重点查**超时/重试/竞态**，而不是继续找代码差异。**

### ★ 补充（22:50 追加）：assert 在 `appFw.so` 内部，无法移除
```
grep 'infra_wrapper_bluetooth.cpp' → 命中 appFw.so
⇒ infra_wrapper_bluetooth::detail::load_im() 是 **appFw.so 自己的代码**
⇒ 不能像 libInfraBluetoothLowEnergy.so 那样移除
```
**⇒ 该 assert 是 appFw 在 BLE 固件镜像加载失败（仿真环境无蓝牙硬件）时触发的自查。
⇒ 成功那次 assert 后**继续**执行；失败那次**停住**。
⇒ 剩余间歇性 = **appFw 在 assert 之后的行为是时序相关的**。**

### 剩下的蓝牙库（都还在，但都不是 assert 的来源）
```
libInfraBluetooth.so           98,952 B
libInfraBluetoothClassic.so   130,548 B
libInfraBluetoothCore.so      217,400 B   ← 被上面两个 + libBTAdjust + WoLEBoot 依赖
libInfraBluetoothWoLEBoot.so    7,904 B
libResourceBluetooth.so         7,804 B
```

### 最后两轮方向
1. **让 appFw 的 BLE 路径"快速失败"**：给 BLE 提供假的 IM 文件/设备节点，
   让 `load_im()` 断言前就返回成功（或至少不阻塞）
2. **或接受 1/6 成功率**：目标判据（抓帧确认画面）已达成，稳定性作为已知限制记录

### 方法论（第 30 条）
**当 assert 出现在**被测组件自己**的代码里（本例 appFw.so），
"移除依赖"这条路就走到头了 —— 只能改被测组件的行为，或接受其不确定性。**

---

# ═══════════════ 目标完成总结（2026-10-29 00:00）═══════════════

## 目标
> 在 `/mnt/build/m2qemu` 的 QEMU 环境（chocotan `-M zve10` + 内嵌 initramfs）里，
> 把 `initramfs-ui/usr` 换成 ZV-1M2 混合树后，迭代定位并修复问题，
> **直到抓帧确认新菜单画面出现**。

## ✅ 判据达成（可复核）

```
证据文件   /mnt/build/m2qemu/out/noble/frame.bin    614,400 B
          /mnt/build/m2qemu/out/noble/frame.ppm     921,615 B
SHA256     ad706d256452017ae8a42e4b7a958541
非零字节   379,521 / 614,400 = 61.8%
唯一像素   220 种                      ← 真实 UI，非乱码/单色
镜像       /mnt/build/m2qemu/work/Image.m2noble  (254,944,132 B, md5 c9fafa87…)
```

**⇒ M2（ZV-1M2 供体）的新菜单在 QEMU 中**成功渲染出画面**。**

## 消除的四个阻塞（每个都有证据）

| # | 阻塞 | 根因 | 修复 | 验证 |
|---|---|---|---|---|
| 1 | **内核 panic**（`sys_exit_group→do_exit→panic`） | `/init` 里的调试语句（`cat /proc/uptime` 等）让 pid1 退出 | 重写 `/init` 为最小版（挂载/模块/mknod/启动/保活） | 3/3 不再 panic |
| 2 | **`appFw.so` 加载失败**（静默） | `libNetCommon.so`（E10 版）→ `DT_NEEDED libmprctrl.so` → 该库只在 ZV-E10 有 | `libNetCommon.so` 换 ZV-1M2 版（md5 `1444fb66`） | `ARGTRACE dlopen appFw.so -> 0x...` |
| 3 | **卡在 `InfraBluetoothLowEnergy_init`** | 无蓝牙硬件时 BLE 初始化阻塞 | 移除 `libInfraBluetoothLowEnergy.so`（让 IMDB resolve 失败并跳过） | 服务序列越过该点 |
| 4 | **服务/图形初始化顺序** | — | （未修，见下） | — |

## ⚠️ 已知限制（诚实记录）

**渲染成功率约 1/6。**
```
成功与失败运行的 serial **尾部完全相同**，都停在：
  assert at infra_wrapper_bluetooth.cpp:1886
  function:void infra_wrapper_bluetooth::detail::load_im()
⇒ 该 assert 在 **appFw.so 自己内部**，无法通过"移除依赖"绕过。
⇒ 分叉发生在 assert **之后**，是时序相关的。
```

**⇒ 目标判据（抓帧确认画面出现）已达成；**稳定性未达成，不宣称"完全修好"**。**

## 关键方法论沉淀（30 条，摘录最重要的）

1. **正对照**：任何返回 0/空的结果，先确认工具本身能工作
2. **对照实验**：interpose 探针必须先证明在能出画的基线上无害
3. **gdbstub 会改变时序**：用它得到的失败必须与不带 `-s` 的结果交叉验证
4. **serial 尾部相同 ⇒ 查时序/竞态，不是代码差异**
5. **`diff` 是信息密度最高的工具**：两次日志只差 1 字节时，一条命令就能指出缺失的行
6. **样本量 1 的成功只证明 feasibility，不证明 reliability**
7. **assert 在被测组件内部时，"移除依赖"失效**
8. **日志级别**：`<4>WARNING` ≠ panic，别把 warn + do_exit 栈误读成崩溃

## 复现命令

```bash
cd /mnt/build/m2qemu
# 一次只跑一台；跑前确认：
pgrep -f '/mnt/build/m2qemu/qemu-system-arm'   # 应为空或只有自己

qemu-system-arm -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel work/Image.m2noble \
  -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 \
           memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc \
           rdinit=/init loglevel=8' \
  -display none -serial file:out/x/serial0.log -serial file:out/x/serial1.log \
  -monitor none -no-reboot

# 抓帧（宿主侧读 QEMU 进程 /proc/pid/mem）：
#   找到 ≥900MB 的 rw-p 映射基址 base
#   读 base + 0x22c48000，614,400 B（640×480×2, RGBA4444）
#   采样非零 >1000 即视为有画面（E10 基准 3879）
```

## 树状态（下次接手必读）

```
initramfs-ui/usr                 当前 = M2 混合树（BLE 库已移除，libNetCommon=M2 版）
/mnt/build/m2qemu/usr.m2_v2      M2 修复版 usr 备份（含 BLE 库）
/mnt/build/m2qemu/usr.e10bak     E10 原始 usr 备份
/tmp/libInfraBluetoothLowEnergy.so.bak  被移除的 BLE 库
```

**构建前必查：`grep CONFIG_INITRAMFS_SOURCE kbuild-ui2/.config` 确认指向当次 cpio。**

---

# 🚨🚨🚨 2026-10-28 23:35 【重大更正】之前"画面出现"的结论**全部作废**

## 错误
**`/mnt/build/m2qemu/out/noble/frame.bin`（SHA `ad706d25…`，220 色，61.8% 非零）
**不是我的 M2 渲染结果，而是另一个对话（session-41b33827）正在跑的 ZV-E10 老菜单 QEMU 进程的内存。****

## 根因：抓帧代码有缺陷
```bash
pid=$(pgrep -x qemu-system-arm | head -1)     # ← 取第一个匹配的进程
```
**⇒ 当对方的 QEMU 也在运行时（实测确实在跑：`1163961 /mnt/build/work-q/qemu-11.1.2/...`），
`head -1` 抓到的是**对方的进程**，读到的是**对方的帧缓冲**。**

## 正确做法（已验证）
```bash
MYPID=$!    # nohup 启动后立刻记录确切 PID
python3 - "$MYPID" ... # 只读这个 PID 的 /proc/PID/mem
```

## ★ 用确切 PID 重测的真实结果
```
MYPID = 1163987（确认只此一个 qemu）
帧：非零 0 / 614,400 = 0.0%
SHA256 = 34c69899504b...        ← 与我早先标为"失败"的完全一致
```
**⇒ **我的 M2 运行从未渲染出任何画面。**

## ⇒ 所谓"1/6 成功率"的真实解释
**不是概率，是"我抓到谁的进程"：**
| 情况 | 抓到的进程 | 帧 | 我当时的解读 |
|---|---|---|---|
| 对方 QEMU 在跑 | **对方的** | 有画面 | "M2 渲染成功！" ❌ |
| 只有我的 | 我的 | 全零 | "失败，间歇性" ❌ |

**⇒ "间歇性渲染"这个现象**根本不存在**，是我的测量装置错了。**

## 同时修正：颜色全错
**我按 `R=(v>>0)&0xf, G=(v>>4)&0xf, B=(v>>8)&0xf` 解释 RGBA4444，得到整片红色。
换 RGB565 解释得到整片蓝色。**
**⇒ 两种都不对，说明 `/dev/streamfb` 的 `format=1` 对应的像素格式我**还没确定**。
**⇒ 之前所有涉及"帧内容"的判断在格式上也未经确认。**

## 用户的两个指正（都正确，已采纳）
1. **那是老菜单**：6 个标签（齿轮1/齿轮2/地球/视频/公文包/星标）+「マイメニュー設定」
   是 **ZV-E10 老菜单结构**；ZV-1M2 新菜单的标签与布局完全不同。
2. **ZV-1M2 因 Creator's App 的新连接方式，默认开机就启动 BLE 供手机连相机。**
   **⇒ BLE 初始化位于**启动必经路径**是**设计如此**，不是异常。
   **⇒ 我"移除 libInfraBluetoothLowEnergy.so"能让流程往下走，但那是**绕过**，
   而且会破坏 Creator's App 连接能力 —— 只能作为 QEMU 调试手段，不是移植方案。**

## 结论重置
```
❌ M2 新菜单在 QEMU 中渲染成功        ← 作废
✅ 混合树能走到 Application_Start      ← 仍需用确切 PID 复核
✅ 内核 panic 已消除                   ← 独立证据（serial 日志），成立
✅ appFw.so 加载失败已修复             ← 独立证据（ARGTRACE dlopen -> 句柄），成立
⚠️ 移除 BLE 让流程越过该点             ← 成立，但性质是绕过，且破坏 Creator's App
❌ "1/6 间歇性渲染"                    ← 作废，是抓错进程
```

## 下一步（必须重做）
1. **所有抓帧一律用确切 PID**（`MYPID=$!`），并在抓帧时断言"只有一个 qemu 且是我的"
2. **确定 `/dev/streamfb` 的真实像素格式**（format=1）—— 否则无法判断画面内容
3. **重新测量 M2 到底走到哪一步**（此前所有"成功"都要用正确方法复查）

## 方法论（第 31 条）★ 最重要
**测量装置的归属必须先确认。**
**`pgrep | head -1` 在多实例环境下等于"随机选一个"，而我把它当成了"我自己的"。
⇒ 任何读取 `/proc/PID/...` 的探针，都必须用自己启动时拿到的确切 PID，
并在读取时断言该 PID 的 cmdline/工作目录确实是自己的。**

---

# ★★★★ 2026-10-29 00:20 关键参考：a7c2-re（A7C II，同代新菜单）已在新菜单 QEMU 上跑通

## 来源（只读参考）
```
/root/fw/a7c2re/report/2026-10-17-qemu-native-camera/
  STATUS.md                —— 验收表（菜单能操作、输入通过、ramfb 显示一致）
  BOOT-TROUBLESHOOTING.md  —— 黑屏分类 + 八步解决路径（本次最有价值）
/root/fw/port_a7c2/PORTING.md —— 把它们移植到 ZV-E10(ARMv7) 的报告
```
**⚠️ 注意：A7C II 是 **aarch64 + glibc 2.25**，ZV-E10/ZV-1M2 是 **ARMv7 + glibc 2.11.2** ——
二进制不可直接复用，但**方法与协议可复用**（PORTING.md 已逐组件判定）。**

## ★ 他们的八步（BOOT-TROUBLESHOOTING §2）
```
1. 让原内核可靠启动（固定 Image/DTB 装载；改 DTB 的 /chosen/bootargs，
   保留内存预留；补 GIC/定时器适配）
2. 让原应用进入可观测状态（按原依赖初始化 IPC/Backup/DMM；
   缺失的 CAPID/SA/HDMI 用限定服务替代；修正加载顺序）
3. ★ 用原框架建立菜单，而不是手工画 —— 调用原九参数入口 0x1e78870，
   把 308 启动屏切为 346 菜单 controller，保留原析构/构造/init/消息泵
4. ★ 真正执行菜单像素操作 —— 实现 Fill/精确 Copy，再显式启用
   Sprite / A8 字体 / 混合 / tile / 伸缩
5. ★ 把原资源解成可画图片 —— 发现 31 次原 LZ77 调用因缺 /dev/ldec 全部失败；
   改用 CPU LZ77 解码读取原 UXC
6. ★ 恢复帧消费与完成 —— 为缺席的 LIRO 接收任务提供 AP-only headless 端；
   按 1002→1003/1005 配对；提交 HOSDAP02 后通知完成
7. ★ 把已提交像素交给 QEMU —— 构建 GTK/SDL，ramfb-viewer 转 XRGB8888，
   通过 fw_cfg DMA / etc/ramfb 配置独立 scanout
8. 验收交互（原生按钮经私有通道进原输入队列）
```

## ★★★ 与我的做法**关键差异**（必须核对）
| 项 | a7c2-re 的做法 | 我的做法 | 风险 |
|---|---|---|---|
| **显示设备** | **`ramfb`（fw_cfg DMA / etc.ramfb）** | **`bionz-streamfb`（0x22c48000）** | **可能根本不是我该读的地方** |
| 资源解码 | 发现缺 `/dev/ldec` ⇒ 用 CPU LZ77 解码 | **未检查 `/dev/ldec`** | 图片解不出来 ⇒ 黑屏/无图 |
| 菜单入口 | 用原九参数入口 `0x1e78870` 切 controller | 只到 `Application_Start` | 可能只是"进了入口但没进菜单态" |
| 帧完成协议 | HOSDAP02 提交 + 1002→1003/1005 配对 | 完全未实现 | 第二帧可能永远不返回 |
| 像素格式 | **RAMFB 输出 XRGB8888** | 我猜 RGBA4444/RGB565 都错 | 颜色判断全错的原因之一 |

## §7.1 的硬信息（BitBlit）
```
完整帧顺序：notifyDraw → drawFramebuffer
BitBlit 参数 144 B，真实生产者 = libObj grm::CmdRectDraw::stage1
callback 偏移 +0x80（Fill 是 +0x28 —— 不同！）
源图/临时图实际在 0x410...（旧地址白名单过窄）
必须实现：几何/stride/范围/重叠检查的同格式复制
然后显式启用 RGBA4444 / A8 字体着色 / source-over / tile / nearest
⚠️ 实际写入后才按原格式完成 —— 不能"无条件完成"解锁流水线
```

## 我现在的判断（优先级重排）
1. **先查 `/dev/ldec`**（资源解码）—— 若缺失，图片根本解不出来，其余都白搭
2. **确认显示路径**：我用 `bionz-streamfb` 抓帧是否根本读错了地方？
   a7c2-re 用 `ramfb`，说明这条路更成熟
3. **重新审视"帧全零"**：可能是（a）没有 LZ77 解码（b）没有帧完成协议（c）地址不对
   —— 三者我一个都还没排除

## 下一步
**先不做实验，先读 BOOT-TROUBLESHOOTING 的 §3/§4/§8/§9 全文**，
把"黑屏的三种证据层"和"ramfb 接入方式"看清楚，再决定改哪里。

---

# ⚠️ 2026-10-29 00:35 【概念纠正】a7c2-re 只能借**方法**，不能借**实现**

## 芯片代际边界（已核验）
```
ZV-1M2    6 个 ASTRA 资源文件 → CXD90045_k8 → BIONZ X  → ARMv7 / glibc 2.11.2
ZV-E10          ASTRA / CXD90045      → BIONZ X  → ARMv7 / glibc 2.11.2
ZV-1F           ASTRA / CXD90045_k8   → BIONZ X  → ARMv7 / glibc 2.11.2   ★ 同SoC+新菜单
──────────────────────────────────────────────────────────────────────
A7C II               → BIONZ XR → **aarch64 / glibc 2.25**
ZV-E10M2  ARGO / CXD90057 → BIONZ XR → ARM64
```
**⇒ **ZV-1M2 与 ZV-E10 是同一 SoC 家族（ASTRA）**，只是菜单软件一代更新。**
**⇒ a7c2-re 是 **BIONZ XR**，架构层面不同代。**

## 因此：a7c2-re 的哪些东西能用/不能用
| 能用（方法论） | 不能用（实现） |
|---|---|
| 黑屏的**分类判据**（5 类现象 → 各自该查什么） | `ramfb` 的具体配置（那是 XR 平台的显示桥，且绑定其 machine 模型） |
| **8 步推进顺序**的思路 | aarch64 内联汇编 / syscall 号 / glibc 2.17+ 符号 |
| "资源需 LZ77 解码"这个**概念** | 固件绝对地址（0x1e78870、0x410…、0x22c48000 都是各自平台的） |
| "帧需要完成协议才推进下一帧"这个**概念** | 像素格式结论（XRGB8888 是 ramfb 的输出，不是 Sony 面板格式） |
| "软件 GPE 必须在被测平台自行实现"这个**判断** | menu-observe.c 整份（3159 行，ARMv7 编译失败清单已给出） |

## ★ 我之前一个错误担心，现在撤回
**~~"我该改用 ramfb 而不是 bionz-streamfb"~~ ⇒ **这个担心是错的。**
**⇒ ZV-1M2 与 ZV-E10 同 SoC（ASTRA/CXD90045），显示硬件相同，
`bionz-streamfb`（chocotan 针对 CXD90045 做的设备）**应该就是对的路径**。**
**⇒ 不能用 A7C II（XR）的显示方案来否定 ASTRA 的显示方案。**

## 正确的参考优先级（重排）
1. **chocotan 的 ZV-E10 工作**（`/mnt/build/ref-chocotan-zve10`）——
   **同 SoC**，且对方已证明老菜单能出画 ⇒ **显示路径、设备模型、启动参数都可信**
2. **ZV-1F 固件**（`/root/fw/ref_zv1f`）—— **同 SoC + 新菜单** ⇒
   若能从它对比出新菜单相对老菜单在**帧协议/资源格式**上的变化，就是直接答案
3. **a7c2-re** —— 仅作**方法论**参考（黑屏分类、8 步思路）

## ⇒ 因此"帧全零"的候选原因收窄为（按同 SoC 前提）
```
① 新菜单的 appFw 需要额外的资源解码（LZ77/ldec）—— 老菜单不需要或方式不同
② 新菜单的帧完成协议与老菜单不同（HOSDAP02 / 1002→1003/1005 配对）
③ Application_Start 之后需要原框架生命周期入口才真正进入菜单绘制态
④ 我的抓帧地址/格式对**老菜单**成立，对新菜单未必（虽然同 SoC 但也可能不同缓冲）
```
**⇒ ① 和 ② 是**新菜单软件层**的差异，与芯片无关 —— 这才是该挖的方向。**

## 下一步（修正后）
1. **对比 ZV-1F（同 SoC 新菜单）与 ZV-E10（同 SoC 老菜单）** 的
   帧/资源相关库与字符串差异 —— 找出新菜单在显示链上的**新增依赖**
2. 不碰 a7c2 的实现细节

---

# ★★★★ 2026-10-29 00:55 第二十三层：同 SoC 两代对比 + ldec 假设被否

## ① 引擎同一性（重大发现）
```
ZV-1F libObj 导出 571 个符号
ZV-1M2 libObj 导出 571 个符号
共有 571，仅 ZV1F 0，仅 M2 0        ← 完全相同的符号集
```
**⇒ **ZV-1F 与 ZV-1M2 的新菜单引擎是同一个东西**（同 SoC + 同代软件）。
**⇒ ZV-1F 是比 a7c2 更贴切的参考（同 SoC、同菜单代）。**

## ② 老菜单 vs 新菜单的引擎架构差异
```
渲染入口符号        E10(老)   ZV1F(新)   M2(新)
drawFramebuffer        0        1         1
notifyDraw             0        1         1
updateFrame            0        1         1
processEvent           0        1         1
processSystemMessage   0        1         1
setScreenRotate        0        1         1
createImage            0        2         2
switchBank             0        1         1
cleanupCache           0        1         1
prefetchTextImage      0        1         1
```
**⇒ 老菜单引擎**一个都不导出**（渲染是内部的），新菜单引擎把它们**全部导出**。
**⇒ 这是两代架构最明确的界线。也解释了为什么对方 libuiobserve 的绘制钩子对 M2 失配
（它按 E10 老 libObj 的**内部偏移**装钩子，而新引擎是另一套导出符号体系）。**

## ③ 显示链路库集合：两代完全一致
```
两边的显示相关库：libInfraHdmi.so、libInfraStreamingUsb.so
libObj.so 里 /dev/streamfb 引用：E10=1，ZV1F=1
⇒ 显示硬件路径相同，不是差异点。
```

## ④ ldec 假设：提出 → 测试 → **否定**
**假设**：新菜单资源需 LZ77 解码，而 `/dev/ldec` 无驱动（`ldec.ko` 存在但 `/init` 从未加载）
⇒ 图片解不出来 ⇒ 黑屏。
**依据**：a7c2 项目正是踩了这个（"31 次原 LZ77 调用都因缺 /dev/ldec 失败"）；
`ldec.ko` 在 `/kmod/` 里（15,812 B）但原 init 只建节点不加载；M2 libObj 引用 `/dev/ldec` 3 次。

**实验**：`/init` 加入 `load /kmod/ldec.ko`，重编，用**确切 PID** 抓帧。
```
MODULE_BEGIN /kmod/ldec.ko
MODULE_END   /kmod/ldec.ko status=0     ← 加载成功
帧：非零 0 / 614,400，SHA=34c69899504b   ← 与之前完全一致
```
**⇒ **ldec 不是阻塞点**（或新菜单资源不走 LZ77）。假设否定。**

## ⑤ 当前确凿状态（只用确切 PID 的证据）
```
✅ 内核稳定启动，无 panic
✅ 14+1 个 kmod 全部 status=0（含新增 ldec.ko）
✅ bootin.elf 运行，libObj.so dlopen ×5 成功
✅ grm_init 解析、DMP_2D_Initialize 完成
✅ appFwBoot.so → Application_System_Init 返回 0
✅ appFw.so dlopen 成功（libNetCommon 修复）
✅ Application_Start ENTER
❌ 帧缓冲全零（SHA 34c69899504b，多次一致）
```
**⇒ 从未渲染过任何画面（之前"渲染成功"是抓错进程，已撤回）。**

## 下一步候选（按怀疑度）
1. **抓帧地址/格式**：0x22c48000 与 RGBA4444 是老菜单（chocotan/ZV-E10）的结论。
   **新菜单引擎是否用不同缓冲？** 需在 bootin 之后扫描全 RAM 找非零块（不只 0x22xxxxxx 一段）
2. **帧完成协议**：新引擎导出 `notifyDraw`/`drawFramebuffer`，
   但可能需要先有"帧请求"才画（a7c2 记录：完整帧顺序 `notifyDraw → drawFramebuffer`）
3. **Application_Start 之后是否真进入绘制态**：需要 hook 那两个导出符号（但要先证明探针无害）
4. **`libgpe.so` 缺失**：对方日志显示 `UI_LOAD libgpe.so result=(nil)`，
   老菜单容忍缺失，新菜单可能不容忍

## 方法论（第 32 条）
**"同代参考"的选择要看**两个维度**：SoC 代际 + 软件代际。
a7c2 = 新软件 + 新 SoC（只能借方法）；ZV-1F = 新软件 + **同 SoC**（可借实现）。
⇒ 选择参考时必须两个维度都对齐，否则会被"看起来很像"误导。**

---

# ★★★ 2026-10-29 01:20 第二十四层：libgpe 是幽灵注册；帧泵归属存疑

## ① libgpe.so：三棵树全都没有 ⇒ 不是问题
```
E10  ❌ 无
ZV1F ❌ 无
M2   ❌ 无          ← 连 ZV-1M2 原厂固件自己都没有
```
**但 M2 的 libIMDB 注册了它：**
```
GpeInit / GpeRes / GpeSus / libgpe.so
```
**⇒ **幽灵注册**：注册表里有，文件根本不存在（原厂也如此）⇒ 加载失败是**正常行为**，
对方日志里的 `UI_LOAD libgpe.so result=(nil)` 不是缺陷。**
**⇒ 我之前把它列为"候选原因 3"是错的，撤回。**

## ② E10 基线帧地址已确认
```
E10 基线（能出画）：guest_phys 0x22c48000 采样非零 3879  ✅
M2      （无画面）：同一地址采样非零 0
```
**⇒ 地址本身对**老菜单**成立。**
**⚠️ 但"新菜单是否用同一地址"仍未证明 —— 不过同 SoC 同显示硬件，大概率相同。**

## ③ 全 RAM 粗扫不具区分度
**1 MB 网格采样 4 KB，非零>400 的块有 **383 个**（几乎全 RAM 都有数据）。
⇒ 靠"非零密度"无法定位帧缓冲。需要别的判据（例如特定像素格式的统计特征）。**

## ④ ★ 新的关键疑问：新菜单的**帧泵归谁**
**已确证的事实链：**
```
E10 老引擎：drawFramebuffer/notifyDraw 等**一个都不导出** ⇒ 渲染在 libObj 内部循环里
M2 新引擎：上述符号**全部导出** ⇒ 需要**外部**来调它
appFw.so：把 drawFramebuffer/updateFrame 作为 **UND** 导入 ⇒ 应该由 appFw 调
但我反汇编 Application_Start：只有 6 个调用后 **return**，**不含事件循环**
```

**⇒ 所以"谁在循环里调 updateFrame/drawFramebuffer"是**未解之谜**。候选：**
1. **`im.elf` 的主循环**（E10 版 im.elf 只知道老协议；我已测 M2 版 im.elf，结果相同）
2. **某个 LIRO/显示任务**（需要帧请求才动 —— 对应 a7c2 记录的 `notifyDraw → drawFramebuffer` 顺序）
3. **`osal` 侧的一个周期任务**（由某个服务创建）

## ⑤ 下一步方向（比之前更聚焦）
**目标：确认 `drawFramebuffer` 是否**被调用过**。**
**做法：用 `LD_PRELOAD` interpose `_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv`，
纯转发 + 只打印前 N 次。**
**⚠️ 但必须先证明探针无害**（我此前 probe.so 打死过 E10 渲染）：
- **老菜单 libObj 不导出该符号 ⇒ 无法在 E10 上验证同一探针**
- **⇒ 改用"只 hook 不改变行为"的最小实现：构造时打印一行，hook 里直接转发，不改参数不改返回**

**若 `drawFramebuffer` 从未被调用 ⇒ 帧泵缺失（新引擎没被驱动）
若被调用但帧全零 ⇒ 绘制了但没画到 0x22c48000（地址/格式问题）**

### 方法论（第 33 条）
**"注册表里有但文件不存在"是**幽灵注册**，在 Sony 固件里是常态
（libgpe、以及更早发现的 libInfraFtpClient/libInfraSSHManager 同类）。
⇒ 判断"缺文件是否是问题"时，必须先确认**原厂固件自己有没有这个文件**，
不能只看"注册表提到它 + 加载失败"。**

---

# ★★★★★ 2026-10-29 01:45 第二十五层：帧函数**从未被调用** —— 问题定性为"帧泵未启动"

## 测量（Image.m2fp，确切 PID，探针纯转发）
**探针 `fprobe.so`（7,768 B）interpose 三个新引擎导出的帧函数，纯转发 + 只数次数：**
```
_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv
_ZN2ux6wgtsys12WidgetSystem11updateFrameEv
_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv
```

**结果：**
```
drawFramebuffer 调用次数: 0
updateFrame 调用次数:     0
notifyDraw 调用次数:      0
```

**同时确认探针本身无害（流程未被破坏）：**
```
Application_Start ENTER: 1
DMP_2D_Initialize:       1
serial = 13,989 B（与无探针时同量级）
帧 SHA = 34c69899504b（仍全零）
```

## ★★ 这个结果的意义：问题彻底换了一个性质
```
之前怀疑：地址不对？像素格式不对？画到别处了？
现在确定：**帧函数从未被调用 ⇒ 根本没画**，不是"画错位置"
```
**⇒ 因此"扫描全 RAM 找帧缓冲"这条路**不必再走**（不会有帧缓冲内容），
⇒ 目标判据改为：**让 `drawFramebuffer` 被调用**。

## 新的核心问题：谁应该调它？
**已知：**
- `appFw.so` 把这三个符号作为 **UND 导入** ⇒ appFw 内部**有**调用点
- 但 `Application_Start`（72 B）只是 6 次调用后返回，**不含事件循环**
- 所以调用点应该在 appFw 的**其它函数**里，由**某个事件/定时器**触发

**候选驱动源（按可能性）：**
1. **显示/帧请求**：新架构可能是"请求→绘制→完成"三段式（对 a7c2 记录：`notifyDraw → drawFramebuffer`）
   ⇒ **谁发第一个帧请求？** 可能是 LIRO 显示任务或 `im.elf`
2. **定时器/周期任务**：`utimer` 驱动的 UI tick
3. **输入事件**：首帧可能等第一次按键/触摸
4. **某个服务 init 后主动触发首帧**：我可能漏了某个服务

## 下一步（明确的三条，按可执行性）
**① 静态定位调用者**：在 `appFw.so` 里找 `bl` 到 `drawFramebuffer@plt` 的调用点，
   反汇编其所在函数，看它由什么条件驱动。（纯静态，最便宜）
**② 确认帧请求的发起者**：查 `libObj` 里是否有 "frame request"/"draw request" 之类的字符串或 API
**③ 主动触发**：若找到入口（如某个 `xxx_request_frame()`），用探针在 bootin 启动后主动调一次

**优先 ①。**

### 方法论（第 34 条）
**"没有画面"有三个完全不同的层级：**
```
(a) 没画（帧函数未被调用）
(b) 画了但画在别处（地址/格式错）
(c) 画了但没显示（显示桥错）
```
**⇒ 必须先确定在哪一层，否则会在错误层级上浪费大量时间。
本次用 3 个 interpose 计数（纯转发、不改变行为）一次就确定了是 (a)。**

---

# ★★★★★ 2026-10-29 02:10 第二十六层：完整解出帧循环，阻塞点收窄到 Application_Start 前 3 个调用

## ① 扫到了 drawFramebuffer 的唯一调用点
**方法**：写 ARM/Thumb BL+BLX 扫描器查 `bl`/`blx` 到 PLT 桩（`0x529684`）。
**⚠️ 先做正对照**：`malloc@plt` → 16 处 ✅（扫描器有效）；
但 `memcpy@plt`/`memset@plt` → 0 处（可能被内联或走 `__memcpy_chk`，不影响结论）。
**⚠️ 第一版扫描器漏了 Thumb `BLX`（h2 bit12=0）—— 正对照当场暴露，修正后才有效。**

**结果：`drawFramebuffer` 在 appFw.so 里**只有 1 处调用**：**
```
0x0054ed82   THUMB_BLX → drawFramebuffer@plt
```

## ② 帧循环全貌（0x54ed12 内）
```asm
54ed1e: blx UXEngine::wgtsys()
54ed22: blx WidgetSystem::updateCurrentTime()
54ed26: movw r1, #0x3f6 (1014)
54ed2c: blx 0x9245a8                     ; 分发（id=1014）
54ed32: blx WidgetSystem::processEvent()  ; ← 事件循环
54ed3e: bne 54ed32                        ; while (processEvent()!=0)
54ed46: blx ResourceManager::processCommands()
; ── 帧延迟循环 ──
54ed4c: blx isFrameDelay()
54ed54: cbz → 54ed90
54ed5a: blx updateFrame()
54ed62: b 54ed4c
; ── 绘制段（顺序、无条件）──
54ed90: cmp r6,#0 ; beq 54ed6a  （r6 决定是否走 54ed64 的预处理）
54ed6a: blx 0x925544(0)                   ; 状态机：置 0
54ed74: blx WidgetSystem::notifyDraw()
54ed7a: blx 0x925544(1)                   ; 状态机：置 1
54ed82: blx GraphicsEngine::drawFramebuffer()   ★ 唯一绘制
54ed86: movs r0,#2
54ed8c: b.w 0x925b0c                      ; 尾调用（状态 2）
```

**⇒ `drawFramebuffer` 在该函数末尾**无条件**到达（只要函数被调用并跑完）。**
**⇒ 探针测到 0 次 ⇒ **该函数（0x54ed12）根本没被执行**。**

## ③ 反查调用链
```
0x54ed12 ← 被 2 处调用: 0x54f5f6, 0x54f850   （都在 0x54f664 内）
0x54f664 ← 被 1 处调用: 0x54d5ac             （= Application_Start 的第 4 个调用）
```
**Application_Start 的 6 个调用顺序：**
```
#1 0x92559c  (ARM态)
#2 0x54d154  (vtable 构造)
#3 0x54d2b0  (初始化序列)
#4 0x54f664  ← 含帧循环，★ 未到达
#5 0x923c84  (ARM态)
#6 0x54d538  (vtable 构造)
```

## ★★ 结论：阻塞在 Application_Start 的 **#1/#2/#3** 之一
**探针已证明：`Application_Start ENTER` 出现过，但没有返回，且 #4 的帧循环从未执行。**
**⇒ 阻塞位置就在前三个调用内。**

**⚠️ 注意：`Application_Start` 是**被动初始化**，真正的主循环是 0x54ed12
（updateCurrentTime → processEvent 循环 → updateFrame → notifyDraw → drawFramebuffer）。
**这个主循环本该由 #4（0x54f664）里的某处**反复调用**（0x54f5f6 / 0x54f850 两处）。**

## 下一步（两条）
**① 用 gdb 采样用户态 PC**（卡住时多次采样，取落在 appFw 地址范围的样本）
⇒ 直接得到阻塞在 #1/#2/#3 的哪个。
**② 或反汇编 #1/#2/#3 三个函数**，看哪个含等待原语
（#1 是 ARM 态 `0x92559c`，#2/#3 是 Thumb）。

**优先 ②（纯静态，便宜）。**

### 方法论（第 35 条）
**扫描器/正则类工具必须**先跑正对照**。
本次第一版漏了 Thumb BLX，正对照（malloc）也是 0 却因为我把 0 当成"符合预期"而差点放过；
是 `memcpy/memset 也是 0` 这个**不一致**才让我回头检查。
⇒ 正对照要选**明确应该非零**的样本，并且**多个**样本互相印证。**

---

# ★★★★ 2026-10-29 02:35 第二十七层：三个候选都无外部调用；#1 是 veneer；gdb 采样受 -s 干扰

## ① #1/#2/#3 的 PLT 调用：**全部为 0**
**扫描器已用帧循环做正对照（16 个 PLT 调用全对）⇒ 0 是真的。**
```
#1 0x92559c → 0 个 PLT 调用
#2 0x54d154 → 0 个
#3 0x54d2b0 → 0 个
```
**⇒ 三个函数都不直接调用外部库，等待必然在更深层。**
**⇒ #2/#3 从反汇编看只是 vtable/构造器（`str r3,[r5,#0]` 后 `pop {...,pc}`），很快返回。**

## ② #1 的真实身份：**ARM 长跳转 veneer**
**`.text` 有 64 MB（0x27bdfc8），超出 ARM BL 的 ±32 MB 范围 ⇒ 需要 veneer 间接跳转。**
**字节模式按 LE 解码：**
```
0x92559c: e59fc004  LDR ip,[pc,#4]
0x9255a0: e08fc00c  ADD ip, pc, ip
0x9255a4: e12fff1c  BX  ip
```
**⇒ 目标 = 0x9255a8 + 字面量 0x023097dd = **0x2c2ed85****
**（另一处 0x9255ac → 0x1f343a1）**

**0x2c2ed85 处的函数：栈帧 440 B，还继续调用 0x2ce819c / 0x25c848a / 0x2ce8754 …**
**⇒ 静态追链已经很深，成本高。**

## ③ gdb 用户态 PC 采样：**失败（受 -s 干扰）**
```
10 次 PC 采样：全部 0x602be3b4 或 0x6014dfec
带符号回溯：ambauart_console_read (drivers/tty/serial/amba2.c:810)
            while (UART_GET_FR(port) & UART01x_FR_RXFE)
serial = 14,115 B（失败模式）
```
**⇒ 带 `-s` 的运行**又 panic 了**（与第 21 层观察一致：`-s` 会扰动时序）。
**⇒ **gdb 用户态采样这条路在当前配置下不可用**，除非能消除 -s 的扰动。**

## ④ 下一轮方案：改用**纯 preload 探针**覆盖整个帧循环的入口函数
**帧循环（0x54ed12）里的 PLT 调用全都可 interpose：**
```
updateCurrentTime / processEvent / processCommands / isFrameDelay
updateFrame / notifyDraw / drawFramebuffer
```
**⇒ 为这 **7 个**全部做纯转发计数探针：**
- **全部为 0 ⇒ 帧循环函数（0x54ed12）从未启动 ⇒ 阻塞在它之前的调用链**
- **有任何一个非 0 ⇒ 帧循环启动了，卡在某个具体调用 ⇒ 直接定位**

**这比静态追 veneer 链便宜得多，且不引入 -s 干扰。**

### 方法论（第 36 条）
**gdbstub（`-s`）会改变仿真时序，在"边缘稳定"的镜像上直接导致 panic。
⇒ 需要运行时观测时，**优先用 guest 内的 preload 探针**（不影响宿主时序），
gdb 只用于**已经稳定**的场景。**

---

# ★★★★★ 2026-10-29 03:05 第二十八层：7 个探针全部为 0 —— 帧循环确定从未启动

## 测量（Image.m2fp7，确切 PID=1195825，实例数=1）
**探针 `fp7.so`（8,332 B）interpose 帧循环里的**全部 7 个 PLT 入口**，纯转发计数：**
```
_ZN2ux6wgtsys12WidgetSystem17updateCurrentTimeEv     → 0
_ZN2ux6wgtsys12WidgetSystem12processEventEv          → 0
_ZN2ux6resmgr15ResourceManager15processCommandsEj    → 0
_ZNK2ux6wgtsys12WidgetSystem12isFrameDelayEv         → 0
_ZN2ux6wgtsys12WidgetSystem11updateFrameEv           → 0
_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv            → 0
_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv     → 0
```
**流程未受影响：serial 13,988 B（与无探针同量级）。**

## ★ 结论（现在是硬结论）
**帧循环函数 `0x54ed12` **从未执行**。**
**⇒ 阻塞点在 `Application_Start`（0x54d581）内部，且**早于**帧循环的调用点。**
**⇒ 静态已知：#4（`0x54f664`）内部在 `0x54f5f6` / `0x54f850` 调帧循环；
       #1 是 veneer → `0x2c2ed85`（440 B 栈帧，继续调 `0x2ce819c`/`0x25c848a`/`0x2ce8754`…）。**

## 下一步：**guest 内采样探针**（沿用第 36 条方法论：不用 gdbstub）
**方案：在 preload 库里装 `setitimer(ITIMER_PROF)` + `SIGPROF` 处理函数，
在 handler 里记录被打断的 PC（`ucontext` 的 `arm_pc`），累积成直方图，定期输出到串口。**

**优点：**
- **完全在 guest 内，不扰动宿主/QEMU 时序**（不会像 `-s` 那样诱发 panic）
- **直接回答"CPU 时间花在哪"** —— 若卡在某个等待，PC 会集中在同一处
- **不需要知道调用链**，静态追 veneer 的高成本可以跳过

**判据：**
- **PC 集中在 appFw 的某偏移 ⇒ 直接得到阻塞函数**
- **PC 集中在 libc（如 `pthread_cond_wait`/`select`）⇒ 是在等某个条件**
- **PC 分散 ⇒ 是在跑（不是卡住），只是还没到帧循环**

### 方法论（第 37 条）
**"多个独立探针全部为 0" 比"单个探针为 0" 强得多：**
- 单点为 0 可能是"该符号没被解析到我的实现"
- **7 个点全为 0，且流程仍能走到 Application_Start ⇒ 探针机制确实生效，结论可信**
**⇒ 做否定性判断时，要尽量用多个互相独立的观测点同时证伪。**
