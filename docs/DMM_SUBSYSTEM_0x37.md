# 子系统 `0x37` = DMM（显存管理）服务 —— 证据与给 QEMU 的接法

> 回答 QEMU 侧：`0x37807D` 是谁？应答结构是什么？
> 结论：**它是 DMM 服务；且不应该伪造队列应答，应该在 API 层拦截。**

---

## 一、Q1 答案：子系统 `0x37` 就是 DMM（Display Memory Manager）

### 证据链（三层，全部可复现）

**① libObj 导入完整的 DMM 客户端类**

```
OBJAVBBCBASECLASS::DmmCh::DmmCh(unsigned short)
OBJAVBBCBASECLASS::DmmCh::allocNew(dmm_comp_alc_unit*, u32, u16, u32)
OBJAVBBCBASECLASS::DmmCh::allocShare(dmm_comp_alc_unit*, u32, u16, u16)
OBJAVBBCBASECLASS::DmmCh::open(dmm_comp_open_unit*, u16, u16, u32)
OBJAVBBCBASECLASS::DmmCh::close(dmm_comp_close_unit*, u16)
OBJAVBBCBASECLASS::DmmCh::free(dmm_comp_free_unit*, u16, u16)
OBJAVBBCBASECLASS::DmmCh::getUnitInfo(dmm_comp_get_unit_info*, u16)
OBJAVBBCBASECLASS::DmmCh::reuse(...)  /  freeSdsAndYc(...)
```
另有 `mcmn_dmm_alloc` / `mcmn_dmm_getextent` 串。

**② 实现在 `libBizFw.so`**（`DmmCh::allocNew` @ `0x35D8C`，260 B）

**③ ★ 决定性：等待 `0x37807D` 的那个函数自己就在调 DMM**

`DataflowInfraReceiver::ReceiveASync`（`dataflowInfraFramework.so` @ `0x228D8`）内部调用序列：

```
osal_rcv_msg_tmo                     ← 等队列 0x37807D（QEMU 观测到的 ARCV）
→ osal_msg_size / memcpy
→ ★ DataflowInfraUtility::AllocOpenMemFromDmmSync(...)
→ arch_phys_to_cache
→ ★ DataflowInfraUtility::CloseMemFromDmmSync(...)
→ ★ DataflowInfraUtility::FreeMemFromDmmSync(...)
→ osal_free_msg
```

⇒ **收消息的路径本身又要向 DMM 申请显存** —— 这就是"无限循环 + 超时"的结构性原因：
DMM 服务不在，收包路径里申请显存失败/阻塞，再回去收包，如此往复。

---

## 二、DMM 请求的真实结构（24 字节，不是 a7c2 的 40 字节）

以 `AllocOpenMemFromDmmSync`（`0x20B80`）为例，它构造的 payload 在栈上 `+0x12C`，长度 **24**：

| 偏移 | 宽 | 值 | 含义 |
|---|---:|---|---|
| `+0x00` | u16 | **1** | 操作码（alloc-open） |
| `+0x04` | u32 | `size` | 申请字节数 |
| `+0x08` | u32 | 0 | — |
| `+0x0C` | u32 | `0xFFFFFFFF` | 未指定物理地址/自动 |
| `+0x10` | u16 | 0 | — |
| `+0x12` | u16 | **1** | 标志 |

发送方式：
```
DataflowInfraMsg::SetSrcId(0xB3)          ← 源 ID = 0xB3
DataflowInfraMsg::SetGlobalMsg(payload, 24)
DataflowInfraSender::SendASync(msg, ...)
DataflowInfraReceiver::ReceiveASync(..., &msg, timeout)
DataflowInfraMsg::GetSrcId(&src) / GetMsgType() / GetMsgNum()
```

⇒ **信封是 `DataflowInfraMsg` 对象**，不是裸 24 字节。
这就是 QEMU 侧"零填充伪造应答 → 崩在 `0xFFFFFFFC`"的原因：**信封无效**。

---

## 三、★★ Q2 答案：不要伪造队列应答，去拦截这 5 个导出函数

`dataflowInfraFramework.so` **导出**以下函数（可直接 `dlsym` 覆盖）：

| 函数 | 地址 | 签名 |
|---|---|---|
| `DataflowInfraUtility::AllocMemFromDmmSync` | `0x20AE1` | `(u32 size, u16 *handle)` |
| `DataflowInfraUtility::AllocOpenMemFromDmmSync` | `0x20B81` | `(u32 size, u16 *handle, int *pa, u32 *pb)` |
| `DataflowInfraUtility::FreeMemFromDmmSync` | `0x20C39` | `(u16 handle)` |
| `DataflowInfraUtility::OpenMemFromDmmSync` | `0x20CC1` | `(u16 handle, int *pa, u32 *pb)` |
| `DataflowInfraUtility::CloseMemFromDmmSync` | `0x20D65` | `(u16 handle)` |

