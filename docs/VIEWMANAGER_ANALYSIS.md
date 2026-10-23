# ViewManager 为什么建不出来：已确认的事实与决定性实验

> 离线静态分析（基于真机 `libObj.so`，md5 `12cf982d…`，已与真机逐字节核对）。
> 目的：给 QEMU 侧一个可直接执行的判定路径，而不是继续盲猜。

---

## 一、`Application_System_Init` 自己**不建** ViewManager

反汇编（`0x8a2409`，144 B）确认它只做三件事：

```
1) snprintf(buf, 32, fmt, arg)          ; 拼一个名字
2) bl 0x8a2314                          ; createRequestModelExecuteEvent(0, buf)
3) ldr r0, [GOT + 0x6CF8]               ; 取一个描述符指针
   blx osal_create_thread(r0, ...)
   blx osal_set_cpu(thread, 15)
```

**它只是"发一个请求 + 起一个线程"，真正的 ViewManager 构造在别处。**

⇒ 所以"`Application_System_Init` 返回了但 `pViewManager` 仍是 0"**不是矛盾**，
而是**预期行为**：它返回不代表 ViewManager 已建好。

## 二、关键全局：`pViewManager`

```
符号: pViewManager    vaddr 0x0149221c    (GOT 槽 0x013dc7b4)
```

这就是 QEMU 侧监控的 `pVM`。它是**唯一**的权威判定量。

## 三、线程描述符的位置（可直接观察）

`Application_System_Init` 的 `osal_create_thread` 第一个参数来自 GOT 槽
`vaddr 0x013DE920`（`R_ARM_RELATIVE`，addend = `0x01455F24`）⇒ 描述符在
**`libObj + 0x1455F24`**（`.data`，**静态初始化**，文件里就能读到）：

```
+0x00  0x01033f63    ; 指向一个字符串
+0x04  0x00000004
+0x08  0x008a2399    ; 指向 createRequestModelExecuteEvent 附近的代码
+0x10  0x00000083
+0x14  0x00000001
+0x18  0x01033fa8    ; 另一个字符串指针
+0x1c  0x00000002
+0x2c  0xffffffff
```

⇒ 想在 QEMU 里确定"线程到底起没起、起的是谁"，直接读
`libObj+0x1455F24` 及其指向的字符串/代码即可（不需要再猜）。

## 四、★ 卡点的性质：**消息池耗尽，不是"等人回复"**

`createRequestModelExecuteEvent`（`0x8a2314`）的第一件事：

```
movs r2, #52
blx  osal_valloc_msg_wait@plt      ← 阻塞点
cbnz r0, <错误分支>
```

而 `osal_valloc_msg_wait` 的实现是
`u_osal_valloc_msg_wait → vpool_alloc → alloc_fblock/free_fblock`
⇒ **它是在消息内存池里取一块，池空才阻塞**；不是 `osal_rcv_msg` 那种等应答。

结合 QEMU 侧实测 `/proc/osal/uipc`：

```
fblock: (19456) free=266752      → 266752 / 19456 ≈ 13 块
队列 0x13A 积压 428 条（占 441 条的 97%）
```

⇒ **推定因果链**：

```
0x13A 无人消费 → 428 条消息各占一个池块 → 池只剩 ~13 块
   → Appframework 线程起来第一件事 valloc_msg_wait(52) 阻塞
   → 线程挂住（与实测 futex 一致）
   → ViewManager 永远不被构造 → pViewManager 保持 0
```

## 五、决定性实验（一步定性）

**在 QEMU 里把队列 0x13A 的消息释放掉，然后观察：**

| 结果 | 结论 |
|---|---|
| `osal_valloc_msg_wait` 立刻返回，`pViewManager` 变成非 0 | ✅ 池耗尽假设成立，问题定位在"0x13A 无消费者" |
| 仍然阻塞 / 仍为 0 | ❌ 池不是原因，回去查线程是否真被创建（读 `libObj+0x1455F24` 描述符与线程表） |

辅助观测（都很便宜）：
- 在 `osal_valloc_msg_wait` 前后各读一次 `fblock` 的 `free` 值 —— 若调用期间 free 不降反不降，说明根本没进分配
- 读 `pViewManager`（`libObj+0x0149221c`）在 `Application_System_Init` 前后各一次，确认时序

## 六、附带确认：`Application_System_Init` 的调用者是谁

`libObj.so` **导出** `Application_System_Init`，且 `im.elf` 里有该字符串 →
im.elf 通过 `dlopen("/usr/lib/libObj.so")` + `dlsym` 拿到并调用。
但 **im.elf 是否调用它取决于 IMDB 判定**（此点已由 QEMU 侧实测：不调它则 `pVM` 恒为 0）。

