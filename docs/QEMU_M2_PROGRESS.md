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

## 62. 2026-10-28 `#GI`（调 InfraInput::GetInstance）首次尝试：命令未被执行

探针加了两件东西：
* `reader2` 线程（**只在 im.elf** 里创建），读 `ttyAM1`，收到 `#GI` 就在单独线程里调
  `libObj_base + 0x208005 | 1`（M2 的 `InfraInput::GetInstance`），并打印 `M2GI base=/ret=`；
* 用完立刻再查注册表（看惰性构造是否顺带注册 `0x7a0020`）。

实测（boot 150s 后发两次 `#GI`）：

```
M2INIT pid=120 cmd=/usr/bin/bootin.elf
M2INIT pid=127 cmd=/usr/bin/im.elf        ← 构造函数都跑了
uart1: ttyAM1 ...                          ← 控制串口存在
regs_before = 122（两个变体合计，与上一轮一致）
007a0020 计数 = 0
没有任何 M2GI 输出                          ← ★ #GI 没被处理
```

**⇒ 命令没送到 reader2，或 reader2 线程没建起来。**

### 可能原因与下一步

1. `pthread_create(reader2)` 在 im.elf 的构造期失败（该进程可能内存/线程受限）；
2. `ttyAM1` 的读竞争/缓冲问题（E10 上曾遇到"open/close 每次重开丢数据"，我们这次保持了 fd 常开，但 im.elf 是 fork+exec 出来的，设备状态可能不同）；
3. 更稳的替代：**让 bootin 的 reader 也能处理 `#GI`**（bootin 的 ttyAM1 读取在 E10 上验证可用），
   通过**跨进程**方式触发 im.elf 里的调用不可行，
   ⇒ 改为把探针逻辑编进 **im.elf 自己启动的最早阶段**（构造期直接调一次 GetInstance，不用命令行触发）。

## 63. 2026-10-28 `GetInstance` 触发点两次失败，原因明确

| 方案 | 结果 | 原因 |
|---|---|---|
| `#GI` 命令（im.elf 里起 reader 线程） | 无输出 | im.elf 构造期 `pthread_create` 不生效 |
| 构造期延迟线程 `gi_worker` | 无输出 | 同上 |
| **挂在 rcv 桩第 400 次调用** | 无输出 | **M2 的 rcv 调用总数只有 1 次**（bootin 只等一个队列），永远到不了 400 |

### 结论与下一步

* **im.elf 的构造期 `pthread_create` 不生效**（三种线程触发都失败，而 stub 本身在该进程内工作正常）
  ⇒ 后续任何"在 im.elf 里起线程"的方案都要放弃，改用**已有线程的桩回调**；
* 触发点要选**在该进程确实高频发生的调用**上：
  M2 里 `osal_reg_msg_queue_cb` / `osal_reg_sync_msg_queue_cb` 有 **122 次**，
  `osal_rcv_msg_tmo` 只有 **1 次** ⇒ **把 gi_fire 挂到注册桩的第 N 次（如第 60 次）**；
* ⚠️ 但注册发生在初始化期，此时调 `GetInstance` 若被其内部信号量卡住，会**把初始化挂死**
  —— 这本身也是一种可用信息（证明死锁就在构造路径里）。

### 目前 M2 侧的确定结论（汇总）

1. `bootin` 只建立 `0x94020a` 一个 rcv 等待，180 秒不动（sync 队列，自己注册后自己等）；
2. `im.elf` 用两个注册 API 共注册 **122 条队列**，但 **`0x7a0020`/`0x7a804c`/`0x7ae01f` 都没有**；
3. M2 libObj **保留 16 个 InfraInput 导出**（含 `SetOsalId`），且不引用 `/dev/input`
   ⇒ 输入是用户态自建，QEMU 无原理性障碍；
4. `input.ko` 的 probe 在 QEMU 里不会被调用（`udif_driver` 无设备节点）⇒ 内核输入假设排除；
5. `InfraInput::GetInstance`(0x208005) 是**惰性单例**，构造路径含 `movs r0,#122`(=0x7A)
   与"无限等信号量"，推论：**M2 可能卡在该构造路径内部**（尚未验证）。

---

# ★★★★★ 2026-10-29 22:00 第五十七层：真实 init 的**完整模块清单**（32 个）—— harness 缺 17 个

## 来源：真实 `/sbin/init`（28,716 B，从 M2 nflasha3 的 initrd.img 提取，ext2 格式）

## ★ init 加载的 32 个模块（从 init 字符串直读）
```
/usr/kmod/{accel, backup, codec_drv, compass, dma330, dmm, grm_gles, grm_ma,
           i2c, indctr, input, iris_dial, kikilog, mag, mcmn_drv, mmc_drv,
           ms_drv, msen_drv, nfc, osal_uipc, osal_ulogio, osal_utm,
           sata_drv, sircs, stream2, stream, udate, upm, usb_extcmd,
           usbg_sen, usb_portMonitor, utimer}.ko
```

## ★★ 我的 harness `/init` 只加载 15 个，**缺 17 个**
```
我的 15 个: devno dmac ldec osal_utm osal_uipc dma330_stub backup
            utimer osal_ulogio kikilog stream stream2 dmm grm_ma grm_gles

❌ 缺（★ = 输入链路关键）:
   i2c.ko ★          indctr.ko ★       input.ko ★★
   iris_dial.ko ★★   udate.ko ★        mcmn_drv.ko ★
   sircs.ko nfc.ko msen_drv.ko sata_drv.ko mag.ko
   codec_drv.ko compass.ko accel.ko upm.ko
   usb_extcmd.ko usbg_sen.ko usb_portMonitor.ko
```

## ★★★ 推断：输入链路模块缺失可能是 `0x7a0020` 未注册的真因
**QEMU 线单独加载 `input.ko` 时 `kbd: init` 没打印（判断 probe 未调用）。
但真实 init **先加载 i2c/indctr/iris_dial/udate/mcmn_drv**，再加载 input.ko。
⇒ `input.ko` 的 probe 很可能需要这些前置模块创建的设备/驱动。**

**⚠️ 且 `udif_driver_register`（input.ko 依赖的框架）**不在任何 .ko 的导出表里**
⇒ 要么在内核，要么由某个未加载模块提供（`mcmn_drv.ko` 是候选）。**

## 建议实验（按真实顺序补输入链）
```
① 加 i2c.ko → indctr.ko → iris_dial.ko → udate.ko → mcmn_drv.ko
② 再加 input.ko（key_tbl=0 Input_debug=1）
③ 看 kbd: init (tbl:N)
④ 看 reg 表是否出现 0x7a0020
⚠️ 只加输入链 6 个，其他（sata/nfc/msen/usb_*）暂不动 —— 避免一次改太多变量
```
**⚠️ 路径：真实 init 用 `/usr/kmod/xxx.ko`（nflasha15 挂 /usr 后其根即 kmod/）。
harness 按实际挂载结构选 `/usr/kmod/` 或 `/kmod/`。**

## ★ 分区挂载的理解修正（QEMU 线/真机线的纠正）
**nflasha15 挂到 `/usr` 后，**镜像根就是 `/usr`** ⇒
镜像里的 `/kmod` = 运行时 `/usr/kmod`（103 个 .ko）。
⇒ 我之前查"v9b 的 /usr/kmod = 0"是**搞错了**（镜像里没有 `usr/kmod` 子目录，因为根就是 /usr）。
⇒ **真机线纠正正确：v9b 的 /kmod 是满的（103 个，与原厂一致）。**

## ★ 真实 init 的挂载与 env 机制（真机线验证 + 我补充）
```
init 明确挂:  /usr（ext2→no-compress→cramfs→vfat 依次尝试）、/system、/setting
/setting 出现 7 次    /usr 出现 46 次    /log 出现 **仅 1 次**
⇒ **/log 可能未在 execve(bootin) 前挂载** ⇒ `LD_PRELOAD=/log/probe.so` 有静默失败风险
⇒ **建议改用 `LD_PRELOAD=/setting/probe.so`**（/setting 明确在挂载列表，bootin 前必就绪）
```
**真机线反汇编确认 `parse_and_set_env`@0x97cc：**
```
含 '=' → putenv(整行)；不含 '=' → unsetenv(整行)；支持注释；单行上限 1024 字节
无白名单过滤（is_oreore_env 只是个标志变量，该函数未引用）
调用时机在 execve(bootin) 之前 ⇒ bootin 及其 fork 的子进程（含 im.elf）都继承
```

## ★ 零代码方案：`LD_DEBUG`（真机线可试）
```
LD_DEBUG=libs / files / bindings    →  glibc 打印库加载与符号绑定到 stderr
LD_BIND_NOW=1                       →  立即绑定，缺失符号立刻暴露
LD_DEBUG_OUTPUT=/setting/lddebug    →  输出到文件，避免刷屏
⇒ 可能**不写任何探针**就看到 appFw.so 是否被 dlopen、卡在哪个符号
```

## 方法论（第 65 条）★★
**"分区镜像里的路径" ≠ "运行时的路径"：
nflasha15 挂载在 `/usr` ⇒ 镜像根 `/kmod` = 运行时 `/usr/kmod`。
⇒ 检查镜像内文件时，必须先把**挂载点**考虑进去，否则会得出"文件不存在"的错误结论。
本次我因此误判"v9b 没有 kmod"，被真机线纠正。
⇒ **更可靠的做法：从 `init` 的字符串里直接读它引用的**运行时路径**，那才是权威。**

---

# ★★★★★ 2026-10-29 22:40 第五十八层：相机 loader 的 `LD_DEBUG` 能力**实测确认**（省真机线一步）

## 提取与分析
```
initramfs /lib/ld-2.11.2.so   101,736 B   （glibc-ports-2.11）
initramfs /lib/libc-2.11.2.so 984,292 B
initramfs /lib/ld-linux.so.3  → 软链（12 B）
```

## ★ 支持的 `LD_*` 变量（完整清单，从 loader 字符串读出）
```
LD_AUDIT
LD_DEBUG            ★
LD_DEBUG_OUTPUT     ★
LD_DYNAMIC_WEAK
LD_LIBRARY_PATH
LD_ORIGIN_PATH
LD_PRELOAD
LD_PROFILE
LD_SHOW_AUXV
LD_USE_LOAD_BIAS
```

## ★ `LD_DEBUG` 的可用选项
```
libs   reloc   files   symbols   bindings   versions   statistics   all
+ "warning: debug option `%s' unknown; try LD_DEBUG=help"
```

## ❌ **`LD_BIND_NOW` 不在支持列表里**
**⇒ glibc 2.11 的 loader 可能不支持（或被编译掉）⇒ 真机线的计划里那行应去掉。
⚠️ 但不影响目标：`LD_DEBUG=bindings` 本身会记录绑定过程，缺失符号会以错误形式出现。**

## ⭐ 意外发现：`LD_AUDIT` 也支持
**rtld-audit 接口可在**每次符号查找/绑定**时回调，比 `LD_PRELOAD` 更可靠
（不用替换符号）。⚠️ 但需实现 `la_version`/`la_symbind` 等，复杂度高于 preload。
⇒ 建议先不用，`LD_DEBUG` 出结果后再说。**

## ⭐ `LD_PROFILE` 也支持
**`LD_PROFILE=libxxx.so` 记录该库调用统计（输出通常到 /var/profile）。
⇒ 后续想知道"哪个函数被调得多"时可用。**

## 使用要点（给真机线）
```
1) LD_DEBUG_OUTPUT 指定的是**前缀**，实际生成 `前缀.<pid>`
   ⇒ 多进程（bootin/im.elf/fork 子进程）各生成一个 ⇒ 正好能区分"哪个进程加载了什么"
2) 先只用 **LD_DEBUG=libs**（输出小，直接回答"appFw.so 有没有被 dlopen"）
   拿到结果后再决定是否开 bindings（appFw 64MB，绑定事件可能几十万条，输出几 MB）
3) ⚠️ 需先确认 /setting 剩余空间（大输出可能写满或阻塞启动）
```

## 明天的真机实验序列（已更新）
```
① 相机 USB 模式改回「大容量存储」
② ~~strings ld-linux.so.3 确认 LD_DEBUG~~ ✅ 已完成，跳过
③ 查 /setting/env.txt 现状（存在/内容/是否被 init 重写）
③a 查 /setting 剩余空间
④ 写 env.txt: LD_DEBUG=libs + LD_DEBUG_OUTPUT=/setting/ldd (+ LD_PRELOAD=/setting/probe.so)
⑤ 重启 → ls /setting/ldd.* → 分析 appFw.so 是否加载
```

## 方法论（第 66 条）★
**"某机制是否可用"这类前置验证，往往可以**离线**完成：
不必等设备连上，直接从**同一固件镜像里提取对应的二进制**检查其字符串表即可。
⇒ 本次 `LD_DEBUG` 支持性验证：设备在充电模式连不上，但从 initramfs 提取
   `ld-2.11.2.so` 一查即知，省掉真机线明天的一个步骤。
⇒ **凡"需要设备在手才能确认"的前提，先问一句"能不能从镜像静态确认"。**

---

# 🌙 2026-10-29 23:10 第五十九层：今晚收尾 —— 明日开工清单（两条线）

## 真机线（`qemu辅助` / session-6d5bc1eb）明日序列
```
① 相机 USB 模式改回「大容量存储」（当前掉在 054c:0994 充电模式）
② ~~strings ld-linux.so.3 确认 LD_DEBUG~~  ✅ 已离线完成（见第 58 层）
③ 查 /setting 现状：
   - /setting/env.txt 是否存在、内容、会不会被 init 重写
   - **试写验证**：`echo test > /setting/_wtest && cat && rm`  ← 比 df 更能确认"可写+路径对+rw挂载"
③a 确认 /setting 剩余空间（当前约 12.7 MB / 20 MB）
④ 写 /setting/env.txt：
     LD_DEBUG=libs
     LD_DEBUG_OUTPUT=/setting/ldd
   （**不加 LD_BIND_NOW**（loader 不支持）、**先不开 bindings**（输出量大））
⑤ 重启 → `ls /setting/ldd.*` → 分析 appFw.so 是否被加载
```

## ⚠️ 今晚补的两个风险点（真机线明日必看）
**① `LD_DEBUG_OUTPUT` 打不开时，glibc **退回写 stderr** ⇒ 灌爆串口 + 拖慢启动 + 可能触发看门狗
⇒ **必须先试写验证 /setting 可写且 rw。****

**② `/setting` 优于 `/tmp` 的额外理由：`/setting` 是持久分区，重启后仍可读；
`/tmp` 若是 tmpfs 重启即失 ⇒ 必须在同一次开机内读完，容错窗口小。**

## 输出量估算（不必担心空间）
```
LD_DEBUG=libs 每库 5~10 行；im.elf ~100 库 + bootin ~20 库 + 子进程
⇒ 合计约几百 KB ~ 1 MB，12.7 MB 够用，无需改 /tmp
```

## QEMU 线（`QEMU` / session-41b33827）明日方向
```
① 按**真实 init 顺序**补输入链模块（第 57 层）：
   i2c.ko → indctr.ko → iris_dial.ko → udate.ko → mcmn_drv.ko → input.ko(key_tbl=0 Input_debug=1)
   判据：出现 `kbd: init (tbl:N)` / reg 表出现 0x7a0020
② 若补模块无效 → 调 InfraInput::GetInstance（惰性单例，构造即注册，但构造路径含无限等信号量，
   必须放**独立线程 + 超时**，挂住本身就是"死锁位置"的答案）
风险：一次只改一个变量（v4 一次加 6 个的教训）
```

## 今晚净收益（从"无从下手"到"一个文件 + 重启"）
```
起点：相机连不上，只有崩溃现象（DMP 之后断在 appFw）
  ↓ 反汇编 /sbin/init
中间：找到代码级确认的注入点（/setting/env.txt + putenv，无白名单，execve 前生效）
  ↓ 提取 initramfs 的 ld-2.11.2.so 查字符串表
终点：LD_DEBUG 可用性已离线证实；发现 LD_BIND_NOW 不支持（避免一次误导性启动）
⇒ 明日实验成本压到：**写一个文件 + 重启 + 看结果**
```

## 方法论（第 67 条）★
**把"需要设备在手"的实验，尽量前置成"只需镜像在手"的分析：
今晚两件事——
① `env.txt` 机制：从 `/sbin/init` 的 `putenv` + `/setting/env.txt` 字符串推出，
   再由真机线反汇编 `parse_and_set_env` 完全确认；
② `LD_DEBUG` 支持性：从 initramfs 提取 loader 查字符串表，当场确认。
⇒ **每轮结束前问一句："还有哪个'必须开机才能验'的前提，其实现在就能静态确认？"**
   —— 这能把真机实验次数压到最少（每次刷写/重启都有风险与成本）。**

## 🌙 第五十九层补遗（真机线明日实操三要点）

**① `env.txt` 千万别带 `\r`**
`parse_and_set_env` 按行 `putenv(整行)` ⇒ 行尾 `\r` 会变成值的一部分
⇒ `LD_DEBUG=libs\r` 会变成未知选项，静默失效。
**正确写法：**
```sh
printf 'LD_DEBUG=libs\nLD_DEBUG_OUTPUT=/setting/ldd\n' > /setting/env.txt
```

**② 启动若循环（camfs.log 显示 3 轮），`ldd.<pid>` 文件会累积**
3 轮 × 若干进程 × <100KB ≈ 1~2 MB，12.7 MB 仍够，**不要删**
⇒ 累积的多轮文件反而能对比"哪一轮走得更远"。

**③ 若 `ls /setting/ldd.*` 一个文件都没有**
⇒ 说明 `env.txt` 没生效（不是 LD_DEBUG 的问题）
⇒ 直接转查"env.txt 是否被 init 覆盖 / 路径是否正确"，
   **不要在 LD_DEBUG 上继续花时间**（避免把"注入失败"误判成"工具不可用"）。

## 真机线今日收敛质量（对方台账记录，我认同）
```
起点：只有崩溃现象（DMP 之后断在 appFw），相机连不上
  ↓ 反汇编 /sbin/init（putenv + /setting/env.txt）
中间：代码级确认的注入点（无白名单，execve 前生效）
  ↓ 提取 initramfs ld-2.11.2.so 查字符串表
终点：LD_DEBUG 可用性离线证实 + 发现 LD_BIND_NOW 不支持
⇒ 明日实验：**写一个文件 + 重启 + 看结果**；失败成本 = 写一个文件（不碰 /usr、不刷分区、删掉即恢复）
```

## 64. 2026-10-29 按真实 init 顺序补 5 个输入链模块：全部加载成功，但 `kbd:` 与 `0x7a0020` 仍为 0

按协作方从**真实 `/sbin/init`** 读出的模块清单（32 个，我们 harness 只有 15 个），
补入输入链模块并按真实顺序加载：

```
load /kmod/i2c.ko
load /kmod/indctr.ko
load /kmod/udate.ko
load /kmod/mcmn_drv.ko
load /kmod/input.ko key_tbl=0 Input_debug=1
```

实测：

```
MODULE_END /kmod/i2c.ko        status=0
MODULE_END /kmod/indctr.ko     status=0
MODULE_END /kmod/udate.ko      status=0
MODULE_END /kmod/mcmn_drv.ko   status=0
MODULE_END /kmod/input.ko      status=0     ← 全部加载成功
serial 27,178 → 31,296（模块确实跑起来了）

但：  grep 'kbd:'    = 0
      grep 007a0020  = 0
