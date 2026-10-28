# OSAL 队列 ID 编码 与 发送 API（ZV-E10 实证）

> 全部来自 `libosal_uipc.so`（ZV-E10 `/usr/lib`）反汇编。

---

## 一、★ 队列 ID 编码：`0x8000` 位 = **全局/跨子系统标志**

`osal_snd_msg`（`0x3C34`）与 `osal_rcv_msg_tmo`（`0x3ED8`）**开头第一条指令就是同一个判断**：

```asm
osal_snd_msg:     3c34: f410 4f00  tst.w r0, #0x8000
osal_rcv_msg_tmo: 3ed8: f410 4f00  tst.w r0, #0x8000
                          ...
                  3c46/3ee8: bne  ← bit15 置位 → 走「全局」分支
                  3c48/3eea:        ← bit15 清零 → 走「本地」分支（尾调用 u_osal_*）
```

**全局分支里还会做 `qid & 0x7FFF`**（`mov.w r0,r0,lsl #17` + `lsrs r0,r0,#17`）
取低 15 位当本地队列索引，而**完整 qid 保存在 r8** 供跨子系统路由用。

### 由此推出编码格式

```
qid = (port_id << 16) | (0x8000 | local_index)
                          └ bit15 = 1 表示"全局/跨子系统"
```

### 用已知样本验证

| qid | port_id (高16) | bit15 | local | 路径 |
|---|---|---|---|---|
| `0x8F013A` | `0x8F01` | **0** | `0x13A` | **本地** |
| `0x8280074D` | `0x8280` | **0** | `0x74D` | **本地** |
| `0x9088076B` | `0x9088` | **0** | `0x76B` | **本地** |
| **`0x37807D`** | `0x3780` | **1** | `0x7D` | **★ 全局/跨子系统** |
| `0x0055011E` | `0x0055` | **0** | `0x11E` | 本地（DataflowInfra 每线程通道） |

⇒ **Q1 答案**：`0x807D` 的 `0x8000` **就是"同步/全局队列"标志**（更准确说：跨子系统全局队列）。
⇒ 这也解释了为什么 QEMU 侧看到其它线程的 `0x0055011E/11F/120/121/122`（bit15=0，本地）；
   而 `0x37807D` 是**唯一带全局标志**的 —— 它确实与众不同。

---

## 二、★ 发送 API 签名：`osal_snd_msg` 只有 **2 个参数**

```asm
3c34: tst.w r0, #0x8000     ; arg0 = qid
3c44: mov   r4, r1          ; arg1 = msg 指针
3c46: bne   3c52            ; 全局分支
3c48: (本地) 尾调用 u_osal_snd_msg   ← r0, r1 原样传下去
3c52: cmp   r0, #0 → 负数则 -1024
3c64: cmp   r1, #0 → 空指针则 -1024
```

```c
int osal_snd_msg(unsigned int qid, void *msg);       // 2 参数
```

**QEMU 侧观察到的"4 个参数"应该是另一个函数** —— 最可能是：

```asm
osal_snd_sync_direct: 4134
  413a: r6 = r0                       ; arg0
  413e: fp = r1                       ; arg1 = 指针
  4140: if (r1 == 0) → -1024
  4156: r3 = *(u32*)(r1)              ; ★ 读 arg1 指向结构的第一个字
  4158: cmp r3, #16 ; bls → -1024     ; ★ 该字段必须 > 16（多半是 size）
  415c: tst.w r0, #0x8000             ; 同样走全局/本地分支
```

⇒ `osal_snd_sync_direct(qid, struct*, …)`，其中 `struct[0] > 16`。

**其它可用的原语**（同一模块）：

| 函数 | 地址 | 大小 |
|---|---|---|
| `osal_snd_msg` | `0x3C34` | 272 B |
| `osal_rcv_msg_tmo` | `0x3ED8` | 276 B |
| `osal_snd_sync_msg` | `0x3EC8` | 16 B（薄封装） |
| `__osal_snd_sync_msg` | `0x3DB0` | 278 B |
| `osal_snd_sync_direct` | `0x4134` | 336 B |
| `osal_reg_msg_queue_cb` | `0x4300` | 220 B |
| `osal_reg_sync_msg_queue_cb` | `0x3AE0` | 116 B |
| `osal_msg_size` | `0x4B80` | 36 B |
| `osal_get_msg_pri` / `set` | `0x4408` / `0x43DC` | 38 / 42 B |
| `osal_set_msgq_broadcast_mode` | `0x4464` | 132 B |

---

