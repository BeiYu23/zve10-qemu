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

---

# 补充（第三轮）：GraphicsEngine 单例在 libObj 里**没有任何创建点**

> 依据 QEMU 侧钉到的变量 `libObj+0x145CAB8` 与 GOT 槽 `libObj+0x13DCB78`。

## 一、先纠正两个符号事实

| 符号 | 地址 | 实际内容 |
|---|---|---|
| `uxengine_init` | `0x6271CC` | **空桩**：`push {r7,lr}; add r7,sp,#0; pop {r7,pc}` |
| （QEMU 实际调用的） | `0x6271D4` | 紧跟其后的**未命名函数** —— 它才假定引擎已存在（所以直接调会 trap） |
| `uxengine_app_fw_init` | `0x627335` | 也是 **6 字节空桩** |

## 二、`UXEngine::gfxeng()` 是纯转发

```
0x610FEC  UXEngine::gfxeng():
    push {r7,lr}; add r7,sp,#0; ldmia sp!,{r7,lr}
    b.w  GraphicsEngine::sys_getInstance()      ← 尾调用，无任何创建逻辑
```

而 `GraphicsEngine::sys_getInstance()`（`0x61229C`）是纯 getter：

```
ldr r3,[GOT+0x4F50]     ; r3 = 0x145CAB8  (&s_instance)
ldr r0,[r3,#0]          ; r0 = s_instance
```

⇒ **两者都没有惰性创建。**

## 三、★ 决定性：`0x145CAB8` 在 libObj 里只有 5 处代码引用，**无一写入非 0**

用 GOT 偏移字面量 `0x4F50`（及其 ±2/±4 变体）全文搜索，代码侧命中：

| 文件偏移 | 作用 |
|---|---|
| `0x6122B0` | `sys_getInstance` 的字面量池 → **读** |
| `0x612500` / `0x612520` / `0x612540` | 三处内联 getInstance + 读成员 `[inst+0x220]` → **读** |
| `0x613130` / `0x6131C4` | **销毁路径**：`free(s_instance); s_instance = 0`（`0x613128: str r3,[r4]`，r3=0） |

另外：
- `0x145CAB8` 落在 **`.bss`**（`.bss` 起点 `0x1459328`）⇒ 加载时恒为 0
- 绝对地址字面量 `0x0145CAB8` 全文只有 **1 处**，就是 GOT 槽本体（`0x13d4b78`）
- 该类**没有 vtable / typeinfo**（非多态），也没有导出构造函数

⇒ **libObj 自己既没有构造函数调用，也没有任何 store 非 0 的写入点。**

## 四、对照：`ResourceManager` 用的是**另一种单例惯用法**

```
0x610218  ResourceManager::sys_getInstance():
    r0 = *(GOT+0)
    if (r0 == 0) r0 = &<静态回退对象>
    return r0
```
即 **弱指针 + 静态默认实例**。而 `GraphicsEngine` 是 **纯指针，无回退**。

⇒ 这个差异说明：`GraphicsEngine` 是**必须由外部先创建好**的，
`ResourceManager` 则自带默认实例。两者设计前提不同。

## 五、结论与给 QEMU 的两条可执行路径

**结论**：`GraphicsEngine` 的实例**不由 libObj 创建**。可能原因（按可能性）：

1. **PLT 介入（interposition）**：libObj 内部调用 `sys_getInstance` 走的是
   `0x108138` 这个 **PLT 桩**，不是直接调用本文件定义。若加载顺序里另有对象
   导出同名符号，就会绑定到那份（而那份可能带惰性创建）。
2. **创建者在外部**（im.elf / 其它库），通过我们尚未定位的路径。

**路径 A（最便宜，能一步定性）**：LD_PRELOAD 一个
`_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv` 的 interposer，
在首次调用时打印 `dladdr()`（**谁真正提供这个符号**）+ 调用栈。
- 若解析到 libObj → 它确实没有创建者，需要在预加载里自己 new
- 若解析到别的库 → 那个库才是入口，直接去看它

