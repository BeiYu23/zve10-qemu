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

---

# ★★★★★ 2026-10-29 03:35 第二十九层：guest 内采样 profiler 成功 —— appFw 在**忙**，且以 BLE assert 收尾

## 工具（`samp.so`，7,488 B）
**`ITIMER_PROF` 150ms + `SIGPROF` handler 记录 `ucontext->uc_mcontext.arm_pc`，直接写串口。
完全在 guest 内 ⇒ 不扰动 QEMU 时序（规避了 `-s` 诱发 panic 的问题，见第 36 条）。**

## 数据（Image.m2samp，MYPID=1203824）
```
采样总数 84
PC 高位分布: 0x2e=15  0x2f=32  0x30=15  0x04=14  其它=8
样本高度分散（每个 PC 只出现 1~2 次）
```
**⇒ **PC 分散而不是集中在同一处 ⇒ 该进程在**活跃执行**，不是阻塞等待。**

## ★ 样本落在哪个模块
**appFw.so 的 `.text` 是 vaddr `0x52ab00`、大小 `0x27bdfc8`（约 41.8 MB）。**
**若 dlopen 基址在 `0x29000000`–`0x2b000000` 一带，则其代码段覆盖 `+0x52ab00`..`+0x7A8AAC8`：
```
base=0x29000000 ⇒ 代码段 0x2952ab00-0x30a8aac8   命中 62/84
base=0x2a000000 ⇒ 代码段 0x2a52ab00-0x31a8aac8   命中 62/84
base=0x2b000000 ⇒ 代码段 0x2b52ab00-0x32a8aac8   命中 62/84
```
**⇒ **74%（62/84）的 CPU 时间花在 **appFw 的代码里**。**

## ★★ 与日志尾部结合
```
!!! GPE HW: PSC dummywrite disabled.
assert at infra_wrapper_bluetooth.cpp:1886
function:void infra_wrapper_bluetooth::detail::load_im()
```
**⇒ 运行以 **BLE 固件镜像加载 assert** 收尾。**

## ★★★ 新假设（解释力最强）
**appFw 卡在 **BLE 固件加载的重试/等待循环** 里：
反复尝试 `load_im()` → 失败 → assert → 重试，占用 CPU（所以样本分散），
**永远走不到后面的帧循环**（所以 7 个帧循环入口全为 0）。**

**这与"移除 `libInfraBluetoothLowEnergy.so` 后能往下走一点"一致：
少了一条 BT 路径，但 appFw 内部还有别的 BT 路径（`infra_wrapper_bluetooth` 在 appFw 自身）。**

## ⚠️ 本轮探针的缺陷（下轮修）
**没打印 PID ⇒ 84 个样本混了 `/init` shell、insmod、bootin、im.elf 等多个进程。
虽然 74% 落在 appFw 区间已足够强，但下轮应加 `getpid()` 以便按进程分离。**

## 下一步（两条，按可执行性）
**① 给 BLE 提供可用的固件镜像/设备节点**，让 `load_im()` 不再失败重试
   - 查 `load_im()` 读哪个路径/设备（反汇编 `infra_wrapper_bluetooth::detail::load_im`）
   - 或伪造该文件使加载"成功"
**② 修采样器加 PID**，确认卡住时到底哪个进程、PC 是否真的在 BLE 相关偏移

**优先 ①（直接解阻塞）。**

### 方法论（第 38 条）
**"样本分散" vs "样本集中" 是区分"忙"与"等"的关键判据：
- 分散 ⇒ 在跑（可能陷入重试/轮询循环）
- 集中在 1~2 个 PC ⇒ 卡在自旋或断点
**⇒ 采样 profiler 比"探针计数为 0"更进一步：后者只说"没到那里"，
前者能说"时间花在哪"。**

---

# ★★★ 2026-10-29 04:05 第三十层：补齐 /system 未解决 BLE assert

## 发现：initramfs 的 /system 是**空目录**
```
我的 initramfs-ui/system/       → 0 个文件
M2 nflasha3 (/system)           → av-cam.bin(17MB) bonobo.bin bt_firm.hcd(60KB)
                                  initrd.img ldr_drv.bin sabin/ vmlinux.bin wole_app.bin
E10 nflasha3                    → 同结构（bt_firm.hcd 61,787 B）
```
**⇒ `bt_firm.hcd`（蓝牙固件）在 /system 里，而我的 initramfs 完全没有 /system 内容。**
**⇒ 这解释了 `load_im()` 失败 + assert 的**直接原因**。**

## 实验：补齐 /system 后重跑（Image.m2sys，cpio 275,110,400 B）
**补入**：`bt_firm.hcd`、`bonobo.bin`、`wole_app.bin`、`ldr_drv.bin`、`sabin/`（只取到 1 个文件 364K）
**结果：**
```
BLE assert 出现次数: 2      （仍存在）
帧循环 7 入口: 全部 0        （仍未启动）
serial: 13,989 B
```
**⇒ **补齐 /system 没有解决**。**
**⚠️ 但注意：`sabin/` 只提取到 1 个文件（364K），提取逻辑有缺陷，可能没取全。**
**⇒ 不能据此完全否定 /system 的作用，只能说"补这几个文件不够"。**

## ★ 下一步的关键改进（必须做）
**当前采样 profiler 只记 PC，**不知道 appFw 的加载基址**，
所以无法把样本 PC 换算成 appFw 内的偏移 ⇒ **无法定位到具体函数**。**

**改进：在 preload 库的构造函数里用 `dladdr()` 取某个 appFw 导出符号的地址，
得到 appFw 的加载基址并打印。**
```c
Dl_info info;
if (dladdr((void*)&some_appFw_symbol_or_dlsym_result, &info))
    print base = info.dli_fbase;
```
**⇒ 有了 base 就能算 `offset = pc - base`，再与 appFw 的符号表/反汇编对照，
直接得到"CPU 花在哪个函数"。**

**⚠️ 更简单：`dlsym(RTLD_DEFAULT, "Application_Start")` 得到绝对地址，
减去已知的 vaddr 0x54d581 即得 base。**

## 方法论（第 39 条）
**采样 profiler 的价值取决于"能否把样本地址映射回源码/函数"。
只有 PC 而没有加载基址 ⇒ 样本等于无用。
⇒ 设计采样器时，**基址记录和 PC 采样同等重要**，必须在同一轮里拿到。**

---

# ★★★★★ 2026-10-29 04:35 第三十一层：采样器成功映射到"模块+偏移"，并得到关键观察

## 工具演进
```
samp.so  (v1) 只记裸 PC，无基址 ⇒ 无法定位（第 39 条指出的缺陷）
samp2.so (v2) 构造时 dlsym("Application_Start") 取基址
             ❌ 失败：构造时 appFw 还没 dlopen，g_base 恒为 0，样本全被过滤
samp3.so (v3) ✅ 在 handler 里用 dladdr(pc) 反查所属模块与基址
```
**⇒ v3 可用，且自动带上模块名。**

## 实测（Image.m2s3）
```
S3 pid=123 mod=appFw.so base=0x2dfb6000 off=0x542524
S3 pid=123 mod=appFw.so base=0x2dfb6000 off=0x59d040
...
样本总数 58，全部属于 appFw.so，进程 pid=123（= im.elf）
```
**偏移分布（各 1 次，高度分散）：**
```
近区（Application_Start=0x54d581 附近）:
  0x542524  0x558e9a  0x59d040
中区:
  0x629a7a  0x636fca  0x6e4a6e  0x713b6c
远区（对应之前解出的 veneer 目标 0x2c2ed85 一带）:
  0x26fc160  0x27e0eae  0x2827e20  0x2b6a702  0x2c66fba  0x2caa728
```

## ★★ 关键观察
**1. 所有样本都在 appFw.so 内，进程是 im.elf（pid=123）**
**2. 偏移**高度分散**（近区 + 中区 + 远区都有）⇒ **不是在某个自旋/死等，而是在**连续执行**大量不同函数**
**3. 样本覆盖了 `Application_Start`（0x54d581）**附近的代码**，
   说明 **`Application_Start` 确实进入了，并且在执行它调用的那些函数**
**4. 远区偏移（0x26xxxxx–0x2caxxxx）与之前解出的 veneer 目标 0x2c2ed85 同带
   ⇒ 说明那些 veneer 调用**确实发生了**（即 #1 之类的调用在执行）**

## ⇒ 逻辑推论
**`Application_Start` 已经进入并**在执行大量代码**（占满 CPU），
但 **7 个帧循环入口仍然全为 0** ⇒
**它消耗的时间在"到达帧循环之前"的初始化里，且这个初始化很长（或陷入循环）。**

**结合日志尾部的 `infra_wrapper_bluetooth::detail::load_im()` assert，
最可能：**BLE 镜像加载路径内部有一个长循环/重试**，占掉了全部 CPU。**
（注意：补齐 `/system/bt_firm.hcd` 未解决，可能路径/内容/接口不匹配）

## 下一步（精确化）
**① 把 58 个样本的偏移与 appFw 符号表对照**，找出重复出现的函数（哪怕各出现 1 次，
   也可以看它们是否集中在某个调用链上）
**② 提高采样密度**（150ms → 20ms）并跑更久，让热点函数浮现（出现多次）
**③ 采样里同时记录**线程 ID**（`syscall(SYS_gettid)`），区分主线程与 UI 线程**

**优先 ②（提高密度最容易，且能让热点自动浮现）。**

### 方法论（第 40 条）
**采样器"只记 PC"和"记 PC+基址+模块名"是**质变**：
前者只能看出"地址在哪里"，后者能直接说"哪个库的哪个偏移"。
⇒ 观测工具必须**自带可解释性**（模块名/基址/偏移），否则数据要二次加工才能用。**

---

# ★★★★★ 2026-10-29 05:05 第三十二层：高密度采样 374 点 —— 执行极度分散，无热点

## 配置
**采样 150ms → **20ms**，上限 2000 点，跑 110 秒（Image.m2s3b）**

## 结果
```
样本总数 374
按模块: appFw.so=357 (95.4%)  libObj.so=11  libmpr.so=6
按 PID : 全部 374 个来自 **pid=124**（单一进程，= im.elf）
热点   : **没有任何偏移出现 ≥3 次**；最高只有 2 个偏移各出现 2 次
日志尾部: assert at infra_wrapper_bluetooth.cpp:1886 / load_im()
```

## ★★ 这个分布说明什么
**单进程、95% 在 appFw、但 374 个样本里**没有热点**（最大 2 次）
⇒ **不是"卡在某个函数自旋"，而是**连续流过大量不同的函数**。**
**⇒ 两种最可能的解释：**
1. **一个很长的初始化序列**（几百个函数依次执行，尚未走完）
2. **反复重跑同一批初始化**（每轮都经过大量函数，但轮次不多到形成热点）

**结合日志末尾的 `load_im()` assert ⇒ 强烈倾向 2：**
**BLE 镜像加载失败 → assert → 上层重试 → 再走一遍大量初始化函数。**

## 已排除/已确认（本阶段累计）
| 项 | 状态 |
|---|---|
| 内核 panic | ✅ 已消除（/init 最小化） |
| appFw.so 加载失败 | ✅ 已修复（libNetCommon 换 M2 版） |
| 帧循环未启动 | ✅ **确证**（7 入口全 0） |
| 帧是"画到别处" | ✅ 排除（全 RAM 扫描无内容） |
| 抓帧地址/进程错 | ✅ 已修正（确切口 PID，之前抓错对方进程） |
| BLE 固件缺失 | ⚠️ 补齐 /system 后仍 assert（sabin 未取全，未完全排除） |
| 执行是否阻塞 | ✅ **不是阻塞，是长/重复执行**（采样分散） |

## 下一步（三条，按可执行性）
**① 反汇编 assert 附近的代码**，看 `load_im()` 失败后是"返回/继续"还是"重试"
   —— 若重试，找到重试循环的上层（谁反复调它）
**② 采样时记录**调用链**（用 `backtrace()` 存下每层返回地址，或至少 LR）
   —— 有调用链就能知道"这 374 个分散样本属于哪条主链"
**③ 记录线程 TID**，确认是否只有主线程在跑（UI 线程是否根本没被创建）

**优先 ①（纯静态，不需再跑 QEMU）。**

### 方法论（第 41 条）
**"无热点"本身是信息：
- 有热点 ⇒ 自旋/死等/单点重试
- 无热点 + 单进程 + 长日志 ⇒ **长初始化或反复重跑**
⇒ 采样分布的形状（集中 vs 分散）比单个热点更有诊断价值。**

---

# ★★★★★ 2026-10-29 06:20 第三十三层：assert 是**我移除 BLE 库的后果**，不是独立阻塞

## 本轮工作
**① 查 a7c2 的蓝牙处理方式（同代新菜单参考）**
```
a7c2 资产清单里蓝牙固件放在**两个位置**：
  menu-root/system/bt_firm.hcd        43,624 B
  menu-root/usr/bin/bt_firm.hcd       60,143 B   ← 还有一份在 /usr/bin
  menu-root/usr/bin/bt_firm_sar.hcd   1,738,760 B
且 prepare-menu-initramfs.sh 明确：
  for file in "$boot/"*.bin "$boot/"*.hcd; do cp -a "$file" "$root/system/"; done
⇒ 他们的做法：把 boot 资产全部塞进 /system
```

**② 发现我的树里蓝牙固件是**混搭**的**
```
system/bt_firm.hcd          60,143 B  = M2 版（我从 nflasha3 拷的）
usr/bin/bt_firm.hcd         61,787 B  = **E10 版**（M2 原版是 60,143）★不一致
usr/bin/bt_firm_sar.hcd     54,028 B
```
**修复：/usr/bin 两个文件都换成 M2 版（md5 208b064a / 374229da）**

**③ 修复后重跑（Image.m2bt）**
```
BLE assert 出现次数: 2         （仍存在）
帧循环 7 入口: 全部 0
帧采样非零: 0
```
**⇒ **固件版本不一致不是原因**。**

## ★★ 真正的解释：assert 是我自己造成的
```
我为了越过 InfraBluetoothLowEnergy_init 的阻塞，
**移除了 libInfraBluetoothLowEnergy.so**
而 appFw 里的 infra_wrapper_bluetooth::detail::load_im() 正是调用该库的 API
⇒ 库不存在 ⇒ 调用失败 ⇒ assert ⇒ 上层重试 ⇒ 大量 CPU 时间落在 appFw 里（与采样数据吻合）
```
**⇒ 上一阶段观察到的"进程在 appFw 里忙碌、无热点、以 load_im assert 收尾"
**全部是我移除 BLE 库的副作用**，不是原始阻塞。**

## 因此路线必须修正
**原路线（错误）：移除 BLE 库 → 越过阻塞 → 期待出画**
**正确路线：**让 `InfraBluetoothLowEnergy_init` **成功**（而不是跳过它）**

