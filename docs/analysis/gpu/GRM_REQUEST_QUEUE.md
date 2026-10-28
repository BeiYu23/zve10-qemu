# 图形请求队列：对象布局与完成通知责任归属

> 依据：ZV-E10 `libObj.so` 反汇编（`0x7D7780` 构造 / `0x7D7C10` worker / `0x7D7D30` 封装）

## 一、队列对象布局（`0x7D7780` 构造，192 字节）

```asm
7d7784: r9 = q
7d7792: str  r1, [r9, #0xA4]        ; 回调由调用者传入
7d7788/96: init_list(q + 0x98)      ; 待处理链表
7d77a2: pthread_mutex_init(q + 0x04) ; 队列锁
7d77ae: pthread_cond_init (q + 0x20) ; 队列条件
7d77ba: pthread_mutex_init(q + 0x50) ; 完成锁
7d77c2: pthread_cond_init (q + 0x68) ; 完成条件
```

| 偏移 | 类型 | 角色 |
|---|---|---|
| `+0x04` | mutex | 队列锁 |
| `+0x20` | cond | 队列空时 worker 等 |
| **`+0x50`** | **mutex** | **完成锁** |
| **`+0x68`** | **cond** | **完成条件（请求方等这个）** |
| `+0x98` | list | 待处理链表 |
| **`+0xA4`** | **ptr** | **回调（构造时传入）** |

四个同步对象在同一个构造函数里初始化 ⇒ 属于同一个「请求/完成」结构。

## 二、worker 循环（`0x7D7C10`）

```c
loop:
    pthread_mutex_lock(q+0x04);              // 0x7D7C32
    if (*(u32*)(q+0x98) == 0) {
        pthread_cond_wait(q+0x20, q+0x04);   // 0x7D7CA2 空闲等待
        goto loop;
    }
    item = dequeue(q+0x98);                  // 0x7D7BF0
    pthread_mutex_unlock(q+0x04);            // 0x7D7C50 ★ 回调前已解锁

    type = *(u32*)(item - 8);

    if (type == 2) {                         // 0x7D7C80
        cb = *(void**)(q+0xA4);
        cb(*(void**)(item + 4));
        free(item - 8);
        goto loop;                            // ★★★ 此处【不】signal
    }
    if (type == 0x8001) {                     // 0x7D7CB0
        cb = *(void**)(q+0xA4);
        cb(*(void**)(item + 4));
        pthread_mutex_lock(q+0x50);           // 0x7D7CBC
        pthread_cond_signal(q+0x68);          // 0x7D7CC2
        pthread_mutex_unlock(q+0x50);         // 0x7D7CC8
        goto loop;
    }
    pthread_mutex_lock(q+0x50);               // 0x7D7C62
    pthread_cond_signal(q+0x68);              // 0x7D7C6A
    pthread_mutex_unlock(q+0x50);             // 0x7D7C70
    return 0;
```

## 三、★ 关键结论：完成通知的责任按 type 分配

| `item[-8]` | 回调参数 | 谁负责 signal |
|---:|---|---|
| **2** | `item[4]` | **回调自己** —— worker 不 signal |
| **0x8001** | `item[4]` | **worker**，在回调返回后 signal |
| 其它 | — | **worker** signal 后返回 |

**⇒ 若请求类型是 2，替换回调却不在其中 signal，请求方将永久等待。**

## 四、signal 的正确姿势

**固件自身在 `0x8001` 分支中的写法即为标准答案：**

```c
pthread_mutex_lock(q+0x50);
pthread_cond_signal(q+0x68);
pthread_mutex_unlock(q+0x50);
```

- **必须持有 `q+0x50`**：它是 `q+0x68` 的配对锁；不持锁 signal 可能丢失唤醒
- **用 `signal` 而非 `broadcast`**：目标是单个等待者
- 请求方在 `pthread_cond_wait(q+0x68, q+0x50)` 中等待时，该函数会原子释放 `q+0x50`
  ⇒ **"请求方持有 q+0x50 在等"这一说法不成立**，回调里加锁不会自锁

## 五、排查提示