**路径 B**：在 `.init_array`（`vaddr 0x1365DD4`，**272 个**初始化函数）里
筛出会调用 `operator new`/`malloc` 且随后 `str` 到某个 `.bss` 全局的函数。
（272 个不多，但要写脚本批量反汇编筛选。）

## 六、附带确认

- `uxengine_init` 真身（`0x6271D4`）第一步就拿 `UXEngine::gfxeng()` 的返回值用，
  所以引擎为 0 时**必然 trap** —— 与 QEMU 实测「直接调它 → 0x0e」完全一致。
- `area_check_data.dat` 从未被打开，与此互相印证：**引擎初始化一步都没进去**。

---

# 补充（第四轮）：穷尽搜索结论 —— **全固件没有任何地方创建 GraphicsEngine**

> 承接 QEMU 侧："在 libObj 里找写 `0x145CAB8` 的代码"。

## 一、穷尽验证（五个方向，全部为负）

| 方向 | 做法 | 结果 |
|---|---|---|
| 1. GOT 槽字面量 | 搜 `0x4F50` 及 ±2/±4 变体 | 代码侧仅 **5 处**：4 处读、1 处写 **0**（销毁） |
| 2. 绝对地址字面量 | 搜 `0x0145CAB8` | 全文 **1 处**，位于 `.rel.dyn`（就是那个 RELATIVE 重定位），**非代码** |
| 3. 换库定义（PLT 介入） | 扫 `/usr/lib` **全部 202 个库** | **只有 libObj 定义** `sys_getInstance`；`viewUnified2.so` 仅导入 |
| 4. 其它分区 | `nflasha2/3/7/10` 全文搜符号名 | **0 处** |
| 5. 多个 libObj 版本 | 列 `/usr/lib` 下 `libObj*` | **只有 1 个** `libObj.so` |

**补充**：`.init_array`（`vaddr 0x1365DD4`，272 个）没有任何函数引用该 GOT 槽 ——
第 1 项已覆盖（写变量必须先拿到 `&var`，而拿到 `&var` 的唯一途径就是那个 GOT 槽）。

⇒ **结论：这个固件里不存在 GraphicsEngine 的创建点。**
不是"没找到"，是**确实没有**。

## 二、这对 QEMU 意味着什么

**不要再找了**。要继续，必须**自己引导（bootstrap）**。

`GraphicsEngine` 是**非多态类**（无 vtable/typeinfo），所有访问都是
`inst->成员` 的定点偏移读取 ⇒ **一个零初始化对象就能让大部分代码继续跑下去**。

### 建议的手工引导

```c
// 1) 分配一个零块，长度取够（成员最大偏移观测到 0x234）
void *p = calloc(1, 0x400);
// 2) 写入单例变量
*(void **)((char *)libObj_base + 0x145CAB8) = p;
// 3) 让它继续跑，看下一个崩在哪，逐个填成员
```

### 已观测到的成员偏移（供迭代填坑）

| 偏移 | 用途 | 来源 |
|---|---|---|
| `+0x74` | 内嵌子对象，`updateResource` 里取它的地址 | `add.w r0, r4, #116` |
| `+0x220` | **指针**；访问器返回 `*(u32*)(inst+0x220)` 并读其 `+0x20` / `+0x40` | `ldr.w r0,[r3,#544]` + `adds r0,#32` |
| `+0x234` | **指针**；`updateResource` 里 `free()` 后置 0 | `ldr.w r0,[r4,#564]` → `blx free` → `str r5,[r4,#564]` |

⇒ 迭代策略：先全零 → 崩在哪看我列的这三个成员 → 给 `+0x220`/`+0x234` 填
`malloc` 出来的最小结构或 NULL（若代码对 NULL 有判空）→ 继续。

## 三、一个可能需要重新审视的前提