**下轮要做的：**
1. **恢复 libInfraBluetoothLowEnergy.so**
2. **查它 init 时阻塞在什么**（用什么采样器/探针看）
   - 可能的依赖：BT 内核驱动、`/dev/` 节点、`/system/bt_firm.hcd`、某个 osal 队列
3. **对照 E10 老菜单**：E10 的 BLE init 为什么能过？（同 SoC，老菜单也初始化 BLE）

### 方法论（第 42 条）
**"移除组件越过了阻塞" ≠ "该组件是问题"：
它也可能是**下一个阻塞的原因**（因为该组件被别处依赖）。
⇒ 移除前先查"谁依赖它"，移除后若出现**新的**失败，要怀疑是自己造成的。**

---

# ★★★★★ 2026-10-29 07:00 第三十四层：恢复 BLE 库后 —— assert 消失，阻塞变成**等待**而非忙等

## 决定性对照（同一棵树，只改 BLE 库有无）

| 配置 | BLE assert | 采样数(100s@20ms) | 服务序列终点 | 状态判读 |
|---|---|---|---|---|
| **移除** `libInfraBluetoothLowEnergy.so` | **2 次** | **374** | `Application_System_Init` | **忙等/重试循环** |
| **恢复** `libInfraBluetoothLowEnergy.so` | **0 次** | **10** | **`emg_init`** | **等待（CPU 空闲）** |

**⇒ 两个结论：**
1. **assert 确实是我移除 BLE 库造成的**（恢复后 0 次）——第 42 条方法论成立
2. **恢复后能**越过** `InfraBluetoothLowEnergy_init`，继续到 `emg_init`
   ⇒ 说明 **BLE init 不是死阻塞，而是很慢/需要等待，但最终能过**

## ★ 当前真实状态（BLE 库存在 = 正确配置）
```
✓ 服务解析到 emg_init（越过了 BLE）
✓ CPU 几乎空闲（100 秒仅 10 个采样）⇒ **在等待，不是在跑**
✗ 帧循环 7 入口仍为 0
✗ 串口 12,037 B（比之前少）
```
**⇒ 阻塞性质变为"等待某个事件/超时"，而不是"忙循环"。**

## 下轮方向（清晰）
**① 等待的是什么？** 12 KB 串口日志 + 10 个采样不足以定位。需要：
   - **拉长运行时间**（例如 300 秒）看它是否**自己继续**（可能只是超时很长）
   - **看 wchan**：但 QEMU 里读不到 guest /proc/.../wchan（除非 /init 里加）
**② 最便宜的第一步：**`/init` 里在启动 bootin 后**定期 dump `/proc/<pid>/wchan` 和 `/proc/<pid>/stat`**
   （`/init` 是 shell，可以直接做；之前我删掉了 /proc dump 是因为它导致 kmsg 写满，
    现在改为**只 dump 目标进程的 wchan，且限量**）
**③ 若发现等在某个 osal 队列/设备 ⇒ 对应补 fake 实现**

### 方法论（第 43 条）
**"忙"与"等"是两种完全不同的故障，采样数是最好的判据：**
- **100 秒内采样数接近理论上限 ⇒ 忙（自旋/重试）**
- **采样数极少 ⇒ 等（阻塞在 I/O、信号量、超时）**
**⇒ 同一个二进制，只改变一个库的有无，就从"忙"变"等"——说明该库参与了状态机。**

---

# ★★★★★ 2026-10-29 07:30 第三十五层：a7c2 如何"跳过设备、直接进菜单"（用户问题的答案）

## 他们的做法（BOOT-TROUBLESHOOTING §2 步骤3 与 §6）
```
现象：初始只有屏集 776（屏ID 0x308）+ 28 项对话框/启动控件，GPE 只有清屏。
      直接构造菜单、或拿 presenter 调 controller 方法 → **崩**。
      原因：两个 dispatcher 不同
        0x1e4bacc → 工厂表 +0  (presenter)
        0x1e4bb68 → 工厂表 +8  (controller)
        表项 +0x14 是 workspace 索引，**不是屏ID**
        0x308 与 0x346 共用 index0 池 ⇒ 裸构造会覆盖存活对象

解决：使用固件**自身 boot 已调用的完整九参数生命周期入口 `0x1e78870`**，
      在**原 UI 线程自然首帧返回后**、带前置检查地**请求菜单屏 0x346**。
      保留：退出、析构、清槽、构造、登记、虚 init、后续请求泵。
      **不强写模式值。**

验证：日志出现 MENU_APPLY_ENTER/RETURN、菜单ctor、MODE_SLOT4 mode=menu，
      随后原请求泵自然创建 0x2ca/count44，之后发出菜单绘图请求。

限制（他们自己标注）：这是**受控菜单入口，不是正常 power-on 全链完成**。
```

## ★ 与我们的情况对比（关键差异，别照抄）
| | a7c2 | 我们（ZV-1M2 树） |
|---|---|---|
| 平台 | aarch64 / glibc 2.25（BIONZ XR） | **ARMv7 / glibc 2.11.2（BIONZ X / ASTRA）** |
| 菜单代 | 比我们新 | 较早 |
| 已到达状态 | **屏 0x308 已建、28 个控件存在、UI 线程有首帧** | **帧循环根本没启动**（7 入口全 0） |
| 阻塞点 | 正常 power-on 链未完成 | 服务序列停在 `emg_init`，CPU 空闲（在等） |

**⇒ 关键差异：a7c2 是"已经有一个能跑的 UI 线程 + 启动屏"，只是 power-on 链没走完，
      所以他们能"在首帧返回后切菜单"。
⇒ 我们更靠前：**连帧循环都没启动**，没有"首帧返回"可言。**

## ⇒ 因此 a7c2 的招法**不能直接套用**，但思路可借：
**"不等设备就绪，而是用固件自身的入口主动把 UI 拉起来"**

**我们需要的等价物（下轮要找）：**
1. **M2 appFw 里"九参数生命周期入口"的等价函数**
   （a7c2 是 `0x1e78870`，我们没有这个地址，得自己找）
2. **M2 的启动屏 ID 与菜单屏 ID**
   （a7c2 是 0x308 → 0x346；M2 的数值必然不同）
3. **工厂表 / dispatcher 的对应结构**
4. **前提：先让帧循环启动** —— 否则切了屏也不会画

## 更现实的下一步（两条）
**① 先解"为什么帧循环不启动"**（这才是我们的真正阻塞）
   - 服务序列停在 `emg_init`，CPU 空闲 ⇒ 在等某个设备/事件
   - **`emg_init` 是什么**？（emergency manager?）查它 init 里等什么
**② 或按 a7c2 思路"主动拉起 UI"** —— 但需要先定位 M2 的生命周期入口，
   且**帧循环不启动时切屏无意义**

**⇒ 优先 ①。**

### 方法论（第 44 条）
**参考别人方案前，先对齐"两边的起始状态"：
a7c2 的问题起点是"启动屏已建、只差切菜单"，
我们的起点是"帧循环未启动、UI 线程无首帧"。
**起点不同 ⇒ 同一个招法不可移植**，只能借思路。**

---

# ★★★★ 2026-10-29 08:00 第三十六层：emg_init 是 6 字节桩，不是阻塞点

## 静态结论
```
libEMG.so 总大小 5,872 B
emg_init        size = 6      ← 极小的桩函数，不可能阻塞
emg_res         size = 6
emg_sus         size = 6
emg_notice_init size = 6
emg_notice_thread size= 6
emg_check_memory_shortage size = 332   ← 唯一有实质逻辑的
```
**⇒ 服务序列停在 `emg_init` **只是日志的最后一条**，它本身立刻返回。**

## 因此重新定位阻塞
**已确证：**
1. **`Application_Start` 被进入但**从未返回**（argtrace 只打出 ENTER）**
2. **帧循环（在 `Application_Start` 的 #4 `0x54f664` 内）从未执行**（7 个探针全 0）
3. **CPU 空闲**（100 秒仅 10 个采样）⇒ **在等待，不是在忙**
4. **服务序列 40 个后无输出**

**⇒ 三点交叉推出：`Application_Start` **阻塞在它的 #1/#2/#3 之一里，且是"等待"性质**。**
- #2（`0x54d154`）、#3（`0x54d2b0`）从反汇编看是 vtable/构造器，很快返回
- **#1 是 veneer → `0x2c2ed85`（440 B 栈帧，继续调 `0x2ce819c`/`0x25c848a`/`0x2ce8754`…）**
- **⇒ 等待最可能在 #1 的那条链里**

## 下轮：取证"到底等什么"
**方案：`/init` 里加**限量**的 wchan 监视**
```sh
( bootin ... ) &
( for i in 1 2 3 4 5 6; do
    sleep 15
    for p in /proc/[0-9]*; do
      c=$(cat $p/comm 2>/dev/null)
      case "$c" in im.elf|bootin.elf)
        echo "WCHAN $c $(cat $p/wchan 2>/dev/null) state=$(awk '{print $3}' $p/stat 2>/dev/null)"
      ;; esac
    done
  done ) &
```
**6 轮 × 2 进程 = 12 行，不会写满 kmsg（吸取之前 /proc 全量 dump 的教训）。**

**判据：**
- **wchan 是 `osal_wai_sem` / `osal_rcv_msg_tmo` ⇒ 等 osal 队列/信号量**
- **wchan 是 `futex` / `poll` ⇒ 等 pthread/设备**
- **wchan 是 `msleep`/`schedule_timeout` ⇒ 定时等待（可能只是超时长）**

### 方法论（第 45 条）
**"日志最后一条" ≠ "阻塞点" —— 又一次上当：
服务序列停在 `emg_init`，但它是个 6 字节桩。
⇒ 判断阻塞点必须看**函数大小/是否含等待原语**，而不是"日志停在哪"。**

---

# ★★★★ 2026-10-29 09:00 第三十七层：emg_init 是空桩；阻塞在 Application_Start #1 链；观测工具连续失败

## ① 静态确证：emg_init 是空桩
```asm
00000914 <emg_init@@Base>:
   914: b580   push {r7, lr}
   916: af00   add  r7, sp, #0
   918: bd80   pop  {r7, pc}        ← 6 字节，立即返回
```
**libEMG.so 总 5,872 B，导出函数全是 6~8 字节的桩（唯一有逻辑的是
`emg_check_memory_shortage`，332 B）。**
**⇒ 服务序列停在 emg_init **只是日志最后一条**，它不是阻塞点（第 45 条成立）。**

## ② 阻塞位置（三点交叉）
```
Application_Start 被进入但从未返回（argtrace 只有 ENTER，无 RETURN）
帧循环（在 #4 0x54f664 内）7 个入口全 0
CPU 空闲（100 秒仅 10 个采样）
⇒ 阻塞在 Application_Start 的 #1/#2/#3 之一，且性质是"等待"
   #2/#3 是 vtable/构造器，很快返回
   #1 是 veneer → 0x2c2ed85（440 B 栈帧，继续调 0x2ce819c/0x25c848a/0x2ce8754…）
⇒ 等待最可能在 #1 那条链里
```

## ③ ★ 观测工具连续失败（重要教训）
**尝试了 4 种方式读取 wchan，全部无输出：**
| 方式 | 结果 | 原因 |
|---|---|---|
| `/init` shell + `case`/`awk` 筛选 | 无输出 | **`/dev/kmsg` 缓冲区满 ⇒ shell 的 echo 静默失败**（之前见过 `write error: No space left on device`）|
| 同上，改 `exec >/dev/console` | serial 掉到 8,276 B，更糟 | /dev/console 在该时刻不可用或阻塞 |
| 最简 shell（无 case/awk） | 无输出 | 同上，仍是 kmsg 满 |
| **C 探针 `wcp.so`（pthread + write(2) + /proc/self/wchan）** | **仍无输出** | **未查明**（可能 pthread_create 在 constructor 早期失败）|

**★ 一个已确认的事实：`ARGTRACE` 的 C `write(2)` 一直有输出，而 shell 的 `echo` 没有
⇒ **/dev/kmsg 满了以后，shell 输出全失效，但 C 的 write(2) 仍可用。**
⚠️ 但 wcp.so 也是 write(2)，却无输出 ⇒ 说明问题在 `pthread_create` 或 constructor 时机。**

## ④ 下一步（两条，都很具体）
**① 修 wcp.so**：不做线程，改为在 constructor 里直接 sleep+读一次（阻塞 constructor，
   但 bootin 的 constructor 阶段很快就会结束，之后...）——不行，会阻塞启动。
   **更稳妥：用 `__attribute__((destructor))` 或在 argtrace 的某个高频函数里顺带采样。**
   **最简方案：在 argtrace 的 dlsym wrapper 里（它被高频调用）计数，
   每 N 次就顺带读一次 /proc/self/wchan 输出 —— 零额外线程。**

**② 或换个思路：直接静态分析 #1 链**（不依赖运行时）
   - 反汇编 `0x2c2ed85` 及其子调用，找 `osal_wai_sem`/`osal_rcv_msg_tmo`/`futex` 等
   - 缺点：链很深，但可用"找 PLT 等待原语"的扫描器自动做

**⇒ 优先 ②（静态、确定、不依赖有问题的运行时观测）。**

### 方法论（第 46 条）
**当观测工具连续失败时，不要继续加码工具，而是**换观测通道**：
本轮的教训是 shell 走 kmsg（会满）、C 走 fd2（可用），
但 C 的多线程 constructor 又引入了新不确定性。
⇒ **最可靠的观测点是"已经在工作的高频函数"里顺带采样**，
因为它已被证明能正常输出。**

---

# ★★★★★ 2026-10-29 09:20 第三十八层：**拿到 wchan 了** —— 两个进程都在等

## 突破：wcp.so 其实成功了，我之前 grep 漏了
**原因：串口每行以 `\r` 开头（`\rWC pid=...`），我用了 `grep "^WC "` 所以匹配不到。
⇒ 教训：串口日志用 `\r` 换行风格，正则不要锚定 `^`。**（第 47 条）

## 实测输出（Image.m2wcp2）
```
WC pid=119 comm=bootin.elf state=S wchan=osal_rcv_msg_tmo
WC pid=126 comm=im.elf     state=S wchan=futex_wait_queue_me
```
**⇒ 两个进程都是 S 状态（可中断睡眠）⇒ **都在等，无人忙**。**

| 进程 | wchan | 含义 |
|---|---|---|
| `bootin.elf` (119) | `osal_rcv_msg_tmo` | **等 osal 消息（带超时）** |
| `im.elf` (126) | `futex_wait_queue_me` | **等 pthread 互斥锁/条件变量** |

## ★★ 这是真正的"等设备"形态
**⇒ 用户直觉正确：**确实在等**。**
**⇒ 具体是两条等待链：**
1. **bootin.elf 在 osal 队列上等消息** —— 可能等某个服务/驱动的就绪通知
2. **im.elf 在 futex 上等** —— appFw 的线程在等另一个线程 signal（很可能就是等 UI 线程/启动完成）