```

### 判读

**即使补齐真实 init 的全部输入链前置模块 + `Input_debug=1`，`input.ko` 仍然不打印 `kbd: init`。**
⇒ 与之前结论一致：**它的 probe 依赖 `udif_driver` 匹配到的设备节点，而 QEMU 的 zve10 机型
不提供该设备** ⇒ `0x7a0020` 不可能由内核模块创建。

⇒ **"缺内核输入模块"这条假设彻底排除。**

### M2 侧最终结论（四条假设已三条排除）

| 假设 | 状态 | 证据 |
|---|---|---|
| 缺内核模块（input.ko 等） | ❌ 排除 | 补 5 个模块 + 调试参数，`kbd:` 仍 0 |
| `0x7a0020` 由内核注册 | ❌ 排除 | 同上 |
| QEMU 缺硬件导致原理性不可行 | ❌ 排除 | M2 libObj 保留 16 个 InfraInput、不引用 /dev/input |
| **InfraInput 构造路径未完成** | ⭕ 唯一剩余 | im.elf 注册 122 条队列却无 0x7a0020；`GetInstance` 是惰性单例且构造路径含无限等信号量 |

### 为什么无法继续推进（技术限制，非结论）

* 在 im.elf 内部**无法启动新线程**（三种触发方式全部无效），
  所以"调 `GetInstance` 观察是否构造/死锁"这个实验做不了；
* 唯一可行替代是"把调用挂到注册桩的第 N 次回调上"，但那发生在初始化期，
  若被信号量卡住会把初始化挂死——属于**破坏性实验**，需要单独一轮专门做并准备好回滚。

⇒ **建议把这条留给真机验证**：如果 v9b 在真机上能起 UI，就证明"输入队列由硬件驱动创建、
QEMU 无法复现"；如果真机也不起，则回到"InfraInput 构造"继续查。

## 65. 2026-10-29 下一步方案定稿（协作方纠正 + 零风险读法）

### 纠正一：QEMU 里不存在"破坏性实验"

挂死的代价 = `kill` QEMU + 重跑一次，**没有持久资产被破坏**。
⇒ 正确做法是"把需要冒险的调用排到最后一轮"，而不是"不敢做"。

### 纠正二：不需要线程 —— 用信号处理器

`setitimer(ITIMER_PROF)` + `sigaction(SIGPROF)` 可在**无线程**的情况下获得执行时机
（协作方在 `samp3.c` 上验证过：150ms 一次，成功取到 374 个样本）。

判据用日志配对，非常干净：

```
调用前写:  "M2GI try n=<计数>\n"
调用后写:  "M2GI ret=<返回值>\n"
只看到 try 没有 ret  ⇒ 确认挂在 GetInstance 里 ⇒ 这本身就是答案
```

⚠️ 信号处理器里调 C++ 非 async-signal-safe，但作为一次性诊断可接受；
调用前后都必须用 `write(2)` 直接落盘（不用 printf/malloc）。

### ★★★ 更好的方案：**不要调用，只读全局实例指针**

反汇编显示 `GetInstance` 开头就是 PIC 模式的全局变量读取：

```asm
208008: ldr  r1, [pc, #…]     ; 字面量在 0x20809c
        add  r1, pc           ; GOT 相对
        ldr  r1, [r1, #0]     ; ← 全局"已初始化标志"
…
20803e: ldr  r3, [pc, …]      ; 实例指针全局
208040: cbnz r3, 208056       ; 非空 ⇒ 直接返回
```

⇒ **只要读到那个全局实例指针，就能区分两种情况，且零风险、零副作用：**

| 全局指针 | 含义 | 下一步 |
|---|---|---|
| `== 0` | 单例**从未构造** ⇒ 断点在"谁该调 GetInstance"更上游 | 找 `appFw` 里那 3 处 `GetInstance` 引用为何没执行 |
| `!= 0` | 单例已构造但队列没注册 ⇒ 断点在**构造内部** | 用信号处理器做"调用 + 日志配对"实验定位死锁 |

### 实施要点（下一轮）

1. 探针里用 `dl_iterate_phdr` 拿 libObj 基址（已有 `libobj_base()`）；
2. 按 PIC 公式算出两个全局变量地址：
   `lit = *(u32*)(base + 0x20809c)`；`addr = (base + <add pc 的下一条指令地址>) + lit`
   （需在 objdump 上确认 `add r1, pc` 的确切偏移，别用估算）；
3. 每轮 boot 后 dump 一次 `*addr`，打印 `M2SINGLE ptr=%08x`；
4. 依据上表分流。

### 顺带

`appFw.so` 有 3 处 `InfraInput::GetInstance` 外部引用（`AdjChkConfig.so`、
`appFw.so`、`libUsbChargingApp.so` 各 1 处）——
若全局指针为 0，说明 **appFw 里那次调用没执行到**，这是最直接的下一步线索。

---

# ★★★★★ 2026-10-29 23:50 第六十层：输入链假设排除；**算出 InfraInput 全局指针地址**

## ① QEMU 线：输入链模块假设**已排除**
```
按真实 init 顺序补 5 个模块：
  load /kmod/i2c.ko        status=0
  load /kmod/indctr.ko     status=0
  load /kmod/udate.ko      status=0
  load /kmod/mcmn_drv.ko   status=0
  load /kmod/input.ko key_tbl=0 Input_debug=1   status=0
serial 27,178 → 31,296（模块确实跑起来了）
grep 'kbd:'   = 0
grep 007a0020 = 0
⇒ **input.ko 的 probe 依赖 udif_driver 匹配的设备节点，QEMU 的 zve10 机型不提供**
⇒ **0x7a0020 不可能由内核模块创建**
```

## 四条假设，已排除三条
| 假设 | 状态 | 证据 |
|---|---|---|
| 缺内核模块（input.ko 等） | ❌ 排除 | 补 5 模块 + Input_debug=1，`kbd:` 仍 0 |
| `0x7a0020` 由内核注册 | ❌ 排除 | 同上 |
| QEMU 缺硬件 ⇒ 原理不可行 | ❌ 排除 | M2 libObj 保留 16 个 InfraInput、不引用 /dev/input |
| **InfraInput 构造未完成** | ⭕ **唯一剩余** | im.elf 注册 122 条队列却无 0x7a0020 |

## ★★ 关键纠正：QEMU 里"破坏性实验"不成立
**QEMU 挂死的代价 = kill 掉进程重跑 ⇒ **无持久资产被破坏**，"准备回滚"不存在。
⇒ 真正的代价只是"这一轮拿不到其它数据"，可通过**把调用放在所有诊断收集完之后**规避。
⇒ 所以这是"最后一轮才能做的实验"，不是"破坏性实验"。**

## ★★★ 不需要线程的方案：信号处理器
**QEMU 线报"im.elf 内部无法启动新线程（三种触发方式全部无效）"。
⇒ **信号处理器不需要线程**（我在 samp3.c 里验证过：`setitimer(ITIMER_PROF)` + `SIGPROF` handler）。**
```c
setitimer(ITIMER_PROF,&it,NULL);     /* 150ms */
sigaction(SIGPROF,&sa,NULL);
/* handler 里：第 N 次信号时调用 GetInstance */
out("M2GI try\n");                   /* 调用前落盘 */
void*r=((void*(*)())0x208005)();     /* 直接调 */
out("M2GI ret=%p\n",r);              /* 能到这里=没挂死 */
```
**⇒ "try" 有、"ret" 无 ⇒ **确认挂在 GetInstance 里** ⇒ 这就是答案。**

## ★★★★★ 更好：**读全局指针**（算出地址了，零风险）
**从 `GetInstance` 反汇编解出（两处 `ldr`+`add` 解出**同一地址**，交叉印证）：**
```
libObj 内偏移 = **0x003d7214**
运行时绝对地址 = libObj加载基址 + 0x3d7214
所在段: .bss (vaddr 0x3c7828, size 0xfcc8)

推导：
  208008: ldr r1,[pc,#144] → 字面量@0x20809c = 0x001cf204
  20800c: add r1, pc       → PC=0x208010
  ⇒ 0x001cf204 + 0x00208010 = 0x003d7214
  20803a: ldr r3,[pc,#108] → 字面量@0x2080a8 = 0x001cf1d4
  20803c: add r3, pc       → PC=0x208040
  ⇒ 0x001cf1d4 + 0x00208040 = 0x003d7214   ← 同一个
```
**判读：**
```
global == 0  ⇒ 单例**从未构造** ⇒ 断点在更上游（谁该调 InfraInput 初始化）
global != 0  ⇒ 单例**已构造**但队列没注册 ⇒ 断点在"构造完成到注册之间"
```

**对比调 GetInstance：**
| | 调 GetInstance | **读全局指针** |
|---|---|---|
| 需要线程 | 是（失败） | **不需要** |
| 副作用 | 有（构造/注册） | **无** |
| 会挂死 | 可能 | **不会** |
| 成本 | 一轮 QEMU + 可能白跑 | **10 分钟** |

## 方法论（第 68 条）★★
**"调用函数来观测状态"之前，先问："这个状态是否已经存在于某个全局变量里？"
本次 `GetInstance` 的返回值本质上就是读一个 .bss 全局指针 ——
**直接读它，比调用函数更安全、更便宜、且能区分更多状态**（函数只会返回 NULL/非 NULL，
而全局指针还能配合邻近字段看出"构造到哪一步"）。
⇒ **观测优先选"读状态"，而不是"触发行为"。**

---

# ★★★★★ 2026-10-30 00:20 第六十一层：InfraInput 全局指针地址**四重验证通过**

## 最终地址
```
M2  libObj:  全局实例指针 = base + **0x003D7214**   （.bss 内）
E10 libObj:  全局实例指针 = base + **0x01492524**   （.bss 内，对照用）
```

## 四重验证
**① objdump 自身注释确认字面量地址**
```
208008: 4924  ldr r1,[pc,#144]  ; (20809c <...>)   ← objdump 直接标出 0x20809c
20800c: 4479  add r1, pc
```

**② M2 两处独立访问算出同一地址**
```
第一处: 0x001cf204 + 0x00208010 = 0x003D7214
第二处: 0x001cf1d4 + 0x00208040 = 0x003D7214   ← 相同
```

**③ E10 的 GetInstance 是字节完全相同的指令序列**
```
8c22a4: 4924  ldr r1,[pc,#144]  ; (8c2338)   ← 同编码
8c22a8: 4479  add r1, pc                      ← 同编码
8c22aa: 6809  ldr r1,[r1,#0]
8c22ac: b971  cbnz r1, ...
```

**④ 两边地址都精确落在各自 .bss 内**
```
M2:  0x003d7214 ∈ [0x3c7828, 0x3d74f0)  ✅
E10: 0x01492524 ∈ [0x1459328, 0x1498e7c) ✅
```

## PIC 取址公式（已核实）
```
Thumb: ldr Rd,[pc,#imm8*4] → 字面量地址 = (该指令地址 + 4) & ~3 + imm8*4
       add Rd, pc          → PC = (该指令地址 + 4) & ~3
⇒ 全局地址 = 字面量值 + (add 指令地址 + 4) & ~3
```

## 使用方式与自检
```c
Dl_info di; dladdr((void*)0x208005, &di);
unsigned long base = (unsigned long)di.dli_fbase;
unsigned long v = *(unsigned long*)(base + 0x3d7214);
```
**三值自检：**
```
v == 0                ⇒ 单例从未构造（最可能的初始状态）
v ≈ 0x2xxxxxxx        ⇒ 合法堆指针 ⇒ 已构造
v 是其他奇怪值         ⇒ ⚠️ 可能地址算错，先别下结论
```
**⚠️ 建议在**确定的早期时点**先读一次（应读到 0），若非 0 则地址或时机需重审。**

## 下一步分叉（取决于读数）
```
v == 0  ⇒ 查"谁该调 InfraInput 初始化、为什么没调到"
          → 反汇编 appFw 的 GetInstance 调用点，看前置条件/分支
v != 0  ⇒ 查"构造完成到注册之间断在哪"
          → 用信号处理器（setitimer+SIGPROF，无需线程）做调用+日志配对
```

## 方法论（第 69 条）★★
**"算出来的地址"必须交叉验证，至少四条独立证据：**
```
① 工具自身的注释（objdump 会标出 PC 相对字面量地址）
② 同一函数内多处独立访问应算出**同一地址**
③ 另一代固件（同代码结构）用同一算法应算出**合法地址**
④ 结果必须落在**语义正确的段**（全局对象 → .bss）
```
**⇒ 四条都过才敢用。本次缺任何一条都可能是"看起来对其实错"的地址 ——
而在 guest 里按错地址读内存，会得到看似合理实则无意义的值，极难察觉。**

## 66. 2026-10-29 ★★★ 决定性读数：M2 的 InfraInput 单例**已构造**（非空）

协作方解出全局实例指针地址（两处 PIC 计算交叉验证 = `libObj + 0x3d7214`）。
我先做**静态核对**（不拍脑袋）：

```
lit@0x20809c = 0x1cf204 ; pc=0x208010 ; sum = 0x3d7214
lit@0x2080a8 = 0x1cf1d4 ; pc=0x208040 ; sum = 0x3d7214   ← 两处一致 ✅
```

然后在探针里（im.elf，第 40 次队列注册回调时）读该地址：

```
M2SINGLE base=0b1e0000 addr=0b5b7214 val=00031420
```

### 判读：InfraInput 单例**已构造**

`val = 0x00031420` 非零，且形态是典型的低地址应用堆指针
（E10 上 InfraInput 实例在 `0x1d4a0`，同量级）。

⇒ 反汇编里 `cbnz r1`（已初始化则跳过构造）在此为"真"分支 ⇒ **单例已存在**。

### ⇒ 断点精确到"构造之后"

| 阶段 | 状态 |
|---|---|
| libObj 加载 / InfraInput 代码就位 | ✅ |
| **InfraInput 单例构造** | ✅ **已完成**（本次实测） |
| 队列注册（`0x7a0020` 等） | ❌ **未发生**（122 条注册表里没有） |
| UI 主循环 / 帧循环 | ❌ 全 0 |

⇒ **缺的是"构造完成 → 队列注册"之间的那一步**，最可能就是
**`InfraInput::SetOsalId(int*, int)` 没有被调用**（该方法在 libObj 内**无直接 BL 调用者**，
只能由虚表/外部驱动；`appFw.so` 只引用 `GetInstance`，不引用 `SetOsalId`）。

### 下一步（方向明确）

1. 找 `SetOsalId` 的**虚表槽位**：dump 实例 `0x31420` 的 vtable，查哪个槽指向 `0x208549`；
2. 找谁该填/调它：在 E10 上同一槽位是被哪个调用者触发的（E10 的实例 vtable 可对照）；
3. 或直接检查实例内部字段：E10 上 `+0x20` 曾是 qid 候选位，看 M2 实例该字段是否为空。

## 67. 2026-10-29 地址四重验证通过；读数值形态与预期不符（需再确认）

协作方对 `base + 0x3D7214` 做了四重验证（objdump 注释、M2 两处自洽、E10 同编码、
两代 .bss 各自命中），地址可信。实测读数（§66）为：

```
M2SINGLE base=0b1e0000 addr=0b5b7214 val=00031420
```

### ⚠️ 但形态与协作方给的三值判据不完全吻合

协作方的自检表写："合法堆指针应在 `0x2xxxxxxx` 附近"。
而实测 `0x00031420` 是**低地址**（约 202 KB）。

**两种解释：**
1. **它确实是实例指针**：E10 上 InfraInput 实例就在 `0x1d4a0`（约 120 KB），低地址是这套系统
   应用对象的常态 ⇒ 非零 ⇒ **已构造**；
2. **它不是指针而是标志/计数**：那么"非零"仍意味着"已初始化"，结论相同但含义不同。

**⇒ 无论哪种，都指向"InfraInput 已初始化"，但需要一次**解引用确认**才能落定。**

### 下一步（一次读数即可判定）

在探针里补充读取，按顺序：
1. `pid`（确认这次读数发生在 im.elf 还是 bootin）；
2. `/proc/self/maps` 是否包含 `0x31420` 这一页（判断是否为合法映射地址）；
3. 若合法，读 `*(u32*)0x31420` —— 若看起来像 vtable 指针（指向 libObj/libview 的 .data），
   即可确认"实例已构造"。

这一步零风险（只读 + maps 检查，不解引用未映射地址）。

---

# ★★★★★ 2026-10-30 01:00 第六十二层：**M2 输入架构变更** —— "0x7a0020 缺失"可能根本不是 bug

## ① 实测：InfraInput 单例**已构造**（QEMU 线）
```
M2SINGLE base=0b1e0000 addr=0b5b7214 val=00031420
⇒ val = 0x31420 非零（典型低地址应用堆指针，E10 上实例在 0x1d4a0，同量级）
⇒ `cbnz r1` 走"真"分支 ⇒ **单例已存在**
```
**⇒ 断点从"构造前"推到"构造之后"。**

## ② `SetOsalId` **不在任何数据段被引用**（两代都是）
```
M2 : SetOsalId @0x208549 → rodata/data.rel.ro/data/got/got.plt/init_array 引用 **0 次**
E10: SetOsalId @0x8c27b9 → 同上 **0 次**
⇒ **不在虚表里**，也没有外部库 UND 引用
⇒ "dump 虚表找槽位"这条路走不通
⇒ 它可能是**死 API**（为外部组件预留但本机型不调）
```

## ③ ★★★ M2 的输入架构**变了**（导出符号对比：E10 26 个 / M2 16 个）

**M2 新增：**
```
SetKeyRepeat(Eibii) / SetKeyRepeatReset(Ei)
SetEventKeyListAll(Ei) / GetPhyKeyStatusCache(EiP...)
```

**M2 移除（14 个）：**
```
snd_InfraInputEvent_to_InputServant          ← ★★★ 关键
InputService::callback_tp / panel / direct / tp_raw / convert / um_zoom
InputService::callback_mode_dial / convert_cap / convert_focus_mag
InputService::callback_dial_signed_speed / lens_dial_signed_speed
InputService::callback_lens_dial_signed_speed_individual
InputService::callback_direct_not_user_operation_and_not_reload_mss
```

## ④ ★★★ 推论："0x7a0020 没注册"**可能不是 bug**
```
E10: InfraInput → (0x7a0020) → snd_InfraInputEvent_to_InputServant → InputServant(0x14a0429)
M2 : 整个 InputService 回调族被移除 + snd_*to_InputServant 也没了
   ⇒ **M2 的输入架构是另一套，不走老队列链路**

而 QEMU 线的 reg 表：
  0x7a0020  ❌ 未注册
  **0x7a0029  ✅ 已注册**（同样是 0x7a 族！）
⇒ **0x7a0029 很可能就是 M2 自己的输入队列**
```

**⚠️ 如果成立 ⇒ 我们一直在追的"缺 0x7a0020"就是**在追一个不存在的 bug**。**

## ⑤ 与真机现象的一致性
**真机 v9b：DMP 之后断在 appFw（UI 没画出来，但 im.elf 起来了）。
⇒ 如果问题真是"输入队列缺失"，appFw 至少会跑起来等输入；
   而现象是 **appFw 自身没跑起来** ⇒ 支持"问题在 appFw 应用层"而非 InfraInput。**

## 下一步（优先级调整）
```
1) 先确认 0x7a0029 是不是 M2 的新输入队列（静态搜常量 0x7a0029 的引用位置）
2) 若确认 ⇒ **放弃"0x7a0020 缺失"这条线**，回到"appFw 应用层为什么没跑起来"
3) 若 0x7a0029 与输入无关 ⇒ 再接回 GetInstance 之后的路径
```

## 方法论（第 70 条）★★★
**"某组件在新版本里没做旧版本做的事" ≠ "新版本有 bug"：
新架构可能**根本不需要**那个动作。
⇒ 判定"缺失=缺陷"之前，必须先证明**旧版本的那个动作在新架构里仍然必要**。
本次差点追了一整晚"缺 0x7a0020"，而它可能只是**架构演进后不再使用的旧队列**。
⇒ **反证方向：找出新架构里"承担同样职责的替代物"（本例：0x7a0029），
   如果存在且正常，那"缺失"就不是问题。**

## 68. 2026-10-29 "0x7a0020 可能不是 bug" 假设的静态核对：部分成立，但不足以结案

协作方发现 M2 的输入架构变化（E10 有 `snd_InfraInputEvent_to_InputServant` + 13 个
`InputService::callback_*`，M2 全部移除），提出"0x7a0020 未注册可能属正常，
`0x7a0029` 才是 M2 的新输入队列，整条追查可能是在追不存在的 bug"。

### 我的静态核对

```
                0x7a0020 常量   0x7a0029 常量   0x14a0429(InputServant)
M2  libObj           14 次           4 次              0 次
E10 libObj           31 次           6 次              1 次
```

M2 实际注册到的 0x7a/0x7b/0x7c/0x7d 族（来自 reg 日志）：

```
0x7a0027 / 0x7a0028 / 0x7a0029 / 0x7a018c / 0x7b018d / 0x7c0190 / 0x7c8074 / 0x7d0033
```

### 判读：假设"部分成立"，但**不能据此结案**

**支持"架构变了"：**
* `0x14a0429`（InputServant 队列）在 M2 的 libObj 里**引用次数为 0**，E10 为 1
  ⇒ 输入端点的确换掉了；
* M2 注册的 0x7a 族是**连续分配的序列**（0x27/0x28/0x29），像动态分配的队列组。

**但反对"0x7a0020 不需要了"：**
* M2 的 libObj 里 **`0x7a0020` 常量仍出现 14 次**（E10 是 31 次）
  ⇒ **代码仍在引用这条队列**，不是死代码。

**⇒ 结论：架构确实变了（InputServant 侧移除），但 `0x7a0020` 仍是活引用。
"缺 0x7a0020"既不能确认为 bug，也不能排除 —— 需要看那 14 处引用在哪些函数里。**

### 下一步（比两种猜测都硬）

反汇编/定位 M2 libObj 中 14 处 `0x7a0020` 常量的**引用点**，看它们落在哪些函数：
* 若集中在 `InfraInput::*` 且是"注册/等待"路径 ⇒ 仍应注册，缺失仍是问题；
* 若集中在"发送/查询"路径（只在有输入时用）⇒ 可能注册由别处完成，需找注册方。

---

# ★★★ 2026-10-30 01:30 第六十三层：纠正我自己的判据；单例读取逻辑重新解读

## ① 撤回我的错误判据
**我此前给的三值判据里写"合法堆指针应在 `0x2xxxxxxx` 附近"——
**这是拿常见 Linux 堆布局套的，对这套固件不成立**，撤回。**

**实测与对照：**
```
E10 InfraInput 实例: 0x1d4a0  (~120 KB)
M2  实测:            0x31420  (~202 KB)   ← 同一量级、同一形态
libObj 基址:         0x0b1e0000
⇒ 实例地址**远低于所有库映射** ⇒ 应用对象分配在**固定低地址预保留区**（非 brk/mmap 堆）
```

## ② 重新解读 GetInstance：`0x3d7214` **就是实例指针**（双重检查单例）
```asm
20800e: ldr r1,[0x3d7214]
208010: cbnz r1, 208030      ; 非零 ⇒ 跳过 init，直接走去加锁/返回
208014: movs r0, #0x7a       ; 为零 ⇒
20802c: bl 0x3d400           ;   先做一次 init（可能内部设置 g）
       ; ── 208030 公共路径 ──
208030: sem_wait(-1)         ; 加锁
20803e: ldr r3,[0x3d7214]    ; **再读同一个全局**（双重检查）
208040: cbnz r3, 208056      ; 非零 ⇒ 返回该实例
208042: new(548); ctor       ; 为零 ⇒ 构造
```
**⇒ 标准双重检查 + 一次性 init 单例。`0x31420` 非零 = **实例已存在**。**
**⇒ 不需要解引用确认；若要做，解引用应得到指向 libObj 映射范围内的 vtable 指针。**

## ③ `SetOsalId` 真的不在任何虚表里
```
搜遍 rodata / data.rel.ro / data / got / got.plt / init_array：
  M2 : 0x208549 引用 **0 次**
  E10: 0x8c27b9 引用 **0 次**
⇒ 不在虚表、无外部 UND 引用 ⇒ 可能是**死 API**
⇒ QEMU 线原计划的"dump 实例 vtable 找 SetOsalId 槽位"会白跑一轮，已提醒
```

## ④ M2 输入架构变更（承接第 62 层）
```
E10 有 snd_InfraInputEvent_to_InputServant + 13 个 InputService::callback_*
M2  全部移除，新增 SetKeyRepeat/SetKeyRepeatReset/SetEventKeyListAll/GetPhyKeyStatusCache

⇒ 而 reg 表里：0x7a0020 ❌ 未注册 / **0x7a0029 ✅ 已注册**（同 0x7a 族）
⇒ **0x7a0029 很可能是 M2 自己的输入队列**
```

## 下一步（已建议 QEMU 线跳过 vtable）
```
1) 确认 0x7a0029 是不是 M2 的新输入队列（静态搜常量引用位置，看是否在 InfraInput 附近）
2) 若确认 ⇒ 放弃"0x7a0020 缺失"，转向"appFw 应用层为什么没跑起来"
```

## 方法论（第 71 条）★
**给判据时，不要用"通用系统经验"（如 Linux 堆布局）代替**对该固件的实测基准**。
本次我给的 `0x2xxxxxxx` 判据来自通用经验，而该系统实测对象地址在 `0x1d4a0`~`0x31420` 量级。
⇒ **凡涉及地址/数值范围的判据，必须附"该值在已知正常样本中的实测范围"，
   否则会把正常值误判为异常。**

## 69. 2026-10-29 对 "0x7a0029 是 M2 新输入队列" 的静态反驳

协作方撤回 `0x2xxxxxxx` 判据（`0x31420` 合法），并建议放弃 vtable 路线
（`SetOsalId` 无任何数据段引用 / 不在虚表），改为确认 `0x7a0029`。

### 但静态数据不支持"0x7a0029 是 M2 特有"

```
0x7a0029 常量出现次数：  M2 = 4 次   E10 = 6 次
```

**⇒ `0x7a0029` 在 **E10** 里同样被引用（而且更多）**，显然不是"M2 新增的输入队列"。
⇒ "0x7a0029 才是 M2 的输入队列、所以 0x7a0020 不需要注册"这个推论**站不住**。

### 真正能判定的是"E10 是否也注册 0x7a0029"

只有拿到 **E10 的注册表**才能对比：
* E10 也注册 `0x7a0029` ⇒ 它是两代共用的普通队列，与"新输入架构"无关；
* E10 不注册 `0x7a0029` ⇒ 才能支持"它是 M2 新队列"。

**⇒ 待做：E10 上单 preload（只挂探针，不挂 libuiobserve）跑一次，拿 122 条级别的注册表。**
（双 preload 会 stall，这条已踩过；单 preload 需要重编一次 E10 镜像。）

---

# ★★★★★ 2026-10-30 02:00 第六十四层：`0x7a0020` 是**输入汇聚点**（14 处常量反查到 8 个子系统）

## 完整反查（每处常量 → LDR 指令 → 所在函数）
```
字面量       LDR 位置    所在函数
0x1ec8fc   0x1ec8d0    EyeFiCard_Init
0x1fa824   0x1fa71a    infra_hotshoe_init          （2 处）
0x1faf94   0x1fae9c    infra_hotshoe_init
0x1fc13c   0x1fc046    InfraDial::SetDialEventInterval_Individual
0x1fe098   0x1fe042    infra_sircs_init
0x1fe168   0x1fe152    infra_sircs_init
0x2007a0   0x200764    InfraTp::SetTpConfig
0x200e04   0x200d4a    InfraTp::SetTpEventSw
0x202150   0x20201c    infra_tp_init               （2 处）
0x20231c   0x2021da    infra_tp_init               （2 处）
0x2079ec   0x207944    InfraInput::GetPhyKeyStatus
0x20820c   —（无 LDR，可能 movw/movt 拼或函数边界偏差）
0x20a844   0x20a5b2    InfraInput::SetEventKeyList （2 处）
0x20d614   0x20d368    PhyKeyConfigFree
```

## ★★★ 关键定性
**这 8 个函数全是**输入源**：**
```
hotshoe（热靴/闪光灯）   sircs（红外遥控）   InfraDial（拨盘）
InfraTp（触摸面板）      EyeFiCard（WiFi 卡）
InfraInput::GetPhyKeyStatus / SetEventKeyList（物理按键）
PhyKeyConfigFree（按键配置）
```
**⇒ `0x7a0020` 不是 InfraInput 私有队列，而是**所有输入源共同投递的汇聚点（input sink）**。
⇒ **"架构变了所以不需要"的猜测被推翻**（8 个子系统活跃引用，绝不可能是死代码）。
⇒ **但"缺 0x7a0020 = bug"也不能确认 —— 因为这 14 处都是**发送方**，不是接收方。**（第 71 条方法论再次印证）

## ★★★ 发送方 vs 接收方的关键区分
```
E10: bootin rcv 0x7a0020 n=6      ← **bootin 是接收方**（收到 6 条输入事件）
M2 : bootin rcv 只有 0x94020a     ← bootin 不接收输入了
M2 : im.elf rcv 40+ 条，无 0x7a0020
⇒ **M2 里没有任何进程注册/接收 0x7a0020**
⇒ 若 M2 输入仍走这条队列（8 个发送方都在），则**输入事件发出去没人收**
```

## 下一步：**E10 的 reg 表**升为最高优先级
**QEMU 线原本计划做但被我劝退的"E10 完整 reg 对照"，现在变成最关键一步。**
```
E10 上 0x7a0020 确实有人接收（bootin n=6）⇒ 必然有人注册了它
⇒ 看 E10 reg 表：哪个进程、lr 指向哪个函数注册的
⇒ 那个函数 = M2 里缺失的接收方
⇒ 对比：M2 的 libObj/appFw 里还有没有这个函数
   - 函数在但没调用 ⇒ 断点在调用链
   - 函数被移除   ⇒ 看 M2 用什么替代（可能即 0x7a0029）
```

## 方法论（第 72 条）★★
**"常量被引用"只能证明代码存活，不能证明该路径被执行，更不能证明角色未变。
`0x7a0020` 在 M2 被 8 个**输入源**引用（发送侧），而缺失的是**接收侧**——
**同一个 id 的发送方与接收方是两件事，必须分开验证。**
⇒ 分析队列/端点类问题时，先问"谁是发送方、谁是接收方"，再分别查各自是否就位。**

---

# ★★★★★ 2026-10-30 02:40 第六十五层：注册者锁定到 `bootin.elf`/`im.elf`（非 libObj）

## ① 扫描器正对照通过
```
osal_snd_msg@plt          → 190 处调用 ✅
osal_rcv_msg_tmo@plt      → 123 处调用 ✅
osal_valloc_msg_wait@plt  → 611 处调用 ✅
```

## ② E10 的 libObj 里 `osal_reg_*` **0 处直接调用**
```
osal_reg_msg_queue_cb@plt        → 0
osal_reg_sync_msg_queue_cb@plt   → 0
⚠️ 但 libObj **导入了**这两个符号 ⇒ 可能经 **GOT 间接调用**（ldr r12,[pc,#X]; blx r12）
⇒ "0 处直接调用" ≠ "没调用"，不能下结论
```

## ③ ★★★★★ 注册真正发生在**小二进制**里
```
                      导入 osal_reg_sync_msg_queue_cb
E10 bootin.elf (15,336 B)   ★
E10 im.elf     (23,240 B)   ★
M2  bootin.elf (15,336 B)   ★
M2  im.elf     (23,240 B)   ★
⇒ 两代的 bootin/im.elf **都导入 sync 注册函数**
```
**⇒ 与 QEMU 线实测吻合：`0x94020a` 由 pid=121 bootin 注册，lr=0x13208，**走 sync 变体**。**

## ④ 由此得出更省的下一步
**`im.elf`/`bootin.elf` 只有 15~23 KB ⇒ **可以完整反汇编，穷举所有注册调用点**。**
```
E10 im.elf 注册了哪些 qid？有 0x7a0020 吗？
M2  im.elf 注册了哪些 qid？少了哪个？
⇒ 若 M2 少了某注册点 ⇒ 直接定位到函数
⇒ 若两代一样 ⇒ 注册点不是问题，问题在"调用时传入的值"
```
**✅ 优点：纯静态、两代都能做、**不需要重编 E10 镜像**（比跑 reg 表更省）。**

**⚠️ 限制：`osal_reg_sync_msg_queue_cb(qid, callback)` 的 qid 若来自
`osal_dynamic_id()` 等运行时计算，静态只能看到"有几个注册点、在哪个函数"，
具体 qid 值仍需运行时。但**与 M2 对比注册点数量/分布**已足够有用。**

## ⑤ 待确认（QEMU 线判断哪个省）
**E10 上 bootin 在 rcv `0x7a0020`（n=6），而 bootin 只导入 sync 注册变体
⇒ **bootin 有可能自己注册了 0x7a0020**。
⇒ 若能拿到 E10 的 reg 表（哪怕只有 bootin 一个进程），可直接确认；
   否则用 im.elf/bootin.elf 完整反汇编替代。**

## 方法论（第 73 条）★★
**"某库没调用某函数"不等于"这个动作没发生"：
libObj **导入了** `osal_reg_*` 却 0 处直接 BL ⇒ 说明调用方式可能是 GOT 间接。
⇒ **静态扫描"直接分支"只能发现直调；对 PIC 代码，必须同时考虑
   `ldr rN,[pc,#X]` + `blx rN` 的间接调用形式，否则会漏。**
**⇒ 更稳的做法：从"谁导入了这个符号"入手（导入表是权威的），
   而不是只从"谁 BL 了这个 PLT"入手。**

## 70. 2026-10-29 ★★★ 决定性对照：**E10 也没有注册 `0x7a0020`** —— 整条"缺输入队列"的追查是追一个不存在的 bug

用同一套探针（两个注册 API 都挂）在 **E10** 上跑（`LD_PRELOAD=libm2probe:libuiobserve`，
本次 `menu=0`，说明渲染没起来，但**注册事件捕获了 352 条**）：

```
E10 unique qids = 297
E10 的 0x7a/0x7b/0x7c 族：
    0x7a0027 / 0x7a0028 / 0x7a0029 / 0x7a018c / 0x7b018d / 0x7c0190 / 0x7c8074
0x7a0020 ：**0 条**

M2 unique qids = 122，其 0x7a/0x7b/0x7c 族：
    0x7a0027 / 0x7a0028 / 0x7a0029 / 0x7a018c / 0x7b018d / 0x7c0190 / 0x7c8074
    ← **与 E10 完全相同的同一组**
```

### ⇒ 结论

1. **`0x7a0020` 在两代里都不是通过 `osal_reg_msg_queue_cb` / `osal_reg_sync_msg_queue_cb` 注册的**
   ⇒ 它由**别的机制**提供（内核 uipc 预置 / 另一套创建 API / 静态队列）；
2. **"M2 缺 `0x7a0020`"不是 bug** —— E10（能出画、bootin 确实在收 `0x7a0020 n=6`）同样没有注册它；
3. **M2 与 E10 在 0x7a/0x7b/0x7c 族上的注册集合完全一致** ⇒ 输入队列这一层**没有问题**；
4. 因此 M2 的真正差异在**更上层**：E10 注册 297 条 vs M2 122 条，
   E10 多出来的部分是 M2 因提前卡住而没走到的服务初始化。

### 这对之前结论的影响

* §54/§57/§61 里"M2 缺输入队列 ⇒ UI 不启动"的推理链**失效**（前提不成立）；
* §68 里"`0x7a0020` 仍被引用 14 次"仍然成立，但"引用"≠"应注册"，两者不矛盾；
* 协作方"架构变了、别追 `0x7a0020`"的方向**被这次对照证实**。

### 下一步（回到原始问题）

**放弃输入队列方向**，回到"`Application_Start` 进入但不返回 / 帧循环 7 入口全 0"：
用 E10(297) 与 M2(122) 的**差集**定位"M2 卡在第几条服务初始化"。
`docs/e10-registered-qids.txt` / `docs/m2-registered-qids-full.txt` 可直接对比。

---

# ★★★★★ 2026-10-30 03:00 第六十六层：`0x7a0020` 是**伪 bug**；且 **reg 表不是"队列清单"**

## ① E10 reg 表对照结果（QEMU 线）
```
E10（menu=0，双 preload）：352 条注册事件 → unique 297
M2                       ：122 条

0x7a/0x7b/0x7c 族：
  E10: 0x7a0027 / 0x7a0028 / 0x7a0029 / 0x7a018c / 0x7b018d / 0x7c0190 / 0x7c8074
  M2 : **完全相同**
  0x7a0020：**两代都是 0 条**
⇒ E10 能出画、bootin 确实在收 0x7a0020，但它**也没注册**
⇒ **"M2 缺 0x7a0020" 是伪 bug，这条追查线关闭**
```

## ② ★★★ 根本解释：`osal_reg_*_msg_queue_cb` 不创建队列，只注册**回调**
```
osal_reg_msg_queue_cb(qid, cb)       → 给队列注册**回调**（自动分发）
osal_reg_sync_msg_queue_cb(qid, cb)  → 同步队列回调
osal_rcv_msg_tmo(qid, buf, tmo)      → **直接接收**（无需任何回调注册）
osal_dynamic_id()                    → 动态分配 id
```
**⇒ 队列可以用"直接接收"方式使用，**完全不需要注册回调** ⇒
   这类队列**永远不会出现在 reg 表里**。**

**⇒ 我们把"回调注册"当成了"队列创建"，所以得出了伪 bug。**
**⇒ 正确表述：reg 表 = **回调注册清单**，不是队列清单。不能用它的缺项推断"队列缺失"。**
**⇒ 但它**是"服务初始化进度"的有效指标**（QEMU 线这一点用对了）。**

## ③ rcv 表仍然可靠，但含义变了
```
E10 bootin rcv: 0x7a0020 n=6    ← 真的在收
M2  bootin rcv: 只有 0x94020a   ← 真的没在收
⇒ 差异是真的；但含义可能是"M2 的 bootin 不再负责接收输入"（架构变更），
  而不是"缺队列"。
⚠️ 若 M2 输入接收方被移除而 8 个发送方仍在发 ⇒ 那才是真问题，
   但需要**另一条证据**（"事件发出去没人收"），不能仅凭 reg 表缺项推断。
```

## ④ ★★★ 真正有价值的信号：297 vs 122 的差集
```
只在 E10 有（175 条）: 0x2802e2 / 0x30e063 / 0x30e064 / 0x30e065 / 0x30e066 …
只在 M2  有（4 条）:   0x3d025f / 0x3d0333 / 0x916085 / 0x97e00c
```
**⇒ 双向差集：**
```
① E10 有、M2 没有 ⇒ M2 没走到的服务初始化
② M2 有、E10 没有 ⇒ **M2 走了 E10 没走的分支（可能是新架构差异）**
```
**⭐⭐ 第 ② 类只有 4 条，信息密度高，**建议优先看**：
- `0x3d025f` / `0x3d0333` — 0x3d 族（对照 0x3e014d=backup，0x3d 相近）
- `0x916085` — 0x9160 族（input.ko 加载时曾新增 0x91608b~8e，同族！可能与输入相关）
- `0x97e00c` — 0x97e 族**

## ⑤ ⚠️ `menu=0` 带来的局限（双向差集要小心）
```
本次 E10 是 menu=0（双 preload 副作用）⇒ 297 条可能不是完整集合
⇒ 差异里混了两类：
   ① M2 提前卡住没走到的
   ② E10 自己也没走到（因 menu=0）的
⇒ 只有"E10 有、M2 没有"且**在 E10 menu=1 时也存在**的条目，
   才能确定是 M2 的问题
```

## 方法论（第 74 条）★★★
**用"注册/回调表"推断"对象是否存在"是危险的：
注册表只记录**被注册的那些**，而对象可能通过**直接使用**（本例 `osal_rcv_msg_tmo` 直收）
或**其它创建路径**存在而不出现在表里。**
**⇒ 判定"缺失"之前，必须先确认：① 该表记录的是什么 ② 目标对象是否有不经过该表的创建/使用路径。
⇒ 本次伪 bug 的代价：一整条追查线（约 8~10 轮）跑在错误前提上。**

## 71. 2026-10-29 按"族"对比注册表：M2 **完全没有** `0x30xx` / `0x31xx` 两个子系统

协作方纠正："reg 表是**回调注册清单**，不是队列清单"——
`0x7a0020` 是直接接收型队列（`osal_rcv_msg_tmo`），本就不需要注册回调
⇒ 它不出现在 reg 表是**正常的**，此前的"缺输入队列"彻底关闭。

改按 qid 的**高 16 位（子系统号）**做族级对比（qid 低 16 位常为动态分配，
逐条对比不可靠；族级统计稳定）：

```
E10 族数 = 63      M2 族数 = 45

E10 有、M2 没有的族（前 20）：
  0028 0030 0031 0037 0038 003c 003e 0083 0087 00a0
  00a1 00a2 00a9 00ae 00c7 0102 0107 0108 0119 012f

E10 各族条目数（前 6）：0x30=46  0x91=43  0x31=24  0x97=18  0xa1=10  0x87=10
M2  各族条目数（前 6）：0x91=31  0xa1=10  0x2ad=6  0xaf=4   0xa5=4   0x7a=4
```

### 关键发现

**`0x30`（46 条）与 `0x31`（24 条）在 M2 里条目数为 0 —— 整个子系统缺失。**
这两个族合计 70 条，占 E10 独有集合（206 条）的三分之一。

⇒ **M2 从未初始化 `0x30` / `0x31` 这两个子系统**
（其余族如 `0x91`、`0xa1` 两边都有，只是数量差）。

### 下一步

1. 从 `docs/e10-registered-qids.txt` 里取 `0x30xx` / `0x31xx` 全部 qid，
   结合 E10 日志中的 `lr` 反查**注册它们的库/函数** ⇒ 就知道缺的是哪个组件；
2. 再在 M2 的 lib 树里查该组件是否存在、是否被加载。

---

# ★★★★★ 2026-10-30 03:30 第六十七层：注册表差集分析 —— **整族缺失** 才是可靠判据

## 基础统计（我独立解析两份 txt 文件）
```
E10 unique = 297    M2 unique = 122
共有 91   /  仅E10 206  /  仅M2 31
91+206=297 ✓   91+31=122 ✓
⚠️ QEMU 线报"E10 多出 175"与我算的 206 有口径差异（差集条数 vs unique），不影响结论
```

## ⚠️★★★ 关键隐患：qid 可能是**动态分配**的，精确值不可比
```
证据：0x7a0027/0x7a0028/0x7a0029/0x7a018c 两代**完全相同** ⇒ 固定 id
      但大量其它 qid 两代不同 ⇒ 很可能是 osal_dynamic_id() 运行时分配
⇒ **"E10 有 0x2802e2、M2 没有"这种比较没有意义** —— M2 可能有同一服务、只是分到不同数字
⇒ **精确 qid 差集这条路不可靠**
```

## ★★★ 可靠替代判据：按**子系统（高16位）**看"整族是否存在"
**子系统号看似固定（0x7a/0x7b/0x91 两代一致）⇒ "某子系统在 M2 完全缺失"不受动态分配影响。**

**27 个子系统在 M2 完全缺失：**
```
0x0028  1    0x0030 46 ★★★ 最大族全缺    0x0031 24 ★★ 第二大全缺
0x0037  4    0x0038  3    0x003c  1       0x003e  1 ★★ backup 同族
0x0083  2    0x0087 10    0x00a0  8       0x00a1  8
0x00a2  2    0x00a9  3    0x00ae  9       0x00c7  2
0x0102/0x0107/0x0108/0x0119/0x012f/0x0135/0x013e/0x0147/0x015b  共 15
0x0276/0x02ba/0x02bf                                            共 6
```

## 三条最有价值线索
### ① `0x0030` 族：46 条全部缺失 —— 最大缺口，最可能是"M2 卡住点"
### ② `0x003e` 族：E10 有 `0x3e414e`，M2 0 条
```
0x3e014d = backup（仓库 qid 表）⇒ 0x3e414e 与它同族
⚠️ 与"backup.ko 是内核模块、用户态看不见"要分开 ——
   0x3e414e 是**用户态**注册的（在 reg 表里），所以是真缺失
⇒ **backup 服务在 M2 里可能没起来**
```
### ③ `0x0091` 族：E10 43 / M2 31，但**交错**（M2 独有 0x916085/86/89/8c/8d，E10 独有 0x916087/9e/a7/a8）
**⇒ 同族内交错 = 动态分配的典型特征 ⇒ 这一族不能按精确值对比。**

## 建议（修正上一轮"优先看 M2 独有 4 条"）
```
1) **优先查 0x0030 族**（46 条全缺）—— 族号固定，"整族缺失"结论可靠
2) 其次查 0x003e 族（backup 同族，语义已知）
3) **不要**依赖精确 qid 差集判断服务多少（动态分配会误导）
```

## ⚠️ 另一局限
```
E10 的 297 条是在 menu=0 时采的 ⇒ 可能不是完整基线
⇒ "M2 缺 206 条"里可能有一部分是 E10 自己也没走完
⚠️ 但"整族缺失"仍有效：若 0x0030 族在 E10 menu=0 就有 46 条，
   M2 一条都没有 ⇒ **M2 比 E10 卡得更早**，此推论不受影响。
```

## 方法论（第 75 条）★★★
**对比两份"运行时分配的 id 清单"时，**精确值对比是陷阱**：
动态分配的 id 在同一次运行内稳定，但跨运行/跨进程会变。
⇒ **可靠做法：对比**分配来源的结构特征**（本例：子系统高16位是否出现、固定 id 是否出现），
   而不是对比具体数值。**
**⇒ 判据："这个值在已知正常的另一次运行里是否完全相同？"
   相同 ⇒ 固定，可比；不同 ⇒ 动态，不可比。**

## 72. 2026-10-29 ⚠️【待验证 / 见 §74 已撤回】VA 0x220000–0x250000 段在 M2 里是否有注册

> **本条结论已作废，勿引用。** `lr` 是**进程虚拟地址 = 加载基址 + 库内偏移**，
> 而 E10 libObj 21 MB / M2 libObj 3.9 MB，两代基址与布局都不同
> ⇒ 同一 VA 在两代可能指向完全不同的代码。
> "M2 同区间 0 条"可能只是**地址不同**，不等于"代码没跑"。
> **须先用 §73 的 `dladdr` 补丁归一化成 `(库名, 库内偏移)` 再判断。**

按"注册点 lr"分组，对比两代（`lr` = 注册调用的返回地址，即"谁注册的"）：

```
E10: lr ∈ [0x220000, 0x250000] 的注册 = 19 条
     族分布：0x30(12) / 0x3e(5) / 0x92(1) / 0x97(1)
M2 : lr ∈ [0x220000, 0x250000] 的注册 = **0 条**
```

### 含义

1. E10 的 `im.elf`(pid=122) 里有一段初始化代码（VA 0x22xxxx–0x24xxxx），
   它一口气注册了 **0x30 / 0x3e(backup 同族) / 0x92 / 0x97** 四个族的队列（本次采到 19 条）；
2. **M2 里这段代码一次都没跑**（同区间 0 条）；
3. 这与"0x30 族 46 条全缺、0x31 族 24 条全缺、0x3e 族缺失"完全对应 ——
   **缺口不是零散的，而是集中在同一个初始化模块**。

### ⇒ 断点定位

**M2 的 `im.elf` 在跑到 VA ≈0x220000 那个模块之前就卡住了。**

下一步：确定该 VA 段属于哪个库/组件（需要 E10 运行时 `/proc/pid/maps` 或
`dladdr(lr & ~1)` 反查），然后检查该库在 M2 树里是否存在、是否被加载。

---

# ★★★ 2026-10-30 04:00 第六十八层：qid→服务映射尝试失败；对"高16位分组"的自我修正

## 尝试：假设 qid 高16位 = libIMDB 表索引 → **验证失败**
**做法：从 libIMDB.so 的 .rodata 按顺序提取 184 个 lib*.so 名字，把序号当索引。**
**验证（用已知 `0x3e014d = backup`）：**
```
假设成立 ⇒ idx 0x3e(62) 应为 libbackup.so
实际     ⇒ idx 0x3e(62) = libInfraCacheMgr.so   ❌
其它抽查：0x30=libdiablo / 0x31=libclema / 0x7a=libusbcmd / 0x8f=libInfraWebApiClient 全不匹配
⇒ **假设错误，撤回**
```
**原因：**
```
① .rodata 字符串顺序 ≠ 表索引顺序（表项有显式 idx 字段）
② libIMDB 有**多张表**（"IMDB:default"/"IMDB:qemu"/"IMDB:nfs"/"IMDB:set"/"IMDB:USBJ"）
   ⇒ 提取时把多张表混在一起
```

## 附带收获
### ① `libInfraInput.so` 是**独立模块**（不只是 libObj 里的符号堆）
```
rodata 模块序列前 20：
 0 libosal_uipc  1 libosal_utm  2 libosal_ulogio  3 libInfraInterchipDatatrans
 4 libNVM  5 libbackup  6 libSaveLoadSettings  7 libbkdmn  8 libOnDemandLoader
 9 libInfraKikiLogGenerator  10 libInfraAccessLog  11 libSysDef  12 libMWF
13 libi2cuipc  14 libI2CRight  15 **libInfraInput.so** ★  16 libInfraError
17 libJiritsu  18 libAtn  19 libGetLog
⇒ **待查：v9/E10 树里是否有 libInfraInput.so 这个文件？
   若有，注册逻辑可能在那里（可解释 libObj 里 osal_reg 0 处直调）**
```
### ② `libbackup.so` 在 rodata 出现**两次**（序号 5 和 130）⇒ 印证多表结构
### ③ 已知可靠映射（用户仓库）
```
0x3e014d backup / 0x8f013a ulog / 0x8f0139 ulog 控制
0x14c0424 grm_ma / 0x14a0429 InputServant
0x94016a 0x94020a 0x940270  bootin 自己的 sync 队列族
```

## ⚠️ 对上一轮分析的**自我修正**
**第 67 层按"高 16 位 = 子系统"分组，得出"27 个子系统整族缺失"。
但注意 `0x14c0424` / `0x14a0429` 是 **8 位十六进制**（高16位=0x014c/0x014a），
与 `0x3e014d` 风格不同 ⇒ **qid 编码规则未确认** ⇒ **"高16位分组"可能不成立。**
⇒ **第 67 层的差集结论需要降级为"待验证"，不能作为结论使用。**

## 更稳妥的判据
```
按"固定 vs 动态"分类：
  - 两代都出现的 qid（91 条）⇒ 固定 id，可靠
  - 只在一边出现        ⇒ 可能是动态分配差异，不可靠
⇒ 真正可靠的差异信号很少（共有 91 条不含差异）
```

## 结论：注册表这条路已到尽头
**数据噪音（动态分配 + 多表 + 编码未确认）> 信号。
建议回到**唯一还没排除**的具体位置：**
```
帧循环 7 入口全 0 + Application_Start 进入但不返回
⇒ 断点在 Application_Start 的 6 个调用里
⇒ 早前已定位：#1 是 veneer → 0x2c2ed85（440 B 栈帧函数，继续调用 0x2ce819c/0x25c848a/0x2ce8754）
⇒ **这条线一直没走完，比追注册表更直接**
```

## 方法论（第 76 条）★★
**"从二进制里提取名字列表"时，**顺序不等于索引**：
多表结构（不同 profile/场景各一份）、显式索引字段、字符串池复用
都会让"按位置编号"失效。
⇒ **必须用**已知样本**做验证（本例：用已知的 backup=0x3e014d 去校验），
   验证不过就立即放弃该假设，不要继续往下算。**
**⇒ 教训：我这次是用已知映射做了验证的（所以及时发现失败）；
   而第 67 层的高16位分组**没有验证**就下了结论 —— 这是不一致的做法。**

---

# ★★★ 2026-10-30 04:30 第六十九层：`0x30`/`0x31` 子系统名**查不到**（两条路都失败）

## 尝试 ① libIMDB 表索引 → 失败（第 68 层已记）
## 尝试 ② libosal 里的子系统名表 → 失败
```
libosal_uipc.so 里搜 backup/ulog/input/grm/emg 等独立字符串 ⇒ **全部 0 个**
osal_subsystem_id 在 dynsym 里也没找到该名字的实现
⇒ **子系统名→id 映射不在 libosal 里**
```

## ★ QEMU 线的族级分析（比我的精确 qid 差集更可靠，我认同）
```
E10 族数 63 / M2 族数 45
0x30 = 46 条（M2 全缺）  0x31 = 24 条（M2 全缺）  合计 70 条 ≈ E10 独有(206)的 1/3
而 0x91 / 0xa1 两边都有、只是数量差
⇒ **M2 不是"整体少跑"，而是"特定两个子系统从没起来"**
```
**★ 且其**自我校验**成立：**
```
"0x30 族有 46 条，若真需渲染后才有，menu=0 这次不可能出现这么多
 ⇒ 0x30/0x31 在 E10 里确实是**早期**就注册的 ⇒ M2 缺它们是真缺口"
```

## `lr=0x82` 这类极小值的解释
```
Thumb BL/BLX 把 LR 置为 (返回地址 | 1)
⇒ lr=0x82（偶数极小）可能是：
   ① 经**寄存器间接调用**（blx rN），返回地址落在小偏移
   ② 或桩读到的不是 LR
⇒ 反查应使用 lr & ~1
⚠️ 更可能：这些注册来自**内核模块**或 preload 未覆盖的进程，lr 落在库基址之外
```

## ★ 可行替代路径（不依赖我提供映射）
### 方案 A（最直接）：探针里加 `dladdr(lr & ~1)` 打印库名
```
下次跑 E10 时，对每条注册：
  Dl_info di; dladdr((void*)(lr & ~1), &di);
  print("REG qid=%08x lib=%s off=0x%lx", qid, di.dli_fname, (lr&~1)-di.dli_fbase);
⇒ **一次拿到全部 qid → 库名映射**，不需要事后反查
```
### 方案 B：正确解析 libIMDB 的 `.data.rel.ro`
```
.data.rel.ro: vaddr 0xf00c, size 0x27b4 (10,164 B)
假设每项 24~40 B ⇒ 约 250~420 项
表项可能含: name_ptr / init_ptr / sus_ptr / res_ptr / idx / enable
⇒ **必须按显式 idx 字段排序**，不能按 rodata 字符串顺序
```

## 时间与收益判断
**现在 04:30。今晚已产出：伪 bug 定位、5+ 条假设排除、297/122 注册表、rcv 表、
真实模块清单（32 个）、LD_DEBUG 可用性、v9b 崩溃位置确认。
⇒ **"0x30/0x31 是什么"只是"在哪停"的线索，不是"为什么停"的答案**，
   不值得再熬夜静态硬挖。明日用方案 A 一次解决。**

## 方法论（第 77 条）★
**"名字→id"这类映射，如果**运行时能自然获得**（如通过 dladdr 从 lr 反查），
就不要试图从静态二进制里硬解析表结构。**
**⇒ 判据：能被观测到的，就不要靠推断。**
**⇒ 本次两条静态路径各花一轮均失败；而在探针里加一行 dladdr 就能一次拿全 —— 
   这是典型的"观测成本远低于解析成本"的场景。**

## 73. 2026-10-29 收工总结 + 下一轮第一件事（已写成可直接套用的补丁）

### M2 侧今晚的确切进展

**已排除（有实验）：**
1. 缺内核模块（补 i2c/indctr/udate/mcmn_drv/input.ko，全 status=0，`kbd:` 仍 0）
2. `0x7a0020` 由内核注册（`input.ko` 的 `udif_driver` probe 在 QEMU 无设备节点，不调用）
3. QEMU 缺硬件导致原理不可行（M2 libObj 保留 16 个 InfraInput 导出、不引用 `/dev/input`）
4. **"缺 `0x7a0020`" 本身是伪 bug** —— E10 也不注册它（reg 表 297 条里没有），
   因为 reg 表是"**回调注册清单**"而非"队列清单"，`0x7a0020` 是直接接收型队列

**已确认（有证据）：**
5. M2 的 `InfraInput` 单例**已构造**（`libObj+0x3d7214` 读到 `0x31420` 非空）
6. M2 的注册表 122 条 vs E10 297 条；两代 0x7a 族注册集合**完全一致**
7. **E10 里有一段代码（VA ≈0x220000–0x250000）注册了 19 条队列
   （族 0x30/0x3e/0x92/0x97），M2 同区间 0 条** ⇒ 该初始化模块在 M2 从未执行
8. 0x30 族（46 条）/ 0x31 族（24 条）在 M2 完全缺失，而它们在 E10 的 `menu=0` run 里
   就出现了 ⇒ 不依赖渲染 ⇒ 缺失是真缺口

### 下一轮第一件事（已定稿，可直接实施）

**在 m2probe.c 的注册桩里加一行 dladdr，打印库名 + 库内偏移：**

```c
/* 在 m2_h_reg / m2_h_regs 里，out() 之前插入 */
Dl_info di;
unsigned long lr = (unsigned long)r[4] & ~1UL;   /* Thumb 位必须清掉 */
if (dladdr((void *)lr, &di) && di.dli_fname) {
    char b[200];
    int n = snprintf(b, sizeof b, "M2LIB pid=%d qid=%08x lib=%s off=%08lx\n",
                     (int)getpid(), r[0], di.dli_fname,
                     lr - (unsigned long)di.dli_fbase);
    out(b, n);
}
```

跑 **E10 一次**即可得到"每条注册来自哪个库/哪个偏移"的完整映射，
从而回答"哪个组件注册了 0x30/0x31/0x3e 族"，再在 M2 树里查该组件状态。

⚠️ 注意：`dladdr` 在 `dli_fname` 非空时是安全调用（不会 dlopen），可在桩内使用。

### 今晚的教训（方法论）

* **追 5 轮才发现前提错误**（把"回调注册"当成"队列创建"）——
  ⇒ 任何"X 缺失"的结论，先确认"X 的观测手段是否能覆盖 X 的创建方式"；
* **E10 对照必须尽早做**：我 §54 就测到"M2 只 1 个 rcv、E10 有 4 个"，
  却没意识到"rcv 等待"≠"reg 注册"，白追 5 轮。

---

# ★★★★★ 2026-10-30 05:00 第七十层：缺口收敛到 **libObj 0x21xxxx~0x25xxxx 区域**（ObjMic/ObjMixer/ObjRenderer）

## QEMU 线的关键方法：按**注册点 lr** 分组（不受动态分配影响）
```
E10: lr ∈ [0x220000, 0x250000] 的注册 = 19 条
     族分布：0x30(12) / 0x3e(5) / 0x92(1) / 0x97(1)
M2 : 同 lr 区间的注册 = **0 条**
⇒ **缺口不是"很多服务各自缺失"，而是"同一段初始化代码整个没跑"**
⇒ 同时解释了 0x30 族 46 条、0x31 族 24 条、0x3e 族（backup 同族）全缺
```

## ★ 我查了那段 VA 归属
```
E10 libObj .text: vaddr 0x10fee0, size 0xd3b028 ⇒ 跨度 0x10fee0 ~ 0xe4af08（13.8 MB）
⇒ 0x220000 / 0x230000 / 0x240000 / 0x250000 **全在 libObj 的 .text 内** ✅

符号反查（最近前置函数）：
  VA 0x220000 → 0x216552  **ObjMic_UnregisterCommand**
  VA 0x230000 → 0x216552  **ObjMic_UnregisterCommand**
  VA 0x240000 → 0x231bc2  **ObjMixer_Destroy**
  VA 0x250000 → 0x24f298  **ObjRenderer_UnregisterCommand**
⇒ 该区域是 libObj 的**音频/媒体对象**区：ObjMic（麦克风）/ ObjMixer（混音器）/ ObjRenderer（渲染器）
```

## ⚠️ 与既有结论的矛盾（已向 QEMU 线指出）
```
我早前扫过：E10 libObj 对 osal_reg_* 的**直接 BL 调用 = 0 处**
（正对照通过：osal_snd_msg 190 处 / osal_rcv_msg_tmo 123 处）
⇒ 若注册确实发生在 libObj 0x22xxxx，则必是**间接调用**（GOT / blx rN）
⇒ 与"0 处直接调用 ≠ 没调用"的既有判断一致
```

## ★ 提出验证方法
**探针里对每条注册做：**
```c
Dl_info di;
if (dladdr((void*)(lr & ~1), &di) && di.dli_fname)
    out("REG qid=%08x lib=%s off=0x%lx\n", qid, di.dli_fname,
        (lr & ~1) - (unsigned long)di.dli_fbase);
```
**一次同时回答：① 库名 ② 库内偏移（off 是否≈0x22xxxx）③ 可直接 objdump 定位函数**

**⚠️ 已请 QEMU 线提供那 19 条的**具体 lr 值**（非区间），以便逐个定位函数。**

## 关键待判明
**⚠️ 若确认是音频/媒体子系统，可能推翻"UI 初始化没跑"的直觉，
变成"**媒体子系统初始化没跑**"。
⚠️ 但 `ObjRenderer`（渲染器）**可能就是 UI 渲染链的一部分**
⇒ 需区分：19 条里哪些在 ObjMic 区（音频，可能无关 UI）、
   哪些在 ObjRenderer 区（渲染，直接相关 UI）。**

## 方法论（第 78 条）★★★
**按"**注册点代码位置（lr）**"分组，比按"注册出来的 id"分组可靠得多：
id 是运行时值（动态分配、跨 boot 不可比），而**代码位置是固定的**。
⇒ 当发现"某族 id 整族缺失"时，下一步应立刻问：**这些 id 是谁注册的（lr）？
   它们的 lr 是否集中？** 如果集中，缺口就是一个**代码模块**而不是多个服务。
⇒ 本次正是靠 lr 分组，把"很多服务各自缺失"收敛成"一段初始化代码没跑"。**

---

# ★★★★★ 2026-10-30 05:30 第七十一层：**lr 是 VA，跨代不可比**（对 QEMU 线方法的关键纠正）

## QEMU 线的方法与其结论
```
按 lr 分组（lr = 谁调用注册 API）：
  E10: lr ∈ [0x220000, 0x250000] 的注册 = 19 条（族 0x30/0x3e/0x92/0x97）
  M2 : 同区间 = 0 条
⇒ 结论："E10 有一段代码注册了 19 条，M2 里这段代码一次没跑"
```

## ⚠️★ 我指出的致命细节：**lr 是进程虚拟地址（VA），依赖加载基址**
```
VA = 库加载基址 + 库内偏移
而两代的库完全不同：
  E10 libObj = 21 MB  (.text 0x10fee0 ~ 0xe4af08)
  M2  libObj = 3.9 MB
⇒ 同样的 VA 0x220000，在两代里可能指向**完全不同的代码**
```

**⇒ 因此 QEMU 线的两个结论都不能直接用：**
### ① "M2 里 [0x220000,0x250000] = 0 条" 可能是**假象**
```
若 M2 的 libObj 加载基址不同，
E10 注册点的库内偏移 0x220000 在 M2 里对应的 VA 可能是 0x1a0000 或 0x310000
⇒ "0 条"可能只是区间选错，不是"代码没跑"
```
### ② "按 lr 排序找最大 = 走到的位置" 更危险
```
若某库加载在高地址（如 0x40000000），其注册 lr 全部很大
⇒ "最大 lr" 指向那个库，而不是"最后执行的模块"
⇒ **按原始 lr 排序 = 按加载地址排序，不是按执行顺序**
```

## ✅ 正确做法：归一化 + 映射到函数名
```c
Dl_info di;
if (dladdr((void*)(lr & ~1), &di) && di.dli_fname) {
    unsigned long off = (lr & ~1) - (unsigned long)di.dli_fbase;
    out("REG qid=%08x lib=%s off=0x%lx\n", qid, di.dli_fname, off);
}
```
**⇒ 用 (库名, off) 比较。**
**⚠️ 但两代 libObj 布局不同（21MB vs 3.9MB）⇒ 同一 off 未必对应同一函数
⇒ **更稳的是映射到 (库名, **函数名**)，而不是 (库名, off)。**

## ★★ 更好的方法：**给注册事件加单调时间戳，按时间排序**
```
"注册数量" ≠ "执行顺序"；"lr 地址顺序" ≠ "时间顺序"
⇒ 一个高地址的库可能很早就注册，一个低地址的库可能很晚才注册

正确做法：
  探针里给每条注册打 CLOCK_MONOTONIC 时间戳
  ⇒ 按**时间**排序 ⇒ 真实的执行顺序
  ⇒ E10 最后一条注册 vs M2 最后一条注册 ⇒ 两者之间就是 M2 停下的位置
```

## 关于"Application_Start 不返回"
**QEMU 线指出：这是**现象**，不是断点（我认同）。
真正该问的是"im.elf 主线程阻塞在哪"（已知 `futex_wait_queue_me`）。**

## 方法论（第 79 条）★★★
**跨进程/跨运行对比"地址类数据"（lr / pc / 指针）时，必须先归一化：
```
原始地址 = 基址 + 偏移  ⇒ 基址不同则原始地址不可比
```
**⇒ 归一化到 (模块名, 模块内偏移)，必要时再映射到**符号名**。
**⚠️ 更根本的：地址顺序 ≠ 时间顺序。想问"执行到哪一步"，
   必须用**时间戳**排序，不能靠地址大小排序。**
**⇒ 本次 QEMU 线的 lr 分组思路正确（比按 qid 分组可靠），
   但"跨代比较原始 lr 值"这一步有基址陷阱 —— 已在偏离结论前指出。**

## 74. 2026-10-29 ★ 撤回 §72 的跨代 lr 对比（协作方指出致命细节）

协作方指出：**`lr` 是进程虚拟地址，跨代不可比** —— 两代 libObj 大小不同
（E10 21 MB / M2 3.9 MB）、加载基址不同 ⇒ 同一个 VA 在两代指向完全不同的代码。
**⇒ §72 "E10 的 VA 0x22-0x25 段注册 19 条、M2 同区间 0 条" 的结论作废**（区间选错即假象）。

同理，我提出的"把 M2 的 122 条按 lr 排序找最大 = 走到的位置"也**作废**：
**地址顺序 ≠ 时间顺序**（高地址库可能很早就注册）。

### 我尝试的替代方案（也失败）

用**串口日志的行顺序**当作时间顺序，比"族序列的最长公共前缀"：

```
E10 事件数 = 352     M2 事件数 = 122
最长公共前缀 = 17 条
第 17 条后 E10: 01a2 0091 007e 007b 016c 0091 …
第 17 条后 M2 : 0080 0091 02ee 007e 00cf 007b …
```

**⇒ 只对上前 17 条就开始分叉。** 原因大概率是**多线程并发注册的交错差异**，
而不是"M2 停在 17 条处"。⇒ **这个指标也不可靠。**

### 结论：跨代"注册表对比"这条路基本走不通

理由汇总（都是自己或协作方验证过的）：
1. qid 低 16 位可能动态分配 ⇒ 精确 qid 不可比；
2. `lr` 是 VA ⇒ 跨代不可比（需 dladdr 归一化 + 再映射函数名，而两代 libObj 布局不同）；
3. 日志顺序受多线程交错影响 ⇒ 族序列前缀只有 17 条；
4. reg 表本身只是"回调注册清单"，不是"队列/服务清单"。

⇒ **继续在注册表上做文章，大概率是在追第四个"不可比数据"的错误结论。**

### 建议的下一步（回到直接观测）

* `im.elf` 主线程已知阻塞在 `futex_wait_queue_me` ⇒ 用**信号处理器**（不用线程）
  在该时刻采样一次 `pthread` 相关状态 / 或读 `Application_Start` 的调用链下一步；
* 或接受"QEMU 侧到此为止"，把 M2 的结论交给真机验证。

---

# 🌙 2026-10-30 06:00 第七十二层：今晚总账 + 明日两条线动作

## ⚠️ 一条重要修正：`VA≈0x22xxxx` 结论应标"待验证"
**QEMU 线账本写了"E10 的 VA≈0x22xxxx 那段代码注册 19 条，M2 同区间 0 条"作为**已确认**。
⚠️ 但这**不能算已确认** —— `lr` 是 VA = 加载基址 + 库内偏移，而两代库布局完全不同
（E10 libObj 21MB / M2 libObj 3.9MB）⇒ **同一 VA 在两代可能指向完全不同的代码。**

⇒ 已建议标为：
```
【待验证】E10 有 19 条注册 lr ∈ [0x220000,0x250000]；M2 同区间 0 条
         —— 但原始 lr 跨代不可比（基址不同），
            需 dladdr 归一化后才能判断是"代码没跑"还是"地址不同"
```
**⇒ dladdr 补丁不是锦上添花，而是这条结论的**前置条件**。**

## 今晚已排除（有实验）
```
① 缺内核模块（input.ko 等）—— 补 5 模块 + Input_debug=1，kbd: 仍 0
② 0x7a0020 由内核注册      —— 同上
③ QEMU 缺硬件 ⇒ 原理不可行 —— M2 libObj 保留 16 个 InfraInput、不引用 /dev/input
④ **"缺 0x7a0020" 是伪 bug** —— E10 也不注册它；reg 表是回调注册清单、不是队列清单
```

## 今晚已确认（有证据）
```
① M2 的 InfraInput 单例**已构造**（全局指针 @base+0x3d7214 = 0x31420 非零）
② 注册表 122 vs 297；两代 0x7a 族**完全一致**（7 条）
③ 0x30(46)族 / 0x31(24)族 在 M2 真缺失（E10 menu=0 run 里就有 ⇒ 不依赖渲染）
④ 真机 v9b：DMP_2D_Initialize 之后断在 appFw；`/log/im_manual.log` 仅 2 行
```

## 今晚的工具发现
```
① 相机 loader `/lib/ld-2.11.2.so` 支持 LD_DEBUG / LD_DEBUG_OUTPUT
   ❌ **不支持 LD_BIND_NOW**（省掉一次误导性启动）
② `/sbin/init` 有 putenv + /setting/env.txt ⇒ 可注入环境变量（无白名单，execve 前生效）
③ 真实 init 加载 **32 个模块**（harness 只加载 15，缺 17，含输入链）
④ InfraInput::GetInstance 全局指针地址 @base+0x3d7214（四重验证）
```

## 明日两条线动作
```
QEMU: 套 dladdr 补丁 → 跑 E10 一次 → 拿 (lib, off) → 再判断"M2 缺哪段代码"
真机: 写 /setting/env.txt (LD_DEBUG=libs + LD_DEBUG_OUTPUT=/setting/ldd)
      → printf 写入（避免 \r 污染）→ 重启 → ls /setting/ldd.* → 看 appFw 是否 dlopen
```

## 方法论（第 80 条·本轮总结）
**今晚最高频的错误模式：**把"可观测的表/数值"当成"对象的身份/存在性"**：
```
× reg 表 → 队列清单（实为回调注册清单）
× qid 值 → 服务身份（实为动态分配）
× libIMDB 字符串顺序 → 索引（实为多表混合）
× lr 原始值 → 跨代代码位置（实为 VA，依赖基址）
× lr 地址顺序 → 执行顺序（实为加载地址顺序）
```
**⇒ 共性：都是**把"某个观测面的值"直接等同于"目标对象的属性"**。
   ⇒ 每次下结论前问一句：**"这个观测值在目标语境下是否唯一、可比、有语义？"**
**⇒ 今晚几乎每一轮的价值都在"提前拦住这类等同"，而不是"找到了新答案"。**

---

# ★★★★★ 2026-10-30 06:40 第七十三层：19 条注册点全部定位 —— 是**虚表分发的 AVBBC 通道管理**，可能是 destroy 路径

## QEMU 线给的 19 个具体 lr（E10，pid=122=im.elf）
```
0x221500 → 0x3e414e            0x242e98 → 0x97e07d
0x227480 → 0x920168            0x243420 → 0x30e07a
0x228080 → 0x30e07b            0x243998 → 0x30e078
0x241068 → 0x3e414e            0x243a28 → 0x30e07c
0x241658 → 0x3e414e            0x243b30 → 0x30e071
0x2416a8 → 0x3e414e            0x244000 → 0x30e075
0x241b48 → 0x3e414e            0x244080 → 0x30e074
                               0x244100 → 0x30e073
                               0x244180 → 0x30e072
                               0x244270 → 0x30e077
                               0x244428 → 0x30e079
                               0x244448 → 0x30e076
```
**★ 形态：`0x30e071..e07c` 共 12 条，lr 密集在 0x243420–0x244448（约 4 KB）⇒ 表驱动/循环注册**

## ★ lr 值确认是**库内偏移**（不是绝对 VA）
```
所有 lr < 0x250000，而库加载基址通常在 0x0xxxxxxx 量级（M2 实测 libObj base=0x0b1e0000）
⇒ lr 是偏移 ⇒ 0x221500~0x244448 落在 E10 libObj .text (0x10fee0~0xe4af08) 内 ✅
⚠️ 但**探针里 lr 的取法需确认**（原始 / &~1 / -4），差 2~4 字节会指向相邻的不同指令
```

## ★★★ 密集区（0x243420 附近）的真实形态
```asm
243410: ldr.w r0, [r5, #324]   ; 取对象指针
243414: cbz   r0, 24342c       ; 空则跳过
243416: ldr   r3, [r0, #0]     ; 取虚表
243418: ldr   r3, [r3, #4]     ; ★ 槽 1
24341a: blx   r3               ; 调用（销毁？）
24341c: ldr   r0, [r5, #76]
24341e: movs  r2, #0
243420: movs  r1, #25 (0x19)   ; ★★ 参数1 = 25
243422: str.w r2, [r5, #324]   ; 清字段
243426: ldr   r3, [r0, #0]     ; 虚表
243428: ldr   r3, [r3, #64]    ; ★★ 槽 16
24342a: blx   r3               ; 调用（lr 落在这附近）
```
**⇒ 形态：**虚表分发的逐条处理**（不是 12 个模块 init）：
   遍历"通道/源"（25 个索引），每条调一次虚表槽 16。**

## ★ 这解释了"libObj 里 0 处直接 BL osal_reg"
**全是 `ldr r3,[r3,#N]; blx r3` 虚表调用，无直接 BL 到 PLT ⇒
**若注册也经虚表间接完成，则"0 处直接 BL"完全解释得了。****
**⚠️ 但也意味着：虚表是运行时构造的（`ldr r3,[r0,#0]`）⇒ **静态追不到具体注册函数**，
   必须靠 dladdr + 运行时 LR 记录。**

## ⚠️ 重要定性修正
**我之前说"ObjMic/ObjMixer/ObjRenderer 音频/媒体区" —— **修正**：
```
disassembly 里出现 OBJAVBBCBASECLASS::DmmChC1、AvbbcBaseClassLog::SetLog3
⇒ 该区域是 **AVBBC 基类家族**（被 Mixer/Mic/Renderer 共用的通用基类），
   不是"音频子系统"
⇒ 形态是 **AVBBC 基类的通道管理/清理**
```

## ⚠️★ 一个必须确认的可能：这可能是 **destroy** 路径而非 create
```
若 19 条来自销毁/清理（Destroy/Release）而非初始化：
  E10 走到"清理 25 个通道" ⇒ 说明它**先创建过**
  M2  没走到清理          ⇒ 可能因为**根本没创建**
⇒ **M2 缺的可能是 create，清理缺失只是结果**
⭐⭐ 这反而更支持"某个初始化模块没跑"，但方向要确认。
```

## 明天开工第一件事
```
1) 确认探针里 lr 的取法（原始 / &~1 / -4）—— 决定后续所有定位
2) dladdr 拿 (lib, off) —— 确认确实在 libObj
3) 用 off 精确定位函数，看调用上下文是 **create** 还是 **destroy**
```

## 方法论（第 81 条）★★
**"注册点地址"落在某函数内，**不等于**"该函数在做注册"：
本次 0x243420 处的指令是 `movs r1,#25`，**不是调用指令**。
⇒ 必须先确认**lr 的语义**（是"调用后地址"还是"调用地址"还是"已减4"），
   才能把 lr 映射到正确的指令，进而判断上下文语义（create/destroy/其他）。
⇒ **地址归属分析的第一问永远是："这个值精确指向哪条指令？"**

## 75. 2026-10-29 ★★★ 重大自查：探针里打印的"lr"其实是 **r4**，不是 lr

压栈顺序：`push {r0,r1,r2,r3,r4,lr}` ⇒ 内存布局：

```
[sp+0]=r0  [sp+4]=r1  [sp+8]=r2  [sp+12]=r3  [sp+16]=r4  [sp+20]=lr
 ⇒ unsigned *r:  r[0]=r0  r[1]=r1  r[2]=r2  r[3]=r3  **r[4]=r4**  **r[5]=lr**
```

而探针里写的是 `r[4]` 并注释成 "lr" ⇒ **所有日志里的 `lr=` 值实际是寄存器 r4 的内容**。

**⇒ 由此作废：**
* §72 的"lr ∈ [0x220000,0x250000]"分析（那是 r4 值）；
* 交给协作方的"19 条 lr 列表"（`docs/m2-19-reg-sites.md`）；
* 我提出的"按 lr 排序找走到哪里"。

**⚠️ 两个独立问题要分开（协作方提醒"待验证 ≠ 已证伪"是对的，但这里更基本）：**

* **问题 A（实现 bug，确定）**：字段取错 —— `r[4]` 是 **r4**，`lr` 在 `r[5]`。
  ⇒ 日志里那一列的值**既不是 VA 也不是偏移，而是 r4 的内容**。
  ⇒ §72 的结论**不是"待验证"，而是"依据的字段是错的"，必须先重测**。
* **问题 B（lr 的坐标系，当前无证据）**：`lr` 究竟是**原始 VA** 还是**已减基址的偏移**，
  **目前无法判断** —— 因为此前记录的那一列是 r4，不是 lr。
  ⇒ **"lr 是进程虚拟地址"这句话不能写成事实**；也不能反过来说"它是偏移"
  （"数值小 ⇒ 像偏移"的推论建立在 r4 上，同样无效）。
  ⇒ 唯一可确定：**先修 `r[4]→r[5]` 取到真正的 lr，再用 `dladdr` 的 `dli_fbase` 定性**。

⇒ 正确顺序：先修字段（`r[5]`）→ 打印 `lr` 与 `r4` 两列 → 再谈可比性。

**⇒ 也解释了协作方指出的异常**："lr=0x243420 落在 `movs r1,#25` 而不是调用指令上"，
以及大量 `lr=0x00000082 / 0x85` 这类极小值 —— 那些是 **r4 作循环计数器/局部变量** 的值，
根本不是返回地址。

### 修复

```c
/* 正确：lr 在 r[5] */
"KEYINJECT/M2REG pid=%d qid=%08x lr=%08x\n", pid, r[0], r[5]
```

并建议同时打印 `r[4]` 以便对照（确认之前的数值来源）。

### 教训（第三条方法论，与前面两条并列）

**"读寄存器"这类底层探针，必须先用一个可验证的样本自检**
（例如：已知调用点的地址，看打印值是否等于"该调用指令之后的地址"）。
我连续两轮基于这个错误字段做了分析，而协作方从**指令形态**（movs 不是调用）反推出异常 ——
**这说明"用第二个源的独立特征交叉验证"比"用同一份数据反复推理"可靠得多。**

---

# ★★★★ 2026-10-30 07:10 第七十四层：【待验证】vs【已证伪】的区分 —— 别把预防性警告当结论

## 背景：QEMU 线收口时把 §72 标为"作废"，但状态应为【待验证】

**我的两条消息时间线（交叉）：**
```
① 看到数值**之前**（预防性警告）：
   "lr 是 VA = 基址 + 偏移 ⇒ 跨代不可比 ⇒ 'M2 同区间 0 条'可能是假象"
② 拿到 19 个值**之后**（观察）：
   "所有 lr < 0x250000，而库加载基址通常在 0x0xxxxxxx 量级
    ⇒ lr **看起来是库内偏移**；0x221500~0x244448 落在 E10 libObj .text 内 ✅"
```
**⇒ 所以"VA 问题"的状态是：**倾向"是偏移 ⇒ 可比"，但未经 dladdr 证实 ⇒ 【待验证】**
**⚠️ 不是"已证伪 ⇒ 作废"。（QEMU 线把预防性警告当成了已证实结论）**

## 为什么这个区分重要
```
若是偏移（很可能）⇒ "E10 有 19 条注册来自 libObj 这些位置" **有效** ⇒ dladdr 只是**确认**
若是 VA（不太可能）⇒ 减去 E10 libObj 基址仍可还原成偏移 ⇒ **仍不是完全作废**
⇒ **两种情况下那些 lr 值都还有价值，只差"减去什么基址"**
```

## ★ 反向证据：支持"已是偏移"
```
M2 libObj 实测加载基址 = 0x0b1e0000
⇒ 若 lr 是**原始 LR 寄存器值**，应是 0x0b1e0000 + offset ≈ **0x0b4xxxxx** 量级
⇒ 而实测记录值是 0x243420
⇒ **说明记录值大概率已减过基址（或来自低地址加载的库）**
```

## 建议的准确标记
```
§72 状态：【待验证·很可能有效】
  E10 有 19 条注册，lr ∈ [0x221500, 0x244448]，形态为虚表分发的逐条处理
  待确认：① 这些值是否已是库内偏移（dladdr 一跑就知道）
         ② 调用上下文是 create 还是 destroy
```

## ★★ 把"坐标系"方法论扩成**四问检查清单**（对应今晚四类错误）
```
对比任何"来自运行时的值"之前：
  ① 它是**绝对量**还是**相对量**？（VA vs 偏移、绝对 vs 相对时间）
     ← 对应错误：lr 跨代 VA vs 偏移
  ② 它的**生成方式**在两边语义相同吗？（dynamic_id vs 固定 id）
     ← 对应错误：qid 动态分配 vs 固定
  ③ 它的**顺序**是真实因果/时间顺序，还是副产物顺序？
     （地址顺序 / 哈希桶顺序 / 多线程交错）
     ← 对应错误：lr 地址顺序 vs 时间顺序
  ④ 它记录的是**目标对象本身**，还是**关于它的某个事件**？
     ← 对应错误：reg 表（事件）vs 队列（对象）
⇒ **四项任一答不上来，这个对比就还不能下结论。**
```

## ★ 关于"im.elf 卡在 futex"这条最干净的线索
```
futex_wait_queue_me 只说明"在等一个 pthread 同步原语"，不能区分：
  ① 等锁（mutex）—— 有另一线程持有
  ② 等条件变量（cond）—— 等某个 signal
  ③ 等读写锁
⇒ 需 hook pthread_cond_wait / pthread_mutex_lock 记录具体被哪个等住
```
**⭐ 且要分清形态：**
```
im.elf 其他线程**也在等** ⇒ 互等 ⇒ 死锁形态
只有一个线程在等、其它都正常 ⇒ 等一个不会来的事件
⇒ **两种形态修法完全不同，必须先分清。**
```

## 方法论（第 82 条）★
**"预防性警告"与"已证实的结论"必须分开标记：
我在看到数值**之前**提出的"跨代不可比"是**风险提示**；
看到数值后发现量级像偏移，风险**降低但未消除**。
⇒ **不要把"我提醒过某个风险"当成"该风险已被证实"；
   也不要反过来，因为"量级看起来对"就认为风险消失。
⇒ 正确状态标记：【待验证】= 风险存在但证据倾向某一侧，需一次低成本验证收口。**

---

# ★★★ 2026-10-30 07:40 第七十五层：账本里"lr 是 VA"这**事实陈述**需修正（可能恰恰相反）

## 问题
**QEMU 线在 §72 的撤回说明里写成了确定事实：**
> "**lr 是进程虚拟地址** = 加载基址 + 库内偏移"

**⚠️ 但这很可能**不成立**，而且方向可能相反。**

## 反向证据（见第 74 层）
```
M2 libObj 实测加载基址 = 0x0b1e0000
⇒ 若 lr 是原始 LR 寄存器值，应 ≈ 0x0b1e0000 + offset ≈ **0x0b4xxxxx** 量级
⇒ 而实测记录值 = 0x243420
⇒ **记录值大概率**不是**原始 VA，而是库内偏移（或已减过基址）**
```

## 为什么必须改（账本是长期资产）
```
若写成"lr 是 VA"：
  → 后来者会把 0x243420 当 VA，去减某个基址 ⇒ **减错**
  → 或以为数据不可用 ⇒ **浪费已有的 19 个值**
若写成"坐标系未确认"：
  → 后来者会先跑一次 dladdr ⇒ **一步定性，不会算错**
```

## 建议表述（三行）
```
【坐标系未确认 · 勿据此下结论】
  已知：E10 有 19 条注册，记录值 lr ∈ [0x221500, 0x244448]，形态为虚表分发的逐条处理
  ❓ 未知：记录值 lr 是**库内偏移**还是**进程 VA**
     - 支持"偏移"：M2 libObj base=0x0b1e0000；若为原始 LR 应 ≈0x0b4xxxxx，实测 0x243420 量级不符
     - 支持"VA"：暂无正面证据
  ⇒ 先跑 dladdr 归一化，再判断"M2 缺哪段代码"
```

## 状态清单修正
```
QEMU 线原清单：已排除 4 / 已确认 3 / 已撤回 2 / 待验证 1
⚠️ 修正：把"已撤回 2"里的第 1 条（VA 对比）从"撤回"改为"坐标系未确认"更准确
⇒ 新清单：已排除 4 / 已确认 3 / 坐标系未确认 1 / 已撤回 1（lr 排序法）/ 待验证 1（futex）
```

## 方法论（第 83 条）★★
**"作废/勿引用"是**行动层面**的标记，但它**不能替代对原因的正确陈述**：
本次"作废"的理由被写成"lr 是 VA"，而这个理由本身很可能是错的
（证据显示记录值更像偏移）。
⇒ **标记"勿引用"时，必须同时写清"为什么不能引用"，
   且这个"为什么"本身也要标明是**已证实**还是**推测**。**
**⇒ 否则未来读者会继承一个**可能错误的事实**，并在新工作中基于它做错误推断
   （本例：减错基址，或误判数据不可用）。**

---

# ★★★★★ 2026-10-30 08:20 第七十六层：**探针打印的是 r4 不是 lr** —— 决定性的自检判据：lr 必为奇数

## 真因（QEMU 线自查发现）
```asm
push {r0,r1,r2,r3,r4,lr}
mov  r0, sp
bl   m2_h_reg
```
**压栈布局：**
```
[sp+0]=r0 [sp+4]=r1 [sp+8]=r2 [sp+12]=r3 [sp+16]=r4 [sp+20]=lr
⇒ r[0..3]=r0..r3  **r[4]=r4**  **r[5]=lr**
```
**⇒ 但代码里写的是 `r[4]`，还注释成 "lr" ⇒ 所有日志里 `lr=` 的值实际是 **r4**。**

**⇒ 这解释了此前两个异常：**
```
① lr=0x243420 落在 `movs r1,#25` 而非调用指令上 —— 因为它是 r4（局部指针/常量）
② lr=0x00000082 / 0x85 这类极小值 —— r4 当循环计数器用
```

## ★★★ 我提供的**零成本自检判据**：正确的 lr 必为**奇数**
```
Thumb 的 BL/BLX 会把 LR 低位设为 1（Thumb 状态标志）
⇒ **正确的 lr 值必然是奇数（bit0 = 1）**
```
**⇒ 对照那 19 个值：全部是偶数**
```
0x221500 / 0x227480 / 0x243420 / 0x243998 / 0x244000 / 0x00000082 … 全偶
⇒ **独立印证：这些不是 lr**
```
**⇒ 修复后可直接用此判据验证：重跑 → 检查所有 lr 的 bit0：**
```
全为 1（奇数）⇒ ✅ 字段改对了
仍有偶数      ⇒ ⚠️ 还有别的问题（内联/尾调用/ARM 模式调用者）
⚠️ 例外：若调用者在 ARM 模式，LR 不置位会是偶数；
   但 libObj 全是 Thumb 代码 ⇒ 本场景"全奇"可靠
```

## 作废清单（全部源于这个字段错误）
```
§72「lr ∈ [0x220000,0x250000] 的 19 条注册」→ 那是 r4 值
docs/m2-19-reg-sites.md 那 19 条              → 同上
"按 lr 排序找走到哪里"                        → 同上
我做的"ObjMixer/AVBBC 区域"定位分析            → 同上（r4=0x243420 落在 .text 内是**巧合**）
"VA 跨代不可比"                               → 实为 r4，既不是 VA 也不是偏移
```

## ⚠️ 重要提醒：r4=0x243420 落在 .text 范围是**巧合**
```
数值落在某段的地址区间内 = 必要不充分条件
⇒ **不能凭"数值在 .text 范围内"就判定它是代码地址**
```

## 明天开工顺序（分两步，避免混变量）
```
1) 改 r[4] → r[5]，同时保留 r[4] 作对照打印
2) **先跑一次，检查 lr 是否全奇** ← 免费且决定性，验证字段改对了
3) 全奇 ⇒ 再加 dladdr，拿 (lib, off)
4) 用 off 精确 objdump 定位，判断 create/destroy 上下文
```

## ★ 今晚"数据质量"的总账
**已作废 5 条：**
```
① 缺 0x7a0020（伪 bug）
② qid 精确差集（动态分配）
③ libIMDB 索引映射（多表）
④ "VA 跨代不可比"（实为 r4）
⑤ 19 条 lr 分析（r4 不是 lr）
外加 3 条"待验证"
```
**⇒ 5 作废 / 3 有效 ⇒ **观测层的错误远多于推理层**。
⇒ 今晚大部分时间不是在"分析问题"，而是在"发现观测数据本身有问题"。**

## 方法论（第 84 条）★★★
**自检判据要选"**违反概率极低的结构性特征**"，而不是"看起来合理"的语义判断：**
```
× 弱自检："lr 应该是个合理地址"    ⇒ r4 也可能是合理地址 ⇒ 验不出来
✅ 强自检："lr 的 bit0 必须为 1"    ⇒ r4 几乎不可能全奇 ⇒ 一验就中
```
**可选的结构性特征：奇偶性 / 对齐 / 范围 / 单调性 / 位宽 / 已知标志位。
⇒ **底层探针（读寄存器、读栈、读结构体）必须先跑这类自检，再分析数据。**
⇒ 10 分钟的自检，能避免整段白做（今晚已有 5 条作废）。**

## 76. 2026-10-29 修复已落地：`lr` 改取 `r[5]`，并加零成本自检判据

### 代码已改（`tools/guest/m2probe.c`）

```
M2REG  pid=%d qid=%08x lr=%08x r4=%08x
                       ^^^^^^^ 取自 r[5]（真 lr）
                                  ^^^^^^^ 保留对照
```

### ★ 强自检判据（协作方提出，零成本）

**Thumb 的 `BL`/`BLX` 会把 LR 最低位置 1** ⇒ **正确的 lr 必然是奇数**。

```
此前记录的 19 个值：0x221500 / 0x227480 / 0x243420 / 0x243998 / 0x244000 …
⇒ **全为偶数** ⇒ 独立印证"那不是 lr"（是 r4）
```

⇒ 明天重跑后**第一件事**：`awk` 检查所有 `lr=` 的 bit0 是否全为 1。
* 全奇 ⇒ 字段改对了，可以继续 dladdr
* 仍有偶数 ⇒ 还有别的问题（内联 / 尾调用 / ARM 模式调用者）

### 强判据 vs 弱判据（方法论第 6 条）

```
× 弱自检："lr 应该是个合理地址" —— r4 也可能是合理地址，验不出来
✅ 强自检："lr 的 bit0 必须为 1" —— 违反概率极低，一验就中
```

**⇒ 选自检判据时，优先选"违反概率极低的结构性特征"**
（奇偶性、对齐、范围、单调性），而不是"看起来合理"的语义判断。

### 今晚的数据质量账

```
作废 5 条：缺 0x7a0020 / qid 精确差集 / libIMDB 索引 / "lr 是 VA" / 19 条 lr 分析
有效 3 条：4 条排除 + 3 条确认（见 §73）
待验证 3 条
⇒ 观测层错误 ≫ 推理层错误 ⇒ 今天大部分时间在"发现仪器本身有问题"
⇒ 结论：先验证仪器，再分析数据
```

### 明天开工顺序（分两次跑，别混变量）

```
1) 重跑 E10 → 只验证"lr 是否全奇"（10 分钟，免费）
2) 确认后再加 dladdr → 拿 (lib, off) → objdump 定位 → 判断 create/destroy 上下文
```

---

# ★★★★★ 2026-10-30 08:50 第七十七层：五个问题的分层 —— 第 ⑤ 条应排最前

## QEMU 线的 A/B 拆分（比"待验证"更精确）
```
问题 A（实现 bug，确定）：字段取错 —— r[4] 是 r4，r[5] 才是 lr
  ⇒ §72 的问题不是"待验证"，而是"**依据的字段是错的**"⇒ 必须先重测，
    谈不上验证或证伪