`viewUnified2.so` 导入的是 `UXEngine::gfxeng()` / `sys_getInstance()`（**只读**），
`uxengine_init` / `uxengine_app_fw_init` 又都是**空桩**。

⇒ **合理怀疑**：这一版 `libObj` 可能是"引擎由外部宿主创建"的变体，
而那个宿主（正常应该是 `im.elf` 或另一个初始化组件）在 QEMU 环境里**没有执行到**。

**这是一个可验证的方向**：查 `im.elf` / `bootin.elf` 里是否有对
`ux::gfxeng::GraphicsEngine` 构造相关的调用（例如通过某个导出工厂函数），
或者查正常启动时 `libObj` 的 `.init_array` 是否有某个函数因前置条件不足而提前返回。

> 但注意：第 1 项的穷尽搜索已证明"写变量"的代码不存在于 libObj，
> 所以若真有宿主，它也只能通过 **`sys_getInstance()` 之外的另一个导出接口** 来完成，
> 而那样的接口在 `dynsym` 里并不存在（GraphicsEngine 只有 6 个导出，无创建类）。

---

# 补充（第五轮）：`GraphicsEngine` 对象布局（供 QEMU 手工构造）

> 方法：反汇编该类全部导出方法，getter/setter 读写的偏移即成员偏移。
> 全部地址基于 `libObj.so`（md5 `12cf982d…`）。

## 一、已确定的成员布局

| 偏移 | 宽度 | 类型/用途 | 证据 |
|---|---:|---|---|
| `+0x3C` | ≥0x28 | **内嵌容器**（资源列表） | `updateResource`: `r5 = inst; adds r5, #0x3c` 后传给 `0x61289C` |
| `+0x60` | 4 | 该容器内字段，**NULL 会打错误日志但继续** | `0x61289C`: `ldr r3,[r0,#0x24]`（r0=inst+0x3c）；为 0 → `Log::write(0x40000000, …)` 后仍往下走 |
| `+0x220` | 4 | **指针** → 字体/布局度量结构（≥0x40 字节） | 三个访问器分别返回 `P`、`P+0x20`、`P+0x40`（`P = *(inst+0x220)`） |
| `+0x234` | 4 | **指针** → `AreaCheckExtensionManager` | `getAreaCheckExtensionManager()` = `ldr.w r0,[r0,#564]`；`updateResource` 里 `free` 后置 0 |
| `+0x239` | 1 | `setOverflowResizeFontAttribute` 第 1 参 | `strb.w r1,[r0,#569]` |
| `+0x23A` | 1 | 同上第 2 参 | `strb.w r2,[r0,#570]` |
| `+0x23B` | 1 | 标志 = 1 | `strb.w r3(=1),[r0,#571]` |
| `+0x23C` | 1 | `setOverflowResizeLayoutAttribute` 参数 | `strb.w r1,[r0,#572]` |
| `+0x23D` | 1 | 标志 = 1 | `strb.w r3(=1),[r0,#573]` |

⇒ **对象大小 ≥ `0x23E`，按 4 对齐取 `0x240`。**

**注意**：`getFramebufferWidth/Height()` 并**不读** GraphicsEngine 的成员 ——
它们是尾调用（`0x63D2D8` / `0x63D2F0`），走**另一个对象的 vtable**
（`*(inst)` → `+0x5c` → `blx` → 取 `*(u16*)(r0+0)` / `(r0+2)`）。
⇒ 这也再次印证 GraphicsEngine **本身没有 vtable**（类内无虚函数）。

## 二、手工构造的最小可用方案