## ⇒ 与 a7c2 方案的关联（现在更清楚了）
**a7c2 的做法是"不等 power-on 链完成，直接调生命周期入口切菜单"。
我们现在的状态正是"卡在等 power-on 链" ⇒ **a7c2 的思路适用于我们**。**
**但前提仍成立：需要一个"能跑起来的 UI 线程"才能切屏。**
**⇒ 关键问题变成：im.elf 的 futex 在等谁 signal？**
- 若等的是"UI 线程就绪" ⇒ 编译器里 UI 线程可能压根没创建 ⇒ 需要主动创建
- 若等的是"某个服务 init 完成" ⇒ 补齐该服务即可

## 下一步（精确）
**① 用我们已有的采样器（samp3）看 im.elf(126) 的调用栈位置**
   —— 但 futex 等待时 PC 在 libc，看不到调用者
   ⇒ **更好：用 `wcp.so` 扩展，读 `/proc/self/task/*/wchan` 和每线程的 `/proc/self/task/*/stat`，
      看 im.elf 里**多少个线程**、各自在等什么**
**② 或查 bootin.elf 在等哪条 osal 消息**（qid），对照对方已拿到的 qid 列表
   （对方此前给出了 `0x007a0020`（输入队列）、`0x007a804c`、`0x007ae01f`、`0x0094020a`）

**⇒ 优先 ①（区分"单线程死等"还是"多线程互相等"），这是关键分叉。**

### 方法论（第 47 条）
**串口日志的行首是 `\r`，用 `grep "^X"` 会漏掉全部内容。
⇒ 分析串口日志时不要锚定行首；或用 `tr -d '\r'` 先规范化。
本轮差点因此放弃一个**成功**的探针（wcp.so 其实工作正常）。**

---

# ★★★★★ 2026-10-29 10:00 第三十九层：bootin.elf 线程布局 —— 有 UIPC 代理线程在等内核

## 实测（Image.m2wct，wcp2.so 枚举 /proc/self/task/*）
```
WCT ---begin pid=120---                       ← bootin.elf
WCT pid=120 tid=120 st=S comm=bootin.elf    wchan=osal_rcv_msg_tmo
WCT pid=120 tid=122 st=R comm=bootin.elf    wchan=0                  ← ★ 有一个线程在 RUN
WCT pid=120 tid=123 st=S comm=uipc_proxy_0  wchan=__k_4u_wait_cb
WCT pid=120 tid=124 st=S comm=uipc_proxy_1  wchan=__k_4u_wait_cb

WCT ---begin pid=126---                       ← im.elf（线程列表被串口截断，未打出）
```

## ★ 关键信息
**1. `bootin.elf` 有 **4 个线程**：**
   - **tid=120 主线程**：等 osal 消息（`osal_rcv_msg_tmo`）
   - **tid=122**：状态 **R（运行中）**，wchan=0 —— **确实有线程在跑**
   - **tid=123/124 `uipc_proxy_0/1`**：等 **`__k_4u_wait_cb`**（内核 UIPC 等待）**

**2. `uipc_proxy_*` 线程的存在说明 UIPC（微 IPC）通道已建立**
   ⇒ **系统在正常的多进程消息框架里，不是"卡死"**

**3. `__k_4u_wait_cb` 是内核里 UIPC 的等待回调**
   ⇒ **这些线程在等对端发消息，符合正常 IPC 形态**

## ★★ 这个发现的意义
**系统**不是死锁**：有线程在 R 状态、有 UIPC 代理线程在工作。
⇒ **它是"启动到一半，在等某个 UIPC 对端/服务响应"。**
⇒ **这正是 a7c2 记录的情况**：他们"对缺失的 CAPID/SA/HDMI 等使用**限定服务**"
（即：**用替身实现补齐那些在仿真环境里不可用的服务**）。

## 下轮（明确）
**① 补取 im.elf 的线程列表**（上次被截断）
   - 减少 dump 频率（12s → 30s）、只打 pid=126 的线程、并把行缩短
**② 找出"缺哪些服务"**：对照 a7c2 的"限定服务"清单
   - 他们缺：CAPID / SA / HDMI 等
   - 我们可能缺同类（尤其 `/dev/` 里没有的硬件对应服务）
**③ 看 `bootin.elf` 在等哪条 osal 消息（qid）**
   - 对方已给 qid：`0x007a0020`(输入) `0x007a804c` `0x007ae01f` `0x0094020a`
   - 可用 argtrace 风格在 `osal_rcv_msg_tmo` 上打 id

### 方法论（第 48 条）
**"卡住"要看**线程状态分布**，而不是看进程整体：
本进程有 1 个 R 线程 + 1 个等 osal 的主线程 + 2 个等内核的 UIPC 代理线程
⇒ **这是"等对端"的形态，不是死锁。**
（我之前只看进程级 wchan，会误判为"整体卡死"。）**

---

# ★★★ 2026-10-29 10:40 第四十层：im.elf 线程列表取不到；阶段状态梳理

## 工具状态
| 探针 | 结果 |
|---|---|
| `wcp.so`（单进程 wchan，无线程） | ✅ 成功：bootin=osal_rcv_msg_tmo，im.elf=futex_wait_queue_me |
| `wcp2.so`（所有进程全线程） | ⚠️ 部分：bootin.elf 4 个线程全拿到；**im.elf 的块标题打出但线程行被串口截断** |
| `wcp3.so`（仅 im.elf，短行） | ❌ 无输出（`is_target()` 过滤后未触发，原因未查） |

**⇒ 结论：线程枚举本身可行，但**串口带宽**是瓶颈；
且过滤器（comm 匹配）在 im.elf 里没生效。**

## 已确立的完整事实链（跨轮汇总）
```
① 内核稳定（panic 已消除，/init 最小化）
② 15 个 kmod 全部加载成功
③ bootin.elf 启动，libObj.so dlopen ×5 成功
④ grm_init 解析、DMP_2D_Initialize 完成
⑤ appFwBoot.so 加载，Application_System_Init 返回 0
⑥ appFw.so 加载成功（libNetCommon 换 M2 版后）
⑦ Application_Start **进入但从未返回**
⑧ 帧循环（在 #4 0x54f664 内）**7 个入口全 0，从未启动**
⑨ bootin.elf 有 4 线程：主线程等 osal 消息、1 个 R、2 个 uipc_proxy 等内核
⑩ im.elf 主线程等 **futex**
⑪ CPU 空闲（100s 仅 10 采样）⇒ 在等，不是忙
⑫ 无 assert（BLE 库恢复后）
```

## ⇒ 定性结论
**系统不是死锁，是**启动流程走到一半、在等多个 IPC/服务响应**。
（有 R 线程、有 UIPC 代理线程在工作，符合 a7c2 描述的"启动未完成"形态。）**

## 剩余工作量的诚实评估
**要真正出画面，还差两步：**
**A. 让 `Application_Start` 返回并启动帧循环**
   —— 需要知道它在等哪个 IPC 响应，并**补上那个响应**（a7c2 称之为"限定服务"）
**B. 帧循环启动后**，可能还需按 a7c2 的方式处理 GPE 绘制与帧完成协议

**⇒ 这是**实质性工程量**，不是再调几个探针能解决的。**
**a7c2 为此投入了：8 步流程 + 软件 GPE 实现 + LZ77 解码 + 帧完成协议 + ramfb 显示桥。**

## 建议的下一步（需要你定方向）
**① 继续深挖"等哪个 IPC 响应"**（工作量中等，可能仍卡在仿真缺硬件）
**② 放弃"完整 boot"，按 a7c2 的**受控入口**路线：
   找到 M2 appFw 的生命周期入口，主动构造容器 → 建启动屏 → 切菜单屏
   （工作量大，但绕开所有 IPC 等待）**
**③ 暂停 QEMU 路线，回到"真机 + 最小改动"验证**（低工作量、低期望）**

### 方法论（第 49 条）
**当推进需要"实现别人的 8 步工程"时，应该先停下来评估总工作量，
而不是继续一轮一轮调探针。
⇒ **每轮都要回答"距离目标还差什么"，而不只是"又排除了什么"。**

---

# 📤 2026-10-29 11:30 交接：转交 QEMU 辅助对话（用户已决定先上真机）

## 交接内容（已发送给 session-41b33827）
1. **纠正历史误判**：我报过的"M2 渲染成功"是抓错进程（抓到对方的 QEMU）
2. **12 条已确证事实链**
3. **libNetCommon → libmprctrl 的真实坑**（跨代移植必踩）
4. **8 个被否掉的假设**（别重复）
5. **5 个工具坑**（kmsg 会满、`\r` 行首、gdbstub 扰动时序、pkill 自杀、忙/等判据）
6. **两个树的状态区分**（真机树 vs QEMU harness 树）
7. **下一步建议**（定位 Application_Start 等的 IPC 响应）

## 真机树最终状态（用户拿去刷）
```
/mnt/build/fw/port_m2/out/usr
  230 MB / 858 文件 / 余量 72 MB
  libNetCommon.so → 1444fb66 (M2)   ✅ 已修
  bt_firm.hcd     → 208b064a (M2)   ✅ 已修
  bt_firm_sar.hcd → 374229da (M2)   ✅ 已修
  BLE 库保留（541,484 B）           ⚠️ 不要移除（破坏 Creator's App）
  全树哈希 965a476010c73ff5
```

## 诚实结论（给用户）
**QEMU 这条线**没有**达到"新菜单出画面"（从未渲染过）。
已达到的是：**把失败从"黑盒无 UI"缩到"启动流程等 IPC 响应"，
并找到/修好了一个真实的加载缺陷（libNetCommon→libmprctrl）。**

**要真正出画面，剩余工作量参照 a7c2：软件 GPE + LZ77 解码 + 帧完成协议 + 显示桥，
属实质性工程，不是再调探针能解决的。**

---

# 🚨 2026-10-29 12:10 第四十一层【重要更正】：`port_m2/out/usr` 不是正确基准，`nflasha15_v9.ext2` 才是

## 我的交接错误（已撤回并更正）

**我曾让真机线刷 `/mnt/build/fw/port_m2/out/usr` —— 三重错误：**
1. **那是构建器输出目录，不是可刷镜像**
2. **它整体落后**：17 个文件还是 E10 版
3. **我在它上面做的"两个修复"是在修一棵旧树**（其中 libNetCommon 在 v9 里本来就是对的）

## 独立验证结果：v9 才是正确基准

```
/mnt/build/nflasha15_v9.ext2
  249,102,336 B (237.56 MiB, = 243,264 块 × 1024, 与原厂几何一致)
  md5 f6ae9f576821b35c    Oct 28 02:47

  /lib/libNetCommon.so              122,876  md5=1444fb66  ✅ M2 版
  /lib/libmprctrl.so                263,812  md5=1350def2  ✅ 存在
  /lib/appFw.so                  64,273,528  md5=f079cda0  ✅
  /lib/libObj.so                  3,931,624  md5=738a2331  ✅
  /lib/libIMDB.so                    40,596  md5=91eca62f  ✅
  /lib/libInfraBluetoothLowEnergy.so 541,484 md5=2ceb0d0d  ✅ 保留
  /bin/bt_firm.hcd                   61,787  md5=be8ac916  ⚠️ E10 版（唯一待修）
```

**⇒ 那条致命链（appFw → libNetCommon(E10) → libmprctrl 缺失）在 v9 里**已提前修好**。**

## 待做的 v9′ = v9 + M2 蓝牙固件

```
把 /bin/bt_firm.hcd      换成 M2 版：60,143 B, md5 208b064a68b3300a5dceae48416502f0
把 /bin/bt_firm_sar.hcd  换成 M2 版：54,028 B, md5 374229da
步骤：debugfs 替换 → e2fsck -fn → 回读校验 md5 → 重算镜像 md5
```

## 给 QEMU 线的同步
**他们原计划把 `port_m2/out/usr` 复制到 `/mnt/build/zv_m2probe/`，
我已提醒改用 v9 解包出的树作 base，否则会在一棵落后 17 个文件的树上测。**

## 方法论（第 50 条）★
**"构建器输出目录" ≠ "最终镜像"。**
我在 `port_m2/out/usr` 上做了修复，却没验证那个目录**是否就是用来生成待刷镜像的那棵树**。
⇒ **改任何东西前，先确认"哪个 artifact 才是真正要交付的"，并回溯它的来源链。**

## 🆕 第四十二层（12:40）：v9 的启动链二进制版本（回答 QEMU 线的门控问题）

```
v9 里：
  /bin/bootin.elf      15,336 B   md5=d8bfb4f7  ← **E10 版**（非 M2 的 3545261c）
  /bin/im.elf          23,240 B   md5=f1c746e3  ← **E10 版**（非 M2 的 d2f47a82）
  /lib/libIMDB.so      40,596 B   md5=91eca62f  ← M2 版
  /lib/appFwBoot.so     8,312 B   md5=bb4cdbab  ← M2 版
```
**⇒ v9 = **E10 启动器 + M2 应用栈** ⇒ 进程名与 E10 完全一致，preload 门控无需修改。**

**★ 但关键：`bootin.elf` 只是启动器，真正跑 `Application_Start` 的是 `im.elf`。
⇒ 只拦 bootin 会漏掉 im.elf 侧的 osal 调用（而"Application_Start 向哪个 qid 发 sync"正发生在 im.elf）。**

**建议：门控同时匹配 `bootin` 和 `im.elf`；或干脆不门控，计数表按 pid 分开记
（preload 库的 .bss 是每进程私有，天然隔离）。**

**判据改进：`osal_snd_sync_msg` 是同步发送（阻塞到对端回复）
⇒ 记录 `entered[qid]` 与 `returned[qid]`，两者差值 > 0 的 qid 就是"发出却无人应答"的队列。**

---

# ✅ 2026-10-29 13:20 第四十三层：v9b 复核通过（真机待刷镜像就绪）

## v9b 独立复核结果（全部通过）
```
/mnt/build/nflasha15_v9b.ext2
  249,102,336 B (237.56 MiB = 3801 × 64 KiB，精确相等)
  md5 2ff338bc3462fc9dc9c3a66da27ade8a
  e2fsck: 985/60960 files, 233039/243264 blocks — 无错误

超级块几何 vs 原厂：blocks/inodes/isize/first/uuid/label/compat **逐字段一致**

关键文件:
  /bin/bt_firm.hcd       60,143  208b064a  ✅ M2 版（v9 里是 be8ac916 E10 版）
  /bin/bt_firm_sar.hcd   54,028  374229da  ✅ M2 版（v9 里本来就对）
  /lib/libNetCommon.so  122,876  1444fb66  ✅ M2 版
  /lib/libmprctrl.so    263,812  1350def2  ✅ 存在

权限: 两个 hcd 文件 Mode 0755 uid 57285 gid 1000
      （v9 原版与原厂 dump 均为 0755，✅ 正确）
```