问题 B（可比性，待验证）：即便取到真 lr，跨代仍不可直接比（VA vs 偏移）
  ⇒ 批评独立成立，但**现在还没到适用阶段**
```

## ⚠️ 我承认：我的"反向证据"失效了
**我说：**
> "M2 libObj base=0x0b1e0000 ⇒ 原始 LR 应 ≈0x0b4xxxxx，实测 0x243420 ⇒ 大概率已是偏移"

**⇒ 每一步推理都成立，但**前提被 bug 破坏**（那列不是 lr 而是 r4）：**
```
r4 可以是循环计数（0x82/0x85）、跨调用保存的表指针（0x243420 这类）
⇒ **"看起来像偏移"完全不能证明它是偏移**
```
**⚠️ 这暴露我自己的一个错误：**
**我用"数值量级符合预期"这种**弱证据**去支撑结论 ——
而第 84 条方法论（选"违反概率极低的结构特征"）我刚提出，却没先用上。**

## ★★★ 第 ⑤ 条（QEMU 线提出，我认为应排最前）
> **⑤ 这个字段的"名字"和它的"实际内容"一致吗？**

**⇒ 顺序上它应该排第一：**
```
⑤ 字段名 == 实际内容吗？   ← 先确认"我没有读错东西"
① 绝对量还是相对量？       ← 再确认"它是什么坐标系"
② 生成方式语义相同吗？
③ 顺序是真实因果顺序吗？
④ 记录的是对象还是事件？
⇒ **⑤ 是所有对比的前提；它不成立时，①~④ 全部无意义。**
```

## ★★★ ⑤ 的可操作化：**每个读出的字段都必须有一个能证伪的自检**
```
对任何"从二进制/内存读出的字段"：
  这个字段的值，有没有一个"结构性特征"能证明它确实是它声称的东西？
    例：lr 必为奇数（Thumb 位）/ 指针必对齐 / 计数必单调 / 时间戳必递增
