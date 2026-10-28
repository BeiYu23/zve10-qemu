
## 181. 2026-10-28 ★ 按 a7c2-re 的方法：按键是"往 InfraInput 队列投一条 20 字节消息"

a7c2-re 的注入器（`runtime/analysis/inject-key.c`）做法：

```c
osal_valloc_msg_wait(queue, &msg, 20, 1);
unsigned *w = msg;
w[0]=key_code; w[1]=sub_key_code; w[2]=status; w[3]=ex_info; w[4]=from_type;
osal_set_msg_src(msg, src);
osal_snd_msg(queue, msg);
```

消息布局来自 `InfraInput::RecvThread` 的解析代码（a7c2: libObj VA 0xd031c..0xd0354）。

他们有证据的实参（a7c2 专属常量）：

```
queue = 0x807a0020     src = 0x92840747
KEYS = {MENU:0x1ab, ENTER:0x1ac, UP:0x3, DOWN:0x30, LEFT:0x15, RIGHT:0x1af, DELETE:0x1f8}
消息: {key_code, sub=0, status=0x1, ex=0, from=0x1}
```

### 在 ZV-E10 上的进展

* 我们的库已实现同样的 `#K <key> <sub> <status> <ex> <from>` 命令
  （`osal_valloc_msg_wait` / `osal_set_msg_src` / `osal_snd_msg` 从 libosal_uipc 取）；
* `#N` 会调 `libObj+0x8c2690(instance)` 拿 helper 对象并 dump 前 0x40 字节 ——
  `SetOsalId` 反汇编显示它把第三个参数存到 `helper+0x20`，那里应该是队列 id；
* **实测 `helper+0x20 == 0`** ⇒ 要么 SetOsalId 尚未被调用，要么实例不同。

### 下一轮

1. 在 ZV-E10 的 libObj 里找 `InfraInput::RecvThread`（它调用 `osal_rcv_msg_tmo`），
   从它的代码里直接读出队列 id 常量；
2. 拿到 qid 后用 `#K` 发 UP/ENTER（a7c2 键码先试），看画面 SHA256 是否变化；
3. 若键码表不同，再从 `CmnViewExpMode::mapKeyCode` 反推 ZV-E10 的键码。

## 182. 2026-10-28 找 ZV-E10 的 InfraInput 队列 id（进行中）

`osal_rcv_msg_tmo` 在 libObj 里只有 3 个调用点（0x113d86 / 0x195364 / 0x19545e）。
最像"输入接收线程"的是 0x113d7c 附近：

```
113d7c: ldr r0,[pc,#292]   ; 字面量 @0x113ea4 = 0x0083014b
113d7e: add.w r1,r7,#116   ; 接收缓冲（r7+0x74）
113d82: mov.w r2,#0xffffffff
113d86: blx osal_rcv_msg_tmo
```

但 0x83014b 是**奇数**（thumb 特征），更像函数指针而不是 qid ⇒ 该处可能还要
`add r0,pc`，或这不是输入线程。

### 备用方案（下一轮）

1. 反汇编 0x113d00..0x113e00 找出真正的 qid 传参路径；
2. 或在运行时用 `#N` 在**输入线程启动后**再 dump helper 对象（当前 +0x20 读到 0）；
3. 若仍拿不到，就枚举少量候选 qid 发消息、以画面 SHA256 变化为准（有风险需逐个试）。