## 三方超级块指纹（重要参考）
```
镜像                    超级块 md5                          前 1KB 引导区 md5
原厂 dump               a24bfd7e5891016567c363b3d541e0a6    0f343b0931126a20f133d67c2b018a3b
v9 原版                 53f4a9de782b1169440847da6ae8a37a    0f343b0931126a20f133d67c2b018a3b
**v9b（待刷）            9c0d90e679d665b0fe9df1661c13385c**  0f343b0931126a20f133d67c2b018a3b
```
**⇒ 三者引导区（前 1KB）完全相同（1024 字节全零的 md5）⇒ ext2 引导区没人动过。**
**⇒ 超级块三家各异（重建镜像会变时间戳/空闲计数/hash seed）⇒
   **超级块 md5 不能证明"是 v9b"，但能**立刻排除"完全没写进去"**（读到 a24bfd7e 即没刷）。**

## `--full` 语义（对方查源码确认）
```python
nblocks = len(target) // bgran          # 249102336/65536 = 3801
if a.full: runs = [[0, nblocks-1]]      # 只覆盖镜像范围内的块
```
**⇒ 只写 3801×64KiB = 237.56 MiB，分区尾部 62.4 MB 原样不动。**
**耗时：写 237.6 + 回读 237.6 = 475.1 MB ÷ 3.1 MB/s ≈ 153 秒（+bootstrap ≈ 175 秒）。**

## 回退双保险（已核实）
```
/root/fw/zve10_backup/nflasha15.img    314,572,800 B  sb_md5 a24bfd7e  整文件 cdec7ad4
/root/fw/zve10_delivery/nflasha15.img  314,572,800 B  sb_md5 a24bfd7e  整文件 cdec7ad4
⇒ 两份独立采集完全一致
```

## 余量
```
v9b: 空闲 10,225 块 = 9.98 MB（以后补小文件不用重建几何）
```

## 方法论（第 51 条）
**重建镜像的超级块 md5 必然与"原厂克隆"不同（时间戳/空闲计数/hash seed）。
⇒ 超级块指纹只能做**反向判据**（"是否还是原厂"），不能做**正向判据**（"是否是目标镜像"）。
⇒ 正向判据必须靠逐块比对（或整文件 md5）。**

---

# ★★★★ 2026-10-29 13:50 第四十四层：找到两个**磁盘侧信标**（应对"完全起不来"取证）

## 背景
**真机刷写后若"完全起不来、无 USB"，`/proc` 读不到 ⇒ 需要不依赖相机启动的取证手段。
我在原厂 dump 的 `/setting`(nflasha2) 里找到两个现成落盘文件。**

## 信标 ① `/setting/ulogio.bin`（3,796 B，原厂 md5 `a401b16a`）
**结构（真实内容）：**
```
2000 0000  LGNM 191H_Dslr_Astra-Diadem     ← 魔数 "LGNM" + 平台标识 "Astra-Diadem"
0c00 0000  RECO 0100 0000 1000 0000        ← "RECO" 记录
4c47 4e4d ...  LOGB 0050 0803 0080 4700    ← "LOGB" 日志缓冲
字符串: LGNM / RECO / LOGB / PRTB / CHAN
```
**写入者：`osal_ulogio.ko`（`/init` 里 `ulogio_initfile=/setting/ulogio.bin`）**
**⇒ 只要启动走到 osal_ulogio 加载 + 日志写入，该文件就会**增长或改变**。**

## 信标 ② `/setting/kemco.txt`（1,925 B，原厂 md5 `d47baed6`）
**内容是把内核 cmdline 落盘（`ramdisk_size=8192 rw noatime ... mem=1G@0 memrsv=...`）**
**⇒ 若被更新，说明启动至少跑到写它的阶段。**

## 判读表（刷后无论能否启动都读一次）
| 现象 | 判读 |
|---|---|
| 两者都没变 | 启动没到 ULOGIO/写盘阶段 ⇒ 比 10-27 更早失败 |
| `ulogio.bin` 变了 | ✅ 走到 osal_ulogio ⇒ 内核 + kmod 起来了 |
| `kemco.txt` 变了 | ✅ 内核 cmdline 落盘 ⇒ 更早阶段正常 |
| `ulogio.bin` 变大很多 | 可能有新 ULOGIO 记录 ⇒ 整文件拉回分析 |

**⇒ 优点：不需要相机启动、不需要 USB/网络、不需要 agent，只要 fastboot 能读分区。**

## 补充：`/log/im_manual.log` 是 im.elf 的写盘痕迹
**真机实测内容：**
```
!!! GLES Vecgen disabled.
===== DMP_2D_Initialize : 0
```
**⇒ 由 im.elf/libObj 写 ⇒ 若刷后该文件有新内容，说明 im.elf 起来了。
⚠️ 但原厂 dump 的 nflasha11 里读不出文件，需真机确认它是否存在。**

## `/setting` 是 **rw 挂载**（不是 ro）
⇒ 正常启动会写这些文件；**反过来说，若 `/setting` 完全没变 ⇒ 启动连 rw 挂载都没到。**

## 方法论（第 52 条）
**当"主通道"（/proc、USB、网络）可能不可用时，应提前准备**旁路取证点**：
本案例中 `/setting` 的两个文件由**内核模块和早期初始化**写入，
比"应用层日志"更早、且不依赖相机可用 ⇒ 是理想的失败取证点。
⇒ **设计实验时应同时规划"成功路径观测"与"失败路径观测"。**

---

# ★★★ 2026-10-29 14:20 第四十五层：`ulogio.bin` 是**配置模板**（我的信标推断被证伪）

## 真机线的证伪实验（正确做法）
**他们用两份相隔两天的 `/setting` 采集做差分：**
```
文件            10-25        10-27 current   变化
ULOGIO.BIN     5394f68f      5394f68f        完全没变  ❌ 不是活日志
KEMCO.TXT      d47baed6      d47baed6        完全没变  ❌
.hasp          82752fc4      53eda240        变了      ✅ 活跃
BACKUP.BIN     06ec67c8      66f065e5        变了      ✅ 活跃
BACKUP.BAK     6682ddc4      1ea320fc        变了      ✅ 活跃
```
**⇒ 我的推断「两者都没变 ⇒ 启动没到 ULOGIO 阶段」**过强**，撤回。**

## 机制解释（我补的）
**拆 `ulogio.bin` 内容：**
```
头部:   LGNM 191H_Dslr_Astra-Diadem
        RECO / LOGB / PRNT / CHAN
订阅列表: ISYS / DBGL / DBCL / FLTA / TERM / SUBS ...
尾部:   ...SUBS(0xbc)... SUBS(0xa9)...
```
**⇒ 这些是 **ULOGIO 子系统/通道标识符**（ISYS=系统, DBGL=debug log, DBCL=debug console,
FLTA=fault, TERM, SUBS=subscribe）。**

**⇒ `osal_ulogio.ko ulogio_initfile=/setting/ulogio.bin` 的语义是
「初始化时**读入**这个配置文件，决定记哪些通道」**
**⇒ 它是**输入配置**，不是写入目标 ⇒ 不随启动变化，与实测一致。**

## 另一处更正：我给的"原厂基线"是旧状态
```
                     delivery dump   current 10-27
ULOGIO.BIN           a401b16a        5394f68f
BACKUP.BIN           20b79fce        66f065e5
.hasp                afc1e4fe        53eda240
```
**⇒ 我引用的 `a401b16a` 来自更早的 `zve10_delivery/nflasha2.img`。
⇒ **基线必须用刷前现读值**，不能用历史 dump 的数字。**

## 增强建议：BACKUP.BIN 做**内容 diff** 而非只看"变没变"
**`BACKUP.BIN` = 1,235,836 B，由 `libbackup.so` + `backup.ko` 管理（nflasha4 "MPR Flash Backup"）。**
**⇒ 若刷后相机部分启动并写了它，把新旧两份拉回做**字节级 diff**：
   diff 的**偏移位置**能定位"哪个服务的哪块设置被写了"（通常按 key/value 或槽位存）。
⇒ 信息量远大于二值判据，成本只多 1.2 MB。**

## 方法论（第 53 条）★ 自我批评
**我提出用 `ULOGIO.BIN`/`KEMCO.TXT` 当信标时，**没有先验证它们是否真的会变** ——
这正是我自己反复强调的"阳性对照"（第 1 条）。
**⇒ 提议一个信标之前，应先用现有历史采集做「它到底会不会变」的差分检验。
⇒ 有历史采集就是天然的正对照材料，不用额外实验。**

---

# ⚠️ 2026-10-29 14:50 第四十六层：恢复 nflasha2 把 **agent preload 通道清掉了**

## 问题
**恢复 `nflasha2`（修 dmode）时，`/setting/mode/` 目录被清空 ⇒ agent 自动加载没了。**

```
                         preload   dmode   zve10_net
nflasha2_current.img     不存在    不存在   不存在   ← 刚刷的
zve10_delivery/nflasha2  不存在    不存在   不存在
```

**而 agent 注入后、恢复前的实测状态是：**
```
/setting/mode/preload = /usr/tool/libefence.so:/log/agent_v14.so
/setting/mode/dmode   = 3e
/setting/mode/zve10_net = off
```

**⇒ 现在刷 v9b，如果菜单起不来 ⇒ **没有 agent 就没有 shell**，只能靠 fastboot 读 /setting 信标。**

## ★ 好消息：v9b 条件具备，可以恢复 preload
**v9b 的 `/usr/tool/` 有：**
```
LeakTracer.so / libjemalloc_tsh.so / libjemalloc_tsh.so.1
libjemalloc.so / uiprobe.so / LeakCheck
```
**`bootin.elf` 的 preload 白名单（早前反汇编）：**
```
/setting/mode/preload
/usr/tool/LeakTracer
/usr/tool/libefence      ← v9b 里没有这个文件！
/usr/tool/libjemalloc_tsh
/usr/tool/libmcheckb
匹配方式: strncmp(whitelist_entry, buf+11, strlen(entry)) —— **前缀匹配**
⇒ `a.so:b.so` 形式只要第一段命中白名单，第二段也会加载
```
**⇒ 可写：`/setting/mode/preload = /usr/tool/LeakTracer.so:/log/agent_v14.so`**
**（用确实存在的 LeakTracer.so 当第一段；libefence 在 v9b 里不存在，
ld.so 会警告 "cannot be preloaded: ignored" 但**非致命**，不过没必要引入噪声）**

## ★★ 为什么 agent 在新菜单上也能用
```
v9b 启动链（已核实）：
  /bin/bootin.elf  = E10 版 (d8bfb4f7)
  /bin/im.elf      = E10 版 (f1c746e3)
  /lib/libIMDB.so  = M2 版
  /lib/appFwBoot.so= M2 版
⇒ 用的仍是 E10 的 bootin/im.elf ⇒ preload 注入位置与原来完全一致 ⇒ agent 应能起来
```

## 两种方案权衡（建议 A）
| 方案 | 优点 | 缺点 |
|---|---|---|
| **A. 先写 preload 再刷** | 刷完有 shell 可查 `/proc/maps`（appFw 是否加载 = 唯一硬判据） | 多加载一个 .so，轻微影响 |
| B. 不写 preload 直接刷 | 最纯净，最接近 M2 原厂行为 | 没 UI 就没 shell，只能靠 fastboot 信标 |

**建议 A：已有两次"完全无 UI 且无 shell"的经历，有 shell 能第一时间拿到硬判据。**

## 关于 dmode
**恢复后也是空的。原厂 dump 里 `mode/` 本来就是空的 ⇒ bootin 用默认值。
⇒ 保守做法：**不写 dmode**，少一个变量（`3e` 可能是当初注入时留下的，必要性未验证）。**

## 方法论（第 54 条）
**"恢复某个分区"会**同时清除**上面所有后续注入的东西。**
本案例：恢复 nflasha2 修 dmode，顺带清掉了 agent 的 preload ⇒ 断了自己的调试通道。
**⇒ 恢复分区前，先列出该分区上"非原厂添加的内容"，确认哪些是可弃的、哪些是调试命脉。**

## 51. 2026-10-28 ZV 侧接手：qid 计数表（M2 混合树，v9 基准）

交接后按纠正后的基准重建了探针镜像：
* 基准树：`nflasha15_v9.ext2`（md5 `f6ae9f57…`，249,102,336 B）解包 → `/mnt/build/zv_m2probe/root`（858 文件, 230MB）
  - `lib/libNetCommon.so` md5 `1444fb66…`（M2 版，非 E10 版）✅
  - `lib/libmprctrl.so` 263,812 ✅（混树漏带的那支）
* initramfs：复制对方 `/mnt/build/m2qemu/work/initramfs-ui`，`usr` 换成 v9 树，
  `init` 里的 `LD_PRELOAD` 换成我们的探针（去掉 argtrace/wcp4，减少变量）
* 探针 `libm2probe.so`：裸汇编 ABI 透明桩，只拦**无前缀** `osal_rcv_msg_tmo` / `osal_snd_sync_msg`，
  **零串口热路径**，只写内存计数；`ttyAM1` 收 `#M2` 命令 dump
* 镜像 `kbuild/arch/arm/boot/Image`（279,454,084 B，`strings | grep M2PROBE` = 1 校验通过）

### 实测计数（boot 后约 2 分钟）

```
M2PROBE pid=120 rq_c=1
M2R pid=120 q=0094020a n=1
M2S pid=120 q=008f013a enter=22 ret=11 stuck=11
M2S pid=120 q=003e014d enter=8  ret=4  stuck=4
```

### 判读

| qid | 语义（E10 参照） | 观测 |
|---|---|---|
| `0x0094020a` | E10 上也是 rcv 队列之一 | rcv n=1（在等） |
| `0x008f013a` | E10 侧同族 `0x008f0139` | **11 次 sync 发送未返回 ← 最大嫌疑** |
| `0x003e014d` | **backup 服务队列**（E10 已知） | 4 次 sync 未返回 ← backup 无响应 |

**⇒ "发了没人回"的队列已定位：`0x008f013a` 和 `0x003e014d`(backup)。**
这与"不是死锁、是在等 IPC 响应"的定性一致。

### 待办

1. `im.elf`(pid≈126) 侧没吐出计数 —— 需要确认它的 cmdline 是否含 `im.elf`（门控），或改门控为"在 im 里也装 reader"
2. 对照 E10 的 `0x008f0139/0x008f013a` 族语义，确认缺的是哪个服务

---

# ★★★★★ 2026-10-29 15:30 第四十七层：`0x8f013a` 是**正常 UI 发送队列**（QEMU 线的嫌疑被推翻）