```c
void *inst = calloc(1, 0x240);          // 全零
// 1) +0x234 留 NULL ⇒ getAreaCheckExtensionManager() 返回 NULL
//    ⇒ uxengine_init 会跳过 setCheckDataPath/setLogPath（那里本来就有 NULL 判断）
// 2) +0x220 留 NULL ⇒ 三个度量访问器返回 NULL / +0x20 / +0x40
//    （若后续解引用它而崩，再换成 malloc(0x60) 的零块）
// 3) +0x60 留 NULL ⇒ 只会打一条错误日志，不中断
*(void **)((char *)libObj_base + 0x145CAB8) = inst;

// 然后调用 uxengine_init 的真身
((void (*)(void))((char *)libObj_base + 0x6271D4))();
```

**预期顺序**：`resmgr::getImageSize` → `grm_res` → `registerFontFaceFile`
→ `UXEngine::gfxeng()`（这次返回非 NULL）→ `getAreaCheckExtensionManager()`
（返回 NULL，跳过 area_check 两行）→ 三次 selector(5/11/12) 查询。

## 三、观察点（判断是否推进了）

| 现象 | 含义 |
|---|---|
| 不再 trap `0x0e` | 引擎已存在，`uxengine_init` 真身开始执行 |
| 日志出现 `setCheckDataPath` 之后的 selector | 已过 area_check 分支 |
| `pViewManager`（`libObj+0x0149221C`）变化 | 链路继续到 ViewManager 构造 |
| 新增 `TextLayout failed` / `Log::write(0x40000000,…)` | 走到 `+0x60` 的判空逻辑（预期路径） |

## 四、一条需要提前知道的限制

即使引擎手工建成、`uxengine_init` 真身跑通，**ViewManager 的创建仍在别处**
（`Application_System_Init` 只发请求 + 起线程；线程体里 `createRequestModelExecuteEvent(1, …)`
发出的是"请求某个 model 执行"，其消费者仍未定位）。

⇒ 手工引导能**验证引擎链**，但**不保证直接出现 UI**。这一步的价值是：
把"引擎初始化能不能跑"这个问题单独隔离出来验证，而不是继续被"创建点不存在"堵死。

---

# 补充（第六轮）：`0x145CBC4` 回调表 + selector 函数 + futex 重新解读

## 一、`libObj+0x145CBC4` 是什么

**它不是 GOT 变量，而是用"直接 PC 相对寻址"访问的模块内静态指针**：

```asm
ldr  r3, [pc, #16]        ; 字面量是"相对增量"，不是绝对地址
add  r3, pc               ; → 得到绝对地址 0x145CBC4
ldr  r3, [r3, #0]         ; r3 = *(0x145CBC4)  ← 那个指针
ldr  r3, [r3, #0x48]      ; 取回调
cbz  r3, skip
blx  r3                   ; 非 NULL 才调用
```

⇒ **它是一个"可选回调表"对象指针**：表内是函数指针，槽位见到 `+0x3C` / `+0x48` / `+0x4C` / `+0x5C` 等。
⇒ 语义是 **"宿主若装了钩子就调用，没装就静默跳过"**。
⇒ `getFramebufferWidth/Height`（`0x63D2D8`/`0x63D2F0`）正是通过它 + `+0x5C` 的槽去拿 framebuffer 尺寸。

**为什么之前搜不到**：它是**直接 PC 相对寻址**，文件里存的是"相对增量"，不是绝对地址
`0x0145CBC4`（后者出现 0 次）。这也解释了为何按 GOT 槽搜只找到 5 处。

> 我后来用"相对增量反解"的方法搜，`0x145CAB8` 出现 3 处疑似引用，
> 逐个核实均为**假阳性**（差值 0x145CAB4 / 0x145CABC，差 4 字节），
> 故"libObj 内无引擎创建点"的结论**仍然成立**，且已被 QEMU 侧 60 次采样实测独立确认。

## 二、selector 函数 `0x640220` 是什么

```asm
640220:
  r4 = r0                                  ; selector
  r3 = *(GOT+…)                            ; 某全局对象
  r0 = *(r3 + 0x14)                        ; 子对象
  if (r0 == 0) return r2                   ; ★ 没有后端就直接返回，不阻塞
  r3 = *(r0)                               ; vtable
  r3 = *(r3 + 0x1C)                        ; 虚函数 @0x1C
  r0 = r3(selector=r4, r2)                  ; 调用
  return r0
```