## 三、★ Q2 答案：可以冒充服务端投递，但要按顺序用两个原语

**不能**直接 `osal_snd_msg(qid, buf)` 传一个裸缓冲 —— 消息必须是 **osal 分配的消息块**
（发送侧内部会去查队列对象 `0x3604`，再把它挂到队列上）。

正确做法：

```c
void *msg = NULL;
// 1) 申请一个消息块（4 参数：qid, &out, size, flags）
(void)osal_valloc_msg_wait(0x37807D, &msg, payload_size, 0);
if (!msg) return -1;

// 2) 填负载
memcpy(msg, payload, payload_size);

// 3) 发送（2 参数）
osal_snd_msg(0x37807D, msg);
```

**注意**：
- `osal_snd_msg` 对空指针返回 `-1024`；对负数 qid 也返回 `-1024`
- 若队列未注册，第 3 步内部查表失败 —— 这时应先确认该队列**存在**（QEMU 侧 uipc dump 里
  `0x3780` port 下有注册记录才行）
- **别自己 malloc 一个裸 buffer 传进去** —— 那正是"零填充伪造崩在 `0xFFFFFFFC`"的同类错误

---

## 四、信号量 ID `0x83037A` 的性质（QEMU 卡点）

### 4.1 格式：与队列同一套约定

`osal_wai_sem_tmo`（`0x2E68`）开头：

```asm
2e68: r3 = 0x1B7741            ; timeout 上限
2e96: blx check_dynamic_sem_id ; ★ 先判断是否"动态 id"
2e9a: tst.w r5, #0x8000        ; ★ 同样检查 bit15
2e9e: bne 全局分支
2ea0: (本地) u_osal_wai_sem_tmo(id, timeout)
```

⇒ 信号量 id 也遵循 `(port << 16) | (0x8000 | local)`：

| id | port | bit15 | local | 判定 |
|---|---|---|---|---|
| **`0x83037A`** | `0x0830` | **0** | `0x37A` | **本地信号量** |

### 4.2 ★ 它是**运行时动态分配**的，不是固定服务 id

`check_dynamic_sem_id`（`0x47C0`）的实现：

```asm
47c2: tst.w r0, #0x8000
47dc: r2 = sem_id & 0x7FFF          ; 本地索引
47e0: r1 = *(table+4)               ; 动态 id 的起始值
47e0: cmp r1, r2                    ; 索引 >= 起始值 ?
47f0: r0 = *(table+12)              ; ★ 位图基址
47f2: r1 = r2 & 31 ; r4 = 1 << r1
47f8: r2 >>= 5
47fc: r3 = bitmap[r2]
4800: r4 &= r3                      ; 检查该位是否已分配
```

⇒ **动态信号量用位图管理**，由运行时分配器发放。

### 4.3 因此：静态定位不到"谁该 signal"

- 常量 `0x0083037A` 在**整个 `/usr/lib` 里只出现在 `libObj.so`**（且是**等待侧**的字面量）
- 其它任何库都没有硬编码这个 id ⇒ **没有模块"认识"这个固定 id**
- 结合 4.2 的位图机制 ⇒ **它是运行时分配出来的 id**，静态无法映射到所有者

⇒ **想找所有者只能动态**。可用的两个原语：

| 用途 | 函数 | 地址 |
|---|---|---|
| 观察谁 **signal** | `osal_sig_sem(id)` | `0x2F74` |
| 查询信号量**状态** | `osal_stat_sem(id, &state)` | `0x2E34` |
| 等待 | `osal_wai_sem_tmo(id, timeout)` | `0x2E68` |

**建议先用 `osal_stat_sem(0x83037A, &state)` 看它是否被创建、当前计数**，
再决定要不要 signal —— 比盲 post 更有信息量。

---

## 五、直接 signal 是否安全？（Q2 回答）

**技术上是安全的**：`osal_sig_sem` 只是给信号量 +1，**不改任何持久状态、不写盘、不改固件**，
最坏情况是让等待方在"条件未真正满足"时继续执行。

**但要预期到两类后果**：
1. **等待方往下走 → 撞到下一个未满足条件**（QEMU 侧已经观察到"跳过 HSEM 后又进 `hrtimer_nanosleep`"）
   ⇒ 这是**信息**，不是坏事，说明后面还有轮询
2. **等待方假设某数据结构已就绪** ⇒ 可能崩在某个数据地址
   ⇒ 崩点地址本身**指向缺什么**，比现在这样干等着更有价值

