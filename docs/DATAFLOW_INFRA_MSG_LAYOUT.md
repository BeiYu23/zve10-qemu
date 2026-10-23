# `DataflowInfraMsg` 内存布局（QEMU 造应答用）

> 全部来自 `dataflowInfraFramework.so`（ZV-E10 `/usr/lib`）反汇编实证。

---

## 一、函数签名（澄清参数序）

`DataflowInfraReceiver::ReceiveASync` 有 **3 个重载**，参数序完全一致：

```cpp
int ReceiveASync(unsigned int qid, DataflowInfraMsg*,   unsigned long timeout);  // 0x228D8, 356B
int ReceiveASync(unsigned int qid, DataflowInfraMsgEx*, unsigned long timeout);  // 0x227F0, 232B
int ReceiveASync(unsigned int qid, DataflowInfraMsgEx2*,unsigned long timeout);  // 0x22704, 236B
```

寄存器：**`this=r0`，`qid=r1`，`msg=r2`，`timeout=r3`**。

⇒ QEMU 侧记录的 `msg=0x0055011E` 是 **r2**。但 `0x…1E` **不是 4 字节对齐**，
   而 C++ 对象指针必须 4 对齐 ⇒ **要么 hook 读错了参数槽，要么这不是 `DataflowInfraMsg*`**。
   **建议分开记录 r1 和 r2，并检查 `msg & 3 == 0`。**

**返回值约定**：`0 = 成功`（调用方用 `cbz r0` 判断），非 0 = 失败。

---

## 二、`DataflowInfraMsgEx` 对象布局（构造函数实证）

```asm
ctor:  [this+0x00] = vptr
       strh 0,  [this+0x08]        ; msgSize/msgNum (u16)
       str  0,  [this+0x04]        ; msgType (u32)
       str  0,  [this+0x0C]        ; srcId   (u32)
       r2 = this + 0x14
       str  r2, [this+0x10]        ; ★ msgBuf 指针 = 指向自己的 +0x14（内嵌缓冲）
```

| 偏移 | 宽 | 字段 | 说明 |
|---|---:|---|---|
| `+0x00` | 4 | vptr | — |
| `+0x04` | 4 | **msgType** | `0`=未初始化 / `1`=远程消息 / `2`=全局(本地)消息 |
| `+0x08` | 2 | **msgSize** | `SetGlobalMsg` 写入（u16） |
| `+0x0C` | 4 | **srcId** | `SetSrcId`/`GetSrcId` 读写 |
| `+0x10` | 4 | **msgBuf*** | 构造时 = `this + 0x14` |
| `+0x14` | ≤0x140 | **内嵌消息缓冲** | 最大 **320 字节**（`GetMaxSize()` 返回 `0x140`） |

---

## 三、各方法的确切行为

### `SetGlobalMsg(void* data, u16 size)` @ `0x20E44`

```
if (msgBuf == 0)          → 错误 -20
if (msgType != 0)         → 错误 -29     ← 必须先处于未初始化状态
if (size >= 0x140)        → 错误 -28     ← 上限 320
*(u16*)(this+0x08) = size
*(u32*)(this+0x04) = 2                   ← ★ 自动设为"全局消息"类型
memcpy(msgBuf, data, size)
```

### `GetMsgType()` @ `0x21026`
```
return *(u32*)(this+0x04)
```

### `GetTopMsgSize()` @ `0x2102E`
```
buf = *(u32*)(this+0x10)
if (!buf) return 0
if (msgType == 2) return *(u16*)(this+0x08)          ← 本地消息：直接读 +8
if (msgType == 1) { off = *(u32*)(buf+12); return *(u16*)(buf+off+4); }   ← 远程消息
return 0
```

### `GetReplyId(u32* out)` @ `0x20FAC`
```
buf = *(u32*)(this+0x10)
if (!buf) → 错误 -20
if (msgType == 1) { *out = *(u32*)(buf+0); return 0; }    ← ★ replyId 在 buf+0
if (msgType == 0 || 2) → 错误 -30                        ← 本地消息没有 replyId
return 0xFFFF
```