⇒ 它是**虚拟分发**：把 selector 转给 `global->[+0x14]` 这个后端对象的 `vtbl[+0x1C]` 方法。
⇒ `uxengine_init` 里 `selector 5 / 11 / 12` 是三次**特性查询**（结果转 bool 传给 GraphicsEngine setter）。
⇒ **后端为空时直接返回、不阻塞** —— 所以 `uxengine_init` 不是阻塞点（事实也证明它跑完了）。

## 三、★ futex 需重新解读

`Appframework 线程 futex_wait_queue_me` **可能根本不是 bug**：

该线程体在 `uxengine_init` 之后是
`snprintf → createRequestModelExecuteEvent(1, buf) → 进入循环`。
一个**事件循环线程在空闲时本来就该坐在 futex 上等消息队列**。

⇒ 需要区分的是：

| 现象 | 含义 |
|---|---|
| 线程**能建管理员**但空闲在 futex | **正常**，只是没人给它派活 |
| 线程在调管理员构造**之前**就卡住 | 真 bug |

**建议的判别方法**：不要只看内核 `futex_wait_queue_me`，而要抓**用户态栈**
（`/proc/<tid>/stack` 只有内核栈；需要 gdb 或 walk 用户栈）
看它停在 `osal_rcv_msg`（= 正常空闲）还是别的调用里。

配套观察：若线程空闲在 `osal_rcv_msg`，那么问题变成
**"谁应该给 Appframework 发 '创建视图' 的消息"** —— 这与
`createRequestModelExecuteEvent(1, …)` 的消费者未定位是同一件事。

---

# 补充（第七轮）：**pViewManager 的写入点找到了** —— ViewManager 构造函数

## 一、★ 写入点

```asm
8a4f5a: ldr  r3, [pc, #40]      ; 字面量 0x4B8C = pViewManager 的 GOT 槽偏移
8a4f5e: ldr.w r3, [r9, r3]      ; r3 = &pViewManager
8a4f62: str  r5, [r3, #0]       ; ★ *pViewManager = this
```

⇒ **`pViewManager` 确实有写入点**（前面"从未被写过"的判断需要修正：
不是没有写入，而是**那个写入者从未被调用**）。

## 二、写入者 = **ViewManager 构造函数 @ `0x8A4E28`**

定位方法：在该区域扫描 `stmdb sp!, {…}` 函数序言，得到唯一的候选：

```asm
8a4e28: e92d 4ff0   stmdb sp!, {r4-r11, lr}   ; 大函数序言
8a4e2c: b083        sub  sp, #12
8a4e2e: af00        add  r7, sp, #0
8a4e30: 4605        mov  r5, r0                 ; r5 = this
        … 多次 blx（取 vtable 槽）+ 逐成员初始化 …
8a4e60: strb.w r3, [r5, #0x28]
8a4e6e: str.w  r0, [r5, #4]
8a4e78: str    r0, [r5, #8]
8a4e82: str    r0, [r5, #12]
8a4e86: str.w  fp, [r5, #16]
8a4e8a: str    r3, [r5, #24]
8a4e8e: str.w  r8, [r5, #20]
8a4e92: str    r4, [r5, #28]
8a4e9a: str    r0, [r5, #0x64]
8a4ea2: str    r0, [r5, #0x60]
        …
8a4f62: str    r5, [&pViewManager]          ; ★ 最后把 this 写进全局
```

**已推断的成员布局（ViewManager）**：

