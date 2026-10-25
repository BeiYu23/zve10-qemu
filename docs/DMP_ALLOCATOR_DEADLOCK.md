# DMP 分配器死锁假设 —— 已被实验证伪

> **状态：❌ 已证伪（2026-10-25，第 2/40 轮实验）**
> 保留本文是为了记录推理路径与证伪方法，避免后人重复。

## 原假设（本文档旧版本）

`grm_init` 的 worker 在回调里进入 DMP 引用计数生命周期，在持有一把全局 mutex 的情况下
尾调用 `dmp_allocator_lib_init`，后者开头无条件等待 semaphore `0x18004B5`（timeout = -1），
锁永不释放 ⇒ 请求方、其它 worker、`initializeConfig` 全部堵死。

## 证伪实验（QEMU 侧执行）

| # | 实验 | 结果 |
|---|---|---|
| ① | interpose `dmp_allocator_mmap_init` → `return 0`（PLT 路径，`DMMI_RET` 命中） | **EN=1 / EX=0，仍卡住** |
| ② | 二进制补丁 `libObj 0x6BCB58`（`dmp_allocator_lib_init` 开头 → `movs r0,#0; bx lr`） | **EN=1 / EX=0，无崩溃，仍卡住** |

**⇒ 即使 `dmp_allocator_lib_init` 立即返回，卡点不变 ⇒ 它不是阻塞源。**

**附带发现**：`dmp_allocator_lib_init` 在 libObj 内部是**直接 `bl`** 调用，不走 PLT，
所以做法 ① 天然无效；做法 ② 是有效的，但同样没解开 ⇒ 假设本身错。

## 更正后的正确图景

反汇编 `libObj+0x7D7C10`（worker 循环）逐指令还原：

```c
worker(queue_t *q)
{
    r5 = q + 4;      // 队列 mutex
    fp = q + 0x20;   // 队列 cond
    r6 = q + 0x50;   // 完成 mutex
    sl = q + 0x68;   // 完成 cond

loop:
    pthread_mutex_lock(q+4);              // 0x7D7C32
    if (q->count == 0) {                  // q+0x98
        pthread_cond_wait(q+0x20, q+4);   // 0x7D7CA2  空闲等待
        goto loop;
    }
    item = dequeue(q + 0x98);             // 0x7D7BF0
    pthread_mutex_unlock(q+4);            // 0x7D7C50  ★ 回调【之前】已解锁

    if (item[-8] == 2) {
        cb = q->[0xA4];                   // 0x7D7C82
        cb(item[4]);                      // 0x7D7C88  ★ 回调（无锁）
        free(item - 8);
        goto loop;
    }
    ...
    pthread_mutex_lock(q+0x50);           // 0x7D7C64
    pthread_cond_signal(q+0x68);          // 0x7D7C6A
    pthread_mutex_unlock(q+0x50);
}
```

**关键点：**

1. **`+0x7D7C37` 是 `0x7D7C32` 那条 `blx` 的返回地址（LR = 0x7D7C36|1），
   而它在 `0x7D7C50` 就被释放 —— 在回调之前。**
   ⇒ "worker 持有队列锁却在回调里等锁" 的前提不成立。

2. **`libObj+0x7858E0` 是 16 字节通用加锁包装**（`return pthread_mutex_lock(m) != 0`），
   mutex 由调用者传入。libObj 里有 **172 个调用点**、**401 个解锁点**。
   ⇒ 所有调用者的 LR 都被压成同一个 `0x7858E9`，**该值无法用于归因**。

3. **真正的阻塞点是回调 `q->[0xA4]`**：worker 出队、解锁、进入回调后不返回，
   于是该 worker 无法再处理后续请求，队列积压，请求方与 `initializeConfig` 全部等待。

4. 回调极可能是 **GPU 命令提交**，提交后等"GPU 完成"信号；QEMU 无真 GPU ⇒ 信号永不到来。
   这与 `CmdGpeFlush → 同步 Flush → callback → 交换缓冲` 的既有描述一致。

## 下一步（待验证）

在 `0x7D7C88` 前后打 enter/exit 两点：

```c
void *cb = *(void **)(q + 0xA4);
log("QCB enter cb=", cb);
cb(*(void **)(item + 4));
log("QCB exit");
```

- 只有 `enter` 无 `exit` ⇒ 确认阻塞在回调
- 取到 `cb` 真实地址后即可解它等待的对象

## 方法论教训

**"看到某线程持有一把锁 + 另一线程等这把锁" 不等于死锁成因** ——
必须先确认**这把锁在该线程的当前调用路径上是否仍被持有**。
本例中 LR 指向的加锁点早已经由对应的 unlock 释放，锁归因是错的。
