# QEMU 实验：M2 栈在仿真硬件上的真实表现

> 环境：`/mnt/build/m2qemu/`（我的独立工作区），chocotan `-M zve10` + 内嵌 initramfs kernel
> 方法：把 `initramfs-ui/usr` 换成 M2 混合树，重打包 cpio + 重编 Image，跑同一条命令

## 一、结论（E10 基线 vs M2 混合）

| 指标 | E10 干净基线 | M2 混合 |
|---|---|---|
| `e10clean` / `m2obs` | 242,898,308 B | 255,473,028 B |
| 服务 `UI_INIT_RETURN` | **61** | **59** |
| `Application_System_Init` | — （E10 是 libObj 提供） | **RESOLVE 0x119a9271 → ENTER → RETURN 0** ✅ |
| 崩溃 | 0 | 0 |
| **帧缓冲 0x22c48000** | **采样非零 3855（有画面）** | **采样非零 0（全黑）** |
| `im.elf` 线程状态 | 875 × `cond_wait`（菜单空闲态） | 43 × `osal_rcv_msg_tmo` + 34 × `futex` |

## 二、★ 最重要的发现：M2 栈比预期走得远得多

**昨天在真机上"完全无 UI"，我以为是断在很前面。QEMU 里实测：**

```
UI_INIT_ENTER=59  UI_INIT_RETURN=59        ← 59 个服务全部初始化成功
UI_RESOLVE pid=121 name=Application_System_Init result=0x119a9271
UI_INIT_ENTER pid=121 tid=213 name=Application_System_Init arg=0x4102601
UI_INIT_RETURN pid=121 tid=213 name=Application_System_Init result=0    ← 成功返回
```

**⇒ `appFwBoot.Application_System_Init` 被正确解析、调用、返回 0。**

**⇒ 昨天我拆出的分叉（`strcmp(arg,"AppFw")` 决定 `appFw.so` vs `AdjChkConfig.so`）
没有卡住 —— 它返回 0，说明走到了 dlopen + dlsym + 建线程。**

**⚠️ 注意 `arg=0x4102601`（非 NULL，奇数地址）—— 仍需确认它指向的字符串是不是 "AppFw"，
即加载的到底是 `appFw.so` 还是 `AdjChkConfig.so`。**

## 三、⇒ 断点收窄到 `Application_System_Init` 之后

**它返回 0 之后，appFwBoot 做了什么（按反汇编）：**
```
osal_create_waitable_thread({fn=<GOT>, entry=dlsym结果, 0x01CF, 1, 0})
osal_set_cpu(thread, 15)
return
```
**线程里跑 `Main()`：**
```
uxengine_init2()                    ← libObj 提供
initializeArena()
Kernel::sys_setCurrentDirectory("/usr/share/app/")
AreaCheckExtensionManager::setCheckDataPath("/usr/share/app/area_check_data.dat")
AreaCheckExtensionManager::setLogPath("/tmp/area_check_log.dat")
GraphicsEngine::setOverflowResizeFontAttribute(28, 21)
GraphicsEngine::setOverflowResizeLayoutAttribute(0x22)
ResourceManager::setDatabase("global.xdb")
ResourceManager::sys_setFontNameWithNamespace(0x33)
entry()                             ← appFw.so 的 Application_Start
```

**⇒ 帧缓冲全黑 + 线程停在 `osal_rcv_msg_tmo`/`futex`，说明
`Main()` 里某一步之后没有进入渲染循环。**

**候选断点（按可疑度）：**
```
① arg 指向 "AppFw" 之外的字符串 ⇒ 加载了 AdjChkConfig.so（不是新菜单）
② uxengine_init2() 内部失败（libObj 的引擎初始化，依赖 /dev/dmpgles2 / dmm.ko）
③ setDatabase("global.xdb") 失败（相对路径/cwd 问题）
④ entry() 调了但 Application_Start 内部早退
```

## 四、复现方式（我的工作区）

```bash
cd /mnt/build/m2qemu
# E10 干净基线
IMAGE=work/Image.e10clean SECONDS_MAX=300 SMP=4 OUT=out/e10 ./start-m2.sh
# M2 混合（带 dlopen 插桩）
IMAGE=work/Image.m2obs SECONDS_MAX=300 SMP=4 OUT=out/m2obs ./start-m2.sh
```
**镜像：`work/Image.e10clean`（242,898,308 B）、`work/Image.m2obs`（255,473,028 B）**

**注意：一次只能跑一台 QEMU；`-smp 4`；等 3-4 分钟。**

## 五、两个已排除/已确认的点

- **`libkeyinject.so` 必须移除** —— 它拦截 `osal_rcv_msg_tmo`，427 万次刷屏会拖慢到到不了菜单（这是之前 `odl_init` 卡住的原因）
- **`libuiobserve.so` 对 M2 的 dlopen/dlsym 插桩仍然有效**（`UI_RESOLVE`/`UI_INIT_ENTER` 都正常打印）；
  但它的 libObj 绘制钩子（12 个硬编码偏移）对 M2 的 libObj 大概率不装 —— 所以没有绘制层标记，这是预期行为