⇒ 两条独立的失败可能都要排除：
1. **没被调用**（IMDB 判定/加载顺序）—— 已实测：主动调用后仍为 0 ⇒ 排除
2. **调用后被池阻塞**（本文推断）—— 待第五节实验判定

## 七、给"新菜单"的意义

新菜单（ZV-1F `appFw.so`）**不使用 `pViewManager`**（它是单体 UI 架构），
但它同样依赖 `libObj` 提供的基础设施（`InfraInput` / `ux::wgtsys` / MWF 事件）。
⇒ **只要旧菜单这条链能通，`libObj` 侧的平台能力就被证明是完整的**，
新菜单移植的接缝就少一大块。

---

# 补充（2026-10-24 第二轮）：`uxengine_init` 才是管理器构造入口

> QEMU 侧给出了两处修正，本文接受并据此重定向分析。

## 〇、更正：消息池耗尽假说**不成立**

QEMU 侧加强 hook 后实测：`osal_valloc_msg_wait` **ret=0（成功）**、
out 指针每次都是有效堆指针、耗时 0–10 ms ⇒ **从未阻塞**。

我此前按"19456 = 块大小"折算"只剩 13 块"是**误读**：`fblock: (19456) free=266752`
的含义是 **`(已用块数) free=空闲字节`**。266,752 空闲字节 ÷ 52 字节/块 ≈ 5000+ 块。

⇒ **"池耗尽 → 线程阻塞"整条链作废**，`0x13A` 排空方案也无必要。

## 一、线程入口：`libObj + 0x8A2398`（正是描述符 `+0x08` 指向处）

```
8a239e: bl 0x6271D4          ; ★ uxengine_init
8a23a2: snprintf(buf,32,...)
8a23d4: bl 0x8A2314          ; createRequestModelExecuteEvent(1, buf)
8a23d8: mov r0, r7           ; 进事件循环
```

⇒ **引擎初始化就在这个线程体里**，"线程阻塞"与"管理器不建"是同一件事。

## 二、★ `uxengine_init`（`libObj+0x6271D4`）做了什么

```
r1 = 640, r2 = 480                      ; 引擎分辨率 640×480 = ZV-E10 面板(921,600 点)
bl  0x610F74   ux::resmgr::ResourceManager::getImageSize(...)
bl  0x627164   （TextSprite 区域内的未命名函数）
bl  0x686440   grm_res                  ; ★ GRM 资源层（ZV-E10 上在 libObj 内部）
bl  0x63D52C   ux::core::TextLayout::registerFontFaceFile(...)
blx UXEngine::gfxeng()                  ; ★ 取 GraphicsEngine
blx GraphicsEngine::getAreaCheckExtensionManager()
    if (结果 != NULL) {
        setCheckDataPath("/tmp/area_check_data.dat")   ; ★ 注意是 /tmp
        setLogPath("/tmp/area_check_log.dat")
    }
后续：三次 selector(5 / 11 / 12) 查询，结果转 bool 传给 GraphicsEngine 的三个设置函数
```

**函数内没有 `dlopen`** ⇒ `viewUnified*.so` 的加载不在这一步（应在 view 创建时按名加载）。

## 三、★ 新发现的具体依赖：`/tmp/area_check_data.dat`

`uxengine_init` 让 AreaCheckExtensionManager 从 **`/tmp/`** 读区域检查数据，
而源文件在 **`/usr/share/app/area_check_data.dat`**（UXAC，10000 条）。

⇒ **开机时必须有东西把 `/usr/share/app/area_check_data.dat` 复制到
`/tmp/area_check_data.dat`**，否则 `AreaCheckExtensionManager` 拿不到数据。

**这是 QEMU 里立刻可查的一项**：
- `/tmp/area_check_data.dat` 在 QEMU 的 guest 里存在吗？
- 谁负责复制？（很可能在 `im.elf` / `bootin.elf` 或某个 init 脚本里）
- 若缺失，先手工放进 `/tmp` 再跑一次，看 `getAreaCheckExtensionManager()` 是否还返回 NULL

## 四、建议的 hook 点（按性价比）

| 观测 | 意义 |
|---|---|
| `UXEngine::gfxeng()` 返回值 | 为 NULL ⇒ 引擎单例没建，后续必然失败 |
| `GraphicsEngine::getAreaCheckExtensionManager()` 返回值 | 为 NULL ⇒ area-check 路径被跳过（说明 GraphicsEngine 未构造完） |
| `/tmp/area_check_data.dat` 是否存在 | 上面那条路径能否生效的前提 |
| `uxengine_init` 是否被调用到 | 若根本没进这个函数，说明线程没起来 |

注意：`uxengine_init` 里分辨率是**硬编码 640×480**，不是运行时探测面板
（与"新菜单 appFw 运行时探测面板"不同）——这对旧菜单是正常的。
