# ★ `0x8A2EE0` 不能直接调 —— 必须先填"名字"，否则走错分支

> 回答 QEMU 侧"为什么放行信号量后又崩在 0x14"。

---

## 一、结论（一句话）

**`0x8A2EE0` 需要 `0x14920E8` 这个名字缓冲区里是 `"X"`。
你们直接调它时缓冲区是空的 ⇒ 名字比较失败 ⇒ 走错误分支 ⇒ `[this+0x14]` 拿到非法值 ⇒ 崩在 0x14。**

**修正：不要直接调 `0x8A2EE0`，改调 `Application_System_Init(0x8A2408)`，参数传 `0`(NULL)。**

---

## 二、证据链

### 2.1 线程体传的名字不是字面量，是 `.bss` 缓冲区

```asm
8a23a2: ldr r1,[pc,#84]
8a23a8: add r1, pc            ; → 0x014920E8
8a23aa: bl 0x8A2EE0           ; name = *(bss 0x14920E8)
```

`0x14920E8` 落在 **`.bss`**（`.bss` = `0x1459328..0x1494E7C`）⇒ **文件里是零，运行时才填**。

### 2.2 `Application_System_Init` 负责填它

`0x8A2408`（144 B）：

```c
Application_System_Init(const char *arg)
{
    char buf[32];
    memset(buf, 0, 32);
    snprintf(buf, 32, fmt, ...);           // 0x8A2418..0x8A242A
    createRequestModelExecuteEvent(0, buf); // 0x8A2432
    if (arg != 0) { dst = 0x14920E8; src = arg; }            // 0x8A2438-0x8A243E
    else          { dst = 0x14920E8; src = "X"; }            // 0x8A2440-0x8A2446
    strncpy(dst, src, 255);                // ★ 0x8A244A 写名字缓冲区
    osal_create_thread(thread_obj);        // 0x8A2452 起线程（线程体 = 0x8A2398）
    osal_set_cpu(thread, 15);              // 0x8A2458
    ...
}
```

**两个分支的目标都解出来是 `0x014920E8`**，默认源是 `0x01033F3F`。

### 2.3 ★ 默认名字 = `"X"`，而且它触发 `initializeConfig()`

`0x8A2E70(this, name)` 的逻辑：

```c
strncpy(this + 0x2C, name, min(strlen, 20));
this[0x3F] = 0;
if (strncmp(REFNAME1, name, 20) == 0)   // REFNAME1 = 0x1033F3F = "X"
    initializeConfig();                  // ★ 初始化配置子系统
else
    sub_44AD0C();                        // 失败/替代路径
if (strncmp("AppConfig.so", name, 20) == 0) { ... }   // REFNAME2 在 0x1033F41
return <所选函数指针>() 的结果;           // 这个返回值 → [this+0x14]
```

字符串区域实证（`0x1033F30` 起）：
```
'ate = %d\0AC %08X\0AppConfig.so\0[FMWK] thread_id: %d\0Appframework\0...'
        ↑ 0x1033F3F = "X"      ↑ 0x1033F41 = "AppConfig.so"
```

### 2.4 崩点算术完全对上

```asm
8a2eee: str r3,[r0,#40]     ; 若 this==0 → 会先崩在 0x28（而不是 0x14）
8a2ef0: mov r4, r0          ; r4 = this
8a2ef4: bl 0x8A2E70         ; ← 这里决定 initializeConfig 还是失败路径
8a2efc: str r0,[r4,#20]     ; [this+0x14] = 0x8A2E70 的返回值
...
8a2f1c: ldr r0,[r4,#20]     ; 读 [this+0x14]    ← 成功
8a2f1e: ldr r3,[r0,#0]      ; ★ 若上面存的是小值(如 20=0x14) → 读地址 0x14 → 崩
```

⇒ **观测到 `address=0x14` ⇒ `[this+0x14]` 存的是 `0x14` 这种小整数，不是指针**
⇒ 说明 `0x8A2E70` 走了**失败分支**（名字不匹配 `"X"`），返回值不是配置对象。

**也回答了"为什么不是崩在 0x28"**：因为 `this != 0`（你们传的 app 指针有效），
所以前两条 `str` 都成功；崩的是"取到的配置对象"这个**间接层**。

---

## 三、★ 给 QEMU 的两条修正（任选其一，都只需一行）

### 方案 A（推荐）：改调 `Application_System_Init`，参数传 0

```c
((void (*)(void *))((char *)libObj_base + 0x8A2408))(0);   // ★ arg = NULL
```

好处：
- 它会**自动**把名字缓冲区填成 `"X"` → `initializeConfig()` 正常运行
- 它会**自己 `osal_create_thread`** 起真正的线程 → 走固件原生路径（正是你们想要的）
- 不需要你们手工造 app 对象

### 方案 B：仍调 `0x8A2EE0`，但先手工填名字

```c
memset((void *)(libObj_base + 0x14920E8), 0, 256);
strcpy((char *)(libObj_base + 0x14920E8), "X");
call(0x8A2EE0, app_ptr);
```

---

## 四、这条为什么重要

之前所有"填 msg / 造对象 / signal 信号量"的努力都**作用在错误的分支上** ——
因为名字为空，`initializeConfig()` 从来没跑过，配置对象根本没生成。

⇒ **先修名字，再谈后面的**。修完之后 `[this+0x14]` 才有可能是真正的配置对象。