## QEMU 线测到的 qid 计数表（M2 树）
```
M2PROBE pid=120 rq_c=1
M2R pid=120 q=0094020a n=1
M2S pid=120 q=008f013a enter=22 ret=11 stuck=11    ← 他们判断为"最大嫌疑"
M2S pid=120 q=003e014d enter=8  ret=4  stuck=4     ← backup 队列
```

## ★ 我的对照结论：`0x8f013a` 在 E10 上是**正常路径**
**E10 成功出画的日志（`/mnt/build/tmp/webui/serial.log`，871 KB）里：**
```
19 次  UI_OBSERVE pid=122 tid=237 ui.send.queue=0x8f013a     ← ★ 就是它！
11 次  UI_OBSERVE pid=122 tid=237 ui.send.queue=0x11006e9
 1 次  ui.send.queue=0x80003b
 1 次  ui.send.queue=0x59000e
```
**⇒ `0x8f013a` 是 **UI 线程发送的目标队列**（`ui.send.queue`），
  E10 渲染成功时也往它发 19~30 次。**

**⇒ M2 测到 `enter=22`，与 E10 的 19~30 次**同量级** ⇒ M2 的 UI 在做和 E10 一样的事。**
**⇒ 它**不是**缺失的服务，而是正常路径。**

## ★ E10 成功出画的完整观测分布（关键参照）
```
239 thread.cond_wait.*          ← UI 线程绝大多数时间在 cond_wait（菜单空闲态）
180 ui.send.word
150 thread.osal_sem.*
105 native.setting /  36 native.setting.tree /  27 native.setting.list
 32 ui.send.queue               ← 只有 32 次发送
 18 native.view.id
 16 notify                      ← 绘制通知
 16 draw                        ← 绘制调用
  9 headless.export_ready       ← ★ 帧导出就绪
  9 headless.committed_native_frame ← ★ 原生帧已提交
  8 native.menu.views.base /  6 native.menu.vptr
```
**⇒ E10 成功路径：少量 ui.send.queue(~32) → notify/draw(各16) → export_ready → committed_native_frame。**

## ★★ `enter - ret = 11` 可能是**在途**，不是卡死
**单次快照无法区分"真卡死"与"采样时在途"。**
**⇒ 改进：采样两次，间隔 30 秒**
```
T=0  enter=22 ret=11
T=30 enter=22 ret=11  ⇒ 差距不变 ⇒ 真卡死
T=30 enter=22 ret=22  ⇒ 差距归零 ⇒ 采样在途，正常
T=30 enter=30 ret=20  ⇒ 双方都在涨 ⇒ 持续工作
```
**⚠️ E10 该队列是**单线程**（tid=237）在发；若测的是 pid 聚合多线程，在途更正常。**

## `0x003e014d`（backup）：倾向认同他们的怀疑，但需对照
**E10 的 UI_OBSERVE 日志里 0 次出现 `0x3e014d`。
⚠️ 但 UI_OBSERVE 只覆盖 libuiobserve 挂钩点，不是全量 osal ⇒ **"E10 日志里没有"≠"E10 没发过"**。
⇒ 建议在 E10 上跑同一个 m2probe，拿 backup 队列的 enter/ret 基线。**

## 给 QEMU 线的下一步建议
**按 E10 成功路径，M2 要出画应经过：**
```
少量 ui.send.queue → notify/draw → export_ready → committed_native_frame
```
**⇒ 在 M2 上也挂 E10 的 libuiobserve（它本就覆盖这些点），一次就能看出 M2 走到哪一步：
   - 有 ui.send.queue ⇒ UI 在发请求（已确认 22 次，正常）
   - 有 notify/draw ⇒ 绘制被调用
   - 有 export_ready / committed ⇒ 帧真的交付了
⇒ 这比猜"哪个 qid 缺服务"直接得多。**

## 方法论（第 55 条）
**跨环境数值对照（E10 vs M2）比单环境异常检测可靠得多：
`enter-ret>0` 在单环境里看似"卡住"，但有了 E10 的基线（19~30 次正常发送）
就能立刻判断 22 次是正常范围。
⇒ **看到异常数值时，先找"正常情况下的同类数值"，再决定是否异常。**

## 52. 2026-10-28 双采样判定：`0x008f013a` / `0x3e014d` 的未返回是**真卡住**，不是"在途"

按协作方建议（采样两次间隔 30s 区分在途/卡死），探针 v2 增加**自动定时 dump**（t=0/30/60s）：

```
M2INIT pid=120 cmd=/usr/bin/bootin.elf
M2INIT pid=128 cmd=/usr/bin/im.elf          ← im.elf 这次也覆盖到了

t=0 : 0x008f013a enter=22 ret=11 | 0x3e014d enter=8 ret=4 | rcv 0x0094020a n=1
t=1 : 0x008f013a enter=22 ret=11 | 0x3e014d enter=8 ret=4 | rcv 0x0094020a n=1
t=2 : 0x008f013a enter=22 ret=11 | 0x3e014d enter=8 ret=4 | rcv 0x0094020a n=1
```

**60 秒内 enter/ret 完全不变 ⇒ 不是采样在途，是永久未返回。**

### 但要与 E10 基线对照才能定性

协作方指出：`0x8f013a` 在 **E10 成功出画**的日志里就是 `ui.send.queue`（19~32 次），
所以"M2 发了 22 次"本身正常。关键问题是 **E10 上这些 send 的 ret 是否等于 enter**。

⇒ 下一轮：把同一个 `libm2probe.so` 挂到 **E10 基线**上跑一次，
拿 `0x8f013a` / `0x3e014d` 的 enter/ret 基线：
* E10 也是 enter=22/ret=11 ⇒ 说明这属正常模式，M2 问题在别处
* E10 是 enter=N/ret=N ⇒ M2 的未返回确属异常，指向缺响应方

### 已知的小问题

`im.elf`(pid=128) 的 `M2INIT` 打出来了（说明库已加载、构造函数已跑），
但它的**定时 dump 没有出现** ⇒ 它的 **stdout 不是 /dev/kmsg**（被 bootin 重定向到别处），
写入丢失。修法：探针直接 `open("/dev/kmsg")` 写，而不是写 fd 1。

---

# ★★★★ 2026-10-29 16:00 第四十八层：双采样确证"永久未返回"；sync 语义侧证

## QEMU 线双采样结果（探针 v2）
```
M2INIT pid=120 cmd=/usr/bin/bootin.elf
M2INIT pid=128 cmd=/usr/bin/im.elf          ← im.elf 覆盖到了

t=0  : 0x008f013a enter=22 ret=11 | 0x3e014d enter=8 ret=4 | rcv 0x0094020a n=1
t=30 : 0x008f013a enter=22 ret=11 | 0x3e014d enter=8 ret=4 | rcv 0x0094020a n=1
t=60 : 0x008f013a enter=22 ret=11 | 0x3e014d enter=8 ret=4 | rcv 0x0094020a n=1
```
**⇒ 60 秒不变 ⇒ **永久未返回**，不是采样在途（第 55 条方法论提出的在途假说被排除）。**

## ★ sync 语义的静态侧证
**拆 `libosal_uipc.so` 的 sync 家族：**
```
osal_snd_sync_msg              size=16     ← 薄包装
__osal_snd_sync_msg            size=278    ← 真正实现
osal_snd_sync_direct           size=336    ← 另一变体
osal_reg_sync_msg_queue_cb     size=116    ← 注册 sync 回调
__osal_finish_sync_cb          size=108    ← ★ sync 完成回调
```
**⇒ 存在 `__osal_finish_sync_cb`（完成回调）+ `osal_reg_sync_msg_queue_cb`（注册入口）
⇒ sync 发送**确实有"完成"状态**，不是 fire-and-forget。
⇒ `enter-ret` 冻结 ⇒ 有操作永远没到完成态 ⇒ 判据在语义上站得住。**

**⚠️ 未能从静态判断 `osal_snd_sync_msg` 是"阻塞等完成"还是"异步+回调"
（反汇编时被 Thumb 符号 +1 偏移坑了，输出错位）。
⇒ 两种情况下都指向"有人没回应"，但"谁应回应"不同 ⇒ **仍需 E10 对照**。**

## 待做的关键对照
**在 E10 基线上挂同一个 `libm2probe.so`，读出画时的 `0x8f013a` / `0x3e014d` enter/ret：**
```
E10: 0x8f013a enter=N ret=N       ⇒ M2 的 ret=11/enter=22 属异常 ⇒ 指向缺响应方
E10: 0x8f013a enter=N ret<N       ⇒ 该判据无效，M2 问题在别处
E10: 0x3e014d 完全不出现           ⇒ M2 的 backup 队列确实异常（更有价值）
```

## ⚠️ 基准一致性提醒（重要）
**QEMU 里测的是 **v9**，真机要刷的是 **v9b**，两者只差 `/bin/bt_firm.hcd`：**
```
v9  : be8ac916 (E10 版, 61,787 B)
v9b : 208b064a (M2 版, 60,143 B)
```
**⇒ QEMU 无蓝牙硬件，此差异大概率不影响；
但若发现任何蓝牙/BLE 相关行为差异，必须归到"v9 vs v9b"这个变量上，不能当成 M2 树固有属性。
⇒ 结论若要指导真机，最终**用 v9b 在 QEMU 复核一次**（成本极低）。**

## 方法论（第 56 条）
**"异常数值"要区分三种可能：采样在途 / 永久未完成 / 语义本就如此。
判据：**
- **单次快照 ⇒ 只能怀疑**
- **多次采样差距不变 ⇒ 排除"在途"**
- **跨环境（已知正常基线）对照 ⇒ 排除"语义本就如此"**
**⇒ 三级递进，缺一步都可能误判。本轮已完成前两步，第三步（E10 对照）是收官。**

## 53. 2026-10-28 ★ E10 对照推翻上一轮判读：`stuck` 是我探针的计数 bug，不是真卡死

在同一套 E10 基线（能出画，`export_ready=1`，serial 929KB）挂**同一个探针**：

```
M2INIT pid=117 cmd=/usr/bin/bootin.elf
M2INIT pid=125 cmd=/usr/bin/im.elf

bootin:  M2R 0x0094020a n=1 / 0x007a804c n=6 / 0x007a0020 n=6 / 0x007ae01f n=1
         M2S 0x008f013a enter=22  ret=11  stuck=11
         M2S 0x003e014d enter=100 ret=50  stuck=50
         M2S 0x00924127 enter=12  ret=6   stuck=6
im.elf:  M2R 0x008f0139 n=52 / 0x00bd017a n=1 / ...
```

**⇒ `0x008f013a` 在"能正常出画"的 E10 上同样是 `enter=22 ret=11`，与 M2 完全一致！**
**⇒ 所有队列的 ret 恒等于 enter 的一半（22/11、100/50、12/6）——这是 2:1 的固定比例，
说明是我的计数器在把"同一次调用"重复计进 enter。**

### 根因（已定位，待修）

```c
bump(g_sq_q, g_sq_n, &g_sq_c, regs[0], 0, 0);        /* enter: ns++ */
if (ret) bump(g_sq_q, g_sq_n, &g_sq_c, regs[0], 1, g_sq_r);  /* ret: ns++ 又加一次, rs++ */
```
`ret` 分支把 `ns` 又加了一次 ⇒ `ns = 2 × 实际调用数`，`rs = 1 ×` ⇒ 永远 2:1。
而且裸汇编桩只调 `on_enter`、**没有在 native 返回后再调 on_return**，
`ret` 参数取的是未定义寄存器值 ⇒ 计数本身也不可靠。

### 修复方案（下一轮）

裸汇编改成"两段式"：进入前计数 → 由 C helper **真正调用** native 并拿返回值 →
返回后计数 → 再用保存的返回值返回调用者。
（native 最多 4 参：`rcv` 3 参、`snd_sync` 2 参，helper 按 4 参转发即可。）

### 结论修正

* §52 里"M2 的 0x8f013a/0x3e014d 真卡住"**作废** —— 该指标在 E10 上给出同样的数，不能区分；
* 但这次对照**保住了 E10 侧的正面数据**：
  `bootin` 等的 4 个 rcv 队列（0x94020a / 0x7a804c / 0x7a0020 / 0x7ae01f）与 E10 完全一致；
  `im.elf` 在 E10 上大量收 0x8f0139（n=52）—— **M2 的 im.elf 是否也有，才是真正可比的信号**。

---

# ★★★★ 2026-10-29 16:40 第四十九层：QEMU 线自查出测量 bug；"M2 卡住"结论作废；**rcv 基线有效**

## QEMU 线的 bug（他们自查发现）
```c
bump(..., 0, 0);                  /* enter: ns++ */
if (ret) bump(..., 1, g_sq_r);    /* ★ ret 分支重复 ns++ ⇒ ns = 2×实际，rs = 1× */
```
**⇒ **所有 snd 的 enter/ret 永远是 2:1**（22/11、100/50、12/6 —— 固定比例，不可能是巧合）。
⇒ **§47/§48 关于"M2 真卡住"的结论**作废**。
⇒ 更根本：裸汇编桩只在进入前计数，native 返回后没有计数，`ret` 参数取的是未定义寄存器值。**

## ★ 关键：**rcv 计数没被这个 bug 污染**（我指出的）
**两次 `ns++` 只发生在 **snd 的 ret 分支** ⇒ rcv 只走一次 `bump(...,0,0)` ⇒ **rcv 数字是单次计数，可信**。
**⇒ **E10 的 rcv 分布是有效基线**，可直接对比。**

## ★ E10 基线（能出画，export_ready=1，serial 929KB）
```
bootin rcv:   0x94020a n=1 | 0x7a804c n=6 | 0x7a0020 n=6 | 0x7ae01f n=1
im.elf rcv:   0x8f0139 n=52 | 0xbd017a n=1
```
## M2 现状（阶段不同：服务 40 个就停）
```
bootin rcv:   0x94020a n=1                                  ← 比 E10 少 3 个
im.elf rcv:   **空白**（stdout 被重定向，未 dump 出）
```

## ★★ 下一轮核心判据（我建议的优先级）
1. **M2 多时间点 rcv dump**（t=0/60/120/180s），看 3 个缺失队列是否**后补出现**
   - **后补 ⇒ 只是慢**；**仍缺 ⇒ 真的走不到，硬结论**
2. **M2 的 im.elf 是否也从 `0x8f0139` 收 ≈52 次** ← **最有价值**（E10 是 52）
   - **收到类似次数 ⇒ UI 通道正常，问题在其后**
   - **收不到 ⇒ UI 通道没建立，硬缺口**
3. snd 桩修复 —— **最低优先级**（判据本身已证明不可靠）

## ★ `0x008f0139` / `0x008f013a` 的关系
```
E10 bootin 发 0x8f013a（22 次，ui.send.queue）
E10 im.elf 收 0x8f0139（52 次）
⇒ 相邻 QID（0x39/0x3a，同子系统 0x008f）⇒ 很可能是同一服务的两个方向 / 请求应答两条队列
```

