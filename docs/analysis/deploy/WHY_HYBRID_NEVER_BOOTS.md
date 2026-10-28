# hybrid 为什么起不来：appFwBoot 的完整启动链拆解

> 方法：把 `appFwBoot.so`（8,312 B，`.text` 仅 0x504 B）完整反汇编，解出它真正的启动逻辑。
> **结论：修正了一个此前的错误假设，并定位到三个新的可能断点。**

## 一、★ 修正：`osal_valloc_msg_wait` 是【分配】不是【接收】

```asm
Application_System_Init(arg):
  memset(buf, 0, 32)
  snprintf(buf, 32, <fmt>, <GOT值>)          ; 组一个名字
  osal_valloc_msg_wait(<queue>, &msg, 52, 1) ; ★ 分配 52 字节消息（wait=等内存，不是等消息）
  if (状态==0):
      填充 cmd=9 + 50 字节 → osal_snd_sync_msg → osal_free_msg
```

**⇒ 之前"appFwBoot 卡在 osal_valloc_msg_wait 等 im.elf 发 52 字节消息"的假设，
在语义上不成立 —— 那是分配消息，不是接收消息。**

## 二、★★ 真正的链（完整解出）

```asm
Application_System_Init(arg):
  ...上面那段...
  if (arg == NULL || strcmp(arg, "AppFw") == 0)
        h   = dlopen("appFw.so", 0x101)          ; 0x101 = RTLD_LAZY|RTLD_GLOBAL
        sym = <Application_Start>
  else
        h   = dlopen("AdjChkConfig.so", 0x101)   ; ★ 调整检查 UI
        sym = "_ZN13app_adjchk_ui9main_loopEv"
  entry = dlsym(h, sym)

  ; 组线程参数结构（栈上 r7+32）:
  ;   [+0x00]=<GOT 函数指针>  [+0x04]=4
  ;   [+0x08]=<GOT 值>        [+0x0C]=entry   ← dlsym 结果
  ;   [+0x10]=0x01CF(u16)     [+0x14]=1      [+0x18]=0
  osal_create_waitable_thread(&param)           ; ★ 建线程，不直接调用
  osal_set_cpu(thread, 15)                      ; 绑到 CPU 15
  return
```

**⇒ 线程入口 `Main(param)`：**

```asm
Main(arg):
  uxengine_init2()                                        ; ← libObj.so 提供
  initializeArena()                                       ; ← appFwBoot 自己（332 B）
  Kernel::sys_setCurrentDirectory("/usr/share/app/")
  AreaCheckExtensionManager::setCheckDataPath("/usr/share/app/area_check_data.dat")
  AreaCheckExtensionManager::setLogPath("/tmp/area_check_log.dat")
  GraphicsEngine::setOverflowResizeFontAttribute(28, 21)
  GraphicsEngine::setOverflowResizeLayoutAttribute(0x22)
  ResourceManager::setDatabase("global.xdb")              ; ★ 相对当前目录
  ResourceManager::sys_setFontNameWithNamespace(0x33)
  if (arg) arg()                                          ; ★ 调用 entry
```

## 三、✅ 已排除：资源文件全部到位

**`Main` 需要的文件，混合镜像里都有且与 ZV-1M2 原版【字节一致】：**

| 文件 | 混合镜像 | ZV-1M2 原版 |
|---|---:|---:|
| `area_check_data.dat` | 1,142,255 | 1,142,255 ✅ |
| `area_check_data_vertical.dat` | 1,024,593 | 1,024,593 ✅ |
| `global.xdb` | 16,112 | 16,112 ✅ |
| `color_cmn.uxc` | 388 | 388 ✅ |
| `style_cmn.uxc` | 2,004 | 2,004 ✅ |
| `lang_cmn.uxc` | 1,048 | 1,048 ✅ |
| `image.uxb` | 2,281,172 | 2,281,172 ✅ |

**⚠️ 注意：ZV-E10 原厂 `/share/app` 里【没有】`area_check_data.dat` ——
老菜单完全不用 AreaCheck。新菜单必需。**

**`uxengine_init2` 的提供者：`libObj.so` 与 `appFwBoot.so` 都导出它 ✓**

## 四、⇒ 三个新的可能断点（按优先级）

### 断点 1【最可疑】参数分叉走错分支 → 加载了 AdjChkConfig 而不是 appFw

**`strcmp(arg, "AppFw")` 决定加载哪个 UI。**
**libIMDB 里有一组模式串：**
```
usbj / resub / adjust / test / IMDB:default
```

**⇒ 若 `arg` 不等于 `"AppFw"` 且非 NULL ⇒ 走 `AdjChkConfig.so` 分支
⇒ 加载的是「调整检查 UI」而不是新菜单 ⇒ 表现为"完全无 UI"。**

**⚠️ `AdjChkConfig.so` 在混合镜像里【存在】（82,440 B，与 M2 原版一致），
所以这条分支不会因缺文件而报错 —— **它会静默加载一个错误的 UI**，症状正好是"没有菜单"。**

**⇒ 这是目前最符合"完全无 UI 且无报错"现象的假设。**

### 断点 2：线程创建后 `Main` 内部失败，但没人检查返回值

**`osal_create_waitable_thread` 返回后 `Application_System_Init` 直接 return，
**不等待线程、不检查线程内的错误**。**
**⇒ 线程内 `uxengine_init2()` / `setDatabase("global.xdb")` 任一失败，
**上层完全看不到** ⇒ 表现为"启动成功但无 UI"。

### 断点 3：内存

**`appFw.so` 是 **61 MB**（64,273,528 B）的共享库。
真机报过 `MemTotal 128,796 kB / MemFree 2,904 kB`（`KEMCO.TXT`）。**
**⇒ 若进程地址空间或可用堆不够 mmap 这 61 MB，`dlopen` 会失败。**
**⚠️ 但 ZV-1M2 在同世代硬件上跑得起来 ⇒ 内存布局应可行，
除非**混合镜像用了 ZV-E10 的内核内存预留，与 M2 的预期不同**。**

## 五、⇒ 能唯一化这三个假设的运行时证据

| 观察 | 能排除什么 |
|---|---|
| `Application_System_Init` 是否被调用（GOT/PLT 断点或日志） | 断点 1 之前的所有问题 |
| **`dlopen` 返回的 handle 是 appFw.so 还是 AdjChkConfig.so** | **断点 1（决定性）** |
| `Main` 线程是否被创建（`/proc/<pid>/task` 数量） | 断点 2 |
| `dlopen("appFw.so")` 返回值 | 断点 3 |
| `/proc/<pid>/maps` 里有没有 appFw.so 的一段 | 综合 |

**⇒ 最省事的一个探针：在 `appFwBoot.so` 的 `Application_System_Init` 入口
打一行日志输出 `arg` 的实际值。**
**⇒ 若它不是 NULL 也不是 "AppFw" ⇒ 断点 1 确认。**

## 六、待确认（我方未能解出）

**`libIMDB` 传给 `Application_System_Init` 的参数从哪来 —— 我的 PC 相对扫描器
没定位到那个 `adr`，需要换方法（如 objdump 全量 + 手工跟 .plt 调用点）。**

**但 `libIMDB` 里那组模式串（`usbj/resub/adjust/test/IMDB:default`）强烈提示
参数来自【启动模式字符串】。**
