# ZV-1M2 新菜单启动协议反汇编（appFwBoot.so 全解）

> 2026-10-28 静态分析。对象：`/usr/lib/appFwBoot.so`（M2，8,196 B 级小桥）
> 目的：找出 `im.elf` 调 `Application_System_Init` 之后的确切行为。

---

## 一、符号布局

```
_init                0x1044
Application_System_Init  0x1270   ← im.elf 通过 dlsym 调用的入口
initializeArena()    0x138c
Main(void*)          0x14d8   ← 图形引擎引导，末尾 blx 回调
_fini                0x16f8
```

## 二、`Application_System_Init(const char *arg)` 完整逻辑

```
① memset(局部 32 字节缓冲, 0)
② snprintf(buf, 32, fmt, global)                  ; 拼一个配置串
③ r = osal_valloc_msg_wait(0x008F013A, 52, 1)     ; 申请 52 字节消息
④ if (r == 0):                                     ; 申请成功才发
       msg[0..1] = (u16)9                          ; ★ 消息类型 = 9
       snprintf(msg+2, 50, fmt2, <串>, buf)
       msg[51] = 0
       osal_snd_sync_msg(0x008F013A, msg)          ; ★★ 同步发送
       osal_free_msg(0x008F013A, msg)
⑤ 分支:
   if (arg == NULL || strcmp(arg, <某串>) == 0):           ← 正常启动
       h  = dlopen("appFw.so", RTLD_LAZY|RTLD_GLOBAL)      ; 0x101
       fn = dlsym(h, "Application_Start")
   else:                                                    ← 调整模式
       h  = dlopen("AdjChkConfig.so", RTLD_LAZY|RTLD_GLOBAL)
       fn = dlsym(h, "_ZN13app_adjchk_ui9main_loopEv")
                                              ; = app_adjchk_ui::main_loop()
⑥ 组线程参数: name="AppFw", size=4, fn=fn, 0x1cf, 1, 0
⑦ osal_create_waitable_thread(&参数)
⑧ osal_set_cpu(thread, 15)        ; 绑到 CPU 15
⑨ 立刻返回（不等线程）
```

**关键结论：**
- `appFw.so` / `Application_Start` 的名字与我们的 hybrid 完全吻合 ✓
- `AdjChkConfig.so` 不是依赖，而是**校准（adjust）模式**的替代入口
- 启动期间会发一条 **type=9 的同步消息到 id 0x008F013A**

## 三、`Main(void *cb)` = 图形引擎引导

```
uxengine_init2()
initializeArena()
sys_setCurrentDirectory("/usr/share/app/")                 ← 工作目录
gfxeng() → GraphicsEngine::getAreaCheckExtensionManager()
    if (manager) {
        setCheckDataPath("/usr/share/app/area_check_data.dat")
        setLogPath("/tmp/area_check_log.dat")               ← /tmp 必须可写
    }
GraphicsEngine::sys_getInstance()
    setOverflowResizeFontAttribute(28, 21)
    setOverflowResizeLayoutAttribute(0x22)
resmgr() → ResourceManager::setDatabase("global.xdb")       ← ★ 必读 /usr/share/app/global.xdb
resmgr() → sys_setFontNameWithNamespace(0x33 = 51)
if (cb) blx cb                                              ← 交回 appFw.so 继续
```

**⇒ Main() 只做"图形引擎 + 资源库 + 字体"三件事，然后回调。
新菜单能否出现，取决于 `global.xdb` 与 `.uxc` 资源能否被这套引擎解析。**

## 四、资源依赖核对（两版对比）

```
文件                              E10          M2           hybrid 取值
area_check_data.dat             1,174,103   1,142,255     M2 ✓
area_check_data_vertical.dat      缺失      1,024,593     M2 ✓  ← ★ M2 独有（竖屏 UI）
global.xdb                        12,252      16,112      M2 ✓  ← ★ 必须 M2 版
fontlist.dat                         360         360      同（内容一致）
style_cmn.uxc                      1,888       2,004      M2 ✓
字体 .ltt                        /usr/share/app/ 与 /tmp/images/         两版一致
```

**⇒ 这些资源全在 `/usr/share`，我们的 hybrid 已整体取 M2 版 ✓**

## 五、兼容性硬指标（全部通过）

| 检查项 | 结果 |
|---|---|
| glibc 版本需求 | 两版库均 ≤ `GLIBC_2.4`，相机有 2.11.2 ✓ |
| 浮点 ABI | 两版均**无** `Tag_ABI_VFP_args`（软浮点），与 `ld-linux.so.3` 一致 ✓ |
| `im.elf` 符号闭包 | 需要 64 个，只缺 `_Jv_RegisterClasses` / `__gmon_start__`（CRT 弱符号，可缺）✓ |
| `bootin.elf` | 同上，缺 2 个弱符号 ✓ |
| `sen.elf` | 同上，缺 2 个弱符号 ✓ |
| IMDB 三符号 | `IMDB_find_entry` / `IMDB_find_target_bit` / `IMDB_get_entries` 全部提供 ✓ |
| `libosal_uipc.so` | 两版同大小、同 69 个导出符号 ✓ |
| `osal_snd_sync_msg` | 两版**字节完全相同**（薄封装，尾调 `u_osal_snd_sync_msg`）✓ |
| `bootin.elf`（两版） | 大小相同、字符串集 318 条零差异，仅 `.rodata` 顺序不同 ⇒ 功能等同 ✓ |
| 模块覆盖 | hybrid 与 M2 原厂相比多缺 **0** 个 ✓ |
| 入口 ABI | 两版 `Application_System_Init` 均只用 R0 ✓ |

## 六、静态分析到此为止的结论

```
所有可静态验证的兼容性指标全部通过：
  符号、版本、ABI、资源、模块表、入口签名、启动器行为

五个硬件维度（USB 芯片 / 电源开关 / ND / settings / 镜头）的差异
  全部落在【启动后的功能层】，没有一项位于启动必经路径

⇒ 静态分析无法再前进；剩余嫌疑只能靠运行时观测
⇒ 最可疑的三个点（按优先级）:
   1. 那条 type=9、id=0x008F013A 的同步消息——若无人应答且 osal 阻塞，
      Application_System_Init 不返回 ⇒ im.elf 卡住 ⇒ 无 UI、无日志（吻合现象）
   2. global.xdb(16,112 B) 与 .uxc 能否被 UXEngine 解析（资源格式代差）
   3. appFw.so 64 MB 在 128 MB 内存机器上的加载压力（内存不足 ⇒ 静默失败）
```

## 七、下一步（运行时观测，仍需一次引导）

```
① 在 E10 上抓 im.elf 的 UIPC 队列统计（osal 队列数）
   —— 正常启动 vs M2 应用层启动，对比队列创建情况
② 用 LD_PRELOAD 拦 osal_snd_sync_msg / osal_valloc_msg_wait
   —— 记录 id、返回值、是否阻塞（带超时看门狗）
③ 用 LD_PRELOAD 拦 dlopen/dlsym
   —— 确认 appFwBoot.so 是否被真正 dlopen、Application_System_Init 是否被调用
④ 记录 /proc/<pid>/status 的 VmRSS，看 appFw.so 64 MB 加载是否 OOM
```