## 方法论（第 57 条）★
**"两个环境都出现同样的异常比例"不是巧合，而是**测量仪器的系统偏差**。
E10 和 M2 的 snd 都是精确 2:1 ⇒ 第一时间就该怀疑计数器本身，而不是怀疑两个环境同时坏了。
**⇒ 当异常呈现**数学上整齐的比例**（2:1、倍数、恒定差值）时，
   优先怀疑测量代码，而不是被测对象。**

## 54. 2026-10-28 ★ 决定性对比：M2 的 bootin 卡在**第一个** rcv 等待，180 秒不前进

探针 v3（修正 snd 重复计数、输出走 /dev/kmsg、4 个时间点 t≈45/90/135/180s）：

### M2（v9 树）

```
M2INIT pid=119 cmd=/usr/bin/bootin.elf
M2INIT pid=128 cmd=/usr/bin/im.elf
t=0/1/2/3（四个时间点，180 秒跨度）全部一致：
  rcv: 0x0094020a n=1          ← 只有这一个，且始终 n=1
  snd: 0x008f013a enter=11
       0x003e014d enter=4
```

### E10 基线（能出画，同一探针）

```
bootin rcv: 0x0094020a n=1 | 0x007a804c n=6 | 0x007a0020 n=6 | 0x007ae01f n=1
im.elf rcv: 0x008f0139 n=52 | 0x00bd017a n=1 | ...
```

### 判读

1. **M2 的 bootin 只建立了 1 个 rcv 等待，E10 有 4 个**；
2. **180 秒内完全不变 ⇒ 不是"还没走到"，是真卡在第一个等待点上**；
3. 其中 E10 的 `0x007a0020` 正是**输入队列**，`0x007a804c`/`0x007ae01f` 是 UI 相关
   ⇒ **M2 的 UI/输入通路上层根本没建立**，与"帧循环 7 入口全 0"的现象完全吻合；
4. `snd 0x003e014d`(backup) 的计数从 4→4 不变，说明 bootin 也没再往 backup 发。

⇒ **断点：`0x0094020a` 这个等待之后的那一步。** 需要确认这个 qid 属于哪个服务、
以及 E10 是在什么条件下从它转向 `0x7a804c/0x7a0020/0x7ae01f` 的。

### 遗留

`im.elf`(pid=128) 仍未吐出任何 dump（M2INIT 有，说明库已加载、构造函数已跑），
即便输出已改成 `/dev/kmsg` + `write(2)` 兜底 ⇒ 更可能是 **im.elf 里定时线程没跑起来**
（或该进程在构造函数之后立刻陷入不可调度状态）。

## 55. 2026-10-28 E10 时间线：4 个队列在首次采样（t≈45s）前就已全部建立

E10 + 探针（`/mnt/build/zv_m2probe/Image.e10probe`）实测行号关联：

```
行 1295   EXPORT_READY（首帧提交，很早）
行 16059  M2T pid=117 t=0   ← 第一个采样点
行 16069  M2T pid=125 t=0
```

采样 t=0 时的 rcv 分布（**已经齐了**）：

```
bootin(117): 0094020a:1, 007a804c:6, 007a0020:6, 007ae01f:1
im.elf(125): 008f0139:52, 016c8477:441, 0080003b:217, 0108e003:51,
             009a015a:2, 0094016a:1, 007b0026:1, 007c8075:1, ...（40+ 条）
t=1/t=2 分布完全一致（只有部分计数增长，如 016c8477 441→741→1040）
```

**⇒ 结论：E10 在首帧（行 1295）之后、t=0 采样（行 16059）之前，
就已经建立了输入队列 `0x7a0020` 等全部等待。
⇒ M2 到 t=180s 仍只有 `0x94020a:1`，说明它连"首帧之后那段"都没走到。**

### 与协作方 qid 语义表的对照（他们提供，来自本仓库历史文档）

```
0x3e014d  backup
0x8f013a  ulog（日志）      ← 不是 UI 队列（我早先判读错误，已剔除）
0x8f0139  ulog 控制队列
0x7a0020  InfraInput 输入队列
0x14a0429 InputServant
0x94016a / 0x94020a  0x94 族（UIPC 注册/管理）
```

### 下一步（收紧）

1. 探针**构造函数里同步 dump 一次**（拿到 t≈0 的真实起点，而不是 45s 后）；
2. 采样加密到前 60 秒每 5 秒一次 ⇒ 才能看到 `0x94020a → 0x7a0020` 的转变发生在第几秒；
3. 同样加密采样跑 M2 ⇒ 直接对比"该在什么时候出现却没出现"。

---

# ⚠️ 2026-10-29 17:20 第五十层：【更正】§191 的"注入后画面变化"已被 §192 推翻

## 我的错误
**我在第 49 层引用 `REALCAM_GROUND_TRUTH_2026-10-28.md` §191：
"注入 0x7a0020 后 6f2d7602→41633658，画面变了"。**
**⇒ **这条已被原作者的 §192 自查推翻**，我引用了过时的证据。**

## 真实情况（作者 §192 A/B 对照）
```
t0           6f2d7602 distinct=225
t1_noinject  6f2d7602 distinct=225   （空等 12s，无注入）
t2_afterDOWN 6f2d7602 distinct=225   （注入 0x30 后）
t3_later     6f2d7602 distinct=225
t4_afterENTER 6f2d7602 distinct=225
⇒ 完全没变。§191 里那次"变化"是**菜单淡入动画**，不是按键效果。
```

## 真正的证据在 §197（可复现）
```
01 基准        画質/画像サイズ1  1/11   768e398e…
02 LEFT 0x15   マイメニュー設定  1/1    d5da977c…
03 DOWN 0x30   同页光标下移             d0b37dd0…
04 DOWN 0x30   同页光标再下移           c5490df2…
05 ENTER 0x1ac ネットワーク2  2/2       4c4ffd46…
06 BACK 0x1f8  菜单退出
07 MENU 0x1ab  保持退出
换一次独立启动重跑，前四步哈希逐字节相同 ⇒ 可复现
```

## 键码表（ZV-E10 实测，可直接复用）
```
UP=0x3  DOWN=0x30  LEFT=0x15  RIGHT=0x1af  ENTER=0x1ac  BACK=0x1f8  MENU=0x1ab
⚠️ 只发一条 status=1 就够；按下+抬起发两条会走两格。
```

## 仍然成立的部分
**`0x7a0020` = InfraInput 输入队列 —— 这一点成立（与 a7c2 的 0x807a0020 只差高位前缀）。
只是"注入让它变化"的**具体证据**要用 §197，不是 §191。**

## E10 时间线实测（QEMU 线做的）
```
行 1295   EXPORT_READY（首帧，非常早）
行 16059  第一个采样点 t=0（45 秒后）
⇒ t=0 时 bootin 的 4 个 rcv 队列已全部齐备：
   0x94020a:1, 0x7a804c:6, 0x7a0020:6, 0x7ae01f:1
⇒ im.elf 有 40+ 条 rcv 记录（0x8f0139:52, 0x16c8477:441, 0x80003b:217, …）
```
**★★ 推论：E10 的 UI 启动**很快**（首帧在日志很早的行就完成了），
而 M2 到 180 秒仍只有 1 个队列 ⇒ **M2 是"真卡住"，不是"慢"。**

## ★★★ 我提的更好方法：**拦队列"注册"而不是"接收"**
**`libosal_uipc.so` 导出（真有这两个注册入口）：**
```
osal_reg_msg_queue_cb        size=116   ← 队列注册
osal_reg_sync_msg_queue_cb   size=116   ← 同步队列注册
reg_sync_msg_queue_cb        size=80
osal_dynamic_id              ← qid 动态分配
osal_subsystem_id            ← 子系统 id
```
**⇒ 拦 `osal_reg_msg_queue_cb` 可以在**队列被创建的那一刻**记录 qid + 时间戳，
   直接回答"0x7a0020 是第几秒创建的"，**完全不需要加密采样**。**
**⇒ 且注册调用次数远少于接收调用（不是热路径），计数开销可忽略。**

## 方法论（第 58 条）★
**引用历史结论前，必须确认它**没有被同一来源的后续章节推翻**。
我引用了 §191，却没看到 §192 已经自查否定 —— 同一个文档里前后矛盾时，
**后面的通常是修正**。
⇒ **引用前，在同一文档里搜一下该结论的关键数字，看它是否在更后面被否定。**

## 56. 2026-10-28 尝试"拦队列注册"（v4）：两次都提前打断 boot，暂缓

按协作方建议，v4 改为拦**冷路径**：`osal_reg_msg_queue_cb` / `osal_reg_sync_msg_queue_cb` /
`osal_dynamic_id` / `osal_subsystem_id`（+ 保留 rcv/snd），并给每条事件打 monotonic 时间戳。

### 结果：boot 在 bootin 启动前就停住

```
v4a（惰性 resolve）: serial=20,400 B，M2INIT/M2Q 计数 = 0
v4b（构造函数里一次性 resolve + 打 M2PTR 指针表）: serial=20,400 B，计数仍 = 0
（对照：v3 能跑到 serial=22,929 B，有 M2INIT/M2T 输出）
```

**⇒ 这两个版本连 `bootin` 都没起来。**

### 最可能的根因

`osal_reg_msg_queue_cb` / `osal_dynamic_id` / `osal_subsystem_id` 属于**库初始化期就会被调用**的
早期 API；而我们的 preload 库**构造函数执行时机晚于**这些早期调用 ⇒ 桩被调用时
`n_*` 指针仍为 NULL ⇒ 桩返回 -1 ⇒ **初始化链被打断**。

（v3 只拦 `osal_rcv_msg_tmo` / `osal_snd_sync_msg`，这两个只在启动后期才被调用，
所以在构造函数补上 resolve 之前也能工作——这也解释了为什么 v3 没事而 v4 必挂。）

### 修复方向（下一轮，三选一）

1. **桩内 NULL 兜底**：指针为空时用 `dlsym(RTLD_NEXT, name)`（**不用 dlopen**，可在早期安全调用），
   再不行就返回"良性值"（reg 类返回 0）而不是 -1；
2. **不在 loader 阶段拦**：保留 v3（rcv/snd），另想办法读**队列注册表**——
   例如在 bootin 起来后（`M2INIT` 之后）再通过 `mprotect` 动态安装 reg 桩；
3. **放弃 reg 拦截**，回到 v3 + 加密 rcv 采样（每 5 秒一次前 60 秒）。

### 备注

* v4 源码已存档：`tools/guest/m2probe_reg_v4.c`；
* **当前 M2 镜像（`/mnt/build/zv_m2probe/kbuild/.../Image`）是 v4b，不能用**，
  需要回退：把 `initramfs-ui/usr/lib/libm2probe.so` 换回 v3 版本并重编；
* v3 的关键结论仍然有效（§54/§55）：M2 bootin 只建立 `0x94020a` 一个等待，180 秒不前进。

---

# ★★★★ 2026-10-29 17:50 第五十一层：拦"注册"打断 boot —— 根因精确定位与修复方向

## 现象（QEMU 线 v4 实测）
```
v4a（惰性 resolve）                    : serial=20,400 B，M2INIT/M2Q 计数 = 0
v4b（构造函数一次性 resolve + 打指针表）: serial=20,400 B，计数仍 = 0
对照 v3（只拦 rcv/snd_sync）           : serial=22,929 B，M2INIT/M2T 正常输出
⇒ v4 连 bootin 都没起来；v3 能跑到服务阶段
```

## ★ 根因精确化（比"构造函数太晚"更进一步）
**关键细节：`M2INIT` 是**构造函数里立刻写的**，它也是 0
⇒ **preload 库的构造函数根本没执行**，不是"执行了但太晚"。**

**机制：**
```
bootin 启动 → ld.so 按依赖顺序初始化各库
  → 某个**更早初始化**的库的构造函数里调用了 osal_reg_msg_queue_cb → 命中桩
  → 此时 preload 库构造函数**还没轮到**
  → native 指针 = 0（.bss 零初始化）→ 桩返回 -1 → 该库初始化失败 → bootin 死
```
**⇒ 同时解释：① 为什么 M2INIT 没出现 ② 为什么 v3 安全
（osal_rcv_msg_tmo 只在启动后期调用，那时构造函数早跑完）。**

## ★ 方向 2 被逻辑排除
**"延迟安装（M2INIT 之后用 mprotect 装 reg 桩）"**构造上不可能成立**：
注册恰恰发生在 bootin 起来之前 —— 这正是打断 boot 的原因。
⇒ 延迟安装 = 等装上时注册已全部发生完 ⇒ 只能记录 0 次。**

## 修复方案（采纳方向 1 + 关键修正）
### ① 桩里**惰性**解析（不要在构造函数里）
```c
static void *g_reg_native = NULL; static int g_null_seen = 0;
int osal_reg_msg_queue_cb(unsigned qid, void *cb){
    if (!g_reg_native){
        g_reg_native = dlsym(RTLD_NEXT, "osal_reg_msg_queue_cb");
        if (!g_reg_native){
            if (!g_null_seen){ g_null_seen = 1;
                out("M2Q NULLNATIVE reg qid=%08x lr=%p\n", qid, __builtin_return_address(0)); }
            return 0;                    /* reg 返回 0 = 良性 */
        }
    }
    out("M2Q t=%llu REG qid=%08x\n", now_ms(), qid);
    return ((int(*)(unsigned,void*))g_reg_native)(qid, cb);
}
```
**✅ `dlsym(RTLD_NEXT,...)` 在早期构造函数阶段**安全**（重定位已完成）；
❌ `dlopen` 才危险（可能死锁在 ld.so 锁）。**

### ② **放弃 `osal_dynamic_id` / `osal_subsystem_id`**
**它们返回的 qid 被调用方拿去构造标识，返回假值会引入隐性错误。
⇒ 只拦 `reg*` 就能回答"0x7a0020 何时被创建"，不需要 dynamic_id。**

### ③ 保留 v3 已验证安全的 `rcv` / `snd_sync`
**最终组合：`osal_reg_msg_queue_cb` + `osal_rcv_msg_tmo` + `osal_snd_sync_msg`**

## ★ `NULLNATIVE` 诊断本身有价值
**若桩在 native 未就绪时被调用，说明**有库在初始化期就注册队列**。
⇒ 打印 `qid` + `lr`（`__builtin_return_address(0)`），再用 `/proc/self/maps` 反查是哪个库。
⇒ 若那个 qid 就是 `0x94020a` ⇒ 与"M2 卡在 0x94020a"完全自洽。**

## 操作纪律
**先换回 v3 再重编；在 v3 基础上**一次只加一个** `osal_reg_msg_queue_cb`。
⇒ 一次加 6 个变量，失败时无法定位是谁的问题（v4 就是教训）。**