| 偏移 | 类型 |
|---|---|
| `+0x04` `+0x08` `+0x0C` | 指针（各由一次 blx 工厂调用返回） |
| `+0x10` `+0x14` `+0x18` `+0x1C` | 指针；`+0x14` 指向带 vtable 的对象（析构里 `ldr r0,[r4,#20]; ldr r3,[r0]; ldr r3,[r3,#0x58]; blx`） |
| `+0x28` | 字节 |
| `+0x60` `+0x64` `+0x68` | 指针（`+0x68` 由析构 `0x8A4D86` 读取） |
| 对象大小 | ≥ `0x6C`，建议按 `0x400` 分配 |

**析构函数 = `0x8A4D86`**（遍历并 free 各成员），与构造函数成对。

## 三、★ 给 QEMU 的直接做法（比盲填更干净）

```c
void *vm = calloc(1, 0x400);
((void (*)(void *))((char *)libObj_base + 0x8A4E28))(vm);   // 调 ViewManager 构造
// 副作用：pViewManager (libObj+0x149221C) 自动被写成 vm
```

⇒ 不需要手工 `*(void**)pVM = xxx`，**构造函数自己会写**，而且顺带把各成员初始化好。

然后再调 `viewManagerIf::openView(...)`（`0x14117D` / `0x140FB9`）看能否继续。

## 四、Appframework 线程体的真实结构（重要）

`libObj+0x8A2398` 全文只有 40 余字节的有效逻辑：

```
8a239e: bl 0x6271D4            ; uxengine_init
8a23aa: bl 0x8A2EE0            ; 构造消息对象
8a23d4: bl 0x8A2314            ; createRequestModelExecuteEvent(1, buf)
8a23d8: bl 0x8A3104            ; ★ 进入大函数（0x174 栈帧 + 循环）= 主循环
8a23de: bl 0x8A306C            ; 清理消息对象
8a23e4: 返回
```

⇒ **线程体不是"初始化一下就退出"，而是 init → 发一次请求 → 进主循环**。
主循环在 `0x8A3104`（栈帧 0x174，含 `Backup_nread`/`timeGetNow` 等调用与回跳）。

⇒ 所以 `Appframework 线程 tid=185 futex_wait_queue_me` **很可能就是主循环在等消息，属于正常空闲**。

判别方法仍是：抓**用户态栈**看是否停在主循环的取消息处。
若确实是空闲，那么"管理图为空"的原因就是 **没人往它的队列投递"创建视图"的消息**，
与 `createRequestModelExecuteEvent(1, …)` 的消费者缺失是同一件事。

---

# 补充（第八轮）：**整个 App 对象图的装配函数找到了 = `0x8A2EE0`**

> QEMU 侧反馈"ViewManager 构造不是单参数、要一个带 vtable 的配置对象"。
> 顺着"找构造函数的调用者"往下走，得到比"造桩"更直接的路。

## 一、构造函数的**唯一**调用点

用 Thumb `BL` 解码扫全 `.text`：调用 `0x8A4E28` 的只有 **1 处** —— `0x8A2FA6`，
位于函数 **`0x8A2EE0`** 内。

而 **`0x8A2EE0` 正是线程体在 `uxengine_init` 之后调用的那个函数**：

```
8a239e: bl 0x6271D4     ; uxengine_init
8a23aa: bl 0x8A2EE0     ; ★ 装配整个 App 对象图（含 ViewManager）
8a23d4: bl 0x8A2314     ; createRequestModelExecuteEvent(1, buf)
8a23d8: bl 0x8A3104     ; 主循环
```

⇒ **ViewManager 的创建本来就在线程体里**。既然 QEMU 侧看到 `pVM==0`，
说明线程**根本没走到 `0x8A2FA6`**。

## 二、`0x8A2EE0` 的装配顺序（完整对象图）