**为什么这条路比伪造队列优雅得多：**

1. **绕开信封问题** —— 不用构造 `DataflowInfraMsg`
2. **绕开递归** —— 收包路径里的 DMM 申请直接被本地满足，不会死循环
3. **绕开 IPC 时序** —— 不需要知道队列 ID、超时、源/目的 ID 的编码
4. 这 5 个都是**普通导出符号**，和你们已经会的 interpose 手法完全一致

**实现建议（bump 分配器）**：

```c
static unsigned char pool[8*1024*1024];
static u32 used;
static u16 handles[256]; static u32 hoff[256]; static u32 hsize[256];
static int nhandle = 0;

int AllocOpenMemFromDmmSync(u32 size, u16 *handle, int *pa, u32 *pb) {
    u32 off = (used + 0xFFF) & ~0xFFFu;
    if (off + size > sizeof pool) return -1;      // 或返回非 0 错误码
    used = off + size;
    handles[nhandle] = (u16)(nhandle + 1);
    hoff[nhandle] = off; hsize[nhandle] = size;
    if (handle) *handle = handles[nhandle];
    if (pa) *pa = (int)(uintptr_t)(pool + off);   // 客户机视角的地址
    if (pb) *pb = size;
    return 0;                                      // 0 = 成功（实测路径里 cbz 判断）
}
```

**还需要一并处理**（同一路径上的其它导入）：
- `arch_phys_to_cache` —— 若客户机做物理↔缓存地址换算，建议先让它恒等
- `DataflowInfraUtility::isPublicCpuTransfer`（`0x20AC5`，26 B）—— 小判断函数，建议照抄语义（可能就是 `(x & 0x80000000)!=0` 之类）

---

## 四、验证判据（沿用之前那条）

DMM 打通后，`0x8A2EE0` 应该能越过"收包超时循环"，继续到：
- `ViewManager::ctor`（`libObj+0x8A4E28`）是否被**固件自己**调用
- `pVM`（`libObj+0x149221C`）是否变为非 0

**如果 `pVM` 变了，就说明 DMM 这条链真的通了。**

---

## ★ 更正（重要）：超时循环里**不涉及 DMM**，`0x37` 不一定是 DMM

上一节把"`ReceiveASync` 里会调 DMM"当成了循环成因，**这是错的**。
重新核对 `ReceiveASync`（`0x228D8`）的**指令顺序**：

```asm
228fc: blx osal_rcv_msg_tmo          ← ① 先收消息
22906: cmp r0, #0
22908: bne 22a0e                     ←    失败 → 直接跳错误路径（不碰 DMM）
22916: bl 0x22548                    ← ② 只有收到消息后才继续
2291e: blx osal_msg_size
22934: blx memcpy
22966: blx isPublicCpuTransfer
2298c: blx GetMsgType / GetMsgNum
229ac: blx AllocOpenMemFromDmmSync    ← ③ 处理"携带内存描述符的消息"时才申请显存
```

⇒ **DMM 申请发生在"收到消息之后"**。QEMU 侧观测到的是"一直超时"，
**根本没进到第 ③ 步**，所以 DMM 未就绪 **不是**当前循环的原因。

### 这个循环的真正含义

`osal_rcv_msg_tmo(qid=0x37807D, …)` 反复返回 `-1022`（超时）
⇒ **就是"队列 0x37807D 上没有任何消息到达"**。

结合 QEMU 侧抓到的 uipc：
```
37A  1 0 134 0000 0     ← 有 1 条积压
37B  0 0 1 0 1 0 0 0    ← 有客户端计数
```
说明 `0x37` 子系统在 OSAL 里**确实有注册**，但**该队列没人投递消息**。

### 队列 ID 的传递路径（已查明）

```
DmmCh / DataflowInfraCh 对象 +4 字段 = qid
        ↓ 作为参数传入
DataflowInfraCh::rcvAsyncGlobalMsg(ch, buf, size, qid, timeout)
        ↓
DataflowInfraReceiver::ReceiveASync(qid, msg, timeout)
        ↓
osal_rcv_msg_tmo(qid, ...)
```

⇒ **qid 是"打开通道时"就定好的字段**，不是运行时算出来的。
⇒ 想知道 `0x37` 归谁，**hook 通道对象的创建或 `ReceiveASync` 的传入参数**最直接。