## 方法论（第 59 条）★
**"延迟安装探针"对**早期初始化期**调用是**逻辑上无效**的：
探针之所以打断启动，正因为它拦的函数在初始化期就被调用；
等进程起来了再装，目标调用早已完成。
⇒ **拦早期函数的唯一可行做法是"随库一起加载 + 桩内惰性解析 + 惰性失败兜底"。**

## 57. 2026-10-28 ★★★ v5 成功：拿到 M2 的 40 条队列注册记录，**输入队列 0x7a0020 从未注册**

按协作方指导，v5 只加**一个**拦截点 `osal_reg_msg_queue_cb`：
惰性 `dlsym(RTLD_NEXT)` + `dlopen` 兜底 + **失败返回 0（良性）**，并打印 `qid` 与返回地址 `lr`。
rcv/snd 保持 v3 原样（尾调用 native）。

### 结果：boot 正常，捕获 40 条注册事件（全部来自 im.elf）

```
M2INIT pid=119 cmd=/usr/bin/bootin.elf
M2INIT pid=125 cmd=/usr/bin/im.elf
M2REG pid=125 qid=007a0029 lr=000312d0      ← 输入族！但注意是 0x29 不是 0x20
M2REG pid=125 qid=00916084 lr=0a18cd60
M2REG pid=125 qid=00916085 ...（大量 0x9160xx）
M2REG pid=125 qid=012002da / 007d0035 ...
```

### 关键判读：该注册的没注册

| qid | 语义 | M2 是否注册 |
|---|---|---|
| `0x7a0020` | **InfraInput 输入队列**（E10 上 bootin 在等它，n=6） | ❌ **从未注册** |
| `0x7a0029` | 0x7a 族，输入子系统的另一条 | ✅ 已注册 |
| `0x7a804c` / `0x7ae01f` | E10 上 UI 相关等待 | ❌ 未注册 |
| `0x94020a` | bootin 在等的第一条（M2 卡在这） | ❌ 未注册（可能由内核模块注册，需另查） |

**⇒ M2 的 im.elf 确实跑起来了、也注册了 40 条队列，但 `0x7a0020` 这条输入队列没被创建。**
⇒ 与"帧循环 7 入口全 0""bootin 只等 0x94020a"三条证据互相印证：
**InfraInput 的输入队列初始化没有发生，UI 主循环因此从未启动。**

### 下一步（决定性对照）

同样用 v5 跑一次 **E10 基线**，看 E10 是否注册 `0x7a0020`：
* E10 注册了 ⇒ M2 少的就是这一步，断点精确到"谁该注册 0x7a0020"（看 E10 的 lr 反查库）
* E10 也没注册 ⇒ 说明 `0x7a0020` 由别处（内核模块/别的进程）创建，需查注册方

完整 M2 注册表：`docs/m2-registered-qids.txt`（40 条）。

---

# ★★★★★ 2026-10-29 18:30 第五十二层：**`input.ko` 未被加载** —— 可能是输入队列缺失的根因

## QEMU 线 v5 战果（拦 `osal_reg_msg_queue_cb`，惰性 dlsym + 良性兜底）
```
boot 正常（serial 23,669 B vs v4 的 20,400 B）
捕获 40 条注册事件（pid=125 im.elf）
  M2REG qid=007a0029 lr=000312d0   ← 输入族，但是 0x29 不是 0x20
  M2REG qid=00916084 / 00916085 ... / 012002da / 007d0035 ...

★ 该注册的没注册：
  0x7a0020  (InfraInput 输入队列)  ❌ 从未注册
  0x7a804c / 0x7ae01f              ❌ 未注册
  0x94020a                          ❌ 未注册
```

## ★★★ 我的发现：`input.ko` 内核模块**确实注册队列**
```
osal_ulogio.ko: UND osal_reg_sync_msg_queue_cb
backup.ko:      UND osal_reg_msg_queue_cb + osal_reg_sync_msg_queue_cb
input.ko:       "input(kbd):osal_reg_msg_queue_cb err:%d"   ← ★ 键盘驱动注册队列
                "input(kbd):osal_alloc_msg err:%d"
                "input(kbd):osal_snd_msg err:%d"
indctr.ko / udate.ko / osal_uipc.ko 同样注册
```
**⇒ **内核模块走内核态 osal（`osal_uipc.ko`），用户态 preload 看不见它们注册的队列。**
⇒ 完美解释为什么 M2 的 40 条里没有 `0x94020a`/`0x8f013a`/`0x3e014d`：**
```
0x3e014d backup      ← backup.ko 注册
0x8f013a/0x8f0139    ← osal_ulogio.ko 注册
0x94020a             ← 某个内核模块（indctr/udate/input?）
0x7a0020 输入队列    ← 很可能是 input.ko 注册
```

## ★★★★ 我的 `/init` 加载清单里**没有 input.ko**
```
我的 /init 15 个模块：
  devno dmac ldec osal_utm osal_uipc dma330_stub backup
  utimer osal_ulogio kikilog stream stream2 dmm grm_ma grm_gles
⇒ 没有 input.ko

v9 的 /kmod/ 有 ~100 个模块（含 input.ko）
我的 initramfs /kmod/ 只有 6 个（devno dmac emmc ldec lld nand）
```

## ★★★★★ 推演（与所有观测自洽）
```
M2 新菜单依赖 input.ko 提供输入队列
  → 但 input.ko 未加载
  → 0x7a0020 从未创建
  → InfraInput 拿不到输入通路
  → UI 主循环不启动
  → 帧循环 7 入口全 0、帧全黑
```
**⚠️ 反证/差异：E10 老菜单在 QEMU 里能出画，且 bootin 在等 `0x7a0020`
⇒ E10 的输入队列是**用户态自建**的（老架构 libObj 内 InfraInput 自己注册），不依赖 input.ko。
⇒ **这正解释了新旧架构的差异：老菜单自建输入队列，新菜单依赖内核输入驱动。**

## 下一步（QEMU 线执行）
1. 从 v9 取 `/kmod/input.ko` → 放进 initramfs `/kmod/` → `/init` 加 `load /kmod/input.ko`
2. 判据：
   - serial 出现 `input(kbd):osal_reg_msg_queue_cb err:...`（err≠0 = 注册失败）
   - **M2REG 出现 `0x7a0020` ⇒ 根因确认**
   - 帧循环入口从 0 变非 0 / 帧非零
3. ⚠️ input.ko 可能依赖 GPIO/i2c，QEMU 里可能加载失败 —— 失败也是有用结论

## `lr` 反查的正确做法（回答 QEMU 线的问题）
**Thumb BL/BLX 把 LR 置为 `返回地址 | 1` ⇒ 反查前必须 `lr & ~1`：**
```c
Dl_info di; unsigned long lr = (unsigned long)__builtin_return_address(0);
if (dladdr((void*)(lr & ~1UL), &di) && di.dli_fname)
    out("M2REG qid=%08x lib=%s off=0x%lx\n", qid, di.dli_fname,
        (lr & ~1UL) - (unsigned long)di.dli_fbase);
```
**`off` = 库内偏移 ⇒ 可 objdump 定位到具体函数。`dladdr` 在已有 fname 时安全，可在桩内调用。**

## 方法论（第 60 条）★
**"用户态探针看不到"不等于"没有发生"：
内核模块通过内核态 osal 注册的队列，用户态 preload 完全不可见。
⇒ **当一个"应该存在"的对象在任何用户态探针里都找不到时，
   要先问"它是否由内核态创建"**，而不是断言"它不存在"。**

## 58. 2026-10-28 加载 `input.ko`：模块装上了，但输入队列仍未被创建

按协作方线索（`input.ko` 里有 `"input(kbd):osal_reg_msg_queue_cb err:%d"`），
从 v9 rootfs 取 `kmod/input.ko` 放进 initramfs，`/init` 增加一行 `load /kmod/input.ko`，重编运行：

```
MODULE_BEGIN /kmod/input.ko
MODULE_END /kmod/input.ko status=0        ← 模块加载成功
（没有任何 "input(kbd):" 内核日志）
unique qids = 40（与未加载前相同）
0x7a0020 仍 ❌ 未注册
新增 qid：0x91608b/8c/8d/8e（0x9160 族，与输入队列无关）
```

### 判读

1. `input.ko` 能加载（status=0），但**没有走到注册输入队列那段代码**
   （否则至少会打印 `"input(kbd):osal_reg_msg_queue_cb err:%d"`）；
2. `0x7a0020` 依然不存在 ⇒ **input.ko 不是（单独）缺的那一环**；
3. 更可能：`input.ko` 的 probe 需要 QEMU 里不存在的硬件（GPIO/按键矩阵/AD 通道），
   probe 失败后直接跳过注册；或它依赖别的模块先加载（`i2c.ko`/`indctr.ko`/`udate.ko` 都在 v9 kmod 里但未加载）。

### 下一步

1. 加载完整的依赖链：`i2c.ko` → `indctr.ko` → `input.ko`（按 v9 `/kmod` 顺序），
   看是否出现 `input(kbd):` 日志；
2. 若 probe 因缺硬件失败 ⇒ 这条假设可以排除，回到"用户态 im.elf 没注册 0x7a0020"的方向
   （E10 对照仍是关键判据）。

---

# ★★★★ 2026-10-29 19:00 第五十三层：input.ko 加载成功但队列未创建；日志前缀真相

## QEMU 线实验结果
```
MODULE_BEGIN /kmod/input.ko
MODULE_END /kmod/input.ko status=0        ← 加载成功
（没有 "input(kbd):" 日志出现）
unique qids = 40（与未加载时相同）
0x7a0020 仍然未注册
新增 qid: 0x91608b / 0x91608c / 0x91608d / 0x91608e
```
**⇒ 加载 input.ko **不足以**创建 0x7a0020。**

## ★ 我挖到的 input.ko 关键信息

### ① 日志前缀真相：`input(kbd):` 只用于一条错误消息
```
kbd: init (tbl:%d)                    ← ★ init 正常路径打这条，前缀是 `kbd:`
kbd: front uipc error %d / front return error / vgrip uipc error
kbd: malloc %s buf size:%u / kzalloc() error / %s buf size:0
input(kbd):osal_reg_msg_queue_cb err:%d   ← 只有这些带 input(kbd):
input(kbd):osal_alloc_msg err:%d
input(kbd):osal_snd_msg err:%d
input:key table size check error %d / malloc error / read error / number error
input:error:%s tables: cur=%p, adc=%p, gpio=%p, front=%p, vgrip=%p
key:%4lx-
table:%d count:%d set:%d stat:%x value:%d opt:%x
```
**⇒ QEMU 线之前 grep `input(kbd):` 所以看不到 init —— 应 grep `kbd:`。**

### ② 三个模块参数
```
parmtype=key_tbl:uint              ← 按键表索引
parmtype=Input_debug:uint          ← ★ 调试开关，置 1 打开详细输出
parmtype=multi_selector_soft:int
depends=                           ← ★ **无模块依赖**
vermagic=3.0.27_nl-rt106+ SMP preempt mod_unload ARMv7
```
**⇒ 应带参数加载：`load /kmod/input.ko key_tbl=0 Input_debug=1`**

### ③ `depends=` 为空 ⇒ "补依赖链（i2c→indctr→udate→input）"不必要
**⚠️ 但 input.ko 仍可能依赖**硬件资源**（GPIO/ADC/设备树），QEMU 里没有 ——
那不是模块依赖问题。**

### ④ 它要读"按键表"
```
input:key table read error / size check error / number error
⇒ key_tbl 参数指定用第几张表；表本身可能编译进模块或经 /proc/设备树提供
⇒ 若 kbd: init 后立刻 key table read error ⇒ 试 key_tbl=1/2
```

## E10 对照的正确设计（我提醒的）
**⚠️ QEMU 线之前测 E10 用的是 **rcv 探针**（看到 0x7a0020 n=6 是"等待"）。
⇒ **必须用 v5 的 **reg 探针**在 E10 上跑一次，看它是否**注册**了 0x7a0020** —— 这才是有效对照。**
**⚠️ 且 E10 那次同样**不加载 input.ko**（保持变量一致）。**

## 方法论（第 61 条）★
**"grep 关键词错了"比"没有日志"更常见：
`input(kbd):` 是一条错误消息的前缀，而 init 正常输出用的是 `kbd:`。
⇒ **判"某模块没跑"之前，先确认 grep 的字符串确实是它正常路径会打的** ——
   最好直接从模块字符串表里取，而不是按命名习惯推测。**

## 59. 2026-10-28 E10 reg 对照失败（双 preload 库导致早期 stall）；input.ko 无任何 kbd: 日志

### ① input.ko 加载后连 `kbd: init` 都没有

协作方给出 input.ko 的真实日志前缀是 `kbd:`（不是 `input(kbd):`），但重新 grep 结果：

```
grep -c 'kbd:' /tmp/m2p/m7.log  → 0
```

⇒ `MODULE_END status=0` 但 **init 里一行都没打** ⇒ 更像 **probe 根本没被调用**
（没有匹配的设备/设备树节点），而不是 init 跑失败。

### ② E10 reg 对照：双 preload 库导致早期 stall

为做对照，把 v5 探针同时塞进 E10 的 `LD_PRELOAD`（`libuiobserve.so:libm2probe.so`）：

```
serial=16,741  menu=0  M2REG=0
```

⇒ 直接卡在早期，连菜单都没到。**这与之前"两个 preload 库同时上会 stall"的旧结论一致**
（`/etc/ld.so.preload` 里放两个库也有同样风险）。

**⇒ E10 reg 对照必须改成"单 preload"**，两种可行做法：
1. 把探针编译进 libuiobserve（合并成一个库）；
2. 或在 E10 上只 preload 探针（放弃 UI_OBSERVE 观测），先拿 reg 表。

E10 基线已恢复原样（`init.bak` / `ui.cpio.bak` 还原，探针库删除）。

## 60. 2026-10-28 ★ 零成本判据定方向：M2 的 libObj 里仍保留用户态 InfraInput

协作方建议先查 `libObj.so` 里有没有 `InfraInput`（决定"内核 input.ko 提供输入"假设是否成立）：

```
M2  libObj.so: InfraInput 导出 16 个
  InfraInput::GetInstance / SetOsalId / RmCallback / SetKeyRepeat / GetKeyInh /
  SendEventInitData / ...（与 E10 同名同族）
E10 libObj.so: InfraInput 导出 31 个
M2  libObj.so 里 '/dev/input'、'input(kbd)'、'kbd_init' 相关字符串 = 0
```

### 判读：新架构**没有**把输入搬到内核，仍是用户态 InfraInput

