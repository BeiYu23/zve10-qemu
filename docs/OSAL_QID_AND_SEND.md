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