- 记录 `item[-8]` 的 type，即可判断该请求的完成通知由谁负责
- 记录 `q+0xA4` 的实际值，确认回调是谁
- 多个 worker（如 141/142/143）可能分属不同队列对象，堵一个不一定堵全部

## 六、谁创建这些队列（`0x7D7D30`）

**`0x7D7D30` 不是"等待完成"的函数，而是「按需创建两个队列（各含 worker）+ 等待」：**

```c
int sub_7D7D30(void *param)                     // r4 = param
{
    r8 = sub_7D7CD0(&local, param, 1, 0);       // 0x7D7D50
    if (r8 != 0) return r8;                      // 非零 ⇒ 错误，直接返回

    pthread_mutex_lock(&g_global);               // 0x7D7D74
    if (*(u32 *)param != 0) goto already;        // "已创建"标志

    r8 = sub_7D7780(q,  cb1); if (r8) goto err;  // 0x7D7D8A  ★ 队列1 + worker
    r8 = sub_7D7780(q2, cb2); if (r8) goto err;  // 0x7D7D9E  ★ 队列2 + worker
    ...
}
```

**要点：**

- **`sub_7D7780` 被调用两次 ⇒ 创建两个队列对象**（各自带一个 worker 线程）
- `*(u32 *)param != 0` 是"已经创建过"的幂等标志
- **把 `0x7D7D30` 入口补成 `return 0` 会跳过整个创建流程**，
  结果是拿到没有对应 worker 的孤儿队列对象，后续"替换回调/接管 worker"都作用在错误对象上

## 七、取项函数 `0x7D7BF0`（32 字节）

```c
void *dequeue(struct list *l)        // l = q + 0x98
{
    count = l->count;                // +0x00
    head  = l->head;                 // +0x04
    l->count = count - 1;
    l->head  = *(void **)head;
    return head;                     // 返回旧 head
}
```

**链表：`struct list { u32 count; void *head; }`，位于 `q+0x98`。必须在 `q+0x04` 锁内调用。**

**条目结构（基址 = 返回指针 − 8）：**

| 偏移 | 字段 |
|---:|---|
| `+0x00` | type（`2` / `0x8001` / 其它）|
| `+0x0C` | 回调参数（`item+4`）|

`free()` 也以 `item−8` 为基址。

## 八、请求方（`0x7D7D30`）的完整等待流程

`0x7D7D30` 创建两个队列后，自己作为**请求方**发起一次请求并等待完成：

```asm
7dc6: pthread_mutex_lock(q2+0x04)              ; 队列锁
7dd2: bl 0x7D7840 (q2+0x98, &desc)             ; ★ 请求入队
7dda: pthread_cond_signal(q2+0x20)             ; ★ 唤醒 worker（队列条件）
7de0: tst  [r7+28], #0x8000                    ; 是否同步请求
7de4: bne  0x7D7F6A                            ; 同步 ⇒ 另一路径
7df2: pthread_mutex_unlock(q2+0x04)
7df6: r1 = q2 + 0x50                           ; 完成锁
7df8: r0 = q2 + 0x68                           ; ★★ 完成条件
7dfc: pthread_cond_wait                        ; ★★★ 等待点
7e02: pthread_mutex_unlock(q2+0x50)
7e06: r3 = [r7, #20]                           ; ★ 醒来后读结果字段
7e08: cmp r3, #0
7e0a: bne 0x7D7E74                             ; 非 0 ⇒ 错误/重试
7e0e: [r4, #0] = 1                             ; 置"已完成"标志
7e18: b 0x7D7D58                               ; 正常返回
```

**要点：**

1. **等待的是 `q2+0x68`，配对锁是 `q2+0x50`** —— 不是另一个队列的 `+0x68`
2. **请求方自己也会 `signal(q2+0x20)`** 来唤醒 worker，所以 worker 不应处于睡眠态
3. **仅仅 signal 完成条件不够**：醒来后要检查 `[r7+20]` 结果字段，
   **= 0 才走成功路径，否则跳 `0x7D7E74`**

## 九、两个队列必须分清