1. M2 保留完整 InfraInput 导出（含 `SetOsalId`）⇒ 输入队列仍由**用户态**建立；
2. M2 的 libObj **完全不引用** `input.ko` 的日志前缀/设备节点 ⇒ 输入不是从内核 kbd 驱动来的；
3. ⇒ 协作方"QEMU 缺键盘硬件 ⇒ M2 永远不出菜单"的推论**不成立**，
   QEMU 路线**没有原理性障碍**；
4. ⇒ 方向 2 成立：**M2 的 InfraInput 初始化没有跑到注册队列那一步**，
   而 E10 跑到了（rcv 表里 `0x7a0020 n=6` 就是它的队列）。

### 下一步（收紧到 InfraInput 初始化）

1. 在 M2 上用 `#Lq` 类似手段 dump `InfraInput::GetInstance()` 的实例
   （E10 上实例在 `libObj+0x8c22a0` 的 GetInstance，M2 的偏移需重找：`0x208005`+1）；
2. 直接调用 M2 的 `InfraInput::GetInstance()`，看返回是否非空（初始化是否完成）；
3. 若实例为空/未初始化 ⇒ 断点在 `infra_input_init` 调用处（谁该调它）。

---

# ★★★★★ 2026-10-29 19:40 第五十四层：InfraInput 仍在用户态；**探针漏 hook sync 注册变体**

## ❌ 假说撤回：新架构**没有**把输入搬到内核
```
InfraInput 在三代 libObj 里的存在性：
  E10 老菜单:  字符串 278, 符号 31
  ZV1F 新菜单: 字符串 245, 符号 16
  M2   新菜单: 字符串 245, 符号 16   ← ★ M2 也有
  具体: InfraInput / InfraInputBase / InfraInputKikilog
        InfraInput::iLogLoad / iLogSave
⇒ M2 新菜单的 InfraInput 依然在用户态 libObj 里，没搬到内核
⇒ 第 52 层"新架构依赖内核 input.ko"的推论**撤回**
```

## ✅ `0x7a0020` 是**编译期常量**
```
M2   libObj.so  LE32(0x7a0020) 出现 14 次
E10  libObj.so  出现 31 次
M2   appFw.so   出现 1 次
⇒ 不是运行时动态分配，是硬编码常量
```

## ★★★ 关键发现：v5 探针**漏了 sync 注册变体**
```
libObj.so 导入:  osal_reg_msg_queue_cb        ← v5 已 hook
                osal_reg_sync_msg_queue_cb   ← ★ **v5 未 hook**
appFw.so  导入:  osal_reg_msg_queue_cb
                osal_reg_sync_msg_queue_cb   ← ★ 同样漏了

libosal_uipc.so 导出两个：
  osal_reg_msg_queue_cb
  osal_reg_sync_msg_queue_cb    ← 未 hook
（另有 u_ 前缀变体）
```
**⇒ **`0x7a0020` 很可能是通过 `osal_reg_sync_msg_queue_cb` 注册的，
   所以那 40 条 reg 表里看不到它。**
**⇒ 补上 sync 变体后预期：**
```
出现 0x7a0020 ⇒ InfraInput 确实初始化了 ⇒ 问题在注册之后
仍无           ⇒ InfraInput 初始化没跑到注册 ⇒ 断点更早
```

## 这也修正了 E10 对照的意义
**之前 E10 的 v5 对照（只 hook 普通变体）也**看不到** sync 注册的队列
⇒ "E10 reg 表里有没有 0x7a0020"必须在**两个变体都 hook**之后做才有意义。
⇒ 建议 E10 和 M2 都用完整版重跑再对比。**

## ✅ input.ko "probe 未调用"的判断已被证实
```
input.ko 导入的内核 API：
  udif_driver_register / udif_driver_unregister / udif_create_proc   ← Sony 私有 driver 框架（非 platform_driver）
  gpio_get_data_bit                                                   ← Sony 私有 GPIO API
  Backup_read / Backup_get_datasize / Backup_read_4Byte               ← 从 Backup 分区读按键表
  add_timer / mod_timer / del_timer_sync                              ← 定时轮询
⇒ udif_driver_register 只**注册**；真正 init 在 udif core 匹配设备后调用的 **probe** 里
⇒ QEMU 无键盘设备节点 ⇒ probe 永不调用 ⇒ `kbd: init` 永不打印 ✅
⇒ MODULE_END status=0 说明符号全部解析成功（内核**确实导出** udif_*/gpio_get_data_bit）
```

## 方法论（第 62 条）★
**"探针没看到"有三种可能，必须逐一排除：**
```
① 目标确实没发生
② 目标走了**另一条代码路径**（本例：sync 注册变体 vs 普通注册变体）
③ 目标由**另一个执行域**创建（本例曾怀疑内核态，已排除）
⇒ 在断言"没发生"之前，先从**导入符号表**确认"目标可能走的所有入口都被 hook 了"。
```

---

# ★★★★★ 2026-10-29 20:20 第五十五层：InfraInput 用户态自建确认；`SetOsalId` 无直接调用者

## ❌「QEMU 缺键盘硬件 ⇒ M2 永远不出菜单」——**作废**（第 52 层推论撤回）
```
M2  libObj.so:  InfraInput 导出 **16 个**
   GetInstance / SetOsalId / RmCallback / SetKeyRepeat / GetKeyInh / SendEventInitData
   SetEventKeyList(1968B) / SetEventKeyListAll / GetEventStatus / GetPhyKeyStatus ...
E10 libObj.so:  InfraInput 导出 31 个（同名同族）
M2  libObj.so 里 '/dev/input'、'input(kbd)'、'kbd_init' 字符串 = **0**
⇒ M2 保留完整用户态 InfraInput，输入队列应由用户态建立
⇒ 与内核 kbd 驱动无关 ⇒ "QEMU 缺硬件"不能作为 v9b 失败先验
```

## ★ `SetOsalId` 在 libObj 里**没有任何直接 BL 调用者**
```
M2 libObj 对 InfraInput::SetOsalId(0x208549) 的 BL/BLX：.text 0 处，.plt 0 处
⇒ 与用户仓库 E10 历史记录一致：
  "InfraInput::SetOsalId(0x8c27b8) 在 libObj 里没有任何直接 BL 调用者（扫描全 .text）"
⇒ **它通过函数指针/虚表调用** ⇒ 初始化走虚函数表
⇒ **构造函数没跑完 ⇒ 虚表槽位空 ⇒ SetOsalId 永不调用**（解释"代码在但队列没注册"）
```

## ★ 三处外部引用 `GetInstance`（不是 SetOsalId）
```
AdjChkConfig.so        UND GetInstance ×1
appFw.so               UND GetInstance ×1   ← ★ 新菜单 UI 会主动拿实例
libUsbChargingApp.so   UND GetInstance ×1
```

## `SetOsalId` 签名
```
_ZN10InfraInput9SetOsalIdEPii  →  InfraInput::SetOsalId(int*, int)   size=108
⇒ 收 int* 数组 + 个数 ⇒ **一次性设置多个队列 id**
```

## ★★★ 一个"诊断可能直接变修复"的假设（QEMU 线将验证）
**若 `GetInstance` 是**惰性单例**（首次调用才构造），
则**调用它就会触发构造**，构造过程可能**顺带注册 0x7a0020**。**
**⇒ 实验：调 GetInstance → 立刻再读 reg 表 → 再读帧循环入口。**

**四种结果判读：**
| 现象 | 含义 |
|---|---|
| 返回非空 + reg 出现 0x7a0020 | ✅ 惰性构造触发注册 = 缺失的那一步 |
| 返回非空 + reg 仍无 | 实例存在但注册逻辑没走（虚表/参数） |
| 返回**空** | 单例从未构造 ⇒ 断点更早 |
| 调用崩溃 | 前置初始化未完成 ⇒ 也是有效信息 |

## 方法论（第 63 条）★★
**凡是"因为环境缺 X 所以不可能"的判断，**必须标注为待验证假说**，
并先找"该环境的软件是否仍保留了 X 的替代实现"的反证。**
**⇒ 本次只用一条 `grep libObj` 就否掉了"QEMU 缺键盘硬件 ⇒ 永远不出菜单"。
⇒ 更严重的是：它差点被用来给真机 v9b **判死刑**（"QEMU 都不行，真机肯定不行"）——
   而真机上 v9b 的成败与 QEMU 是否缺键盘**毫无关系**。**
**⇒ 教训：环境性结论不能跨环境外推，尤其是用来做"放弃"决策时。**

## 61. 2026-10-28 ★★★ 补齐 sync 变体后定论：M2 的输入队列 `0x7a0020` **两个 API 都没注册**

协作方指出 v5 只 hook 了 `osal_reg_msg_queue_cb`，漏了 `osal_reg_sync_msg_queue_cb`
（libObj/appFw 两个都导入）。补上后重跑 M2：

```
M2REGSY 条数 = 82        （此前 M2REG = 40）
两变体合计 unique qid = 122

关键队列：
  0x7a0020  ❌ 未注册        ← InfraInput 输入队列
  0x7a804c  ❌ 未注册
  0x7ae01f  ❌ 未注册
  0x7a0029  ✅ 已注册
  0x94020a  ✅ 已注册（pid=121 bootin，lr=0x13208，sync 变体）
  0x3e014d  ❌ 未注册（backup，应为内核模块注册）
  0x8f013a  ❌ 未注册（ulog，内核模块）
  0x8f0139  ✅ 已注册（pid=127）
```

### 定论

1. **`0x94020a` 是 bootin 自己通过 sync 变体注册的**（不是内核模块），
   它注册完就等它 —— 这是正常的同步 RPC 模式；
2. **`0x7a0020` / `0x7a804c` / `0x7ae01f` 在 M2 上两个 API 都没有注册记录**
   ⇒ 不是"hook 漏了"，是真的没创建；
3. 而 M2 的 `libObj.so` **仍导出 16 个 InfraInput 符号**（含 `SetOsalId`）
   ⇒ **InfraInput 的初始化没有执行到"注册队列"这一步**，断点在更上游。

### 下一步（精确到 InfraInput）

* M2 的 `InfraInput::GetInstance` 在 `libObj` dynsym 偏移 **0x208005**（E10 为 0x8c22a0）；
* 直接在 guest 里调用它：
  * 返回 NULL ⇒ `InfraInput` 单例从未构造 ⇒ 断点在 `infra_input_init` 的调用者
  * 返回非空 ⇒ 单例已建但没注册队列 ⇒ 断点在 `SetOsalId`/`RegQueue` 分支

完整注册表（122 条）：`docs/m2-registered-qids-full.txt`。

---

# ★★★★★ 2026-10-29 20:50 第五十六层：`InfraInput::GetInstance` 是**惰性单例**，构造即注册

## 反汇编证据（M2 libObj @0x208004 Thumb）
```asm
20800e: ldr  r1, [flag]
208010: cbnz r1, 208030        ; 已初始化 → 跳过构造
208014: movs r0, #122 (0x7A)   ; ★ 0x7A = 122 = 输入子系统号
20802c: bl   0x3d400           ; ★ 初始化/注册调用（带 0x7A）
208036: blx  0x3a754           ; ★★ osal_wai_sem_tmo(..., -1) **无限等待**
208040: cbnz r3, 208056        ; 实例已存在 → 返回
208042: mov.w r0, #548 (0x224) ; new(548)
20804c: bl   0x207ce4          ; ★★ 构造函数
208066: blx  0x3ce5c           ; osal_sig_sem 释放信号量
```

## ★★★ 三个关键发现
### ① 惰性单例 ⇒ **调用即构造**
**⇒ 在 guest 里调 `GetInstance` 不只是诊断 —— 它会触发构造，
   而构造过程可能正是"注册输入队列"的那一步。**

### ② `movs r0, #122` = **0x7A** = `0x7a0020` 的高 16 位
```
0x7a0020 = 0x007A_0020  →  高16位 = 0x7A = 122
⇒ 构造路径把 0x7A 传给 0x3d400 ⇒ 极可能就是输入子系统的初始化/注册
```

### ③ ⚠️ **死锁风险**：构造路径里 `osal_wai_sem_tmo(..., -1)` **无限等**
**⇒ 若信号量永不设置，调用 `GetInstance` 会永久挂住。
⇒ 信号量只在构造完成后（`0x208066: osal_sig_sem`）才释放。**

**典型死锁形态：**
```
线程A 进 GetInstance → 持信号量 → 构造中阻塞
线程B 进 GetInstance → 等信号量 → 永久等
⇒ 与观察到的"bootin 卡 osal_rcv_msg_tmo / im.elf 卡 futex"形态吻合
```

## ★ 旧线索关联
**早前探针抓到 `osal_wai_sem_tmo id=0x9401c3 tmo=-1`（无限等）
⇒ **很可能就是 GetInstance 里这个信号量**。
⇒ 若 M2 卡在 0x9401c3 ⇒ 说明它已进入 GetInstance 构造路径但构造没完成。**

## 实验建议（QEMU 线将执行）
```
1) 探针里起**独立线程**，延迟 N 秒后调 GetInstance（不要在主线程调，避免挂死整个 boot）
2) 调用前后各 dump 一次 reg 表（看是否新增 0x7a0020）
3) 加超时保护；若无"返回"日志 ⇒ 挂住了
```
**判读：**
| 现象 | 含义 |
|---|---|
| 返回非空 + reg 出现 0x7a0020 | ✅ 构造即注册 ⇒ 找到缺失的调用 |
| 返回非空 + reg 无新增 | 构造了但注册在别处（追 0x3d400） |
| **挂住不返回** | ✅ **死锁就在这里** ⇒ 找谁该 signal 那个信号量 |

## 排除的记录
**`0x94020a` 是 bootin 自己通过 `osal_reg_sync_msg_queue_cb` 注册的（pid=121, lr=0x13208）
⇒ 第 52 层"内核模块注册 0x94020a"的猜测**推翻**。**

## 补齐 sync 变体后的完整注册表（QEMU 线）
```
M2REGSY = 82 条（此前 M2REG = 40），两变体合计 unique qid = 122
  0x7a0020  ❌ 未注册     0x7a804c ❌     0x7ae01f ❌
  0x7a0029  ✅            0x94020a ✅    0x8f0139 ✅
⇒ 0x7a0020 两个 API 都没有记录 ⇒ **不是 hook 漏了，是真的没创建**
⇒ 支持"断点在 InfraInput 初始化更上游"**
```

## 方法论（第 64 条）★★
**"调用某个 API 做诊断"之前，先确认**该 API 是否有副作用**：
本次 `GetInstance` 是惰性单例 ⇒ **调用本身就会构造/注册** ⇒
- 既是诊断（看返回是否非空）
- 也可能是修复（触发缺失的构造）
- 还可能是**风险**（构造里无限等信号量 ⇒ 调用会挂死）
**⇒ 在 guest 里调未知 API 前，必须静态确认：① 是否有副作用 ② 是否有阻塞点 ③ 放哪个线程/线程安全。**