### `GetSrcId(u32* out)` @ `0x20FF8` / `SetSrcId(u32)` @ `0x20FA0`
```
读写 *(u32*)(this+0x0C)
```

### `MakeDataflowHeader()` @ `0x2105E`
```
buf = *(u32*)(this+0x10)
buf[0] = 0
*(u16*)(buf+8) = 0
r1 = 0xA0A0A0A0          ← ★ 魔数（写到哪里需继续确认，很可能是 buf+4）
```

---

## 四、★ 造一条合法应答的最小要求

### 路线 A（推荐）：全局消息，最简单

```c
DataflowInfraMsgEx *m = (DataflowInfraMsgEx *)msg;   // 调用方给的指针
m->[0x04] = 2;                 // msgType = 2（全局/本地）
m->[0x08] = (u16)size;         // msgSize
m->[0x0C] = srcId;             // 源 ID
// m->[0x10] 仍指向 m+0x14，无需改
memcpy((char*)m + 0x14, payload, size);   // payload 拷进内嵌缓冲
// m->[0x14] 起的布局由"业务协议"决定（这次是 24 字节 dmm_comp_alc_unit）
return 0;                       // 0 = 成功
```

**注意 `size < 0x140`（320）**，否则 `GetTopMsgSize` 侧会不认。

### 路线 B（远程风格，带 replyId）

若调用方走 `msgType==1` 分支，则内嵌缓冲布局必须是：
```
buf+0x00  replyId  (u32)
buf+0x0C  off      (u32)      ← 偏移量
buf+off+4 topMsgSize (u16)
```

---

## 五、QEMU 侧"返回 0 就重试 / 返回 1 就崩"的解释

- **返回 0 但没填 msg** ⇒ 调用方认为收到消息，去 `GetMsgType()` 拿到 **0（未初始化）**
  ⇒ 类型不认识 ⇒ 回到收包循环 ⇒ 表现为"重试"
- **返回 1（错误）** ⇒ 调用方走错误路径 ⇒ 用未初始化的 msg 解析 ⇒ 崩在 `msg` 相关地址

⇒ **正确做法是"返回 0 + 把 msg 填成合法内容"**，而不是靠返回值区分。

---

## 六、`ReceiveASync` 内部实证（`0x228D8`，逐条）

```asm
228e2: r6 = r1                     ; qid
228de: r5 = r2                     ; msg
228e4: r2 = r3                     ; timeout
228f6: r1 = sp+8
228fa: r0 = r6                     ; ★ qid 直接来自 r1，无换算
228fc: blx osal_rcv_msg_tmo(qid=r1, &localbuf, timeout)
22900: 转换 OSAL 错误码 → r4
22908: if (r4 != 0) goto 错误路径   ; 0 = 成功
2290c: r8 = *(sp+8)                ; 收到的 osal 消息指针
22910: r9 = r5 + 16                ; ★ msg+0x10 = 消息缓冲指针
22916: bl 0x22548                  ; 从 osal 消息取源地址
2291a: str r0, [r5, #12]           ; ★ msg+0x0C = srcId（由本函数写入）
2291e: osal_msg_size(msg)
22924: r8 = min(size, 0x100)       ; ★ 单条上限 256 字节
```

**结论**：
1. **qid 就是 r1**（三个重载都一样，`0x227F0` 处同样是 `r0 = r5(r1)`）
2. 本函数**自己会写 `msg+0x0C = srcId`**，并读取 `msg+0x10` 当缓冲指针
3. 单条消息**上限 256 字节**（不是 320）

⇒ QEMU 侧记录的 `a1=0x0055011E` **确实是 qid**；
   而早前观测到的 `0x37807D` 说明 **存在两个不同的等待者**（两次运行/两条路径）。
   **建议在 uipc dump 里查子系统 `0x0055`**（`0x0055011E` = 子系统 `0x55` + 局部 `0x011E`），
   看它是否也有注册与客户端计数 —— 那才是这次拦到的那个。

---