⇒ **找不到这样的特征 ⇒ 说明你还没有能力验证它读对了。**
```
**⇒ 这与第 84 条是同一件事的两面：
   "选自检判据要选强特征" + "先确认字段名与内容一致"
   ⇒ 合并为：**每个读取的字段都必须有一个能证伪的自检。****

## 明天顺序（确认）
```
① 修字段：lr = r[5]，同时打印 r4 作对照
② 重跑 E10，**先检查 lr 是否全奇**（⑤ 的自检）
③ 确认 lr 列真变了、且全奇 ⇒ 再用 dladdr 归一化
④ 最后才谈"M2 缺哪段"
⚠️ 明天第一件事不是套 dladdr，而是先修字段 —— 否则 dladdr 建在 r4 上，第二次踩同一个坑
```

## futex 那条（保持"待验证"+ 具体化方案）
```
hook pthread_cond_wait / pthread_mutex_lock（PLT 可拦），记录：
  哪些线程在等、等的是哪个对象地址
⇒ 多线程等同一对象 ⇒ 死锁形态
⇒ 只有一线程等、其它正常结束 ⇒ 等一个不会来的事件
⚠️ pthread_* 是热路径，只统计不打印，避免影响时序
```

## 方法论（第 85 条）★★★（今晚最重要）
**五问检查清单（用于任何跨版本/跨环境的运行时数据对比）：**
```
⑤ 字段名 == 实际内容吗？        ← **前提，最先确认**
① 绝对量还是相对量？
② 生成方式语义是否相同？
③ 顺序是真实因果顺序吗？
④ 记录的是对象还是关于它的事件？
⇒ ⑤ 不成立时，①~④ 全部无意义。
```
**⇒ 可操作化：**每个读出的字段都必须有一个能证伪的自检**（强结构特征：
   奇偶 / 对齐 / 范围 / 单调 / 位宽 / 已知标志位）。
⇒ **找不到这样的特征，说明你还没有能力验证它读对了 —— 那就先别用这个数据。**

## 77. 2026-10-29 方法论文档（今晚沉淀，共 6 条 + 一条前置规则）

### ★ 前置规则（第 ⑤ 条，必须最先问）

**⑤ 这个字段的"名字"和它的"实际内容"一致吗？**

前四条都在问"这个数值能不能比"，**第 ⑤ 条先问"这个数值到底是什么"**。
⇒ 顺序上它排第一；它不成立时，①~④ 全部无意义。

**可操作化：**
> 对任何"从二进制/内存读出来的字段"，先问：**这个值有没有一个结构性特征，
> 能证明它确实是它声称的东西？**
> 例：`lr` 必为奇数（Thumb 位）、指针必对齐、计数必单调、时间戳必递增。
> **找不到这样的特征 ⇒ 说明还没有能力验证它读对了。**

### 四问（对比任何运行时值之前）

1. **绝对量还是相对量？**（VA vs 偏移、绝对时间 vs 相对时间）
2. **生成方式语义在两边的相同吗？**（`osal_dynamic_id` vs 固定 id）
3. **顺序是真实因果/时间顺序，还是副产物顺序？**（地址顺序 / 哈希桶 / 多线程交错）
4. **记录的是对象本身，还是关于它的事件？**（reg 表 = "被注册了"，≠ "队列存在"）

### 两条元规则

5. **先验证仪器，再分析数据**（今晚作废 5 条 / 有效 3 条，观测层错误 ≫ 推理层错误）
6. **选自检判据时选"违反概率极低的结构特征"**（奇偶/对齐/范围/单调性），
   而不是"看起来合理"的语义判断

### 今晚这些规则对应的具体失误

| 规则 | 当晚的失误 |
|---|---|
| ⑤ | 把 `r4` 当 `lr` 打印并连续两轮据此推理 |
| ① | 用原始 VA 跨代比较（且连"是不是 VA"都未确认） |
| ② | 用精确 qid 差集比较（低 16 位动态分配） |
| ③ | 按 lr 地址排序推断执行顺序；按日志族序列前缀（受线程交错影响，仅 17 条） |
| ④ | 把 reg 表（回调注册事件）当成队列/服务清单 ⇒ 追了 5 轮伪 bug |

### ★ ⑦ 形态是"验证器"，不是"识别器"（今晚最终形态的方法论）

> **形态推理（数值量级/奇偶/对齐/范围）只能在"字段已确认"之后用来判断"它有没有异常"，
> 不能用来回答"这是什么"。**

| 用法 | 是否成立 |
|---|---|
| 已知是 `lr` ⇒ 发现某个 `lr` 为偶数 ⇒ 判为异常（Thumb 位未置） | ✅ 验证器 |
| 字段未确认 ⇒ 数值量级像偏移 ⇒ 推断"所以它是偏移" | ❌ 误当识别器 |

**今晚两次踩同一个坑：**
1. 用 `r4` 当 `lr`，推出"注册点在 ObjMixer/AVBBC 区域"；
2. 被纠正后，又用 `r4` 的量级推出"所以是偏移"。

**⇒ 与 ⑤ 合起来：先确认字段（⑤），确认后形态只能做验证（⑦），不能做识别。**

---

# ★★★★★ 2026-10-30 09:20 第七十八层：**形态是"验证器"，不是"识别器"**（今晚方法论的最终形态）

## 我第二次犯同一个错（已被 QEMU 线指出并撤回）
**我说：**
> "M2 libObj base=0x0b1e0000，若 lr 是原始 LR 应 ≈0x0b4xxxxx，而实测 0x243420 ⇒ 大概率是偏移"

**⚠️ 前提是"0x243420 是 lr"，但**它是 r4**。**
```
r4 的值是 0x82/0x85/0x243420 ⇒ 只能说明"r4 是小整数或表指针"
⇒ **对 lr 的坐标系零信息量**
⇒ 写成"支持偏移的证据" ⇒ 给后来人一个**假证据**
```
**✅ 所以"**当前无证据**"是唯一正确的写法（既不能说"是 VA"，也不能说"倾向偏移"）。**

## ★★★ 形态推理的适用边界（本层核心）
```
✅ 可用：**已知字段正确**时，用形态判断"这个值是否异常"
   例：确认是 lr 后，发现某个 lr 是偶数 ⇒ 异常（Thumb 位没置）
       —— 这正是"lr 必为奇数"那个自检，它**只在字段已确认后才有效**

❌ 不能用：**字段未确认**时，用形态猜测"字段可能是什么"
   例：数值量级像偏移 ⇒ 猜"所以它是偏移"
```

**⇒ 一句话：**
> **形态是"验证器"，不是"识别器"。**
> **它只能在"我已经知道这是什么"之后用来检查"它有没有异常"，
>   不能用来回答"这是什么"。**

## 今晚我两次都栽在这个反向用法上
```
第一次：用 r4 当 lr，推出"注册点在 ObjMixer/AVBBC 区域"     ← 形态当证据
第二次：被纠正后，又用 r4 的量级推出"所以是偏移"             ← 同一错误第二次
```

## 与第 85 条的关系
**第 85 条五问清单里 ⑤ 的**收紧版**（QEMU 线提出，更好）：**
> **⑤ 在解释任何数值之前，先确认"这个数值确实来自你认为的那个字段/寄存器/表项"
> —— 形态推理不能反过来证明字段正确。**

**⇒ 本层补充的是"为什么不能反推"以及"什么时候可以正推"：
   ⇒ 形态推理是**单向**的：字段 → 形态（可推）；形态 → 字段（不可推）。**

## 方法论（第 86 条）★★★（今晚最终形态）
**形态推理是**单向验证**，不是**双向识别**：**
```
✅ 字段已知正确 → 用形态检验值是否异常      （验证器）
❌ 字段未知     → 用形态猜测字段是什么      （识别器，无效）
```
**⇒ 形态的可选特征：奇偶 / 对齐 / 范围 / 单调 / 位宽 / 已知标志位。
⇒ **在字段未确认之前，这些特征一个都不能用。**
⇒ 判据："如果这个值真的是 X，它应该有什么结构特征？" ——
   这能检验 X，**不能证明**它是 X。**

---

# 🏁 2026-10-30 09:40 第八十层：本次会话收尾（明日起点）

## 双方已完全对齐，无未决分歧

## 今晚交付物
```
账本: /root/fw/oss/docs/QEMU_M2_PROGRESS.md   （100 节 / 86 条方法论）
数据: docs/e10-registered-qids.txt (297) / docs/m2-registered-qids-full.txt (122)
代码: tools/guest/m2probe.c（已修 lr=r[5]，保留 r4 对照）
      docs/m2-19-reg-sites.md（⚠️ 作废，基于 r4）
```

## 明天 QEMU 线起点（严格按序）
```
① **先修字段并验证**：重跑 E10，检查 lr 是否**全奇**（Thumb 位）
   grep 'M2REG' log | sed 's/.*lr=//;s/ .*//' | awk '{print strtonum("0x"$1)%2}' | sort | uniq -c
   全 1 ⇒ 字段改对；仍有 0 ⇒ 内联/尾调用/ARM 调用者，需再查
② 确认后再加 dladdr → 拿 (lib, off)
③ objdump 定位 → 判断 create / destroy 上下文
④ 最后才谈"M2 缺哪段代码"
⚠️ ①②不可合并 —— 否则 dladdr 建在错误字段上，第二次踩同一个坑
⚠️ futex 方案若做，**先在 E10 验证"加 pthread 拦截后仍能出画"，再上 M2**
```

## 明天真机线起点
```
printf 'LD_DEBUG=libs\nLD_DEBUG_OUTPUT=/setting/ldd\n' > /setting/env.txt
（先试写验证 /setting 可写且 rw）
→ 重启 → ls /setting/ldd.* → 看 appFw.so 是否被 dlopen
```

## 今晚的核心教训（三条方法论，按重要性）
```
第 85 条 五问检查清单，⑤"字段名==实际内容吗"排最前（前提规则）
第 84 条 自检要选"违反概率极低的结构特征"（奇偶/对齐/范围/单调）
第 86 条 形态是验证器不是识别器（单向推理，不可反推）
⇒ 合并成一句：**在相信任何观测数据之前，先证明你读对了它。**
```

## 数据质量账
```
作废 5 / 有效 3 / 待验证 3
⇒ 观测层错误 ≫ 推理层错误
⇒ 今晚大部分时间不是在分析问题，是在发现"观测数据本身有问题"
⇒ 这 5 次作废换来的是一套"以后不再踩"的机制，不是损失
```

## 78. 2026-10-29 ★ 探针修复验证通过 + dladdr 成功：M2 注册来源按库统计

### ① 仪器自检通过（关键）

修 `lr` 取值（`r[4]`→`r[5]`）后重跑 M2：

```
注册事件 124 条
lr 奇偶： odd=124  even=0     ← 全为奇数（Thumb 位），字段取对了 ✅
真实 lr 量级：0x9825 / 0xa31f（小，来自 im.elf/bootin）
             0x40d5bb7 / 0x415b1d5（中，某个库）
             0xcd34e81（大，另一个库）
⇒ lr 是**真实 VA**（跨模块变化），此前的"是偏移/是 VA"之争至此自动解答：
  记录值是 VA；要跨代比较必须先 dladdr 归一化。
```

### ② dladdr 解析成功：M2 的注册来源（按库）

```
53  /usr/lib/libObj.so
14  /usr/lib/libMWF.so
10  /usr/lib/libInfraBluetoothCore.so
 6  /usr/lib/libInfraNetConnectionManagerEther.so
 4  /usr/lib/libWlanResource.so
 4  /usr/lib/libUsbControl.so
 4  /usr/lib/libRsrcHotshoe.so
 3  /usr/lib/libInfraAirplaneMode.so
 3  /usr/bin/im.elf
 2  /usr/lib/libsencore.so
 …（完整表见 docs/m2-reg-libs.txt）
```

**⇒ 这是第一份"跨代可比的真实数据"**：`(库名, 库内偏移)` 的坐标系两代一致，
不再受 VA/动态 qid 影响。

### 下一步（下一轮）

用**同一份修好的探针**跑 E10，得到 E10 的 `(库名, 数量)` 分布，
与上面逐条对比 ⇒ 找出"M2 里**完全没有注册**的库"（那些库的 init 从未执行）。
那份差集就是真正可执行的断点清单。

## 79. 2026-10-29 ★★★ 跨代可比数据出炉：M2 有 10 个库的注册**完全没发生**

同一份修复后探针（`lr=r[5]` + dladdr）分别跑 E10 与 M2，按库统计注册事件：

```
lib                                  E10   M2   判读
/usr/lib/libMWF.so                   122   14
/usr/lib/libObj.so                   111   53
/usr/lib/libbackup.so                 44    0   ★ M2 完全没跑
/usr/lib/libNetService.so              6    0   ★
/usr/lib/libupdaterufp.so              6    0   ★
/usr/lib/libmprctrl.so                 4    0   ★
/usr/lib/libdig_level.so               3    0   ★
/usr/lib/libInfraHttpClient.so         3    0   ★
/usr/lib/libmpr.so                     3    0   ★
/usr/lib/libgps_rsrc.so                2    0   ★
/usr/lib/libInfraIPsec.so              1    0   ★
/usr/lib/libInfraNetDeviceInfo.so      1    0   ★
（其余两代都有：BluetoothCore 10/10、Ether 6/6、WlanResource 4/4、RsrcHotshoe 4/4 …）
M2 独有：libUsbControl 4 / libsencore 2 / libInfraNetRfTest 2 / libBusPower 2 / libUsbBusPower 2
```

### 判读

1. **`libbackup.so` 在 M2 里 0 次注册，而文件存在且大小与 E10 完全相同（9,384 B）**
   ⇒ 不是缺文件，是**它的初始化从未被调用**；
2. `backup.ko` 在两边都是 `status=0` ⇒ **不是内核模块问题**，是用户态 `libbackup.so` 的 init 没跑；
3. `libMWF.so` 122 → 14、`libObj.so` 111 → 53 ⇒ M2 的初始化**远早于**这些库就停住了；
4. 缺失的 10 个库都是"下游服务"（NetService / updaterufp / mpr* / dig_level /
   HttpClient / gps_rsrc / IPsec / NetDeviceInfo）⇒ 与"卡在早期初始化"一致。

### 下一轮（收敛到"最先没跑的那个"）

要给注册事件**加时间戳**（同一进程内日志顺序≈时间顺序，但多线程会交错）：
按时间取每条库的**首次注册**，排序后第一个"E10 有、M2 没有"的库
⇒ 那就是 M2 停下的位置。

⚠️ 不要再用偏移/地址排序（§77 方法论 ③）。

## 80. 2026-10-29 ★★★ "卡死 vs 慢"判定：M2 冻结在 134 条注册，帧恒为全黑

持续采样 14 分钟（每分钟记一次注册数与帧统计）：

```
t=1min reg=134 | distinct=1 (全黑)
t=2min reg=134 | distinct=1
t=3min reg=134 | distinct=1
t=4min reg=134 | distinct=1
t=5min reg=134 | distinct=1
```

**⇒ 注册数从第 1 分钟起就冻结在 134（E10 同期为 352），帧 distinct 恒为 1（均匀黑屏）。**
**⇒ M2 是"卡死"，不是"慢"。**

### 与 E10 事件序列的边界对齐

按"每条库首次出现的事件序号"比较（同一坐标系：事件序号）：

```
E10#0 bootin → #1 osal_uipc → #2 im.elf → #5 osal_ulogio → #6 KikiLogGenerator
→ #8 MWF → #9 AccessLog → #10 libObj → #14 Jiritsu   （前 14 条**两代完全同序同号**）
→ E10#15 libuiobserve（我方探针，忽略）
→ E10#17 ResourceBluetooth（M2 #21）→ E10#21 RsrcHotshoe（M2 #38）→ E10#24 InfraHdmi（M2 #51）
⇒ 前 14 条完全一致，之后 M2 的序号**逐渐变大**（同一条事件被排到更后面）
```

**⚠️ 重要修正**：`libbackup.so` 在 E10 的首次注册是 **#220**，
而 M2 只到 134 ⇒ **"backup 没跑"只是"M2 没走到"，不是独立故障。**
（§79 里把 backup 44→0 当作候选根因，此处降级为"症状"。）

### 结论

**M2 的前 14 个初始化步骤与 E10 完全一致，然后在 134 条注册处冻结；
它没有停在某个"特定库"，而是停在某个**同步点**上**（与 im.elf 主线程 futex 等待吻合）。

### 下一轮

hook `pthread_cond_wait` / `pthread_mutex_lock`（PLT 可拦），只在 im.elf 里统计：
记录"哪个线程等哪个 cond 地址、多少线程等同一个地址"
⇒ 多个线程等同一对象 = 死锁形态；单线程等 = 等一个不会来的 signal。
（先在 E10 验证加了拦截仍能出画，再上 M2。）

## 81. 2026-10-29 ★★★ 冻结位置精确到函数：`MWF::EventWrapper::RegisterCallBack`

### 对齐结果（同一个坐标系：事件序号）

```
M2 最后一条： REGSY qid=0097e00c lib=libMWF.so off=0x1f200
E10 #134 条： REGSY qid=0097e000 lib=libMWF.so off=0x1f200   ← 同一条（同 lib、同 off、同 qid 族）
```
⇒ **M2 与 E10 执行到同一个调用点后，M2 就停住了。**

### 该 off 对应的函数（反汇编 libMWF.so）

```
0001f1d8  <MWF::EventWrapper::RegisterCallBack(unsigned,
                                                 void(*)(void*,int,void*,unsigned), void*)>
  1f1fc: blx  osal_reg_sync_msg_queue_cb@plt    ← 注册 sync 队列
  1f200: mov  r1, r0                            ← ★ 我们的 lr=0x1f200 就是这条
  1f202: mov  r0, r4
  1f204: blx  ...ERR_CNV(OSAL_ERR)              ← 把返回值转成错误码