```asm
8a2efa: bl 0x8A2E70                 ; 名字 strncpy 到 app+0x2C（≤20 字符）
8a2efc: str r0,[app,#0x14]          ; ★ 配置对象 = [app+0x14]
8a2f0e: blx osal_wai_sem_tmo        ; ★★ 信号量等待（可能就是那个 futex！）
8a2f26: blx pipe                    ; 建管道 + fcntl(O_NONBLOCK)
8a2f4a: new(0x28) … → [app+0x10]
8a2f6c: new(?)    → [app+0x0C]
8a2f72: new(0x58) → [app+0x08]
8a2f82: new(0xAC) → [app+0x04]
8a2f94: new(0x70) → [app+0x00]  ← ★ ViewManager（大小 = 0x70 ✓ 与我推断的 ≥0x6C 吻合）
8a2fa0: str r3,[sp,#0]              ; ViewManager 第 5 参 = [app+0x14]（配置对象）
8a2fa6: bl 0x8A4E28                 ; ViewManager::ctor
8a2fac: str r5,[app,#0]             ; 保存 ViewManager*
```

## 三、ViewManager 构造签名（据此确定）

```
ctor(this,  r1=[app+0x10],  r2=[app+8],  r3=[app+4],  arg5=[app+0x14])
                  │             │           │              └─ 配置/工厂对象（带 vtable）
                  │             │           └─ 0xAC 的 manager
                  │             └─ 0x58 的 manager
                  └─ 0x28 的对象
```

**配置对象的 vtable 槽**（构造里被调用）：`+8`、`+0x10`、`+0x14`、`+0x18`、`+0x44`、`+0x48`
（`+8` 返回工厂对象；其余槽用来填充 ViewManager 的 `+0/+4/+8/+0xC` 等成员）。

## 四、App 对象成员布局

| 偏移 | 内容 |
|---|---|
| `+0x00` | **ViewManager\***（`new(0x70)`） |
| `+0x04` | manager（`new(0xAC)`） |
| `+0x08` | manager（`new(0x58)`） |
| `+0x0C` | manager（size 未定） |
| `+0x10` | 对象（`new(0x28)` 级别），作为 ViewManager 的 r1 |
| `+0x14` | **配置/工厂对象**，作为 ViewManager 的 arg5 |
| `+0x18` | 由 ViewManager 构造写入（= 原 r3 参数） |
| `+0x24` `+0x28` | int（0 与 0x7FFFFFFF） |
| `+0x2C..+0x3F` | 名字字符串（`strncpy` ≤20） |

## 五、因此推荐的下一步（比"造桩"简单得多）

**不要手工造配置对象桩，直接调 `0x8A2EE0`** —— 它按正确顺序把整张图建起来：

```c
void *app = calloc(1, 0x400);
((void (*)(void *, const char *))((char *)libObj_base + 0x8A2EE0))(app, name);
// 副作用：app+0x14 配置对象、app+0/4/8/C 四个管理器、
//         以及 pViewManager 全部就位
```

**但它内部有 `osal_wai_sem_tmo`** —— 如果它阻塞，那个信号量极可能就是 QEMU 侧
观测到的 `futex`（`libObj+0x145DF44` 附近）。这样一来因果链就闭合了：

```
线程体 → 0x8A2EE0 → 等信号量（阻塞）
        ⇒ 走不到 0x8A2FA6 ⇒ ViewManager 不创建 ⇒ pVM == 0
```

**验证方法**：让线程 185 的 PC 落在 `0x8A2EE0..0x8A2F10` 之间，就证明是这个信号量。
然后找谁 `post` 它（该信号量由 `0x8A2F0E` 处的字面量指定）——那才是真正缺的组件。

## 六、给决策的建议

| 选项 | 评价 |
|---|---|
| 1（造配置桩） | **不建议** —— 配置对象 vtable 有 6+ 槽，每个返回又是另一种对象，级联无界 |
| **1'（调 `0x8A2EE0`）** | **建议先试** —— 一次调用就能验证"是不是卡在信号量"，成本极低，信息量最大 |
| 3（降级固化） | 若 1' 证明卡在信号量且无人 post，就走这条 |

⇒ **先做 1'，一次实验就能定性**；不管结果如何都不会比现在更差。
