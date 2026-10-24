# 两个通用问题：配置模块加载链 & glibc 里绕过 abort() 的 SIGABRT

## 一、initializeConfig 不返回：它加载的是【多个】配置入口

### 1.1 结构：4 个调用点，全在配置构造函数 0x14BD58 里

```c
ctor(r4):
  // 调用1 @0x14BD90（必需）
  [r4+8] = load(path1, sym1, arg2_1, &out1);
  if ([r4+8] == 0) goto FAILURE;          // ★ 第一个失败即整体失败

  // 调用2 @0x14BDA8
  [r4+4] = load(path2, sym2, arg2_2, &out2);
  if ([r4+4] != 0) goto NEXT;             // 成功则跳过调用3

  // 调用3 @0x14BDC0（调用2 的 fallback）
  [r4+4] = load(path3, sym3, arg2_3, &out3);

NEXT:
  if ([r4+4] == 0) goto FAILURE;

  // 调用4 @0x14BDDA
  [r4+0xC] = load(path4, sym4, arg2_4, &out4);
  if ([r4+0xC] == 0) goto FAILURE;        // 失败路径会打日志并返回 NULL
```

### 1.2 能确认的字符串（偏移有 1~2 字节误差，但可辨识）

```
调用4: path = "libObj.so"    sym = "initializeModelTree"
调用2: arg2 = "getViewConfig"   path 在 viewUnified2.so 那一带
调用3: 同调用2（fallback）
调用1: 在 libObj.so / initializeModelConfig / getModelConfig 那一带
```

相邻字符串区（0xE4D3B0 起）里依次是：
`sym( %s ) failed - dlerror() = %s`、`libObj.so`、`initializeModelConfig`、
`getModelConfig`、`viewUnified2.so`、`initializeViewConfig`、`getViewConfig`、
`initializeModelTree`、`getModelTree` …

## 二、★ 为什么你们的 dlopen 探针只看到 libObj.so

**关键机制：`dlopen` 一个【已经加载】的 `.so`，不会触发 `open()` 系统调用**
—— 动态链接器在已加载列表里查到就**直接返回现有 handle**。

⇒ 如果 `viewUnified2.so` 在启动早期已被 `im.elf` 的 OnDemandLoader 手工加载，
那么 `dlopen("viewUnified2.so")` **不会有任何 open 记录**，但**仍然成功返回**。

**⇒ 你们的推断是对的：模块早已加载，`initializeConfig` 只是在调它们内部的入口函数。**

**⇒ 卡点在【入口函数内部】，不在加载环节。**

### 建议的探针（你们计划的那条，是对的）

```c
void *h = dlopen("viewUnified2.so", RTLD_LAZY|RTLD_GLOBAL);
void *f = dlsym(h, "initializeViewConfig");   // 或 dlsym(RTLD_DEFAULT, ...)
log("initViewConfig = %p", f);
```

拿到地址后 **hook `initializeViewConfig` 记 enter/exit**：
- 有 enter 无 exit ⇒ 卡在它内部（即 `new(0x58)` + ctor `0x67D6E4`）
- 连 enter 都没有 ⇒ 调用1（必需的那个 libObj 入口）就失败了，整体没走到调用2

**也可以直接 hook 构造函数链**：`0x67D6E4` → `0x6235B8` → `0x67D5C6` → `0x48FFE4` → `0x45C90C` → `0x5E4CE0`，逐层打点。

## 三、glibc 2.11 里"发出 SIGABRT 但绕过 abort()"的路径

### 3.1 ★ 先解释"为什么拦不到"

**glibc 的 `abort()` 会主动把 SIGABRT 的处理方式重置为 `SIG_DFL`，然后才 raise。**

伪代码：
```c
void abort(void) {
    // 1. unblock SIGABRT
    // 2. ★ 把 SIGABRT 的 disposition 设回 SIG_DFL（覆盖你装的 handler！）
    // 3. raise(SIGABRT)
}
```

**⇒ 你用 `sigaction` 装的 handler 会在 `abort()` 内部被覆盖掉**，
这就是"拦不到"的根本原因 —— **不是绕过 abort，而是 abort 吃掉了你的 handler。**

### 3.2 会发 SIGABRT 的路径（glibc 2.11）

| 路径 | 触发条件 |
|---|---|
| `abort()` | 直接调用（handler 会被重置） |
| `__assert_fail` | `assert()` 失败 |
| `__stack_chk_fail` / `__fortify_fail` | **栈溢出**（-fstack-protector） |
| `__chk_fail` | `_FORTIFY_SOURCE` 检查失败 |
| `malloc_printerr` / `__libc_message` | **堆损坏**（`free(): invalid pointer`、`double free` 等） |
| `__libc_fatal` | 内部致命错误 |
| `pthread` 致命错误 | 例如 mutex 状态非法 |
| **直接 `raise(SIGABRT)`** | 应用自己发 |
| **`kill()` / `tgkill()` / `syscall(SYS_tgkill, ...)`** | 应用自己发（**完全绕过 abort**） |

### 3.3 怎么才能真正抓到

**不要依赖 sigaction**（会被 abort 重置）。按优先级：

```c
/* ① interpose abort 本身 —— 最直接 */
void abort(void) {
    backtrace_symbols_fd(bt, n, 2);   /* 先留现场 */
    _exit(99);                        /* 或调 real_abort() */
}

/* ② interpose 这些"真正的发起者" */
__libc_message / malloc_printerr / __stack_chk_fail / __assert_fail / __fortify_fail

/* ③ 若以上都没命中 ⇒ 是直接 raise/kill */
raise / kill / tgkill / syscall(SYS_tgkill,...)
```

**④ 最可靠（不依赖 interpose）**：让 guest 进程跑在 **gdb / ptrace** 下，
`catch signal SIGABRT` —— 能看到真正停在哪条指令。

**⑤ 补充**：`LD_PRELOAD` 里同时设一个 **SIGABRT handler** 也有用 ——
虽然 abort 会覆盖，但**直接 kill(tgkill) 的路径不会覆盖**，所以两条路都覆盖住。

### 3.4 针对你们"新菜单 SIGABRT 无消息"

"无消息"最可能对应 **`__libc_message`** 路径（堆损坏/内部错误）——
它在 2.11 里通常**会**打印，但如果 stdout 被重定向或缓冲区没 flush，就看不到。

**⇒ 建议**：interpose `__libc_message` 并把格式化后的字符串**直接 write(2, ...)**，
不要走 stdio（避免缓冲丢失）。
