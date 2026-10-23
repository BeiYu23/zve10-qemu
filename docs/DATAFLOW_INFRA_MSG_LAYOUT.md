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
