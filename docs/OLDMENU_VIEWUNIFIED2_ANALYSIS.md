# 老菜单 `viewUnified2.so` 分析：构造函数不调用 osal、不导入 pthread

> 问题来自 QEMU 线：老菜单 `initializeConfig` 卡住，怀疑
> `viewUnified2.so` 的 1768 个 `DT_INIT_ARRAY` 构造函数里有调 `osal_*` 导致阻塞。

## 一、结论：构造函数里 **0** 处 osal 调用

```
.init_array:  1768 项（全部有效，落在 .text 内，0x18EF20..0x8D98D8）
全 .text 的 osal_* 调用点:  86 处
★ 落在构造函数内的:        0 处
```

按符号分布：

| 符号 | 调用点 |
|---|---:|
| `osal_valloc_msg_wait` | 28 |
| `osal_free_msg` | 24 |
| `osal_snd_msg` | 18 |
| `osal_snd_sync_msg` | 10 |
| `osal_wai_sem_tmo` | 4 |
| `osal_rcv_msg_tmo` | 2 |
| `osal_dynamic_id` / `osal_sig_sem` | 0 |

## 二、★ `viewUnified2.so` **不导入** 任何 pthread/sem 原语

```
UND 符号里 pthread_* / sem_* : 0 个
唯一的等待原语: osal_wai_sem_tmo
```

**⇒ 它不可能自己直接进入 futex 等待。**

`osal_wai_sem_tmo` 的 4 个调用点全在**非构造函数**：

```
0x428EB4 → 函数 0x428EA0
0x429920 → 函数 0x429908
0x5D4F72 → 函数 0x5D4F3C
0x5D501E → 函数 0x5D5000
```
这些函数的调用者（8 处）**也全部是非构造函数**。

**⇒ viewUnified2 的静态初始化不会阻塞。**

## 三、⚠️ 需要先回答的前提：**那真的是死锁吗？**

**condvar 等待在正常运行时极常见 —— 空闲 worker 线程就是这样：**

| 情形 | 判断 |
|---|---|
| 主线程继续跑 + worker 在 `cond_wait` | **正常**，不是死锁 |
| **主线程也阻塞** | 才是死锁 |

**⇒ 建议先确认三件事，再决定是否深挖：**
1. **主线程（tid == pid）是否也阻塞？**
2. `lr=0x0409CD8D` 对应 libpthread 的哪个函数（`cond_wait` / `sem_wait` / `join`）？
3. 该线程的创建者是谁（App 自起的 worker ⇒ 空闲等待正常）？

**⇒ 若只有 worker 在等，那 `initializeConfig` 卡住的真正原因在别处，
继续追这个等待会白费一轮。**

## 四、工具教训（本轮再次踩到）

**前导零 bug**：`readelf`/`objdump` 的标签是 `0018ca14`，而指令操作数是 `18ca14`。

```
❌ grep -E "blx\s+0018ca14 "     → 恒 0（看起来像"没有调用"）
✅ grep -E "blx\s+0*18ca14 "     → 真实计数
```

**⇒ 本项目第二次踩同一个坑。规则：所有地址匹配必须用 `0*` 前缀，并先做阳性对照。**