## 七、`osal_dynamic_id`：**不要 hook**

- 定义在 **`libosal_uipc.so` @ `0x4841`**（约 55 个库导入它）
- 签名是 **3 参数**（不是 1 参数），且内部**带锁**：

```asm
4840: push {r4-r11,lr}
4844: ands r3, r0, #2          ; flags bit1
4850: r8 = r1
485c: r6 = r2
485e: cmp r2, #0 ; beq 错误    ; ★ r2 必须非 0
486a: ands r2, r0, #16         ; flags bit4
4872: blx osal_lock            ; ★ 内部加锁
487c: ands fp, r4, #64         ; flags bit6
```

⇒ QEMU 侧"hook 它之后 `rm_init` 卡住"，最可能是**没把调用正确转发**（拦住了锁/分配路径）。

**建议**：**不要 hook `osal_dynamic_id`**。
若一定要看子系统分配，就做"**包装**"（先调原函数再记返回），且注意它内部会拿锁，
在钩子里做任何耗时操作都可能死锁。

---

## 八、给 QEMU 的最小合法消息（复述，含实证修正）

```c
char *m = (char *)msg;
*(u32*)(m + 0x04) = 2;                 // msgType = 2（全局/本地，最简单）
*(u16*)(m + 0x08) = (u16)size;         // size，必须 ≤ 0x100（本路径上限）
*(u32*)(m + 0x0C) = srcId;             // 源 ID（真路径由 ReceiveASync 自己写）
// m+0x10 不用动：已由构造函数指向 m+0x14
memcpy(m + 0x14, payload, size);       // 业务负载
return 0;                               // 0 = 成功
```

**若调用方走 `msgType==1`（远程消息）分支**，内嵌缓冲布局：
```
buf+0x00  replyId   (u32)
buf+0x0C  off       (u32)
buf+off+4 topMsgSize(u16)
```

---

## 九、✅ 确认：`r1` 就是 qid，直接透传（机器码级证据）

```asm
228e2: 460e        mov   r6, r1        ; ★ r6 = 入口 r1（保存）
228f6: f107 0108   add.w r1, r7, #8    ; ★ r1 被覆盖 = 局部缓冲 (sp+8)
228fa: 4630        mov   r0, r6        ; ★ arg0 = 入口 r1
228fc: f7fd ed0e   blx   osal_rcv_msg_tmo
```

⇒ **`osal_rcv_msg_tmo(qid = 入口 r1, buf = &sp[8], timeout)`**

**结论：`r1` 是 qid，接收缓冲是局部变量，不是 r1。**
⇒ 因此 `0x0055011E` 与 `0x37807D` **必然来自两次不同的调用**（不同线程/不同调用点）。

---

## 十、为什么 `self` 每次都变、`msgType/msgSize` 每次都是 0

### `self` 是**调用方的栈上对象**，不是堆对象

`DataflowInfraCh::rcvAsyncGlobalMsg` 里：

```asm
33e56: add.w r0, r7, #276 ; 0x114      ; ← 栈上槽位
33e58: blx   DataflowInfraReceiverC1   ; 构造一个局部 receiver
33e5e: add.w r0, r7, #276 ; 0x114
33e62: mov   r1, r4                    ; qid
33e64: mov   r2, r7                    ; msg
33e68: blx   ReceiveASync              ; this = 栈地址
```

⇒ **`self` 永远是调用方栈帧里的地址** ⇒ 每次调用/每个线程都不同，**这是正常的**，
   不代表"对象不对"。

### `t=0 sz=0` 每次都是 0 的含义

`msg` 是调用方（`DmmCh`/`DataflowInfraCh` 的使用者）持有的对象。
进入时 `msgType==0 && msgSize==0` 说明：
- 调用方**每次调用前都重新初始化了 msg**（或用的是刚构造的新对象）

⇒ **在 interposer 里填好它、返回 0，下一轮会被覆盖 —— 所以"填了没效果"是预期的。**

真正该做的是**让调用方的业务流程往前走**，而不是反复往一个每轮都被重置的对象里灌数据。