```

⇒ **M2 的"最后一条事件"= `RegisterCallBack` 里注册调用返回后的那一条指令。**

### ⇒ 卡点在"RegisterCallBack 返回之后"

E10 在 #134 之后继续（#135~ 都是 libObj 的注册），说明**该函数在 E10 正常返回、调用者继续**；
而 M2 在它返回后**不再有任何注册事件** ⇒ 阻塞发生在**调用者紧接着的下一步**。

`libMWF.so` 两代几乎同源（247,864 B vs 247,776 B，md5 不同但 .text 起点相同 0x1e1a4）
⇒ 不是库版本差异导致。

### 下一轮（两种最省的做法）

1. **在该函数返回处（0x1f200）之外，再记录"调用者"**：
   探针改为记录 `__builtin_return_address(1)`（即 RegisterCallBack 的调用者），
   就能知道"返回后回到谁" ⇒ 下一个该查的函数；
2. **直接看 M2 的 snd_sync 计数**：如果某条 qid 的 sync 发送一直不返回，
   就是"等一个不会来的应答"（probe 已有 `osal_snd_sync_msg` 计数，只需补 enter/ret 时间戳对比）。

## 82. 2026-10-29 失败记录：给 snd_sync 加 enter/ret 计数导致探针失效（已回滚）

尝试：把 `osal_snd_sync_msg` 桩从"尾调用 native"改为"C helper 调用 native 并计数返回"，
以便输出 `M2STUCK qid=.. enter=.. ret=..` 表（判"哪条 sync 永远不返回"）。

结果：**探针完全无输出**（`M2REG/REGSY/M2LIB` 全为 0，`M2INIT` 仅 1 条，serial 24 KB）。

⇒ 说明改动破坏了 im.elf 里的早期执行（很可能 snd_sync 的 ABI 或调用时机与桩不兼容，
  早期一次 snd_sync 走桩即挂）。

**处置：已回滚到已知可用版本**（`osal_snd_sync_msg` 恢复"尾调用 native"、不带 ret 计数），
源码与 .so 已同步到 `tools/guest/`。

**教训（与 §77 一致）**：每次只改一个变量，且改完必须先看"事件数是否仍在 124 左右"，
再谈新数据 —— 本次直接看新输出，浪费了一轮。

## 83. 2026-10-29 冻结前最后一次动作（按日志行序）与 17 条 sync 目标队列

探针新增：`osal_snd_sync_msg` 首次遇到每个 qid 时打印一行 `M2SND qid=.. lib=.. off=..`。

本次运行（events=115，snd=17）：

```
M2SND qid=012182ed libBusPower         off 0x5504
M2SND qid=00c881bb libUsbControl       off 0xa9e0
M2SND qid=012282f6 libUsbControl       off 0xa9e0
M2SND qid=00cf016d libMWF              off 0x1f0a8
M2SND qid=011502f7 libInfraNetRfTest   off 0x1202
M2SND qid=01010227 / 016c8477 / 007d0149  libObj  off 0x20d11c
```

**日志行序比较（行序 ≈ 单流时间序）：**
```
最后一条 M2SND：行 481  (qid=007d0149, libObj off 0x20d11c)
最后一条 M2LIB：行 547  (REG qid=011b02aa, libObj off 0x1fcea6)
⇒ 冻结前**最后动作是一次注册**（libObj off 0x1fcea6），不是 snd_sync
```

### 观察

* 冻结前的最后调用点在不同 run 间略有变化
  （上次是 `libMWF 0x1f200` = MWF::EventWrapper::RegisterCallBack，
   本次是 `libObj 0x1fcea6` 的一次 REG）⇒ **说明冻结不是"某个固定函数卡住"，
  更像"某个共享同步点/单例在多个线程上先后撞上"**；
* 但两者都发生在 **早期初始化（MWF/Obj 的 EventWrapper 注册链）** 内，与 §80 的
  "134 条事件后冻结"一致。

### 下一轮

用 `__builtin_return_address(1)`（或 `lr` 的 caller）记录"注册调用的调用者"，
把最后几次注册的**上一层调用栈**打出来 ⇒ 就能知道是谁在调用这些注册、
以及它返回后应该继续做什么（从而定位"应该发生而没发生"的下一步）。

## 84. 2026-10-29 失败记录：按真实 init 加载全部 32 个模块 → 反而退步

想法：真实 `/sbin/init` 加载 32 个模块，我们只加载 15+5 个；缺的模块可能提供
"MWF/Obj 的 sync 队列应答方"，所以直接照真实清单全加载。

做法：把 `/init` 的模块段替换为真实 init 的 32 个模块（含 `iris_dial/msen_drv/nfc/sata_drv` 等）。

结果：

```
insmod: can't insert '/usr/kmod/iris_dial.ko': No such file or directory
insmod: can't insert '/usr/kmod/msen_drv.ko': No such file or directory
insmod: can't insert '/usr/kmod/nfc.ko': No such file or directory
insmod: can't insert '/usr/kmod/sata_drv.ko': No such file or directory
注册事件 = 1（原来 115~134）
帧 distinct = 1（全黑）
```

⇒ **不但没推进，注册事件从 ~124 掉到 1** ⇒ 某个模块加载失败/顺序变化打断了早期初始化。
（v9 树里 `iris_dial/msen_drv/nfc/sata_drv` 这 4 个 .ko 不存在，其余应能加载。）

### 处置

已把 `/init` 恢复为"已知可用"版本（m2qemu 原始 init + 我加的 5 个输入链模块 +
`LD_PRELOAD=/usr/lib/libm2probe.so`），并重编 Image 复原。
已知可用 init 存档：`tools/guest/m2-init-known-good.sh`。

### 教训

**"照真实清单补模块"不能一次全上**：必须一次只加 1~2 个，
且每个都校验"注册事件数仍在 ~120 以上"（否则就是引入了新变量）。
这与 §82 的教训完全相同 —— 本轮又犯了一次。

## 85. 2026-10-29 回滚确认：镜像已恢复正常基线

```
events = 122   （已知可用区间 115~134）✅
snd    = 17    （与 §83 一致）
帧     = distinct=1（仍全黑，符合当前认知：M2 未出画）
```

⇒ 目录树/镜像已回到"已知可用 + 探针有效"的状态，可继续做单变量实验。

## ★ 硬规则（写入文档顶部，后续每轮先执行）

> **任何 `/init` 或模块列表改动后，必须先跑一次并断言 `注册事件 ≥ 120`；
> 不满足则立刻回滚，不得在该状态上继续分析或下结论。**

依据：§82（改 snd_sync 计数 → 事件 0）、§84（全量换模块 → 事件 1）两次都是
"没先验基线就继续"，各浪费一轮。

## 当前准确的进度定位（交接用）

| 维度 | 结论 | 证据 |
|---|---|---|
| 是否渲染 | ❌ 从未渲染 | 帧 `distinct=1` 恒定（§80 5 分钟采样） |
| 是否卡死 | ✅ 卡死（非慢） | 注册数冻结 134、帧不变 |
| 卡死位置 | 早期初始化（MWF/Obj 的 EventWrapper 注册链） | 最后动作 libObj off 0x1fcea6 / MWF off 0x1f200（§81/§83） |
| 形态 | 非"固定函数阻塞"，像共享同步点 | 最后调用点随 run 变化（§83） |
| 输入队列 | 与故障无关 | 两代都不注册 0x7a0020；reg 表是回调清单（§74） |
| 内核模块 | 不是缺模块（全量加载反而更差） | §84 |
| 已排除 | 缺硬件/缺模块/伪 bug/慢 | §73/§79/§80/§84 |

**下一步（按优先级）**
1. 记录 `__builtin_return_address(1)`（注册调用的调用者），定位"最后几次注册"的上一层；
2. 单变量试模块：先把 `dma330_stub.ko` 换回真实 `dma330.ko`，验事件数后再继续；
3. 若仍卡：考虑 M2 的 `im.elf` 与 v9 树里 `im.elf` 版本是否匹配（E10 版 im.elf + M2 libs 的混搭风险）。

## 86. 2026-10-29 "启动器版本混搭"假设排查：所有树里都是同一份 bootin/im.elf（无法验证 M2 变体）

```
v9 树 (zv_m2probe/root/bin)
  bootin.elf md5 d8bfb4f7fc  15,336 B
  im.elf     md5 f1c746e3f5  23,240 B

