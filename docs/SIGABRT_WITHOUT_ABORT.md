# SIGABRT 但没有 Assertion 文本、abort 也拦不到：可能原因与查法

## 一、glibc 2.11 里能发出 SIGABRT 的路径（本机 libc 实测符号）

```
abort           0x25AE1   GLIBC_2.4        ← 已 interpose，未触发
raise           0x24D65   GLIBC_2.4        ← ★ 未 interpose
gsignal         0x24D65   GLIBC_2.4        ← ★★ 与 raise **同一个地址**（glibc 别名）
__assert_fail   0x1F98D   GLIBC_2.4        ← 已 interpose，未触发
__libc_fatal    0x53D79   GLIBC_PRIVATE    ← 未 interpose
__fortify_fail  0xB19D1   GLIBC_PRIVATE    ← 未 interpose
__stack_chk_fail 0xB19C1  GLIBC_2.4        ← 已 interpose，未触发
```

⇒ **`raise` 与 `gsignal` 是同一个函数**，interpose 任一个即可覆盖两个。

## 二、★ "interposer 已加载但一次没被调用" 的两种解释

### 解释 A：有人**直接调 `raise(SIGABRT)`**，不是 abort

`abort()` 内部才调 `raise(SIGABRT)`；若某处**直接**调 `raise`/`gsignal`，
则 `abort` 的 interposer 永远不会触发，而 **Assertion 文本也不会有**（因为压根没走 assert）。

这与"没有 Assertion 文本 + abort 未触发"**完全吻合**。

### 解释 B：调用方绕过了 preload（RTLD_DEEPBIND）

若那个库是用 **`RTLD_DEEPBIND`** dlopen 的，它的 `abort`/`__assert_fail`
会**直接绑定到 libc**，不经过 preload 的 interposer。

> **a7c2 正是为这一点显式剥掉 DEEPBIND 的**（源码原文）：
> ```c
> if (flags & 8) { /* RTLD_DEEPBIND hides a library's osal IPC calls from these hooks; strip it. */
>     flags &= ~8;
> }
> ```
> 他们注释里说的是 osal IPC，但**同理适用于 abort/assert**。

### 解释 C：其它线程用 `pthread_kill(thread, SIGABRT)`

glibc 2.11 的 `pthread_kill` 在 **libpthread**（与 libc 分开），未 interpose。
若某个 watchdog 线程主动杀 App 线程，也会得到"无文本 SIGABRT"。

## 三、a7c2 有没有拦这些？——**没有**

翻了 `menu-observe.c` 的全部 interposer：

```
dlopen / osal_dynamic_id / osal_reg_sync_msg_queue_cb / osal_reg_msg_queue_cb
osal_snd_sync_direct / osal_snd_sync_msg / osal_rcv_msg_tmo / osal_valloc_msg_wait
osal_free_msg / osal_snd_msg / kikilog_set_notify_cb
GPE: GpermFillRect / GpermBitBlit / FillRect / BitBlit / Flush / SendDrawComp / InitializeDraw
（MonQueue 系列在 `#if 0` 块里，未启用）
```

**没有任何 raise/gsignal/pthread_kill/__libc_fatal** ⇒ 没有先例可抄，要自己加。

## 四、建议动作（按性价比）

| # | 动作 | 说明 |
|---|---|---|
| **1** | **在内核信号投递处打印 `siginfo`**：`si_code` / `si_pid` / `si_uid` | **最省事**：`SI_TKILL` + `si_pid==self` ⇒ 自己 tgkill；`SI_USER` ⇒ 外部 kill；`SI_QUEUE` ⇒ sigqueue |
| 2 | 内核侧 **dump 投递线程的用户态栈** | 比 interpose 更直接，能看到 raise 之上的调用者 |
| **3** | interpose **`raise`**（`gsignal` 同一个地址，自动覆盖） | 抓投递点 |
| 4 | interpose **`pthread_kill`**（在 libpthread，需单独 hook） | 排除跨线程 |
| 5 | **剥 `RTLD_DEEPBIND`** | 排除解释 B |
| 6 | interpose `__libc_fatal` / `__fortify_fail`（GLIBC_PRIVATE） | 兜底 |

> **顺序建议：先做 1（内核打印 siginfo）** —— 一行日志就能把 A/B/C 三种可能缩到一种，
> 比逐个 interpose 快得多。

## 五、关于 256MB 逐页映射（问题 3）

**`remap_pfn_range` 映射"连续物理区"是「一次调用」就够的**，
逐页循环只在内存**非连续**（如 vmalloc）时才需要。

⇒ 既然窗口来自 `memmap=32M$0x08000000` 的**连续保留区**：
```
一次性 remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT, size, PAGE_SHARED)
```
**不要逐页循环，也不要映射 256MB。**

- 逐页循环 → 数万次 PTE 插入 + 你看到的 `do_mmap_pgoff` 告警
- 映射 256MB → 无谓开销，且真机窗口没这么大

**惰性映射不建议**：需要实现 `vma->vm_ops->fault`，代码更多、路径更怪；
而 App 是 `str.w r3,[r0],#4` 的**批量写**，逐页 fault 反而更慢。
**先把保留区按需加大（32M → 64M）比惰性映射简单可靠。**
