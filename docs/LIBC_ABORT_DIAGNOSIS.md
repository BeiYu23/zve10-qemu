# `trap 6` 不是非法指令 —— 是 `abort()` 触发的 SIGABRT

> QEMU 侧报告：新崩溃 `pc=libc-2.11.2.so+0x15796`、`lr=+0x24D8F`、`trap_no=6(非法指令)`、
> `r0=0 r1=0x86 r2=6 r3=0x86`。

## 一、反汇编结论：这是 glibc 的 `abort()` 路径

### `pc = libc+0x15796`

```
00015790 <syscall stub>:
   15790:  b580   push {r7, lr}
   15792:  4667   mov  r7, ip        ; ip = 系统调用号
   15794:  df00   svc  0             ; 触发系统调用
   15796:  bd80   pop  {r7, pc}      ; ← pc 落在这里（svc 之后）
```

### `lr = libc+0x24D8F` 在 `raise()` 里

```
00024d64 <raise@@GLIBC_2.4>:
   24d76:  mov.w ip, #224    ; 0xE0 = gettid
   24d7a:  bl    15790       ; 调 syscall stub
   24d86:  mov.w ip, #268    ; 0x10C = tgkill
   24d8a:  bl    15790       ; ← 返回地址 0x24D8F
   24d8e:  cmn.w r0, #4096
```

### 寄存器与 `tgkill(tgid, tid, sig)` 完全吻合

| 寄存器 | 值 | 含义 |
|---|---|---|
| `r0` | 0 | tgkill 返回值 = **成功** |
| `r1` | 0x86 = **134** | tid（与报告的 pid 134 一致） |
| `r2` | **6** | **信号号 = SIGABRT** |
| `r3` | 0x86 | tid 冗余传参 |

⇒ **`trap_no=6` 是 SIGABRT 的信号号，不是"未定义指令异常"。**
⇒ **进程是调用了 `abort()`（或 `__assert_fail` 等致命路径），不是跳到野指针。**

## 二、真正的谜题：**谁调了 `abort()`**

glibc 在 abort 前**一定会先打印一条消息**（到 stderr，进串口）。按这些串去 grep 崩溃前的日志：

| 消息 | 来源 | 含义 |
|---|---|---|
| `Assertion '...' failed.` | `__assert_fail`（libc+**0x1F98C**） | 断言失败，会带 **文件名:行号:函数名** |
| `free(): invalid pointer` | malloc 检查 | 堆被写坏 |
| `free(): invalid next size (fast)` | 同上 | 越界写 |
| `double free or corruption (top/fasttop)` | 同上 | 重复释放 |
| `corrupted double-linked list` | 同上 | 链表被破坏 |
| `munmap_chunk(): invalid pointer` | 同上 | 释放非法指针 |
| `Accessing a corrupted shared library` | 动态加载 | 库不一致 |
| `terminate called after throwing an instance of ...` | **libstdc++** | C++ 异常无人接住 |

**只要有其中一条，就能直接定位到文件名+行号。**

## 三、libc 本体信息（供对照）

| 项 | 值 |
|---|---|
| 路径 | rootfs（nflasha7）**`/lib/libc-2.11.2.so`** |
| 大小 | **984,292 B** |
| md5 | **`5b1d404e664af8aa22c2e6babc3e800a`** |
| 加载器 | `/lib/ld-linux.so.3` → 同一个 `ld-2.11.2.so` |
| 符号 | **有 `.dynsym`（2153 条）**，可查导出符号；无 `.symtab` |

关键地址（相对 libc 基址）：

```
syscall stub        0x15790
raise               0x24D65
abort               0x25AE1
__assert_fail       0x1F98D
```

## 四、a7c2 没撞过这一处

翻了他们的仓库，没有 libc abort 相关记录（只有他们自己 C 代码里的 `assert`）。
⇒ **这是新领域，没有现成答案可抄。**

## 五、建议的下一步（按成本）

1. **grep 串口日志**找上表任一消息 —— 一条就定性（成本≈0）
2. 若没有消息：让内核异常钩子**多 dump 32 个字栈**，从 `abort` 之上还原调用者
3. 若怀疑断言：interpose `__assert_fail`（`-nostdlib`，把 `r0..r3` 存到一块固定内存，
   再 `svc` write 到串口），就能拿到「文件/行号/函数/表达式」四个参数
