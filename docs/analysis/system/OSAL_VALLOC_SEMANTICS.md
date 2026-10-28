# `osal_valloc_msg_wait` 是**消息池分配**，不是等 IPC 应答

> 回答 QEMU 侧："Appframework 卡在 `osal_valloc_msg_wait`，是在等分配还是在等应答？"
> 结论：**在等分配**。这改变了整条因果链的解释。

## 一、实现证据（`libosal_uipc.so`）

符号与内部函数：

```
osal_valloc_msg_wait        ← 对外接口
  └ u_osal_valloc_msg_wait   ← 内部实现
      └ vpool_alloc → alloc_fblock / free_fblock
vpool_dump / fblock_dump / msgq_dump   ← 调试输出
0x%x Already free
[U]**UIPC ERROR** %s: double free detected. 0x%08x   ← 池的 double-free 检查
```

⇒ `valloc` = 从 **vpool（消息内存池）** 取一块；池中无可用块时**阻塞等待**。
它**不是** `osal_rcv_msg`/`snd_sync` 那种"发请求等对端回复"的语义。

（对比：真正的"等回复"是 `osal_snd_sync_msg` / `osal_rcv_msg_tmo`。）

## 二、★ 池账本：结合 QEMU 侧实测数字

QEMU 侧 `/proc/osal/uipc` 里：

```
Global: msgqs 273, sems 71, msgs 441, cbs 63
fblock: (19456) free=266752
有积压队列(M>0)：13A=428, 20=4, 3C=2, F9=2, 48=1
```

`(19456) free=266752` 对应池 dump 的 `(%d) free=%d` 格式。按块大小 19456 B 折算：

```
266752 / 19456 ≈ 13.7  → 只剩约 13 块
```

而**队列 0x13A 里压着 428 条未消费消息**（占全部 441 条的 97%）。
每条消息在未释放前**占着一块**。

⇒ **池几乎被 0x13A 的积压消息吃干**，于是任何后续 `valloc_msg_wait`
都在等空闲块 —— 这解释了"Appframework 线程卡在 futex"，
且**与"等谁回复"完全无关**。

## 三、修正后的因果链

```
无人消费队列 0x13A
      ↓
428 条消息常驻，各占一个池块
      ↓
vpool 只剩 ~13 块
      ↓
Appframework 起线程后第一件事 createRequestModelExecuteEvent
  → osal_valloc_msg_wait(52)  ← libObj+0x8A232E
      ↓
阻塞（等空闲块），futex 挂住
      ↓
ViewManager / GraphicsEngine / WidgetSystem 永远建不起来 → 没有 UI
```

⇒ **要修的不是"补应答方"，而是"为什么 0x13A 没人消费"**。
一旦有消费者把队列排空，池就恢复，`valloc_msg_wait` 立刻返回。

## 四、`0x13A` / 子系统 `0x8F` 的归属：静态查不到，只能真机读

- `libMWF.so` **导入 `osal_dynamic_id`** ⇒ **子系统 ID 是运行时动态分配的**，
  不是编译期固定枚举
- `libSysDef.so` 里那 77 个 `CATEID_*`（`CATEID_APP_VIEWS` / `CATEID_RENDERER` …）
  是**日志分类**，不是 UIPC 数字 ID
- IMDB 表里的 `target` 是 target bit，也不是子系统 ID

⇒ **`0x8F` 只能从 `proc/osal/uipc` 本身读出**（它按进程列出子系统与队列）。
真机一份 → 逐行 diff 就能知道 0x13A 在真机上属于谁、有没有消费者。

## 五、对 QEMU 侧的下一步建议（按性价比）

1. **先确认池是否真的紧**：在 QEMU 里 dump `vpool` / `fblock` 的完整统计
   （`vpool_dump`/`fblock_dump` 的输出格式已知：`(%d) free=%d`）。
   若确认只剩十几块 ⇒ 因果链成立，**问题就是 0x13A 的泄漏**
2. **找出谁在往 0x13A 发**：interpose `osal_snd_msg` / `osal_snd_sync_msg`
   记录调用者 LR + 目标 qid，即可定位"发送方"。发送方找到了，
   消费者要找的目标就明确了。
3. interpose `osal_valloc_msg_wait` 时，除了参数**还要记录"返回耗时"**：
   - 立即返回 ⇒ 不是池问题
   - 长时间阻塞 ⇒ 池紧，回到第 1 步

## 六、真机侧我能配合的

采集脚本已包含整棵 `/proc/osal/`（含 `uipc`）。抓到真机副本后：
- 对比真机 `Global` 行的 msgqs/msgs 数量
- 对比**真机有没有 M>0 的积压队列**（若真机全是 0 ⇒ 强烈支持"QEMU 里 0x13A 无消费者"）
- 看真机 0x13A 属于哪个进程