⇒ **QEMU 侧提出的"逐步补齐就绪条件"策略是对的**，我支持。
   每一步只补一个条件、观察下一个等待点，这是当前信息量最大的推进方式。

**一条纪律**：每次只 signal **一次**，不要放进循环里 —— 否则会掩盖"到底等了几次"。

---

## 六、★ 更正：`0x83037A` 是**静态** id，且信号量状态在**内核模块**里

### 6.1 我上轮说"动态分配"是错的

`check_dynamic_sem_id`（`0x47C0`）的逻辑：

```asm
47c2: tst.w r0, #0x8000
47d0: ite ne → r4 = 0 (bit15 置位) : r4 = 1 (bit15 清零)
47dc: r0 = r4 ^ 1                  ; = bit15 ? 1 : 0
47e0: cmp base, index
47e2: ite hi → r0 = 0
47e6: andls   → r0 &= 1
47ec: cbz r0 → 跳过位图检查
```

⇒ **位图检查只对 `bit15=1` 的 id 生效**。
`0x83037A` 的 bit15 = **0** ⇒ **走静态路径，不是动态分配**。

### 6.2 ★ 信号量实现在内核模块 `osal_uipc.ko`

用户态三个函数全都是 **ioctl 薄封装**（无用户态状态）：

```asm
u_osal_stat_sem:     ioctl(fd, 0x80000000, &{id,  &out})
u_osal_wai_sem_tmo:  ioctl(fd, 0x8001,     &{id,  timeout})
u_osal_sig_sem:      ioctl(fd, 0x8002,     &{id,  0})
```

内核模块里对应符号：
```
__k_uipc_cdev_ioctl   __k_4u_sig_sem   __k_4u_wai_sem_tmo
__k_4u_stat_sem       __k_4u_wait_cb    get_bsem / bsem_dump
字符串: "[] GSEM"   "[] BSE"
```

⇒ **signal 到达的是内核里的同一个对象**（id 原样透传，见 6.3）。
⇒ **"signal 返回 0 但没放行" 的原因只能在 `osal_uipc.ko` 内部**，
   而 `__k_4u_wait_cb` 的存在提示它可能是**"等待回调"语义**，不是简单计数。

### 6.3 两个函数对 id 的处理完全一致

```asm
osal_sig_sem:      2f7e: tst.w r4, #0x8000 ; 2f84(本地) → r0=r4 尾调 u_osal_sig_sem
osal_wai_sem_tmo:  2e9a: tst.w r5, #0x8000 ; 2ea0(本地) → u_osal_wai_sem_tmo(id, timeout)
```

⇒ **同一 id、同一路透传**，signal 不可能打到别的对象上。

---

## 七、给 QEMU 的三条可执行建议

### ① 诊断：用 `osal_stat_sem` 验证 signal 是否真的落地

```c
unsigned int st[2] = {0};
osal_stat_sem(0x83037A, st);        // signal 前
osal_sig_sem(0x83037A);
osal_stat_sem(0x83037A, st2);       // signal 后
```
- **状态变了** ⇒ signal 落地了，但等待方不在这个 sem 上（或语义不是计数）
- **状态没变** ⇒ signal 没落地（id 解析或内核侧拒绝）

### ② ★ 试"抢先 signal"（在进入 `0x8A2EE0` **之前**就 post）

如果它是**二元/事件**信号量，等待方会把它**消费掉**：
- **之后**再 signal 时，等待方可能已经错过了那一次
- **之前** post，等待方第一次 wait 就能立即通过

```c
osal_sig_sem(0x83037A);      // 先预置
call(0x8A2EE0);              // 再进
```
这条比"事后 signal × 6"更可能奏效。

### ③ ★★ 更快的路：直接**中和这个 wait**，逐个打通

既然每次都被"某个 ready 条件"挡住，而 QEMU 环境是实验性的、弄坏也无成本：

```c
int my_wai_sem_tmo(u32 id, int timeout) {
    if (id == 0x83037A) return 0;      // 只中和这一个
    return real_wai_sem_tmo(id, timeout);
}
```

你们已经试过"返回 0 跳过"——结果不再出现 HSEM，但进了另一个 `hrtimer_nanosleep`。
**这本身就是进展**：说明后面还有下一个等待点。

⇒ **策略：每中和一个等待点，就记录下一个等待点的 (类型, id, LR)**，
   直到全部通过或出现数据依赖崩溃。
   这条路每一轮都能推进一个点，且不会卡死在某一个上。

**判据仍然是**：`pVM`（`0x149221C`）是否变非 0、`ViewManager::ctor`（`0x8A4E28`）是否被固件自己调用。