| 变量 | 地址（某次运行）| 完成条件 | 角色 |
|---|---|---|---|
| **q2** | `0x072E5C28` | **`0x072E5C90`** | **请求方等这个** |
| q | `0x072E5CD0` | `0x072E5D38` | 另一个队列 |

**⇒ 若 worker 去 signal `q+0x68` 而非 `q2+0x68`，请求方永远不会被唤醒。**

## 十、worker 不 signal 的三种可能

结合 worker 循环的 type 分支：

| 可能 | 判据 |
|---|---|
| **item 类型为 `2`**（按设计不 signal，责任在 cb）| 打 `item[-8]` |
| **cb 内部卡住**（不返回）| 打 cb 的 enter/exit |
| **worker 未取到 item**（队列空/被抢）| 观察 `q+0x98` 的 count |

## 十一、实测结论（QEMU 侧，libc 层 cond 统计）

- 一整轮只有 **7 次** `pthread_cond_signal`
- **`0x072E5C90`（q2+0x68）从未出现在 signal 列表**
- 其中一次 signal 的 `lr` 落在 `libMWF.so` 内 ⇒ **libMWF 内部确实会 SetSignal**（此前"0 次"是 LD_PRELOAD 假阴性）
- **⇒ 事件系统是活的，但 grm 这条链的完成通知从未发出**

**该统计方式（hook libc 的 `pthread_cond_wait/signal`，用 `cond − 0x08` 反查 `ObjSignal`，
并读 `+0x3C` 标志位）能绕开"同模块内直接调用不可见"的盲区。**

## 十二、结果字段（`out + 16`）的精确位置

`0x7D7D30` 的栈帧与 out 结构：

```asm
7d34: sub  sp, #60
7d38: r7 = sp + 16              ; 帧指针
7d3e: r0 = r7 + 4               ; ★ out 结构基址
7d50: bl   0x7D7CD0             ; 初始化
```

而 `0x7D7CD0` **明确把它置 0**：

```asm
7d1c: movs r4, #0
7d26: str  r4, [r2, #16]        ; ★ [out + 16] = 0
```

**`out + 16 = (r7+4) + 16 = r7 + 20`** —— 正是 requester 醒来后检查的 `[r7, #20]`。

**⇒ 结果字段初始就是 0；若运行时观测到非 0，说明中途被覆盖。**

## 十三、谁可能覆盖它

**`0x7D7840`（入队函数，在 `0x7D7DD2` 被调用）对 `+16` 有 12 处写入：**

```
7d78b8  7d78c0  7d7916  7d7946  7d79f8  7d7a10
7d7a78  7d7a94  7d7b06  7d7b0e  7d7bac  7d7bbc
```

**但无法静态确定这些写入的对象是否与 `out` 是同一块内存** —— 它们也可能是链表 item 节点。

## 十四、确定写入者的观察法

盯住 `A = (r7 + 4) + 16` 这一个地址，在四个时刻读取：

| 时刻 | 期望 | 若非期望则说明 |
|---|---|---|
| `0x7D7CD0` 返回后 | 0 | 初始化异常 |
| `0x7D7840`（入队）返回后 | ? | **若已非 0 ⇒ 入队函数写的** |
| 进入 `cond_wait` 前 | ? | |
| 醒来后（`0x7D7E06` 读之前）| ? | 已知非 0 ⇒ 可定位区间 |

**注意：`r7` 由 `sp+16` 推导，探针应在 `0x7D7E06` 读之前取当时的 `r7`，
避免因栈帧变化算错地址。**

## 十五、QEMU 侧已完成的实现（ab_v1531）

```
接管 q2 的 worker：
  取项 → 在【独立线程】调原回调（worker 不被卡住）
       → 等 300ms → trylock(q2+0x50) → signal(q2+0x68) → unlock
       → free(item-8)
```

**实测：**
```
W5 q=0x072e5cd0 / W5 q=0x072e5c28      两个队列都被接管
W5D t=0x00008001                       q2 取到同步请求
CBX enter / CBX exit                   原回调正常返回
W5S q68=0x072e5c90                     ★ 精确 signal 到 q2 完成条件
```

**⇒ 完成通知链路已闭合；剩余问题收敛为「一个 32 位字段的语义」。**