全盘搜索同名的其它树：
  /mnt/build/fw/emu/root/usr/bin/bootin.elf      d8bfb4f7fc  相同
  /mnt/build/fw/port_m2/out/usr/bin/bootin.elf   d8bfb4f7fc  相同
  /mnt/build/fw/port_m2/out/usr/bin/im.elf       f1c746e3f5  相同
  /mnt/build/m2qemu/usr.m2_v2/bin/im.elf         f1c746e3f5  相同
  /mnt/build/m2qemu/work/initramfs-ui/usr/bin/*  相同
```

**⇒ 所有可得树（含标称 M2 的 port_m2 / usr.m2_v2）用的都是同一份启动器**，
与协作方曾提到的"M2 版 md5 3545261c / d2f47a82"对不上 —— 那份 M2 变体在本机**不存在**。

### 结论

* **无法做"换 M2 启动器"这个单变量实验**（没有材料）；
* 但也说明：这些启动器很可能**跨机型通用**（bootin 只是按 IMDB 表加载 appFwBoot/appFw），
  "混搭启动器"更可能不是元凶 —— 该假设**降级为低优先级**，不再投入。

## 87. 2026-10-29 接力指南（下次从这里继续，不必重读全部历史）

### 环境（已就绪，无需重建）

```
基准树     : /mnt/build/zv_m2probe/root        （v9 nflasha15_v9.ext2 解包，858 文件 / 230MB）
initramfs  : /mnt/build/zv_m2probe/initramfs-ui （init 已含探针 preload + 5 个输入模块）
已知可用init: tools/guest/m2-init-known-good.sh （事件数 122 的版本）
探针源码   : tools/guest/m2probe.c             （lr=r[5] + dladdr + M2LIB/M2SND；已编译验证）
镜像       : /mnt/build/zv_m2probe/kbuild/arch/arm/boot/Image
构建命令   : make -C /mnt/build/ref-chocotan-zve10/work/linux-kernel \
               O=/mnt/build/zv_m2probe/kbuild ARCH=arm \
               CROSS_COMPILE=/mnt/build/ref-chocotan-zve10/work/armv7-eabihf--glibc--stable-2020.08-1/bin/arm-linux- Image
跑一次     : qemu-system-arm -M zve10 -cpu cortex-a9 -smp 4 -m 1024 -kernel <Image> \
               -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=0x8000000@0x20000000:uc rdinit=/init loglevel=8' \
               -display none -serial file:/tmp/m2p/x.log -serial unix:/tmp/m2p/x.sock,server=on,wait=off -monitor none -no-reboot
抓帧       : python3 /mnt/build/cap.py <tag>   （distinct>3 才算画出来）
```

### 三条硬规则（违反过的代价都记在 §82/§84）

1. **改 init/模块/探针后，必须先跑一次并断言"注册事件 ≥ 120"**，否则立刻回滚；
2. 一次只改一个变量；
3. 跨版本比较前先确认坐标系（VA vs 偏移、动态 id vs 固定、事件 vs 对象）。

### 已知结论（可直接引用，无需复验）

* M2 **卡死**（非慢）：注册数冻结 ~122-134，帧 `distinct=1` 恒定（§80）；
* 冻结位置在**早期初始化**（MWF/Obj 的 EventWrapper 注册链），且随 run 微变 ⇒ 像共享同步点（§81/§83）；
* `0x7a0020` 缺失是**伪 bug**（reg 表只是回调注册清单；两代都不注册它）（§74）；
* 缺内核模块**不是**原因（全量加载反而更差）（§84）；
* 启动器混搭**无法验证且低优先级**（所有树同一份 bootin/im.elf）（§86）。

### 下一步（按顺序）

1. 探针加 `__builtin_return_address(1)`，打印"注册调用的调用者"（库+偏移），
   确认最后几次注册的上一层调用链 → 指认"返回后本应继续的下一步"；
2. 单变量模块实验：`dma330_stub.ko` → `dma330.ko`（每次验事件数）；
3. 若仍无进展：把 `im.elf` 的 4 个线程逐个用探针标注状态（等锁/等 cond/等 osal），
   区分"死锁"与"等一个不会来的 signal"。

## 88. 2026-10-29 单变量模块实验：真实 `dma330.ko` 加载失败，保留 `dma330_stub.ko`

按 §87 计划做第一个单变量实验（只换这一个模块）：

```
load /usr/kmod/dma330.ko          （替代 dma330_stub.ko）
→ MODULE_END /usr/kmod/dma330.ko status=14     ← 加载失败
→ events=117（基线 122，属正常波动）
→ 帧 distinct=1（仍全黑）
```

**⇒ QEMU 里真实 `dma330.ko` 加载失败（status=14），
之前用 `dma330_stub.ko` 是正确的取舍，不是遗漏。**

已把 init 回滚为 `dma330_stub.ko` 并重编镜像恢复基线。

**这条假设（"缺真实 DMA330 模块导致初始化卡住"）排除。**

## 89. 2026-10-29 尝试 hook `pthread_cond_wait`：拿到 condvar 地址，但事件数掉到 40（按硬规则回滚）

改动：预加载 `pthread_cond_wait` 透明桩（尾调用 native），只打印前 6 次的 cond 地址 + 调用者库/偏移。

观测（**注意这是被扰动的 run**）：

```
M2CW n=1..6  cond=0a15bff0  lib=/usr/lib/libObj.so  off=0x181926
events = 40     ← 基线 122 ⇒ 违规
帧 distinct = 1（仍全黑）
```

### 判读

1. **6 次等待全部落在同一个 condvar `0x0a15bff0`、同一个调用点 `libObj+0x181926`**
   ⇒ 与"共享同步点"的形态一致（§83）；
2. **但没法直接采用**：`events` 从 122 掉到 40，说明拦截 `pthread_cond_wait` 扰动了初始化时序
   （它是热路径 + 影响线程调度）。

### 处置（遵守 §85 硬规则）

**已回滚**：移除 `pthread_cond_wait` 桩并重编镜像，恢复基线（源码同步 `tools/guest/`）。

### 下一步（换一种不扰动的方式）

不要拦截 `pthread_cond_wait`；改为**只在"冻结之后"读取一次状态**：
* 用一条 `#M2` 命令触发"读某个已知 condvar 的等待者数量"不可行（无此 API），
* 可行替代：**在探针里对 `libObj+0x181926` 做一次性静态分析**（反汇编该函数，看它等的是哪个全局对象），
  再找谁负责 signal 它 —— 全程静态，零扰动。

## 90. 2026-10-29 ★ 静态定位等待点：一个"标志位 + cond_signal"的生产者/消费者等待环

对 `libObj.so` 的 `0x181926`（M2 冻结时 `pthread_cond_wait` 的返回地址）反汇编：

```asm
181910: mov   r0, r5
181912: blx   pthread_mutex_lock
181916: ldr.w r3, [r4, #152]      ; ★ 检查对象 r4 的 0x98 偏移处的标志
18191a: cmp   r3, #0
18191c: bne   1818be              ; 标志已置 ⇒ 回到循环体
18191e: mov   r1, r5              ; mutex
181920: mov   r0, fp              ; condvar（运行时=0x0a15bff0）
181922: blx   pthread_cond_wait
181926: b     1818be              ; ★ 我们的采样点：等待返回后继续循环
```

同区域内还能看到**生产者一侧**的代码：

```asm
1818e4: blx pthread_mutex_lock
1818ea: blx pthread_cond_signal    ; ★ 唤醒
1818f0: blx pthread_mutex_unlock
```

⇒ **这是标准"等标志位 → cond_wait → 被 signal 唤醒 → 复查标志"的模式。**
M2 冻结在这里 ⇒ **标志位 `[对象+0x98]` 从未被置位，或 signal 从未发生** —— 即"生产者那一步没跑"。

### 结论（本轮定位成果）

* 等待点：`libObj` 内某对象的 `+0x98` 标志 + condvar（运行时 `0x0a15bff0`）；
* 该函数在 `InfraMediaConfig_Resume` 附近的巨型 .text 区（符号为最近前置，仅作参考）；
* **不是死锁**（同一函数里有 signal 路径），而是**等一个不会到来的信号** ——
  与"某个初始化步骤没执行"一致（§80/§81 的结论）。

### 下一步

1. 从 `+0x98` 这个对象偏移反推它属于哪个类（在 libObj 里搜对该偏移的 store 指令，
   找"谁负责置位"）；
2. 拿到置位点后，反查它的调用者 ⇒ 就是"没跑的那一步"。

## 91. 2026-10-29 追"谁置位 flag[obj+0x98]"：候选 35 处，尚未唯一化

扫描 libObj `.text` 中**对象相对**（非 `r7/sp` 栈帧）写入 `[rX,#152]` 的指令：

```
候选写入点 = 35 处（若不过滤栈帧则 76 处）
例：0x3d9b0 strb r3,[r4,#152]   （置 0/1，unimodel 区）
    0x9d1e0 / 0x9d39c str r1,[r0,#152]（ColorImageBlockBuffer 区，先读后比较）
    0xa8e48 str r3,[r5,#152]     （TsIndicControl 区）
```

**⇒ 偏移 0x98 被多个类共用，单靠"偏移"无法唯一确定是哪个对象**，
需要更强的关联证据（例如：等待点所在函数里 `r4` 的来源、或 condvar 与对象的绑定关系）。

### 本轮结论

* 等待模式已确证：**flag[obj+0x98] + cond_wait 循环，signal 路径存在**（§90）；
* 但"哪个对象的 0x98"尚未确定 ⇒ 静态追查需要更多上下文，成本上升。

### 下一步（两条，按性价比）

1. **动态但零扰动**：在探针里只读一次（不拦截 cond_wait）——
   在冻结后由 `#M2` 命令触发，读取等待线程的 `r4`/`fp` 寄存器值
   （用 `ptrace` 从探针内的 fork 子进程短暂 attach，采样后立即 detach；
   这不会像拦截那样改变时序，且只做一次）；
2. **接受现状收口**：M2 在 QEMU 内的阻塞点已定位到"等一个不会来的标志"，
   把结论交给真机验证（真机上该标志可能由硬件/服务置位）。

## 92. 2026-10-29 ★★★ 找到并打通相机日期/时钟服务：`osal_snd_sync_direct(0x90013d, …)` 返回 0

### 线索来源：a7c2-re 的启动前置条件

`report/2026-10-17-qemu-native-camera/runtime/analysis/seed-date-clock.c` + `qemu-menu-probe.py`：

```
if insmod /usr/kmod/udate.ko && /simulation/seed-date-clock; then touch /tmp/date-clock.ready; fi
...
prefix = 'test -f /tmp/date-clock.ready && ' + prefix     # 后续启动以此为前提
```

协议（a7c2 实测）：
```
struct { unsigned short w[12]; }        // 24 字节
w[8]  = operation   (2=读当前, 3=读初始/默认, 0=写)
w[9]  = 0x7fff      (handler 必须回填 status)
osal_snd_sync_direct(0x90013d, &m)
```

### 我方实测（M2 v9 树，init 在 bootin 之前跑 seeder）

```
SEED qid=90013d ret=0     st=0000      ← ★ 服务存在且应答成功
SEED qid=90013e ret=-1020 st=7fff
SEED qid=900141 ret=-1020 st=7fff
SEED qid=9001b9 ret=-1020 st=7fff
SEED qid=900140 ret=-1020 st=7fff
SEED qid=900142 ret=-1020 st=7fff
```

**⇒ `0x90013d` 就是本机型的日期/时钟服务队列，且当前是"活动"的（ret=0）；**
其余候选 qid 返回 -1020（无该服务）。

### 尚未完成

本轮只做了 **op=2（读）**，没有写（a7c2 的完整流程是：读当前 → 无效则读默认(op3) → 写(op0) → 回读校验）。
所以**还没验证"喂时钟能否让 M2 越过冻结"**。

### 下一轮

1. 按 a7c2 `seed_date_clock()` 的完整流程实现：读 op2 → 判有效期 → 读 op3 取默认 → 写 op0 → 回读校验；
2. 校验通过后 `touch /tmp/date-clock.ready`（或直接继续），再启动 bootin；
3. 观察注册事件是否突破 121~134、帧是否 `distinct>3`。

## 93. 2026-10-29 日期服务协议探测：只有 op=2（读）被接受，写操作被拒

在 `0x90013d` 上扫描 op 0..7（24 字节消息），随后用 48 字节消息读/写/回读：

```
op=0 ret=0 st=fc00     op=1 st=fc00     op=2 st=0000  ← 唯一成功
op=3..7 全部 st=fc00
```

48 字节消息实测（字节序按 b0..b47）：

```
DC read2  : 30 ........(b16=02,b17=00, b18=b19=00 成功) ..... b46=03,b47=00
DC write0 : 30 ........(b16=00, b18=fc00 被拒)  （我们填的 e4070101 被原样回显，说明未解析）
DC reread : 与 read2 相同（时钟状态未改变）
```

**⇒ 本机型的日期服务：读（op=2）可用，写操作不是 op=0（被 -1024 拒绝），
协议与 a7c2 不同 —— 需要另找写入的 op 或另一条队列。**

### 对"能否靠喂时钟解锁 M2"的判断

* 服务在、读得通，但**写入未成功** ⇒ 时钟仍是未初始化状态；
* 同时 **注册事件与帧完全没变化**（events=115/121，`distinct=1`）⇒
  **本条尚未证明日期与 M2 卡死有关**，仍是候选而非结论。

### 下一轮（收敛）

1. 继续扫 op 0x08..0x20 与 0x100 以上（有些固件把操作放在 b16 的 word 里，值域更大）；
2. 或换查：`0x90013d` 之外是否有"set/write"专用 qid（扫 0x900100..0x9001ff 的 ret==0）；
3. 若确认写不了 ⇒ 判定 a7c2 的 date-seeding 路线在 ZV-E10 固件上不适用，回到 cond_wait 所在对象。

## 94. 2026-10-29 日期服务定位收敛：`0x90013d` 是唯一应答者；写入 op 仍未找到

### 扫描 0x900100..0x9001ff（256 个 qid，op=2 读）

```
DCQ qid=90013d read_ok b20=00 b32=00 b46=00     ← 唯一应答
（其余 255 个均不应答 ⇒ 该区间只有这一个服务）
```

⇒ **`0x90013d` 确认是本机型的日期/时钟服务队列，与 a7c2 相同。**

### 写入仍未通

* op=0 被拒（status = 0xfc00/-1024）；op=1、3..7 全被拒；
* 只有 op=2（读）成功，返回体在 24/48 两种消息尺寸下尾部字节不一致（0300 vs 0000）
  ⇒ **响应体的字段布局还没摸对**。

### 新线索：谁在用这个 qid

在 v9 的 218 个 .so 中搜常量 `0x0090013d`：

```
10 处  libObj.so        ← 客户端就在 libObj
 2 处  libsencore.so
 2 处  libCertificateManager.so
 1 处  libmprctrl.so / libmpr.so / libmintx.so
```

⇒ 下一步可从 **libObj 里这 10 处引用**反推调用点（看它如何组包、op 用什么值），
比继续盲扫 op 值高效得多。

### 现状

M2 仍黑屏（events≈116，`distinct=1`）；date-seeding 与卡死的因果关系**尚未证实**。

## 95. 2026-10-29 日期协议溯源：客户端不在 libutil_date.so，仍指向 libObj

### 排查

```
libutil_date.so (5,824 B) 里 0x0090013d 出现 0 次
  ⇒ 它只提供纯计算函数（date_time_add_seconds / get_days_of_month /
    diff_datetime / add_subseconds），不碰 UIPC 队列
libObj.so 里 0x0090013d 出现 10 次，位置在 .rodata/literal pool
  （VA 0xd1f30 / 0x1b89a8 / 0x1d2a98 …），但**用常规 `ldr rt,[pc,#imm]` 扫描找不到加载点**
  ⇒ 说明常量是通过 `movw/movt` 或表结构访问的，静态定位成本较高
```

### 结论（本轮）

* 日期服务的**客户端在 libObj 内部**，但它的调用点无法用简单扫描定位；
* 已验证的事实不变：`0x90013d` 是唯一应答的日期队列，**只有 op=2（读）被接受**，
  op=0/1/3..7 全部返回 `-1024`；
* **写入协议未解** ⇒ a7c2 的 `seed-date-clock` 流程**无法逐字照搬到本机型**。

### 建议的下一步（按性价比排序）

1. **对比 a7c2 与本次的响应体**：把 a7c2 固件里 `0x90013d` 的读响应 dump 出来
   （他们仓库有 `DATE_CLOCK_CALL` 日志），逐字节和我们 `DC read2` 的响应比对，
   差异处就是"字段布局不同"的具体位置；
2. 若时间成本过高：**承认 date-seeding 不是本次卡点**，回到 §90 的
   `flag[obj+0x98] + cond_wait`，用零扰动方式（一次性 ptrace 采样 r4/fp）确认对象；
3. 真机 v9b 结果优先（若真机能起，则 QEMU 侧就是缺硬件/服务，不必再挖）。

## 96. 2026-10-29 关键判定：M2 的 App **从未**调用日期服务（0x90013d 命中 0）

检查 6 份 M2 运行日志中的 `0x90013d`（探针会记录每个 sync 发送 qid 的首现）：

```
mF.log / mH.log / mL.log / mO.log / mQ.log / mR.log   命中 = 0
```

**⇒ 从启动到冻结，**没有任何** `osal_snd_sync_msg` 发往 `0x90013d`。**

⚠️ 但存在一个已知盲区：**探针没有 hook `osal_snd_sync_direct`**
（`grep -c snd_sync_direct m2probe.c` = 0），而 a7c2 的 seed 程序用的正是 `direct` 变体
⇒ **App 若走 `direct`，我们看不到**。

### 判读

1. **date-seeding 与本卡点的因果关系目前没有任何证据**：App 既没发（sync_msg 路径），
   我们也没成功写（write 被拒）；
2. 更可能的情况：**日期服务与"卡在 cond_wait"无关**，a7c2 的 date 步骤是
   为他们的 `MENU_OPEN_SET` 路线准备的前置，不是通用必需。

### 下一步（收敛，二选一）

A. **补 hook `osal_snd_sync_direct`**（一次改动、只加日志、不改变调用），
   跑一次即可确认 App 是否走 direct 且是否发往 0x90013d —— 若仍为 0，彻底关闭 date 线；
B. 直接关闭 date 线，回到 §90 的 `flag[obj+0x98]` 对象确认（一次性 ptrace 采样）。

**建议先做 A（最便宜、能给出"是/否"的确定答案）。**

## 97. 2026-10-29 ★★★ 补齐 `snd_sync_direct` hook：App **确实**在启动路径上调用日期服务 `0x90013d`

补上 `osal_snd_sync_direct` 的透明桩（只记录每个 qid 首现，不改调用），重跑 M2：

```
events = 121   （基线正常，未扰动 ✅）
sd（direct 发送的目标 qid）= 6 个：
  0x920277
  0x910042
  0x910041
  0x90013f
  0x90013d      ← ★ 就是日期/时钟服务
  0x910040
```

### 意义（推翻上一轮的"无关"判断）

* 上一轮"6 份日志命中 0"是因为**只 hook 了 `snd_sync_msg`**，而 App 走的是 **`direct`**；
* **⇒ 日期服务确实在 App 的启动路径上被调用**，a7c2 把"喂时钟"作为启动前置是有道理的；
* 我们自己的 seeder 调 `0x90013d` 能拿到 `ret=0`（op=2 成功），
  但 **App 可能用别的 op（写/设状态）而这个 op 被服务拒绝（-1024）**
  ⇒ App 可能因此进入**重试循环**，表现为"冻结"。

### 下一轮（关键实验）

1. **在桩里记录 `0x90013d` 的调用次数与 op 值**（读 message 的 b16/b20，
   只统计不改变行为），跑一次；
2. 若看到 **同一 op 反复调用且返回 -1024** ⇒ 就是它导致的重试循环；
3. 再查该 op 为何被拒：很可能是 `udate.ko` 缺少 `/setting`/backup 数据
   （我们有 `Backup_load=1`，但备份分区在 QEMU 里是空的）。

## 98. 2026-10-29 App 对日期服务的调用：**只有 1 次，op=2（读）** —— 排除"重试循环"假设

在 direct 桩里对 `0x90013d` 记录调用次数与消息头部（只读不改行为）：

```
events = 116（基线）
M2DOPS n=1  b16=0002  b18=0000  b20=00 b21=00
```

解读：`b16` 按小端读得 `0x0002` ⇒ **op = 2（读当前时钟）**，与 a7c2 的"读"操作一致。

**⇒ App 只读一次时钟就继续往下走了，没有重试、没有卡在日期服务上。**

### 结论

* §97 的假设"App 用某个被拒的 op 反复重试 ⇒ 看似冻结"**不成立**；
* 日期服务在启动路径上只是一个"读一次"的步骤，**它很可能不是卡点**；
* 但"读到的时钟状态是未初始化（state=3）"这一点仍未排除——App 可能在后续某处
  依据该状态进入等待（对应 §90 的 cond_wait 环），**这需要下一步验证**。

### 下一步

1. **把时钟真正写成功**再观察：目标不是"解协议"，而是"让 App 读到有效时间"。
   当前已知：op=0/1/3..7 全被 -1024 拒绝、op=2 成功。
   ⇒ 试 **24 字节消息 + size 字段 48**（a7c2 的原始形态）+ op=0 的写入；
   若仍被拒，尝试先用 `Backup_load=1` 提供备份数据（udate 可能需要）。
2. 若写入始终失败 ⇒ 判定 date 线**不可达**，回到 §90 的 `flag[obj+0x98]` 对象确认。

## 99. 2026-10-29 日期服务写入判定：本机型**只支持 op=2（读）**，写入不可达

按 a7c2 的 24 字节原始形态（size 字段=48）重试，并尝试多种 op 编码：

```
S_read2  r=0 : ...0002 0000 ...          ← op=2 成功
S_ww000  r=0 : ...0000 fc00 e4070101      ← op=0  被拒(-1024)
S_ww000  r=0 : ...0000 fc00 e4070101      ← op=0x100 实际低字节仍是 0 → 同样被拒
S_ww200  r=0 : ...0002 00fc ...           ← op=0x200 低字节=2（但高字节被当状态位）→ 仍拒
S_ww300/400/00c ：全部 fc00
```

**⇒ 只有 `b16 == 0x0002` 被接受；其余（含 a7c2 用的 op=0）一律 `-1024`。
⇒ 本机型的 udate 服务是只读的（或写入走另一条队列，未找到）。**

### 综合结论（date 线）

| 事实 | 证据 |
|---|---|
| 日期服务存在且应答 | 0x90013d ret=0（§92/§94） |
| App 在启动路径上**读一次** | 只有 1 次调用、op=2（§98） |
| 写操作不可达 | op=0/1/3..7 全 -1024（§93/§99） |
| 日期与卡死的因果 | **无证据** |

**⇒ a7c2 的 `seed-date-clock` 前置步骤在本机型不可复现**（服务只读），
且 App 仅读一次即继续 ⇒ **date 线关闭，不再投入。**

### 回到主线

卡点仍是 §90 的 `flag[obj+0x98] + cond_wait`。下一步（已获用户同意）：
**一次性 ptrace 采样**等待线程的 `r4`/`fp`，确认该对象是谁，再找 signal 方。

## 100. 2026-10-29 ★ 零扰动线程采样：第一个发现在"父进程已消失"上

实现：探针在 im.elf 里 `fork()` 一个子进程（不建线程），子进程 sleep 50s 后
枚举 `/proc/<getppid()>/task/*`，打印每个线程的 `state` 与 `wchan`。

实测（events=116，基线正常）：

```
M2T tid=1 st=S wchan=do_wait        ← 只列出 1 个线程
```

**⚠️ 关键推理**：`getppid()` 返回的是 **1**（init），而不是 im.elf 的 pid（本次 M2INIT 显示
`pid=139 cmd=/usr/bin/im.elf`）⇒ 说明**子进程被 init 收养，即 im.elf 在 50 秒前就已退出**。

**⇒ 与"im.elf 挂起在 futex"的旧结论冲突：M2 的 im.elf 可能是**退出/崩溃**，而非阻塞。**

### 但必须先排除采样器自身的偏差

本次实现有两个缺陷，可能导致假象：
1. `getppid()` 是在 sleep **之后**才读的 —— 若 im.elf 在此期间退出，结果必然是 1；
   应**在 fork 当时**记下 ppid；
2. 只打印了 1 个线程，无法判断是"父进程已死"还是"父进程只有 1 个线程"。

### 下一轮（1 行改动 + 跑一次）

把 ppid 改为 fork 时捕获，并额外打印：
* 父进程是否仍存在（`/proc/<pid>` 可打开）；
* 若存在，枚举其全部 task 的 state/wchan；
* 若不存在，打印 `M2T parent_gone`（这本身就是"im.elf 已退出"的强证据）。

⚠️ 若确认为"im.elf 退出"，则整个排查方向要改：从"谁该 signal"变成
**"im.elf 为什么退出/崩溃"**（可再查 exit code、core、以及退出前最后一条注册）。

## 101. 2026-10-29 ★★★★ 两个致命崩溃全部定位并修复：事件数 116 → 181，im.elf 不再退出

### 崩溃 1：探针库未定义符号（我们自己造成的）

```
/usr/bin/im.elf: symbol lookup error: /usr/lib/libm2probe.so: undefined symbol: read_singleton
```

早期一次失败的撤销把 `read_singleton` 的**定义**删了、**调用**留下 ⇒ 动态链接在首次解析时失败
⇒ **im.elf 直接退出**。后续所有"冻结在 116~134 条注册"的观测**很大程度是这个 bug 造成的**。

已修复（删除残留调用；同时补齐 `libobj_base`/`gi_fire`）。

### 崩溃 2：libmprctrl 与 libmpr 的 ABI 不匹配（栈本身的问题）

```
/usr/bin/im.elf: symbol lookup error: /usr/lib/libmprctrl.so:
undefined symbol: _ZN5MprIf34SetCreateParam_SupportContentTypesm
```

精确比对：

```
libmprctrl.so 需要   …SupportContentTypes**m** (unsigned long)
E10 libmpr.so 提供   …SupportContentTypes**m**   ✅
M2  libmpr.so 提供   …SupportContentTypes**j**   ✗
且 libmprctrl.so 在两棵树里 md5 完全相同（1350def23c）
⇒ v9 树里是「E10 的 libmprctrl + M2 的 libmpr」混搭
```

处置：把 **E10 的 libmpr.so**（13,021,036 B）放进 v9 树 ⇒ 崩溃消失。

### 结果

```
events        : 116~143  →  **181**
symbol errors : 1        →  **0**
帧            : distinct=1（仍黑，未亮屏）
im.elf 线程（fork 子进程零扰动采样，t=50s）：
  tid=139 osal_rcv_msg_tmo      ← 主线程在等消息
  tid=141/142/143 __k_4u_wait_cb
  tid=144 poll_schedule_timeout
  tid=145 osal_rcv_msg_tmo
```

**⇒ im.elf 不再退出，而是**正常运行并在等消息**——这才是"应用起来了但还没出画"的形态。**

### 下一轮

1. 连跑 5~10 分钟确认 im.elf 稳定、事件是否继续增长；
2. 对比 E10 的线程等待集合，看 M2 少等了/多等了哪条队列；
3. 检查渲染路径是否被调用（不拦截渲染函数，只统计"是否有人 blit 到帧缓冲"）。

## 102. 2026-10-29 修复后稳定在 181 条注册（比旧值 116~134 大幅前推），帧仍黑

6 分钟采样（每 60s）：

```
t=1m ev=181  distinct=1
t=2m ev=181  distinct=1
… t=6m ev=181 distinct=1
```

⇒ **稳定在 181，不再增长；帧恒黑。** 但相比修复前的 116~134，已经**多跑了约 50 条注册**。

### 库分布对比（修复后）

```
74 libObj     18 libMWF     10 libInfraBluetoothCore
 6 libNetService   6 libInfraNetConnectionManagerEther
 4 libupdaterufp   4 libmprctrl   4 libWlanResource …
```

**`libNetService` / `libupdaterufp` / `libmprctrl` 从 0 变成有注册** —— 这三个之前完全没跑，
印证 §101 的 mpr 修复确实解开了整段初始化。

### 停住的精确位置

M2 最后三条事件：

```
M2LIB REG   qid=012f0335  libdig_level.so off=0x1d90
M2LIB REGSY qid=00a00349  libdig_level.so off=0x1dc6
M2LIB REGSY qid=00920169  /usr/bin/im.elf  off=0x2426     ← 最后一条
```

E10 在**同一位置**（其第 186 条事件）是：

```
M2LIB REGSY qid=00a00349  libdig_level.so off=0x1dbe   ← 同一条注册（同 qid！）
M2LIB REGSY qid=00800228  libObj.so off=0x6fd49a       ← E10 接着往下走
M2LIB REG     qid=00904142  libObj.so off=0x419766
```

**⇒ M2 在"注册 dig_level 之后、libObj 的下一批注册之前"停住。**

### 下一轮

1. 反汇编 `libObj.so` 在 `off≈0x6fd49a / 0x419766` 附近的代码（E10 的下一条动作），
   看它在 M2 里为什么没执行（缺依赖/等应答/条件不满足）；
2. 同时用 fork 采样对比 E10 与 M2 的线程等待集合（M2 现在线程都在 `osal_rcv_msg_tmo`、
   `__k_4u_wait_cb`、`poll_schedule_timeout`）。

## 103. 2026-10-29 系统性 ABI 扫描：运行时已无致命符号错误，剩余停滞是**行为性**的

对 `root/lib` 下全部 `.so` 做两轮符号解析扫描：

```
第一轮（含 libc/libstdc++ 符号）: 143 个库"有未解析符号" —— 大部分是 libc/libstdc++，噪声
第二轮（只保留 App 级 C++ 符号，且排除 @VERSION 与 libstdc++/operator new/delete）:
  6 个库存在真正未解析的 App 符号
    libObj.so          35 例（多为 std::string 等，疑似 libstdc++ 未纳入比对）
    libmprctrl.so      16 例（如 MprIf::DeleteMMgr(unsigned long)）
    libInfraRemote.so   6 例
    libmpr.so           2 例
    appFw.so            2 例
    AdjChkConfig.so     1 例
```

⚠️ 注意：**这是 `root/lib`（v9 原始混搭树）的结果**；运行时实际用的是
`initramfs-ui/usr/lib`（已把 `libmpr.so` 换为 E10 版）。

### 关键判据：运行时已经**没有** `symbol lookup error`

修复后连续两次运行：

```
grep -c 'symbol lookup error'  = 0
事件数 = 181（稳定）
```

**⇒ 至少"致命的"ABI 不匹配已消除；M2 停在 181 条注册是**行为性**停滞
（应用正常运行、线程在 osal_rcv_msg_tmo / __k_4u_wait_cb / poll_schedule_timeout 上等待），
而不是加载/符号失败。**

### 下一轮

1. 补齐扫描脚本的 libstdc++/libc 符号表（把版本号剥掉再比对），确认 libObj 那 35 例是否真缺；
2. 若确认无缺 ⇒ 专注行为：对比 E10 在同一注册点之后的**调用差异**
   （E10 接着做 `libObj off=0x6fd49a` 的注册，M2 没做）；
3. 用 fork 采样同时抓 E10/M2 的线程等待集合，找出"M2 少等了哪条队列"。

## 104. 2026-10-29 E10 对照失败：当前探针与 E10 组合不兼容（事件 0）

用**同一份探针**（含 fork 线程采样 + snd_sync_direct 桩）跑 E10（`libm2probe:libuiobserve`）：

```
events = 0     menu = 0
```

**⇒ 探针当前的改动（fork 采样 / sd 桩）在 E10 上直接把早期初始化打断**，
无法用它对 E10 做线程等待集合对照。

（对比：修复前的简单版探针在 E10 上能拿到 352 条注册。）

### 处置

* **不做**"用当前探针跑 E10"的对照（成本高、且需要先修探针兼容性）；
* E10 侧已有的历史数据（352 条注册、各库分布、事件序）仍可用于**静态/顺序**对照；
* M2 侧继续用当前探针（事件 181 正常）。

### 当前 M2 状态（本轮不变）

```
事件 = 181（6 分钟稳定）
帧   = distinct=1（黑）
线程 = osal_rcv_msg_tmo ×2、__k_4u_wait_cb ×3、poll_schedule_timeout ×1
```

### 下一轮（收敛到最小可验证动作）

沿 E10 的历史事件序，找 **M2 的第 181 条之后、E10 的对应动作**（`libObj off≈0x6fd49a`），
反汇编该函数看它需要什么前置条件；同时在 M2 侧确认该函数是否被执行过
（用探针记录 `libObj` 该偏移附近的注册是否出现 —— 未出现即未被调用）。

## 105. 2026-10-29 用"qid 族"跨版本对比（语义稳定，不依赖偏移）：真正的差异只有一族

对比 M2（修复后，181 条 / 60 族）与 E10（352 条 / 63 族）的 **qid 高 16 位族**：

```
E10 有、M2 无的族，及其在 E10 事件序中的首次位置：
  0x00ae  E10 #171   ← ★ 早于 M2 的 181，属"M2 漏做"
  0x0087  E10 #188   ┐
  0x003e  E10 #220   │ 都晚于 181 ⇒ 只是"M2 还没走到"
  0x0030  E10 #274   │
  0x0031  E10 #288   ┘
```

**⇒ 唯一真正异常的是 `0xae` 族：E10 在第 171 条就注册了，M2 已到 181 却完全没有。**

### 该族来自哪里

```
E10: REGSY qid=00aee02e / 00aee02f  lib=/usr/lib/libMWF.so  off=0x1f200
⇒ 就是 §81 定位过的 MWF::EventWrapper::RegisterCallBack
```

MWF 的注册总数对比：

```
E10: 122 条来自 libMWF.so
M2 :  18 条来自 libMWF.so        ← 差距巨大
```

**⇒ M2 的 MWF（回调注册框架）只被用了 18 次，E10 用了 122 次 ⇒
大量组件在 M2 里没有走到"注册回调"这一步。**

### 结论

* 之前用"库/偏移"做的对比都受版本差异污染（§103/§104），
  **qid 族是唯一跨版本稳定的坐标**；
* 差异收敛到：**MWF::EventWrapper::RegisterCallBack 被调用的次数**
  （M2 18 vs E10 122），首个缺失族 `0xae` 在 E10 的 #171 就已经出现。

### 下一轮

1. 在 E10 历史日志里统计 `libMWF.so off=0x1f200` 的**注册序列**（qid 递增规律），
   找出 M2 的 18 条对应哪些、缺的 104 条分别来自哪些调用者（用 `r4` 不同对象区分）；
2. 反汇编 `MWF::EventWrapper::RegisterCallBack` 的调用者，定位"该调而没调"的组件。

## 106. 2026-10-29 MWF EventWrapper 注册序列对比：M2 走到 18 条就停，E10 继续到 122

抽取两代 `libMWF.so off=0x1f200`（= `MWF::EventWrapper::RegisterCallBack`）的注册序列：

```
E10 : 122 条
M2  :  18 条

M2 的 18 条（全部）:
  0x97e01f 0xcf016d 0xc5e01e 0xc58437 0xb0e01d 0xb0e01c 0xb08170
  0xafe01b 0xafe01a 0xaf816f 0x115e017 0x11502f7 0x1150299
  0x97e03d 0xc7e03c 0x276e03b 0x276870c 0x276e03a
```

**⇒ M2 与 E10 的前几条**同名同序**（0x97e01f / 0xcf016d 都在），随后 M2 停止。**
⇒ 低 16 位是动态分配（两代同一服务拿到不同数字），**精确 qid 不可比，但"18 vs 122"这个计数是可靠的**。

### 结论

**M2 的应用在完成 18 次 MWF 回调注册后不再前进**（线程转入 `osal_rcv_msg_tmo` 等待），
而 E10 会继续注册到 122 次 ⇒ **M2 缺的是"第 19 次及以后"的组件初始化**。

### 现实评估（写给接力者）

* 已经排除：符号崩溃（§101）、ABI 混搭（§101/§103）、日期服务（§99）、内核模块（§84）、
  输入队列（§74）、启动器（§86）；
* 现在停在"应用等消息"这个形态上，**没有任何崩溃或错误日志**；
* 想再往下，需要知道"第 19 个 EventWrapper 属于哪个组件"——这要靠反汇编
  `RegisterCallBack` 的调用者（按 `r4` 对象区分），或等真机数据对照。

## 107. 2026-10-29 再次踩到"坐标系不一致"：gpe/uxengine 的对比是无效的

一度以为发现"E10 有 173 条 gpe、M2 只有 1 条"是重大差异，**但这是无效对比**：

```
E10 的 run 里同时也加载了 libuiobserve.so（我们的可视化探针），它会打印大量 GPE 相关行；
M2 的 run 只加载 libm2probe.so（不打印 GPE）
⇒ 173 vs 1 反映的是"两个 run 挂了不同的探针库"，不是应用行为差异
```

**⇒ 按 §77 方法论的"坐标系"检查：被比较的量必须先确认在同一坐标系下产生。**
同类陷阱今天已出现 4 次（VA vs 偏移、动态 qid、地址序 vs 时间序、仪器差异）。

### 仍然成立的进展（本轮无新增实质推进）

```
M2: 181 条注册、60 个 qid 族、18 条 MWF EventWrapper 注册、帧 distinct=1、无崩溃无错误
E10: 352 条、63 族、122 条 MWF、能出画
```

差异仍收敛为：**M2 的 MWF 回调注册只做了 18 次（E10 122 次）**。

### 若要继续，需要以下之一

1. **等一个"干净的 E10 基线"**：E10 只用 libm2probe（去掉 libuiobserve），但那样 E10 不出画；
   替代方案是给 M2 也加 libuiobserve（但它会 stall，§96 已知）；
2. **反汇编 `MWF::EventWrapper::RegisterCallBack` 的调用者**，按对象区分 18 条 vs 122 条，
   找出第 19 个组件；
3. **真机 v9b 数据**：若真机能起 UI，则 QEMU 侧差别可能来自缺失的硬件服务。

## 108. 2026-10-29 追"第 19 个组件"：定位到 9 个候选库，但当前探针无法区分调用者

静态查出调用 `MWF::EventWrapper::RegisterCallBack` 的库（9 个）：

```
libGOConnectionManager  libInfraFsys     libInfraIPTC     libInfraMediaDetect
libInfraMediaMount     libInfraNetConnectionManager      libNetCommon
libObj                 libmprctrl
```

但用"注册归属库"比较两代时发现：**这些库自己的名字在两代日志里几乎都是 0**
（因为 dladdr 把注册归到了 `libMWF.so` 这个符号所属库，而不是调用者）。

```
libGOConnectionManager M2=0 E10=0     libInfraFsys M2=0 E10=0
libInfraIPTC           M2=0 E10=0     libInfraMediaDetect M2=1 E10=2
libObj                 M2=84 E10=111  libmprctrl M2=7 E10=4
```

**⇒ 需要记录"RegisterCallBack 的调用者"（二级返回地址）才能区分 18 vs 122 的来源。**

### 下一轮（唯一明确动作）

在探针对 `osal_reg_sync_msg_queue_cb` 记录 lr 时，**再往上取一层调用者**：
* 现在记录的是 `MWF::EventWrapper::RegisterCallBack` 内的返回地址（off 0x1f200）；
* 需要的是**它的调用者**（谁调了 RegisterCallBack）。

实现方式：在 MWF 的 `RegisterCallBack` 入口下探针（按 libMWF 偏移 0x1f1d8），
记录此时栈上的返回地址 `*(void**)(sp+…)` —— 这次要**先在一个 E10 能出画的 run 里验证
"加了该探针仍出画"**（§85 硬规则），再用于 M2。

## 109. 2026-10-29 ★★★ 参考 a7c2-re 路线：M2 的渲染层**可以**用同样办法接管（希望回升）

### 关键发现：M2 与 A7C2 是同一套图形抽象

```
a7c2-re 的软件光栅化挂点: GRM_screenGetOnBitmap / GpermFillRect / GpermBitBlit（GPE 层）
M2 的 libObj.so 导出 21 个 GRM_* 符号，包含：
  GRM_screenGetOnBitmap   GRM_gpermFillrect   GRM_gpermRectblit
  GRM_bitmapCreate/Release/WriteLockBits…    GRM_fbrm*（framebuffer region manager）
```

**并且 M2 的 libObj 是经 PLT 调用这些函数的**（可被 LD_PRELOAD 拦截）：

```
3 × <GRM_gpermFillrect@plt>
6 × <GRM_gpermRectblit@plt>
```

### 工作量重估：不需要从零写

```
a7c2-re 的软件渲染部分（可直接移植参考）：
  gpe-raster.c    90 行
  gpe-sprite.c   113 行
  gpe-copy.h      12 行
  headless-osd.c 326 行      ← 多帧捕获/导出
  合计约 541 行（此前我按 E10 的 ui-observe 估成 700+，偏悲观）
```

### 由此修正上一轮的结论

* **"渲染必然做不到" ⇒ 改为"渲染有现成路线"**：只要拦截 `GRM_gpermFillrect` /
  `GRM_gpermRectblit`（+ 可选 `GRM_screenGetOnBitmap`），用软件实现填 RGBA4444，
  就能把画面写到我们的抓帧缓冲（`0x22c48000`，格式一致）。
* **真正的剩余障碍仍是"应用初始化只完成 18/122 次 MWF 注册"**——
  没有绘制调用，拦截也无从生效。
* a7c2-re 还有一条可能绕过初始化的思路：**`MENU_OPEN_SET`（直接强制打开某个菜单画面集）**，
  即不等正常启动流程完成，主动驱动菜单系统 ⇒ 值得在 M2 上找对应入口。

### 下一步（已排好）

1. 本轮已开始：记录 im.elf 所有 `osal_rcv_msg_tmo` 等待队列（qid + 调用库），
   判断那 104 个未启动组件是否依赖硬件服务（grm/stream/media）；
2. 若依赖硬件 ⇒ QEMU 停；若不依赖 ⇒ 修通初始化后立即套用 GRM 拦截方案出画。

## 110. 2026-10-29 强制打开菜单（a7c2 MENU_OPEN_SET）路线调研

### a7c2-re 的机制（已读源码）

```
1. 捕获 UI manager 对象（create_screen 返回后、UI 线程内）
2. 校验 vtable：== appfw_base + 0x5b0c480，且 vtable[3] == appfw_base + 0x1e77dd8
3. 调 vtable 槽 3：
   open_screen(manager, &owner, &screen, &previous, 1, 0)
   （注释写明：manager slot3 是"入队"，不同步渲染）
4. MENU_OPEN_SET=owner,screen[,previous]，id 上限 0x4b7
```

⇒ 本质是**直接调用 UI manager 的 open-screen 虚函数**，绕过正常按键流程。

### M2 的现状（本轮核对）

* M2 用的是 **UXEngine（`ux::`）** 框架（libObj 导出 `ux::core` / `ux::gfxeng` / `ux::resmgr`），
  **与 A7C2 的 `appFw` 结构不同** ⇒ 偏移/vtable 不能照搬；
* libObj 里**没有**导出任何 "open screen / scene / page / transit" 类入口
  （只有 `GraphicsEngine::setScreenRotate` 这类图形设置）；
* 图形原语是 `GRM_gpermFillrect` / `GRM_gpermRectblit`（PLT 可拦截，§109 已确认）。

### 下一轮（M2 版的强制开菜单）

1. **先找出 M2 的 UI 管理对象在哪**：谁创建 `ux::gfxeng::GraphicsEngine`、
   以及哪个二进制/库持有菜单 controller（用字符串 `LayoutMaster_Menu` / `.uxc` 反查加载者）；
2. 找到后 dump 其 vtable，按 a7c2 的模式定位 "open screen" 槽（参数形如
   `(manager, &owner, &screen, &prev, bool, bool)`）；
3. 一旦能强制入队，配合 GRM 拦截（§109）即可产生画面。

## 111. 2026-10-29 ★★★ 用 /proc/pid/maps 定位根因：**UI 应用 `appFw.so` 从未被加载**

给 fork 采样器加 "dump `/proc/<pid>/maps`"，运行后得到 im.elf 实际加载的 94 个库。

```
已加载（UI 相关）: /appFwBoot.so  （有）
未加载:            /appFw.so  ← ★★ 真正的 UI 应用，从未加载
```

### 加载链（反汇编 appFwBoot.so，8,312 B）

```
导出: Application_System_Init (0x1270) / Main (0x14d8)
字符串: AppConfig.so / appFw.so / Application_Start / AdjChkConfig.so
        _ZN13app_adjchk_ui9main_loopEv / "AppFw" / "/usr/share/app/"

代码: 0x12f0 strcmp(...) 后分支
      0x12fe dlopen  ← AppConfig.so 或 AdjChkConfig.so（按 target 选择）
      0x1310 dlopen  ← 另一个配置库
      （appFw.so 的字符串在 .rodata VA=0x171e，Application_Start 在 0x1727）
```

**⇒ 设计意图：appFwBoot.so 应 dlopen `appFw.so`、dlsym `Application_Start` 并调用它。**
**⇒ 实际：`appFw.so` 从未出现在 maps 里 ⇒ 这一步没执行（或 dlopen 失败）。**

### 这是当前唯一的硬阻塞

* 没有 appFw.so ⇒ 没有 UI、没有菜单、没有 controller、没有绘制调用
  ⇒ **强制开菜单（§110）也不可能成功**（manager 对象根本不存在）；
* 与之前所有观测自洽：MWF 注册只有 18 次、帧恒黑、线程在等消息。

### 下一轮（决定性且便宜）

**在探针里拦 `dlopen`**（只记录不改变行为）：打印每次 dlopen 的 `filename`、返回句柄、`dlerror()`。
* 若看到 `dlopen("appFw.so")` 失败 + dlerror ⇒ **直接拿到失败原因**（缺依赖/路径/权限），
  这很可能一击命中；
* 若根本没看到该调用 ⇒ 断点在 appFwBoot 更早的分支（strcmp/target 选择），
  下一步就查 `target=qemu` 与 `target=product` 的分支差异。

## 112. 2026-10-29 ★★★ dlopen 拦截：40 次加载尝试，**appFw.so 一次都没被尝试**

拦 `dlopen`（只记录首次出现的文件名，带重入保护），实测：

```
events = 181（基线正常）
M2DL = 40 个不同的 dlopen 目标，全部是 IMDB 模块表里的库：
  libosal_uipc / libosal_utm / libosal_ulogio / libInfraInterchipDatatrans /
  libNVM / libbackup / libSaveLoadSettings / libbkdmn / libOnDemandLoader /
  … /usr/lib/libObj.so / libInfraError / libJiritsu / libAtn / libGetLog /
  libUniqIDGen / … / libInfraFsys / CameraOnDemandSaver / libInfraOptionInstall / libAiDictionary

grep 'appfw' 命中 = 0     ← ★★★ 从未尝试加载
```

### 结论（比 §111 更硬）

**不是 "dlopen 失败"，而是 "根本没调用"。**
⇒ im.elf 的模块加载循环只做了这 40 个（IMDB 表的一轮），**UI 应用 appFw.so 从未被尝试**。
⇒ 菜单/controller 不存在 ⇒ 强制开菜单（§110）必然失败 ⇒ 帧恒黑。

### 现在的唯一问题

**im.elf 主线程卡在 appFw.so 加载之前的哪一步？**

已知：im.elf 主线程（tid=139）wchan = `osal_rcv_msg_tmo`（在等消息）。
appFwBoot.so（8,312 B，已加载）里 `Application_System_Init` 有两个 dlopen（配置库），
且含字符串 `appFw.so` / `Application_Start` ⇒ **它本该 dlopen appFw.so 并调用其
`Application_Start`**，但没走到。

### 下一轮（两条路，可并行验证）

**A. 主动替它做**（`force-load`）：在探针构造期或延迟线程里直接
`dlopen("/usr/lib/appFw.so")` + `dlsym("Application_Start")` + 调用，
看能否绕过 im.elf 的等待直接拉起 UI（风险：缺少 appFwBoot 的初始化上下文，
可能崩；但 QEMU 里代价低）。

**B. 找出它在等谁**：`osal_rcv_msg_tmo` 是 libosal 内部调用，PLT 拦不到；
改用"读 `wchan` + 该线程的 `/proc/<pid>/task/<tid>/stack`"▶ 若栈能给出
调用链（`osal_rcv_msg_tmo` ← 哪个库），就能定位等待者。

## 113. 2026-10-29 ★★★★ 根因链闭合：`appFw.so` 加载失败因为缺 `sidb::referer::s_count`（在 libmpr 里）

强制 dlopen 实验（在 `snd_sync_direct` 第 15 次回调用一次性触发）：

```
M2FORCE appFw_handle=(nil) err=/usr/lib/appFw.so: undefined symbol: _ZN4sidb7referer7s_countE
```

**符号溯源：**
```
_ZN4sidb7referer7s_countE 定义于 lib/libmpr.so（M2 版）
E10 的 libmpr.so 不定义它
⇒ 我们为修 libmprctrl 的 ABI 把 libmpr 换成 E10 版，结果把 appFw 需要的符号弄丢了！
⇒ 但正常启动路径里 libmpr 从未被加载（40 个 dlopen 目标中没有它）
⇒ 所以 appFw.so 永远加载失败 ⇒ 没有 UI ⇒ 黑屏
```

### 解决方案（已验证）

1. **恢复 M2 的 `libmpr.so`**（3,038,540 B）；
2. **补一个 ABI 垫片**：定义 `_ZN5MprIf34SetCreateParam_SupportContentTypesEm`，
   转发到 M2 的 `…ContentTypesEj` ⇒ 解决 libmprctrl 的符号缺失，实测 `M2SHIM typesM called` ✅；
3. **在加载 appFw 之前先 `dlopen("/usr/lib/libmpr.so", RTLD_GLOBAL)`** ⇒ 让 s_count 进入全局作用域。

结果：
```
M2PRE   libmpr=0x30dc0  err=none
M2FORCE appFw_handle=0x63c78 err=none        ← ★ UI 应用（64 MB）加载成功
M2FORCE Application_Start=0xc54f581          ← 找到 UI 入口
```

（注意：双 LD_PRELOAD 会 stall，实测 events 10；必须用运行时 dlopen 而非 preload。）

### 下一个障碍：`Application_Start` 必须跑在独立线程

直接同步调用 ⇒ 阻塞（它就是 UI 主循环）：

```
M2CALL before   （之后没有 after，且注册事件掉到 10 ⇒ 调用线程被占死）
```

固件自己的做法（反汇编 appFwBoot.so Application_System_Init）：

```
在栈上构造 32 字节参数块：
  +0  = 入口函数相关        +4  = 4
  +8  = 库内取的值          +12 = 参数
  +16 = 0x01cf (u16)        +20 = 1
  +28 = 0
然后: osal_create_waitable_thread(&params)   ← 单指针参数
```

### 下一轮

按上述布局构造参数块，用 `osal_create_waitable_thread` 在**独立线程**里启动
`Application_Start`（而不是同步调用），观察是否出现 UI 绘制与帧变化。
⚠️ 若线程创建失败，退回"用固件自己的 appFwBoot 流程"——但它卡在更早的等待。

## 114. 2026-10-29 线程化启动 attempt：appFw 加载成功但线程创建未执行，boot 退化到 events=10

按固件参数块布局（从 appFwBoot 反汇编解出）在探针里构造：

```
struct { const char *name;  // +0  "AppFw"
         int prio;          // +4  4
         int stack;         // +8  （固件从库数据取值，未知；试 0x20000）
         void *fn;          // +12 Application_Start
         unsigned short flags; // +16 0x1cf
         short pad; int a;  // +20 1
         int b; int c; }    // +28 0
→ osal_create_waitable_thread(&tp)
```

实测：

```
M2PRE   libmpr=0x31518 err=none
M2FORCE appFw_handle=0x66688 err=none
M2FORCE Application_Start=0xc4b9581
（没有 M2THREAD 输出）
events = 10        ← 严重退化（基线 181）
最后事件停在第 ~10 条（早期注册）
```

**⇒ 在 `snd_sync_direct` 第 15 次回调处做 64 MB 的 dlopen 太早，直接把启动掐断；
线程创建代码也未被记录到。**

### 下一轮（明确的三点）

1. **把触发点推后**：不在第 15 次 sync 调用触发，改为"等启动完全稳定后再做"
   —— 用一个 fork 子进程 sleep(120) 然后 `kill(getppid(), SIGUSR1)` 触发父进程信号处理器
   （信号处理器不需要线程，§89 已验证可行）；
2. **分步日志**：M2PRE → appFw 句柄 → Application_Start 指针 → cw 指针 → rc，
   每一步都 out()，以确定卡在哪一步；
3. **确认 `osal_create_waitable_thread` 的解析来源**（libosal_utm / libosal_uipc），
   避免在 dlsym 环节失败。

## 115. 2026-10-29 信号触发方案未生效：M2SIG 零输出，events 退化到 124

按 §114 计划实现：构造期注册 SIGUSR1 处理器（SA_SIGINFO）+
fork 子进程 sleep(150) 后 `kill(parent, SIGUSR1)`。

实测：

```
events = 124（基线 181）        ← 退化
M2SIG 输出 = 0                  ← 处理器从未被调用
最后事件仍是 libMWF off=0x1f200 的注册（与 181 那次类似位置，但总数少）
```

**⇒ 两个可能：**
1. 子进程的 `kill` 没送达（父进程 pid 记录时机/子进程被 init 收养等）；
2. `sigaction` 在这个静态链接风格的 im.elf 里未生效（app 自己可能改过信号处置）。

### 当前配置的净效果

* 通配探针（M2LIB/M2RCV/M2MAPS/SND/SD）+ 强制加载代码**同时存在**时，
  基线从 181 掉到 124 ⇒ **按 §85 硬规则必须回滚强制加载代码**；
* 但 §113 的结论仍然有效且重要：
  **appFw.so 需要 libmpr 全局可见才能加载**（缺 `sidb::referer::s_count`）。

### 交接状态（诚实）

```
✅ 已证实：appFw.so 可加载（dlopen libmpr(RTLD_GLOBAL) → dlopen appFw → dlsym Application_Start）
✅ 已证实：Application_Start 是 UI 主循环（同步调用会阻塞）
❌ 未解决：如何在不干扰主启动流程的前提下，把它在独立线程里拉起来
```

三条候选路径（留给下一轮）：
1. 用 `osal_create_waitable_thread`（参数块布局已在 §113 记录），但触发时机要晚于 181 条注册且
   不能让大 dlopen 落在关键路径上；
2. 用 `ptrace` 从外部注入（完全不影响目标进程内部状态，但需要 guest 里有 ptrace 工具）；
3. 接受 QEMU 侧到此，转真机验证（真机上 appFw 走正常路径，可能根本不需要这些）。

## 116. 2026-10-29 ★★★ 关键澄清：181 → 124 的差异来自 **libmpr 版本**，不是探针代码

把探针回滚到"事件 181"的提交（`0134664`）后重跑：**events 仍是 124**。
⇒ 说明 181 → 124 的变化**不是探针造成的**，而是我们后来把
`initramfs-ui/usr/lib/libmpr.so` 从 **E10 版(13 MB)** 换成了 **M2 版(3 MB)**。

### 两种配置的取舍（重要）

| 配置 | 注册事件 | appFw.so 能否加载 | 后果 |
|---|---|---|---|
| libmpr = **E10 版** (13 MB) | **181** | ❌ 缺 `sidb::referer::s_count`（E10 版不提供） | 走得远，但**永远没有 UI** |
| libmpr = **M2 版** (3 MB) + ABI 垫片 | **124** | ✅ 可以（需先全局 dlopen libmpr） | 走得近，但**UI 应用可加载** |

**⇒ 结论：M2 版 libmpr 才是"正确"的那一个。**
181 那条路之所以走得更远，是因为 E10 的 libmpr 恰好满足了 libmprctrl 的符号需求，
却让 appFw 永远无法加载 —— **是"看起来更健康、实际死路"的配置**。

### 当前状态（clean baseline）

```
libmpr = M2 版(3,038,540 B)
探针   = 已知可用版（无强制加载代码），events = 124，帧 distinct=1
垫片   = 需要时再加（§113 的 M2SHIM，已验证可解决 libmprctrl 的符号缺失）
```

### 下一步

在"M2 版 libmpr + 垫片"这个**正确底座**上，重新审视：
1. 为什么只到 124 条注册（少了哪些库的初始化）；
2. 用哪种低干扰方式拉起 appFw（§115 的三条路径）。

## 117. 2026-10-29 在正确底座（M2 libmpr）上加 ABI 垫片：符号问题解决，但事件数 124→112

单变量实验：在干净探针里追加 `M2SHIM`（`…SupportContentTypes(unsigned long)` → 转发到 `(unsigned int)`）。

```
M2SHIM typesM called = 1        ← 垫片被调用（libmprctrl 的符号缺失已解决）
events = 112（上一轮同配置未加垫片时 124）
帧 distinct = 1
```

**判读：**
* ✅ **垫片确实解决了 `libmprctrl` 的 ABI 缺失**（函数被调用，不再报 symbol lookup error）；
* ⚠️ 事件数 112 略低于 120 阈值 ⇒ 按硬规则标记为"需复核"，但历史方差为 115~181，
  112 可能只是启动时序抖动，**尚不能判定为回归**；
* **结论：垫片是必需且有效的**（否则 libmprctrl 无法工作），保留。

### 当前稳定配置（交接基线）

```
树      : /mnt/build/zv_m2probe/root（v9 混搭树）
initramfs: initramfs-ui，libmpr.so = **M2 版**(3,038,540 B)
探针    : m2probe.c（M2LIB/M2RCV/M2MAPS/M2SND/M2SD + M2SHIM；无强制加载代码）
基线    : events ≈ 112~124，帧 distinct=1
已知可加载 UI：dlopen(libmpr, RTLD_GLOBAL) → dlopen(appFw.so) → dlsym(Application_Start) ✅
```

### 剩余工作（下一轮起）

1. 复核 112 vs 124 是否为噪声（连跑两次取稳定值）；
2. 用低干扰方式拉起 appFw（§115 三条路径）；
3. 若 UI 起来后仍黑屏 ⇒ 套用 §109 的 GRM 拦截（`GRM_gpermFillrect`/`gpermRectblit`，已确认 PLT 可拦）。

## 118. 2026-10-29 基线复核：124 是稳定值，112 属噪声

同一配置（M2 版 libmpr + M2SHIM 垫片）连跑两次：

```
第 1 次: events = 112
第 2 次: events = 124（M2SHIM typesM called = 1）
帧两次均 distinct = 1
```

**⇒ 基线稳定为 `events = 124`，112 是启动时序抖动，不是回归。垫片保留。**

### 当前已确认的完整事实链（供接手者快速理解）

```
1. 应用栈能起来：im.elf 存活、无线程崩溃、无 symbol lookup error
2. 但 UI 应用 appFw.so 从未被加载（40 个 dlopen 目标里没有它）
3. 原因：appFw.so 依赖 sidb::referer::s_count，该符号只在 M2 版 libmpr.so 里
4. 而正常启动路径不加载 libmpr ⇒ appFw 必然加载失败
5. 我们手动 dlopen(libmpr, RTLD_GLOBAL) → dlopen(appFw) 成功、拿到 Application_Start ✅
6. Application_Start 是 UI 主循环（同步调用会阻塞）
7. 拉起它的两次尝试（早期回调触发 / 信号触发）都因干扰主启动流程而失败
```

### 下一步唯一未试的路径

**外部 ptrace 注入**：从另一个进程 attach 到 im.elf，在目标线程里执行
`dlopen(libmpr, RTLD_GLOBAL)` → `dlopen(appFw)` → `dlsym(Application_Start)` → 创建线程调用。
优点：完全不改变目标进程的启动时序；缺点：guest 里需要 ptrace 工具（QEMU 的 zve10 机型支持，但需自带二进制）。

## 119. 2026-10-29 ★★★ 解析 appFwBoot 的两个 dlopen 目标：`appFw.so` vs `AdjChkConfig.so`

用 PIC 公式解析 `Application_System_Init` 里的字面量，得到确切字符串：

```
lit@0x1374 (0x41e) + pc 0x1300  →  0x171e = "appFw.so"          ← 分支 A（0x12fe）
lit@0x137c (0x427) + pc 0x1312  →  0x1739 = "AdjChkConfig.so"   ← 分支 B（0x1310）
lit@0x1380 (0x42f) + pc 0x131a  →  0x1749 = "_ZN13app_adjchk_ui9main_loopEv"
lit@0x1384 (0x442) + pc 0x1326  →  0x1768 = "AppFw"（线程名）
```

控制流（0x12e4 起）：

```
if (r8 == 0)                     → dlopen("appFw.so")      ← 分支 A
else if (strcmp(target, X) == 0)  → dlopen("appFw.so")      ← 分支 A
else                              → dlopen("AdjChkConfig.so") ← 分支 B
然后 dlsym(handle, "_ZN13app_adjchk_ui9main_loopEv")  …（B 分支的入口）
并用 osal_create_waitable_thread 以 "AppFw" 为名启动
```

### 含义

1. **正常（产品）路径应该走分支 A：dlopen `appFw.so`（真正的 UI 应用）**；
2. 若 `target` 字符串不匹配，则走分支 B，加载 `AdjChkConfig.so` 并跑它的 `main_loop`
   —— **那是一个"调整检查 UI"，不是主菜单**；
3. **我们的 40 次 dlopen 记录里 A、B 两个目标都没有** ⇒
   **`Application_System_Init` 这段代码根本没有执行到**（不是在分支选择上出错）。

### ⇒ 下一步（非常明确）

**问题从"appFw 加载失败"变成"`Application_System_Init` 为什么没执行"。**
appFwBoot.so 已加载（在 maps 里），说明动态链接完成；它的初始化函数没跑，
要么是调用者（im.elf）还没调用它，要么它被更早的等待挡住了。
**⇒ 查 im.elf 的启动顺序里，谁负责调用 appFwBoot 的 `Application_System_Init`。**

## 120. 2026-10-29 定位 `Application_System_Init` 的调用方：**libIMDB.so**

全树符号/字符串扫描：

```
定义 Application_System_Init : 仅 lib/appFwBoot.so
引用（任何 lib/bin 的 UND 或字符串）: 无其他库直接引用
含字符串 "Application_System_Init" 和 "appFwBoot" : 仅 lib/libIMDB.so
```

**⇒ 模块加载与启动由 `libIMDB.so`（IMDB 模块注册表）统一驱动：
im.elf 通过 libIMDB 加载 `appFwBoot.so`，并由 libIMDB 按其模块表调用
`Application_System_Init` 入口。**

### 现状链条

```
✅ appFwBoot.so 已加载（在 im.elf 的 maps 里）
❌ 其 Application_System_Init 从未执行（否则会 dlopen appFw.so 或 AdjChkConfig.so，
   而 40 次 dlopen 记录里两者都没有）
❌ 因而 appFw.so（UI 应用）永远不加载 ⇒ 黑屏
```

### 下一轮（唯一任务）

**在 libIMDB.so 里定位"调用模块入口"的代码**：
1. 找引用字符串 `Application_System_Init` 的代码点（PIC 取址）；
2. 看它的前置条件（例如前一个模块是否报告 ready、某个队列是否有应答）；
3. 若条件依赖某模块的应答，而该模块在 QEMU 里根本起不来 ⇒ 判定 QEMU 侧不可复现，
   把结论交给真机（真机上该模块正常，链条会继续）。

## 121. 2026-10-29 libIMDB 的模块描述表：`{模块文件, 入口符号}` 成对出现

`libIMDB.so` 里 `appFwBoot.so` 与 `Application_System_Init` 相邻（0x55dc / 0x55e9），
前后还有 `BIZ_Suspend` / `BIZ_Resume` 等入口符号名，构成一张字符串表：

```
…\0BIZ_Suspend\0BIZ_Resume\0appFwBoot.so\0Application_System_Init\0…
```

**⇒ libIMDB 用「模块名 + 入口符号名」的描述表驱动加载与启动，
用 Thumb `ldr`+`add pc` 扫描找不到引用点 ⇒ 这些字符串是被**指针表**（.data.rel.ro）引用的。**
（libIMDB 的 Flags 非纯 Thumb，代码/表混合，静态定位成本较高。）

### 本轮结论（链条完整形态）

```
im.elf → libIMDB（模块描述表）→ 逐个 dlopen 模块(40 个) → 按表调用入口
        ✔ appFwBoot.so 已加载
        ✘ 但 "Application_System_Init" 从未被调用（否则会 dlopen appFw.so）
        ⇒ appFw.so（64MB UI 应用）永不加载 ⇒ 黑屏
```

**⇒ 最后一个未知：libIMDB 调用入口的前置条件（很可能依赖某个模块 ready/应答）。**
若该条件依赖 QEMU 缺失的硬件服务 ⇒ QEMU 路线在此自然终止；
真机上只要那些服务正常，链条就会走到 `dlopen("appFw.so")`。

---

# ★★★★★★ 2026-10-30 17:00 第八十一层：`appFwBoot` 完整反汇编 —— **根因闭合：dlopen 失败被静默吞掉**

## ① `appFwBoot.so`（8,312 B）完全拆解

### `Application_System_Init`（284 B）
```asm
12e4: cmp  r8, #0                ; r8 = 传入的字符串参数
12e8: beq  12f6                  ; arg==NULL → 分支A
12ea: strcmp(arg, "AppConfig.so")
12f4: cbnz r0, 1308              ; 不等 → 分支B
      ; ── 分支 A（正常 UI）──
12f6: dlopen("appFw.so", 0x101)          ; ★ RTLD_LAZY|RTLD_GLOBAL
1302: dlsym(handle, "Application_Start")  ; ★
1306: b 1318
      ; ── 分支 B（调整检查 UI）──
1308: dlopen("AdjChkConfig.so", 0x101)
1314: dlsym(handle, "_ZN13app_adjchk_ui9main_loopEv")
      ; ── 汇合 ──
1318: dlsym(...)
133e: str  r0, [r7, #44]         ; fn 存进线程参数
1344: osal_create_waitable_thread(Main, ...)
134a: osal_set_cpu(thread, 15)
```

### 解出的字符串常量（rodata）
```
0x1711  AppConfig.so          ← strcmp 目标
0x171e  appFw.so              ← ★ dlopen A
0x1727  Application_Start     ← ★ dlsym A
0x1739  AdjChkConfig.so       ← ★ dlopen B
0x1749  _ZN13app_adjchk_ui9main_loopEv   ← dlsym B
0x1768  AppFw
0x176e  /usr/share/app/
0x17b9  global.xdb
```

### `Main`（120 B）
```asm
14de: uxengine_init2()                       ; ★ 引擎初始化（走到 DMP_2D_Initialize）
14e2: initializeArena()
14ea: sys_setCurrentDirectory("/usr/share/app/")
150c-1520: GraphicsEngine 字体/布局属性
152a: setDatabase("global.xdb")
1534: sys_setFontNameWithNamespace(51)
1538: cbz  r4, 153c        ; ★★★ **if (fn == NULL) 跳过**
153a: blx  r4              ; fn() —— 这里才进 Application_Start
```

## ② ★★★ 根因：**dlopen 失败且返回值未检查**
```
dlopen("appFw.so") 返回值**完全没有检查**
⇒ dlopen 失败 ⇒ handle=NULL
⇒ dlsym(NULL,"Application_Start") ⇒ fn=NULL
⇒ Main 里 `cbz r4` ⇒ **跳过 fn() 调用**
⇒ uxengine_init2 跑完、DMP_2D_Initialize 返回 0，然后什么都没发生
```

**⭐⭐⭐⭐⭐ 与真机 `/log/im_manual.log` **只有两行**完全吻合：**
```
!!! GLES Vecgen disabled.
===== DMP_2D_Initialize : 0
```
**⇒ 不是"崩在 appFw"，而是**根本没调用到 AppStart**。**

## ③ 为什么 dlopen 会失败
**appFw.so 缺 `_ZN4sidb7referer7s_countE`（数据符号），
它**只在 M2 版 libmpr.so 里定义**；而正常启动路径里在 appFwBoot 之前**没有人加载 libmpr**。**
⇒ **手动先 `dlopen("libmpr.so", RTLD_GLOBAL)` 再 `dlopen("appFw.so")` 即可成功**（QEMU 线已实测）。

**⇒ 真正缺口 = "谁应该在 appFwBoot 之前加载 libmpr"。**
**⚠️ libIMDB 里提到 libmpr 3 次 ⇒ 大概率是 IMDB 的某服务负责加载它
⇒ 这与"服务初始化没跑到的那一步（0x30/0x31 族 / 19 条注册）"是同一件事。**

## ④ 新假设（待验证）：`im.elf` 传入的 arg 决定加载哪个 UI
```
arg == NULL 或 == "AppConfig.so"  ⇒ dlopen("appFw.so")        → Application_Start（正常）
否则                              ⇒ dlopen("AdjChkConfig.so") → app_adjchk_ui::main_loop（调整模式）
⇒ **若 im.elf 传的 arg 不是 "AppConfig.so"，就会走分支B ⇒ 黑屏！**
⇒ 立即查 argtrace 里 Application_System_Init 的参数。
```

## ⑤ 静态分析的另外两条结论
### appFw.so 画像
```
64,273,528 B / .text 41.8 MB / .bss 76 MB
仅 3 个导出（Application_Start 72 B + 两个零长）
754 导入 / 15 DT_NEEDED:
  libNetCommon / libGOConnectionManager / **AdjChkConfig** / libopus /
  libSoundTouch / libqrencode / libInfraSKD + 标准库
⚠️ **不直接 DT_NEEDED libmpr** ⇒ mpr 符号靠运行时已加载的全局符号解析
导入 56 个 mpr* 符号：**54 个真实实现 / 1 个桩（PublishPublicKey）/ 1 个在 dataflowInfraFramework**
⇒ **appFw 用的 mpr 接口是完整的**（此前担心的"74% 是桩"不影响它）
```
### libmpr.so 两代完全不同
```
M2 : 3,038,540 B / 6,092 导出 / 642 个 MprIf 中 **480 个(74%)是 ≤8B 空桩**
    内容：XAVC 元数据 / sidb record / execqueue / LibMediaProfileIf
E10: 13,021,036 B / 3,234 导出
    内容：MXF 格式 / clip 管理 / 配音(dubbing) / still player / 录制
⇒ **仅 193 个符号相同 ⇒ 是两套架构，不是版本差异**
⇒ 但 appFw 实际调用的 54 个 mpr:: 函数在 M2 版里**都是真实实现**
```

## ⑥ ABI 混搭（QEMU 线确诊）
```
libmprctrl.so 需要  MprIf::SetCreateParam_SupportContentTypes(**unsigned long**)  ← 'm'
M2  libmpr.so 提供  …SupportContentTypes(**unsigned int**)                        ← 'j'
E10 libmpr.so 提供  …SupportContentTypes(**unsigned long**)                        ← 'm'
libmprctrl.so 两棵树 md5 完全相同
⇒ **v9b = 「E10 的 libmprctrl + M2 的 libmpr」混搭**
⚠️ 但换 E10 版 libmpr 是**死路**：appFw 反而加载不了（缺 sidb::referer::s_count）
⇒ **判断配置对错的标准应是"UI 应用能否加载"，不是"符号是否全部解析"**
```

## 方法论（第 87 条）★★★
**"没看到 X"的三种可能必须分清：**
```
① 从没尝试 X
② 尝试了但失败了（且失败未打日志）      ← 本例
③ 尝试成功但结果被忽略
⇒ **判据：查"尝试点"的返回值与错误信息（dlerror），而不是只看"结果是否出现"。**
**⇒ 本例中 "maps 里没有 appFw.so" 被误读为"没尝试 dlopen"，
   实际是"dlopen 失败" —— 两者修法完全不同。**

## 122. 2026-10-29 ★★★ 重要自我纠错：我的"40 次 dlopen"是**日志上限**，不是全部尝试

协作方完整反汇编 `appFwBoot.so` 后指出：**它确实 dlopen 了 appFw.so，但失败被静默吞掉**：

```
12e4: cmp r8,#0        ; 传入的 arg
12e8: beq 12f6         ; NULL → 分支A
12ea: strcmp(arg, "AppConfig.so")
12f4: cbnz → 1308      ; 不等 → 分支B
分支A: dlopen("appFw.so", 0x101=RTLD_LAZY|RTLD_GLOBAL)
       dlsym(handle, "Application_Start")
分支B: dlopen("AdjChkConfig.so"); dlsym(handle, "app_adjchk_ui::main_loop()")
汇合:  str fn → 线程参数; osal_create_waitable_thread(Main, fn); osal_set_cpu(t,15)

Main: uxengine_init2 → initializeArena → setCwd("/usr/share/app/")
      → GraphicsEngine 字体/布局 → setDatabase("global.xdb")
      → cbz r4,skip; blx r4   ← ★ fn==NULL 时**直接跳过**，静默继续
```

**⇒ dlopen 的返回值完全没有检查 ⇒ 失败 ⇒ fn=NULL ⇒ Main 跳过调用 ⇒ 界面永不出现，且无任何错误日志。**

**⚠️ 并且这解释了真机 `/log/im_manual.log` 只有两行（GLES Vecgen disabled / DMP_2D_Initialize: 0）的现象。**

### 我的错误

我在探针里限定了"最多记录 40 个不同的 dlopen 名字"（`#define NQ5 40`），
而日志里恰好 `dl=40` ⇒ **`appFw.so` 可能是在第 41 个之后才尝试的，被我的上限吞掉了**。
⇒ "40 次尝试里没有 appFw" **不能**推出"从未尝试加载 appFw"。

### 下一步（明确）

1. 把 dlopen 记录上限提高（40→200），并**记录返回值与 dlerror**
   ⇒ 直接确认 `dlopen("appFw.so") == NULL` 及其错误文本；
2. 顺便确认 im.elf 传给 `Application_System_Init` 的 arg 是 **NULL 还是 "AppConfig.so"**
   （决定走 appFw 还是 AdjChkConfig 分支）；
3. 修复点：**让 libmpr.so 在 appFwBoot 之前进入全局作用域**
   （我们已验证：先 dlopen libmpr(RTLD_GLOBAL) 再 dlopen appFw 就能成功）。

---

# ★★★★★ 2026-10-30 18:00 第八十二层：libmpr **确实在运行**；缺口可能是"作用域"而非"未加载"

## ① ⚠️ 关键反驳：我早前采样数据证明 libmpr 在运行
```
out/s3b   : libmpr 样本=6 / 总374   （appFw=357, libObj=11）
out/btsamp: libmpr 样本=5 / 总10    （50%！）
out/s3    : 0 / 58
抽样: S3 pid=124 mod=libmpr.so base=0x20a38000 off=0xaede0
⇒ pid=124 = im.elf；samp3 用 dladdr(pc) 反查模块名 ⇒ 模块归属可靠
⇒ **libmpr 确实被加载并执行代码**
```
**⚠️ 因此"正常启动路径从不加载 libmpr"这个结论**可能是日志上限截断造成的假象**
（QEMU 线自己发现探针 `#define NQ5 40` 正好打满）。**

## ② ★★★ 新假设：加载了但**不在全局作用域**（或**时序在后**）
**`sidb::referer::s_count` 是**数据符号**（size=4），数据符号在 RTLD_LAZY 下也必须立即解析。
⇒ appFw 的 dlopen 能否解析它，取决于 libmpr 符号是否在**全局作用域**：**
```
dlopen("libmpr.so", RTLD_LAZY)              → 局部作用域 ⇒ appFw 解析不到
dlopen("libmpr.so", RTLD_LAZY|RTLD_GLOBAL)  → 全局       ⇒ appFw 能解析 ✓
LD_PRELOAD=/usr/lib/libmpr.so               → 全局       ⇒ 同上 ✓
DT_NEEDED libmpr.so（在 appFw 上）          → 依赖加载   ⇒ 符号可见 ✓
```
**⇒ 两个待区分的可能：**
```
假设A：libmpr 被以 RTLD_LOCAL 加载 ⇒ 符号不全局
假设B：libmpr 在 appFw 的 dlopen **之后**才加载 ⇒ 时序问题
```
**两者修法相同（提前/全局加载），但验证方法不同。**

## ③ `sidb::referer::s_count` 唯一定义处
```
搜索 M2 + E10 全部 .so：
  ✅ 只有 libmpr.so 定义 _ZN4sidb7referer7s_countE (size=4)
⇒ appFw.so 的依赖是**真实存在**的，只是没被声明
```

## ④ `libmprdummy.so` 线索：**M2 没有**
```
E10: ✅ 存在 (5,748 B, md5 43eacdb0)
M2 : ❌ 不存在
v9b: ❌ 不存在
⚠️ 但 M2 的 libIMDB **表里列了 libmprdummy.so** ⇒ 又一个**幽灵注册**
   （与 libgpe.so 同类现象：注册表里有，文件不存在，原厂如此）
⇒ **"配置错位"假设减弱**：M2 原厂也没有 dummy，说明它不该出现
```

## ⑤ 工具就绪：patchelf 0.18.0
```
apt-get install patchelf ✅ 成功
appFw.so    : .dynamic 43 槽 / 39 项 ⇒ **4 空槽**
appFwBoot.so: .dynamic 35 槽 / 31 项 ⇒ **4 空槽**
⇒ 两个都可安全 --add-needed（无需移位）
⚠️ 但 patchelf 会重写 .dynstr ⇒ 文件布局变化 ⇒ **必须先在 QEMU 验证改后的 .so 能加载**
```

## ⑥ 方案排序（基于"作用域/时序"假设）
```
① LD_PRELOAD=/usr/lib/libmpr.so        ← 最简、零 ELF 修改、真机可用 env.txt、完全可逆
② patchelf --add-needed libmpr.so appFw.so   ← 正道（依赖本就存在，只是未声明）
③ patchelf --add-needed libmpr.so appFwBoot.so ← 可行但作用域取决于 IMDB 的 dlopen flags
④ 运行时注入                            ← 已证干扰时序，排除
```

## ⑦ libIMDB 表里的关键排布
```
libInfraMediaMount.so → InfraMediaMount_Init
libInfraMediaDetect.so → InfraMediaDetect_Init/_Suspend/_Resume
libmprdummy.so                              ← 幽灵（M2 无此文件）
libdiablo.so / libclema.so
**libmpr.so**                               ← ★ **它后面没有 init 符号**（不像 libmprctrl）
libmprctrl.so → InfraMprCtrl_Init
libMprSeq.so → MprSequencerIf_Init
⇒ libmpr 可能**不是作为服务加载**，而是作为某个服务的**依赖**
```

## 验证顺序（都很便宜）
```
1) 试 LD_PRELOAD=libmpr（真机用 env.txt / QEMU 用 preload）验证"作用域假设"
2) 若成立且要最小化 → patchelf --add-needed libmpr.so appFw.so
3) 若 1) 无效 ⇒ 转向"时序假设"（libmpr 加载在 appFw 之后）
```

## 方法论（第 88 条）★★
**"日志里没有 X"再次不等于"X 没发生"：
QEMU 线的 dlopen 记录有 `NQ5=40` 上限，正好打满 ⇒ appFw/libmpr 的加载尝试被吞。
⇒ **凡有数量上限的日志/计数器，必须检查"是否打满"**（打满 = 后面的全部丢失）。
⇒ **判据：在日志里记录"已丢弃 N 条"或"limit reached"标记，
   让"截断"本身成为可见信号。**

## 123. 2026-10-29 提高 dlopen 上限到 120 + 记录失败原因：**libmpr 确实被加载了 3 次，但 appFw 仍从未被尝试**

改动：`NQ5 40 → 120`；对名字含 `appFw`/`AdjChk` 的 dlopen 打印句柄与 `dlerror()`。

实测：

```
events = 111
dl（记录到的不同 dlopen 目标）= 61        ← 说明旧上限 40 确实截断了
含 libmpr 的 dlopen = 3 次               ← ★ libmpr 被加载过（协作方的采样是对的）
M2DLERR（appFw/AdjChk 的 dlopen） = 0 条  ← ★ appFw 与 AdjChkConfig 都没被尝试加载
```

### 结论（修正上一轮）

1. **"正常路径从不加载 libmpr"是错的** —— 它被 dlopen 了 3 次（很可能 RTLD_LOCAL，
   所以 `s_count` 对 appFw 不可见；§122 的作用域假设成立）；
2. **但 appFw.so 仍然一次都没被 dlopen** ⇒ **`Application_System_Init` 至今没有被调用**；
3. `libmprdummy.so` 在 v9 树里**不存在**（只有 libmpr.so / libmprctrl.so）⇒ "配置错位"假设无法验证。

### ⇒ 真正的阻塞点仍是：libIMDB 没有走到"调用 appFwBoot 的入口符号"这一步

这与 §121 一致：libIMDB 用 `{模块名, 入口符号}` 表驱动，40~61 个模块被 dlopen 了，
但**入口调用必须先满足某个前置条件**，而该条件在 QEMU 里未满足。

---

# ★★★★★ 2026-10-30 17:30 第八十三层：bootin 的 preload 完整逻辑（代码级）+ 崩溃态调试通道结论

## ① 用户问题：「全部崩溃时能否 USB shell 调试」→ 结论：**不需要**
**真机线实测：**
```
Pico 走 bootrom 阶段，不依赖 Linux/内核/文件系统
  接 USB 线 D+/D-（GP28/GP27 经 470Ω），模拟 bootrom 串口协议 ⇒ **不需要 UART**
  实测：10-28 相机 3 秒重启循环时注入成功；刷 v9b 后刷回原厂
⇒ **崩溃态已有保险**

gadget（v9b3 内置）：/lib/mprshim.so + bootin.elf 脚本
  ⇒ 只需内核活着 ⇒ **半死态可用**
  ⚠️ v9b 在真机上**不是 boot loop**（是"没 UI"，内核活着）⇒ gadget 方案成立
```

## ② 两条"免费 shell"路线均已排除
```
× launch_shell.elf：init 里**无条件调用**（0x9f9a），是 PID 1 正常移交，不是维护模式
   launch_shell() = signal(SIGCHLD,IGN) + execl("/usr/bin/launch_shell.elf", argv0, buf1,buf2,NULL)
   buf1/buf2 = 两个全局决定的单字符 "0"/"1"
× gdb --args：机制存在（bootin flags bit4 ⇒ 命令前插 "gdb --args "），
   但全镜像**没有 gdb/gdbserver 二进制**（v9b /bin /sbin /tool /lib /local + rootfs 都没有）
   ⇒ 且 gdb 是交互式的，无 tty/网络 ⇒ 即使触发也只会"im.elf 不再启动" ⇒ 更糟
```

## ③ ★★★★★ bootin 的 `/setting/mode/preload` 处理（代码级完整解出）
```asm
9990: tst.w r3, #64 (0x40)     ; ★★★ **bit6 = preload 路径总开关**
9994: beq   0x9a1c             ; bit6==0 ⇒ **完全不读该文件**
9996: ldr   r1, =0xa6dc        ; "LD_PRELOAD="（11 字符）
999e: strncpy(buf, "LD_PRELOAD=", 512)   ; ★ buf 预清零
99a4: open("/setting/mode/preload", O_RDONLY)
99b4: add.w r1, r7, #11        ; ★ 读入位置 = **buf + 11**（"LD_PRELOAD=" 之后）
99b8: read(fd, buf+11, 498)
      ; ── 白名单：strncmp(buf+11, whitelist[i], strlen(whitelist[i])) 前缀匹配 ──
9b24: tst.w r3, #64            ; ★ putenv 也受同一 bit6 门控
9b2c: blx  putenv
```
**⇒ 三个确认：**
```
① 文件内容必须是**纯路径**（"LD_PRELOAD=" 由 bootin 自己加）
② **不需要换行符**（strncpy 预清零 ⇒ 自动 NUL 结尾）
③ 白名单是**前缀匹配** ⇒ "/usr/tool/LeakTracer.so:/usr/lib/libmpr.so" 开头命中即整条通过
```

## ④ ★★★ bootin 会**清空环境**，但保留 LD_PRELOAD / MALLOC_OPTIONS
```
rodata:
  0x275c  'Bb system()'
  0x2768  'Bb clearing env'    ← 清空
          'LD_PRELOAD'          ← 不带等号 = **保留名单**
          'MALLOC_OPTIONS'
⇒ **清空时 LD_PRELOAD 被保留** ⇒ 可传给 im.elf
```

## ⑤ 给真机线的两条关键建议
```
⚠️ 风险：preload 文件路径受 bit6 门控；若 bit6 未置位，文件写得再对也不读
   ⇒ 这可能是"environ 里始终为空"的**真因**（不是双前缀格式错）

✅ 更稳做法：**bootin.elf 脚本里直接 `export LD_PRELOAD=...`**
   不依赖 bit6（env 清空是另一段代码，保留名单会保住 LD_PRELOAD）

✅ 建议两条都放（互为保险，内容相同时无害）：
   export LD_PRELOAD=<同内容>
   printf '<纯路径>' > /setting/mode/preload
   exec /usr/bin/bootin.real "$@"
```

## ⑥ 其他 bootin 参数（rodata 已解出）
```
支持：gdbs/GDBS/gdb/GDB、nolt/NOLT、nowfree/NOWFREE、killall、help/-h、-v
      dmode=/d=、amode=/a=、file=/f=、target=、sim
      target=resub / target=qemu
命令行片段：'s g r u ' / 's g r u l ' / 's g r u l i a '（三变体，dmode 决定）
           'debug ' 'cho ' 'cipa ' 'dummy_susres ' 'lazy-global ' '& '
命令拼接：若 flags&0x10 ⇒ 前缀 "gdb --args "
         基串 "/usr/bin/im.elf " + 字母变体 + 上述标志 + "& "
```
**⚠️ bootin 必须用 `objdump -M force-thumb` 反汇编，否则全是假指令（真机线踩过）。**

## 方法论（第 89 条）★★
**"配置文件格式对了"不等于"配置会被读取"：
本次 bootin 的 preload 路径由 `flags & 0x40` 门控 —— 位没开时**文件根本不会被 open**。
⇒ 排查"配置不生效"时，先查**读取是否有前置开关**（门控位/环境/前置文件），
   再查格式对不对。否则会反复修格式而问题始终不变。**

## 124. 2026-10-29 钩 `dlsym` 失败（递归自杀）；已回滚；给出正确做法

按协作方建议实现 `dlsym` 透明桩（打印符号名 + 返回值），结果：

```
events = 0     dlsym 记录 = 0
```

**⇒ 探针库自爆。** 原因：`m2_call_dlsym` 内部用 `dlsym(RTLD_NEXT,"dlsym")` 解析原生函数，
而**这个调用又会被我们自己的 `dlsym` 桩拦截** ⇒ 无限递归 ⇒ 启动即死。

### 正确做法（下一轮）

1. 不要把 `m2_call_dlsym` 写成会调用 `dlsym` 的普通函数；
2. 用 `dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.4")` 或在 `__attribute__((constructor))`
   里预先用 `dlopen("libdl.so.2")+dlsym` 拿到原生 `dlsym`（构造期不经过我们的桩）；
3. 或者改成"只记录、不代理"：桩内先取原生地址（静态缓存），再 `bx` 过去。

### 另一个已排除的方向

本轮还测了 `bootin` 的 `target` 参数（`target=qemu` → `target=product`）：

```
events = 124，dl = 68，appFw 尝试 = 0
⇒ **target 参数不影响是否加载 appFw**（结果与 target=qemu 一致）
```

## 125. 2026-10-29 ★★★★ 决定性发现：**IMDB 表登记 200 个模块，v9 树里缺 125 个**

对 `lib/libIMDB.so` 里的模块名做正则提取，与 `root/lib` 实际文件比对：

```
IMDB 表登记: 200 个 .so
树里缺失:    125 个（62%！）
缺失示例:  AppConfig.so / libBizInitializer.so / gui.so / libDDS.so /
           libAdjModule.so / libAdjustUIProxy.so / libAtome*.so /
           libCameraStillDate.so / libCoreSync.so / libHotplug.so /
           libHandleRsrc.so / libI2SRight.so …
```

**并且这些模块在 E10 树、usr.m2_v2、port_m2 里也都不存在**（各树 .so 数量：151 / 152 / 150）。
⇒ **不是路径问题，是这几个树本身都是"只含部分模块"的精简集合。**

### 含义（这可能解释一切）

1. `im.elf` 按 IMDB 表**串行**逐条 `dlopen → dlsym → fn(arg)`；
   遇到缺失模块会 `dlopen` 失败（`return -1`，静默）；
2. 关键的 **`libBizInitializer.so`（BIZ_Init，业务初始化）缺失** ⇒ 那批
   `0x30/0x31` 族服务注册永远不会发生（与我们观测到的"缺失族"吻合！）；
3. `AppConfig.so` 也缺失 ⇒ 如果它本该在 appFwBoot 之前/同时被加载，链条更早断；
4. **⇒ M2 在 QEMU 里黑屏，很可能不是"移植错误"，而是"这棵树缺了 125 个模块"。**

### 下一步（方向性变化）

* **优先找完整的 M2 固件树**（那 125 个 .so 是必需的）；只有补全它们，
  `BIZ_Init → ... → appFwBoot.Application_System_Init → dlopen(appFw.so)` 这条链才可能走通；
* QEMU 侧用现在这套 harness 验证"补模块后链条是否前进"（补一个跑一次，看 events 与 M2DLSYM 是否增加）；
* 真机侧同样适用：**若 v9b 的 /usr/lib 也缺这 125 个，那 v9b 注定黑屏**，与 libmpr 无关。

## 126. 2026-10-29 ⚠️ 对 §125 的限定：IMDB 有 6 张按 target 分组的表，"缺 125 个"可能被高估

`libIMDB.so` 里存在多个表选择器字符串：

```
IMDB:default / IMDB:qemu / IMDB:nfs / IMDB:set / IMDB:USBJ / IMDB:RE-SUB
```

说明**模块表是按 target/机型变体分组的**。我 §125 里的"200 个登记模块"是从整个
`.rodata` 里正则提取的**并集**，并不等于"本次 target=qemu 启动应该加载的集合"。

⚠️ 因此 **"缺 125 个"这个数字需要打折**：其中相当一部分可能属于其它变体表
（nfs/usbj/re-sub 等），本机型根本不会去加载。

### 仍需确认的两件事

1. **`IMDB:qemu` 表实际包含哪些模块**（而不是并集）；
2. 该表里**是否存在树中缺失的模块**（尤其是 `libBizInitializer.so` / `AppConfig.so`）。

### 下一步（最省的静态做法）

用"字符串相邻性"或"表项指针数组"把 `IMDB:qemu` 表解析出来：
* 每个表在 .data.rel.ro 里应有一组指针（指向 name/entry/arg 字符串）；
* 找到指向 `"IMDB:qemu"` 的引用点，其邻近的指针数组就是该表。

拿到 qemu 表后再与树比对，才是可靠结论。

---

# ★★★ 2026-10-30 18:00 第八十四层：IMDB 表 vs 树内文件比对 —— **阴性结果**（假阳性太多）

## 比对数据
```
libIMDB 表里的模块名: 194
v9b /lib (=/usr/lib): 209   rootfs /lib: 35
⇒ 表里有、两处都没有的: **115 个**
```

## ⚠️ 但大部分是假阳性
```
① 部分"模块"是 **libObj.so 的内部组件**，不需要独立文件：
   infra_tp_init / infra_hotshoe_init / uxengine_init2 → ✅ libObj.so 导出
② 有些是**已知幽灵**（原厂也没有）：libgpe.so / libmprdummy.so
③ 只有 5 个是"E10 有但混搭树丢了"：
   libInfraBGMMeta / libInfraCacheMgr / libInfraHighlight
   libInfraMakeScenario / libmprdummy
⇒ **"115 个缺失"不是真问题**
```

## ★ BIZ_Init 假设**降级**
```
libBizInitializer.so → 三处都没有（v9b / rootfs / E10）
BIZ_Init 符号        → libObj / appFw / appFwBoot 都不导出
⇒ 即使文件存在，dlsym("BIZ_Init") 也会失败

⚠️ 更关键：im.elf 的 load_module 在 dlopen 失败时返回 -1，
   但实测 dl=68 说明**循环继续** ⇒ 缺失模块不阻塞后续模块
⇒ **"BIZ_Init 阻塞"站不住**
```

## ✅ 比对澄清了一个重要机制
```
appFwBoot 导入 uxengine_init2
libObj.so **导出** uxengine_init2
⇒ appFwBoot 的 Main 调 uxengine_init2 时从**已全局加载的 libObj** 解析
⇒ **解释了为什么"没有 appFw 也能跑到 DMP_2D_Initialize"**
⇒ appFwBoot 只依赖 libObj（已加载），appFw 是它的**输出**不是**输入**
⇒ 这条链条现在完全自洽
```

## 问题回到原点（两种可能仍未区分）
```
① dlsym(handle,"Application_System_Init") 返回 NULL ⇒ 静默跳过
② 循环根本没处理到 appFwBoot 这一项（前面某项 fn() 阻塞）
⇒ QEMU 线修 dlsym 钩子是唯一能区分的办法
```

## dlsym 钩子的两个坑（给 QEMU 线的建议）
```
① glibc 2.11 的 dlsym 可能在 libc 而非 libdl（libdl 只是空壳）
   ⇒ 构造函数里用 RTLD_NEXT 应该安全（桩还没生效）
   ⇒ 或 dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.4")
② 若 dlsym 在构造函数**之前**被别的库 init 调用，桩仍会递归
⭐ 建议：**第一版只记录不转发**（返回 NULL），先看"谁在调 dlsym、要什么符号"
   ⚠️ 返回 NULL 会让很多库加载失败 ⇒ 只跑一轮看日志
```

## 方法论（第 90 条）★★
**"表项 vs 文件"这类比对，必须先确认"表项是否都对应独立文件"：
libIMDB 混装了①独立 .so 模块 ②libObj 内部组件 ③幽灵注册，
⇒ 直接做集合差集会得到大量假阳性（本次 115 个里绝大多数是假阳性）。
⇒ **做差集前先分类**：表项是否有对应的独立文件语义？没有的话就不该参与差集。**

## 127. 2026-10-29 补 `libBizInitializer.so` 桩实验：**dlopen 了，但入口函数仍未被调用**

造了一个 8 KB 的桩 `libBizInitializer.so`（导出 `BIZ_Init`/`BIZ_Suspend`/`BIZ_Resume`，
被调用时往 `/dev/kmsg` 打一行），放进 `initramfs-ui/usr/lib/`，重跑：

```
events = 124（不变）
dl（dlopen 目标数）= 70   （上一轮 68 ⇒ 桩确实被 dlopen 了 ✅）
BIZ_Init 被调用 = 0        ← ★ 入口函数从未被调用
appFw 尝试 = 0
```

### 关键含义

**`libBizInitializer.so` 被 dlopen 成功、且导出符号齐全，但 `BIZ_Init` 从未被调用**
—— 与 `appFwBoot.so` 的症状**完全一样**（也是 dlopen 成功、入口未调用）。

**⇒ 这不是"某个模块阻塞"，而是"im.elf 的加载循环在某个点之后不再调用任何模块入口"。**
（协作方的判断得到验证：dlopen 失败不阻塞循环；真正的问题是"入口调用"这一环。）

### 下一步（唯一能区分的实验）

**dlsym 钩子 v2 —— 只记录、不转发**（避免上一版的递归自杀）：
桩内第一次被调用时用 `dlopen("libc.so.6")+dlvsym` 取原生地址（构造期完成），
记录 `(handle, sym, result)` 后转发；若取不到则记 NULL 并记录调用者 lr。
看"最后一条 dlsym 是什么符号、返回什么"。

## 128. 2026-10-29 静态验证 IMDB 表项：入口字段**确实非空**（排除"表数据缺失"假设）

在 `libIMDB.so` 里定位到指向各字符串的**指针表项**：

```
libBizInitializer.so      → 表项 @0x8934
BIZ_Init                  → 指针 @0x8938   （紧跟其后 ⇒ 同一表项的 entry 字段）
appFwBoot.so              → 表项 @0x898c
Application_System_Init   → 指针 @0x8990   （紧跟 ⇒ entry 字段有效）
AppConfig.so              → 指针 @0x89a4 / 0x89d0（arg 字段）
```

**⇒ 表项结构确实是 `{模块名, 入口符号名, 参数…}`，且入口字段**指向真实字符串**，
不存在"entry 为空导致直接跳过"的情况。**

### 因此 §127 的两种可能中，② 被排除

* ✅ 表数据正确（entry 非空、参数正确）
* ❌ 那就只剩 ①：**`dlsym(handle, entry)` 返回 NULL** 或 **调用环节根本没执行**

### 下一轮（唯一手段）

修好 dlsym 钩子 v2（只记录、不转发；构造期预取原生地址，避免递归），
观察：
```
是否请求 "Application_System_Init"？返回 NULL 还是地址？
是否请求 "BIZ_Init"？
最后一条 dlsym 是什么？
```
这能一次区分"dlsym 失败"和"根本没调到 dlsym"。

## 129. 2026-10-29 下一实验设计：用 **`dlclose` 钩子**代替 `dlsym` 钩子（避开递归雷区）

### 为什么放弃 dlsym 钩子

`dlsym` 桩内必须解析"原生 dlsym"，所有常规手段都会**再进我们的桩**：
`dlsym(RTLD_NEXT,…)`（直接递归）、`dlopen("libdl.so.2")+dlsym`（dlopen 也不必经过我们的桩）、
`dlvsym(RTLD_DEFAULT,…)`（会搜到我们自己的桩）。
⇒ 上一版因此 `events=0`（§124）。

### 替代方案：钩 `dlclose`

im.elf 的 `load_module()` 在 **dlsym 失败**路径上会调用 `dlclose(handle)`：

```asm
dlsym(handle, entry) → fn
if (dlerror()) { dlclose(handle); return -1; }     ← ★ 只有失败才走这里
if (fn) fn(arg)
```

**⇒ 只要统计 `dlclose` 调用次数与句柄**：
* **有 dlclose 调用** ⇒ 证明"dlsym 失败并回滚"确实发生（可能是符号名对不上/作用域问题）；
* **没有 dlclose 调用** ⇒ 说明根本没走到 dlsym 之后，或者 dlsym 全部成功但 `fn(arg)` 没执行
  （⇒ 需要另找观测点）。

`dlclose` 桩无递归风险（我们自己的库不调用 dlclose），实现与现有 `dlopen` 桩同构。

### 实施注意（§85 硬规则）

改完必须先验 `events ≈ 124`，再谈新数据；若退化立即回滚。

## 130. 2026-10-29 ★★★★ 决定性结果：`libBizInitializer.so` / `appFw.so` **从未被 dlopen 过**

给 dlopen 桩加上"对含 `BizInitializer`/`appFw`/`AdjChk` 的名字记录**返回值与 dlerror**"：

```
events = 124（基线正常）
M2DLH 记录 = **0 条**     ← ★ 这三个库一次都没被 dlopen 过
```

**⇒ 我上一轮 §127 的"桩被 dlopen 了（68→70）"是误读——那个 +2 与桩无关。
`libBizInitializer.so` 从来没有被 im.elf 的加载循环碰到。**

### 最后被 dlopen 的模块（70 个的末尾）

```
libDpro_EVF.so / libInfraUpmPro.so / libfan.so / libEMG.so /
libInfraEthernetService.so / **libNetService.so**
```

**⇒ 加载循环停在这附近（`libNetService.so` 是最后一个）。**
IMDB 表里排在它后面的 `libBizInitializer.so`、`appFwBoot.so`（**但它在 maps 里，见下**）、
`appFw.so` 都不会被处理。

⚠️ 矛盾点：`appFwBoot.so` **在 im.elf 的 maps 里**却从未被 dlopen 过
⇒ 说明它是**作为某个库的 DT_NEEDED 依赖**被加载的，不是 IMDB 加载的。
（这也解释了为什么它的入口从未被调用：**IMDB 从没把它当作模块处理**。）

### ⇒ 结论

**问题不是"dlsym 失败"、也不是"表数据错"，而是：im.elf 的模块加载循环在
`libNetService.so` 之后就停止了。**

### 下一轮（唯一任务）

找出循环停止的原因（三种）：
1. 紧接着的下一条 IMDB 表项让循环 `return`（例如某个模块 `dlopen` 失败 ⇒ 循环 break）；
2. 某个模块的 `fn(arg)` 阻塞，循环卡在调用里（此时"最后 dlopen 的是 NetService"合理：
   它的入口被调用后没返回）；
3. `IMDB_find_target_bit` 过滤（target 不匹配 ⇒ 后面的项被跳过）。

**判据**：若循环是 break，后续还会不会有其它 dlopen（比如运行时的）？现在没有 ⇒ 更像 ②（卡在某个 fn）。
⇒ 下一步：对 `libNetService.so` 的入口函数下桩（看它是否被调用、是否返回）。

## 131. 2026-10-29 定位"循环停止点"的模块入口：`libNetService.so → netservice_init`

从 `libIMDB.so` 的字符串序列解析出 `libNetService.so` 所在表项的入口符号：

```
libNetService.so  → netservice_init / netservice_sus / netservice_res      （@0x4dae 附近）
libInfraIPsec.so  → InfraIPsec_init / InfraIPsec_exit / InfraIPsec_sus   （紧随其后）
```

`libNetService.so`（678,984 B）确实导出 `netservice_init`（`0x4efd4`）。

**⇒ 高度疑似：`netservice_init(...)` 被 im.elf 调用后**阻塞**，
导致 IMDB 的串行加载循环卡在这里 ⇒ 后面的 `libBizInitializer.so` / `appFwBoot.so` / `appFw.so`
永远不会被加载。**

### 与已有观测的吻合

* im.elf 主线程（tid=139）wchan = `osal_rcv_msg_tmo` ⇒ **在等消息**，
  与 `netservice_init` 内部等待网络/服务就绪的形态一致；
* 注册事件冻结在 122~134 附近 ⇒ 后续模块的注册也全部没发生。

### 下一轮（两种做法）

1. **确认 `netservice_init` 是否真的不返回**：在预加载库里拦 `osal_rcv_msg_tmo` /
   `osal_rcv_msg`，记录调用者 lr 是否落在 `libNetService.so` 内（若落在，即证明卡在它里面）；
2. **若确认**：反汇编 `netservice_init` 找它等的具体事件；判断该事件是否依赖 QEMU 缺失的
   硬件/网络服务 ⇒ 若是，则 **QEMU 侧此路终止**，但真机上该服务存在 ⇒ v9b 可能自行走通。

## 132. 2026-10-29 两个"接收"钩子都零命中 ⇒ 等待不在 rcv 上

对 `osal_rcv_msg`（无超时版）也加了透明桩（记录 qid + 调用库）：

```
events = 124（基线正常）
M2RCV2（osal_rcv_msg）命中 = 0
M2RCV （osal_rcv_msg_tmo）命中 = 0（此前已测）
```

**⇒ im.elf 进程既不通过 `osal_rcv_msg`、也不通过 `osal_rcv_msg_tmo` 等待
（至少不经过这两个导出符号的 PLT 调用）。**

### 那 wchan 里的 `osal_rcv_msg_tmo` 是怎么来的？

`wchan` 显示的是**内核记录的睡眠点符号**，它可以在**任何进过该函数后睡着的路径**上出现
（包括 libosal 内部包装函数在调用它之后的长睡眠），**不能反推调用者**。

### 下一个应该钩的信号量等待

协作方此前用他们的探针抓到过：

```
osal_wai_sem_tmo id=0x9401c3 tmo=-1     ← 无限等信号量
```

**⇒ 真正让主线程停住的，很可能是一个**信号量**等待，而不是消息接收。**

### 下一轮

钩 `osal_wai_sem_tmo`（记录 sem id + 调用者库/偏移），确定：
1. 谁在等信号量；
2. 该信号量本应由哪个模块 `osal_set_sem`；
⇒ 如果应释放它的是"尚未加载的模块"（如 `netservice_init` 之后的模块），就形成闭锁：
   **前一个模块等后一个模块的信号量 ⇒ 死锁**，那 QEMU 侧就无法通过，需要真机验证。

## 133. 2026-10-29 `osal_wai_sem_tmo` 钩子：未生效 + 基线退化（events 91），已回滚

加了 `osal_wai_sem_tmo` 透明桩（记录 sem id / tmo / 调用库）：

```
events = 91（基线 124）   ← 退化
M2SEM 记录 = 0            ← 钩子没有产生任何记录
.so 里也检索不到 "M2SEM" 字符串 ⇒ 该代码块很可能根本没被编入
```

**⇒ 又是一次"改编译产物没生效"的情况（与 §82/§84/§124 同类）。**
按 §85 硬规则已回滚（移除钩子、重编基线）。

### 观察到的规律（值得记录）

今晚多次出现"**加了代码但 .so 里没有**"：
`strings` 检索不到新增标记，且行为退化。原因可能是：
* 编译命令静默失败（旧 .so 残留）；
* 或某些函数/字符串因优化被消除。

**⇒ 今后每次改探针后必须第一步验证：`grep -ac <新标记> libm2probe.so` 必须 > 0，
再谈 QEMU 运行结果。** 这条应加入 §85 硬规则。

## 134. 2026-10-29 基线复核：停点是**每次运行都不同**（106~124，最后 dlopen 模块也不同）

回滚后重跑两次对比：

```
run A: events = 124，最后 dlopen = libNetService.so
run B: events = 106，最后 dlopen = libInfraBluetooth.so     ← 本次
```

**⇒ 加载停点**不是固定的**：既不是每次都停在 `libNetService`，也不是固定条数。

### 这个"方差"本身是重要信息

1. **排除"确定性死锁"**：如果是"某模块等某信号量、对方永不释放"的确定性闭锁，
   停点应当每次相同；
2. **更可能是竞态/时序**：例如某个模块的初始化等待超时/事件，
   而 QEMU 下事件到达时机不稳定 ⇒ 有时走到 124、有时 106 就停；
3. 也因此，**"卡住位置"不能再用单次运行来定位** —— 需要多次运行取**交集**
   （哪些模块**总是**在停止点之前、哪些**从未**出现）。

### 下一轮（方法修正）

多次运行（3~5 次），统计每个模块名出现频率：
* 100% 出现 ⇒ 稳定加载的模块；
* 从未出现 ⇒ 候选"被挡住"的模块（如 `libBizInitializer.so` / `appFw.so` 从未出现）；
* 结合"从未出现清单"与 IMDB 表顺序 ⇒ 定位真正的分界点。

---

# ★★★★★ 2026-10-30 18:30 第八十五层：**完整 IMDB 表解析（186 项）** —— 停点区间全是 USB/网络服务

## ★ 完整表结构（按表内顺序）
**表项布局（已验证）：**
```
+0x0E u16 field14 | +0x10 ptr field16(模块名) | +0x14 ptr field20(入口符号)
+0x24 ptr field36(argA) | +0x28 ptr field40(argB)   跨度 0x2C(44B)
表在 .data.rel.ro (vaddr 0xf00c, size 0x27b4)
⇒ 解析出 **186 项**
```

## 关键位置
```
[ 66] [ 90] [155] [175]  libInfraBluetooth.*      ← 分散四处
[ 75] [ 77] [ 78] [101]  libNetService*           ← 分散
[105] libInfraMFi.so           → InfraMFi_init
[106] libInfraHttpClient.so    → InfraHttpClient_Init
[107] libInfraMediaPwrMgr.so   → InfraMediaPwrMgr_init
[108] libInfraCameraBackground.so → CameraBackground_init
[109] libBizInitializer.so     → BIZ_Init          ← 文件/符号都不存在，空转
>>> [110] appFwBoot.so         → Application_System_Init  arg=**AppConfig.so**
[111] AdjChkConfig.so          → AppConfig.so
[112] subapp.so                → subapp_init
[114] /usr/local/lib/libVssApp.so → vssapp_init
[116] gui.so                   → DPro_App_UI_init
```

## ★★★ [118]–[130] 整段是 **USB 服务链**
```
[118] libInfraUsbHostStorage.so → UsbHostStorage_Init
[119] libInfraMtpServer.so      → MtpServer_Init
[120] libInfraMscServer.so      → MscServer_Init
[121] libInfraExtCmdSvc.so      → ExtCmdSvc_Init
[122] libusbcmd.so              → UsbCmd_ExtCmdSvcWrap_Init
[124] libInfraJetScsiServer.so  → JetScsiServer_init
[129] libInfraPtpExtCmd.so      → PtpExtCmd_Init
[130] libInfraAppDlUsbCommunication.so → AppDlUsbCommunication_Init
⇒ **若循环停在 USB 服务链 ⇒ 很可能在等 USB 硬件/枚举（QEMU 无真实 USB 设备）
   ⇒ 解释"停点每次不同"（超时/竞态）**
```

## ⚠️ 与 QEMU 线结论的矛盾（已指出）
**QEMU 线说"appFwBoot.so 在 maps 里但是**作为依赖**被加载的，不是 IMDB 处理的"。**
**但实测：**
```
① 遍历 v9b 全部 .so + .elf：**没有任何库 DT_NEEDED appFwBoot.so**（只有自身 SONAME 自引用）
② appFwBoot 在 IMDB [110]，表项完全有效
⇒ (a)在maps里 + (b)没人NEEDED它 + (c)IMDB未处理[110] 三条**不能同时成立**
   ⇒ 除非有别的东西 dlopen 了它
```

**⇒ 判别实验（已建议）：hook dlopen 记录 **lr（调用者）**：
```
lr 落在 im.elf 0xb53c（load_module 的 dlopen 点）⇒ IMDB 确实处理了 [110] ⇒ 断点在 dlsym/fn
lr 在别处 ⇒ 是别的模块 dlopen 的 ⇒ 断点在更早
```

## ★ 真机 vs QEMU 的关键区别（我修正后的判断）
```
真机 /log/im_manual.log 有 'DMP_2D_Initialize : 0'
⇒ Main 跑了 ⇒ **Application_System_Init 被调用过**
⇒ dlopen("appFw.so") **一定执行过**（无条件路径，且 arg="AppConfig.so" 走分支 A）
⇒ **真机是"尝试了但失败"，与 QEMU 的"根本没尝试"不同**
⇒ 这个区别决定修法：真机补符号作用域，QEMU 查 IMDB 进度
```

## QEMU 线状态：**goal 已标 blocked（40 轮）**
```
原因（对方自述）：
  · 三个能区分问题的钩子全失败（dlsym 递归自杀 / osal_wai_sem_tmo 未编入 / osal_rcv 零命中）
  · 每次改探针 30~50% 概率把基线从 124 打到 <110，需回滚
  · **加载停点每次不同（124 条 vs 106 条）⇒ 不是确定性死锁，是竞态/时序**
剩余选项：①多次运行取交集 ②反汇编 [95]-[130] 入口看阻塞 ③外部 ptrace
⇒ 我建议优先②（纯静态零风险），并提出可分担一半工作
```

## 方法论（第 91 条）★★
**"多条观测事实互斥"是一个强信号 —— 说明**观测口径**或**假设**有问题：
本次 (a)在maps里 (b)没人NEEDED (c)IMDB没处理 三条互斥，
⇒ 必有一条是错的，而"哪条错"本身就能定位问题（用 dlopen 记 lr 一次区分）。
⇒ **遇到互斥事实时，不要选一条相信，而要设计能区分它们的最小实验。**

## 135. 2026-10-29 ★★★★ 矛盾破解线索：`appFwBoot.so` 在 maps 里，但 6 次运行的 dlopen 记录里**从未出现**

对 6 份历史日志做统计：

```
dl.log    appFwBoot=0  appFw=0  total=40
dl2.log   appFwBoot=0  appFw=0  total=61
dh.log    appFwBoot=0  appFw=0  total=70
r2.log    appFwBoot=0  appFw=0  total=70
sem.log   appFwBoot=0  appFw=0  total=62
base3.log appFwBoot=0  appFw=0  total=60
```

**⇒ `appFwBoot.so` 明明在 im.elf 的 maps 里（§111 的 M2MAPS 证据），
却从未经过我们拦截的 `dlopen`。**

### 这意味着：**我们的 `dlopen` 观测是不完整的**

glibc 有内部加载入口 **`__libc_dlopen_mode`**（以及 `__libc_dlsym`），
NSS/部分库会走它 —— **这些调用不经过 `dlopen@plt`，我们的 preload 拦不到。**

**⇒ 于是有两种可能，都成立：**
1. **IMDB 循环确实处理了 appFwBoot（[110] 项）**，但它是通过 `__libc_dlopen_mode` 或
   某模块内部加载完成的 ⇒ 所以 maps 里有、日志里没有；
2. 或某模块（如 `libBizInitializer` 相关路径）间接加载了它。

**⇒ 我此前"appFwBoot 是作为 DT_NEEDED 依赖被加载"的推断无法成立**
（协作方已核实：全树没有任何库 NEEDED 它）。
**⇒ 因此"IMDB 从未处理 [110]"也是错的** —— 日志缺失是因为**观测手段漏了入口**。

### 下一轮（明确）

1. 额外拦截 **`__libc_dlopen_mode`** 与 **`__libc_dlsym`**（同样记录名字/符号/返回）；
2. 复跑后确认：
   * 若看到 `appFwBoot.so` / `Application_System_Init` ⇒ **IMDB 确实处理了它**，
     断点在 `dlsym` 或 `fn(arg)` 这一步；
   * 若仍看不到 ⇒ 加载来自更底层（需继续往 `_dl_open` 方向查）。

## 136. 2026-10-29 ★ 矛盾已解释：跨运行 + 跨仪器对比；且本次 maps 采样器失效

### 同一次运行内同时采（本轮）

```
events = 124, dl = 69, 最后 dlopen = libfan.so
maps 里 appFwBoot = 0
dlopen 里 appFwBoot = 0          ← 两者一致
maps 记录条数 = 1（"M2MAPS nodir"）← ★ 采样器这次失效了
```

### 旧数据（第 17 轮那次运行）

```
M2MAPS 记录 94 条，其中 **/appFwBoot.so 出现 1 次**
但那次运行 **还没有 dlopen 钩子**（钩子是下一轮才加的）
```

### ⇒ 结论：不是仪器漏了 `__libc_dlopen_mode`（协作方已核实 im.elf 只用公开 dlopen/dlsym），
**而是我把"第 17 轮的 maps" 与"第 18 轮之后的 dlopen 日志"拼在一起比较**——
**跨运行、跨仪器**，这正是 §77 方法论的"坐标系不一致"（这次是**时间维度**）。

**⇒ 撤销 §135 的结论**：
* `__libc_dlopen_mode` 假设**不成立**（im.elf 无此导入）；
* "IMDB 处理了 appFwBoot 但日志漏了"**无证据**；
* 更可能：**有些运行走到了 [110]（所以 maps 里有），有些没走到（所以日志里没有）**——
  与"停点每次不同"一致。

### 遗留问题

本轮 `dump_maps` 失效（只输出 `M2MAPS nodir`），因为 fork 子进程在 t=50s 时
父进程可能已变化/`dump_pid` 失效。
**⇒ 要在同一次运行里做可靠对比，必须先修好 maps 采样器**（改为在父进程内、
用一次 `read` 全量读 `/proc/self/maps`，而不是 fork 子进程去读父进程）。

### 修正后的下一轮

1. 修 `dump_maps`（父进程自读 `/proc/self/maps`，输出到串口）；
2. 同一次运行内：dlopen 日志 + maps + 一次"走得最远"的序列 ⇒ 真正定位断点；
3. 若某次确实走到 [110]（maps 有 appFwBoot 且 dlopen 日志也有）⇒ 断点在 `if(fn) fn(arg)`，
   查 `global+0x28` 的 bit14（协作方发现它控制"是否检查 dlerror/传哪个 arg"）。

---

# ★★★★★ 2026-10-30 19:00 第八十六层：`dl_iterate_phdr` 可用 —— 替代 `/proc/self/maps` 的更好方案

## 验证：相机 libc-2.11.2.so（984,292 B）的 dl 相关符号
```
dl_iterate_phdr@@GLIBC_2.4            ✅ 公开 API，可用
__libc_dl_error_tsd@@GLIBC_PRIVATE
__libc_dlclose@@GLIBC_PRIVATE
__libc_dlsym@@GLIBC_PRIVATE
__libc_dlopen_mode@@GLIBC_PRIVATE     ← 存在但是 PRIVATE，libc 内部用（NSS 等）
```
**⭐ 关键：im.elf 导入的是 `dlopen@GLIBC_2.4`（公开）⇒
   QEMU 线的 `__libc_dlopen_mode` 假设撤销正确 ✓**

## ★ 建议：用 `dl_iterate_phdr` 替代 `/proc/self/maps`
**QEMU 线的 maps 采样器坏了（`M2MAPS nodir`，因为 fork 子进程读不到父进程 maps）。**

**`dl_iterate_phdr` 的优势：**
```
✅ 不需要 fork
✅ 不需要 /proc 挂载
✅ 直接给 **库名 + 基址**（dli_fname / dli_addr），不用解析 maps 文本
✅ 任意时刻可调
✅ glibc 2.3+ 支持，2.11.2 已验证
```

**用法：**
```c
#include <link.h>
static int dump_one(struct dl_phdr_info *info, size_t sz, void *data) {
    out("OBJ name=%s base=0x%08x\n", info->dlpi_name, (unsigned)info->dlpi_addr);
    return 0;
}
dl_iterate_phdr(dump_one, NULL);   /* 无需 fork、无需 /proc */
```
**注意：第一个回调通常是主程序（dlpi_name 为空串）；im.elf 加载 ~70 个 .so ⇒ 约 70 行**

## ★★ 额外好处：能精确"选出走得最远的那次运行"
```
每次运行结束调 dl_iterate_phdr
⇒ 数一下加载了多少个 .so
⇒ **加载最多的那次 = 走得最远的那次**
⇒ 那次的 dlopen 序列最后几项 = 真正的断点
⚠️ 比"看 dlopen 日志长度"可靠（日志可能漏，dl_iterate_phdr 不会）
```

## 修正后的三步实验
```
1. 用 dl_iterate_phdr 替代 maps 解析
2. 同一次运行内：dlopen 钩子记 (模块名, 返回值, lr) + 结束时 dl_iterate_phdr 记全部库
3. 两相对照：
   · 两边都有 appFwBoot ⇒ **那次走到了 [110]！** ⇒ 断点在 `if(fn) fn(arg)`
   · 只有 dl_iterate_phdr 有 ⇒ 才是真仪器漏洞（再考虑 __libc_dlopen_mode）
   · 两边都没有 ⇒ 停在更早（[95]-[130]）
```

## 方法论（第 92 条）★★
**枚举"已加载对象"这类需求，优先用**标准 API**而不是解析 `/proc` 文本：
```
/proc/self/maps   ⇒ 需 /proc 挂载、需同进程、文本解析易错
dl_iterate_phdr   ⇒ 标准 API、直接给结构化数据（名字+基址）、无外部依赖
```
**⇒ 判断标准："这个信息有没有现成的 API？" 有就用 API，
   因为 API 的语义稳定，而文本格式会变、还依赖挂载状态。**
