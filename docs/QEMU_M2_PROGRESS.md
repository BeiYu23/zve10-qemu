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
