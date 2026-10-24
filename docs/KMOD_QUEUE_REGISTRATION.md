# backup 队列注册：原厂真实调用 与 为什么你们的 stub 弄崩了 bootin

---

## 一、★ 原厂 `backup.ko` 的真实注册调用（ARM 反汇编实证）

`backup_osal_init`（`backup.ko` @ `0x50C0`）：

```asm
50d0: ldr r0,[pc,#116]    ; = 0x003E014D   ← 同步队列 qid
50d4: ldr r1,[pc,#116]    ; = 0x000000C8   ← ★ 200（大小！不是指针）
50d8: ldr r2,[pc,#116]    ; = 0x00000000   ← ★ NULL
50dc: mov r3, r0          ; r3 = 0x3E014D
50e0: bl  osal_reg_sync_msg_queue_cb

5108: ldr r0,[pc,#76]     ; = 0x003E0150   ← 异步队列 qid
510c: ldr r1,[pc,#76]     ; = 0x000000CC   ← ★ 204
5110: mov r3, r0          ; r3 = qid
5114: ldr r2,[pc,#72]     ; = 0x00000000   ← NULL
5118: bl  osal_reg_msg_queue_cb
```

**⇒ 原厂签名是 `(qid, size, NULL, qid)`** —— `r1` 是**缓冲大小**，`r2` 是 **NULL**。

> ⚠️ **反汇编 .ko 的坑**：`backup.ko` 的 `.text` **文件偏移 = vaddr + 0x34**。
> 直接把 vaddr 当文件偏移读字面量会得到**指令字节**而不是字面量值 ——
> 我一开始就踩了这个坑，读出 `0xebfffffe` 这种垃圾。
> **你们之前"r1 是输出指针"的结论，很可能也受了这个影响。**

---

## 二、★ 关键判断：stub 抢占队列 = bootin 崩溃的原因

你们现在的状态：
```
BKREGSYNC rc=0 slot=0x60B9C920     ← 你的 stub 注册了 0x3E014D
bootin.elf 崩溃 address=0x0
INITCFG_ENTER=0
```

**因果链很清楚**：
1. `0x3E014D` 在 **bootin 阶段**就有人用（bootin 发同步请求）
2. 你的 stub 把它注册成"服务端"，但**从不回包**
3. bootin 拿到空应答 → 解引用空指针 → **崩在 0x0**

**⇒ 不是"注册错了"，是"你抢了一个本来就有人用的队列，却没提供应答"。**

---

## 三、★ 建议的顺序（先救回来，再谈推进）

### 第 0 步：立刻撤销 0x3E014D 的注册
把 bootin 从"Exception>"救回来。**这是最高优先级**，其余都基于它。

### 第 1 步：先试**原厂 `backup.ko` 本体**
`/usr/kmod/backup.ko` 就在固件里。它自己会正确注册 `0x3E014D`/`0x3E0150`，
**如果能 insmod 成功，你根本不需要 stub。**

先确认：
- QEMU 里 `backup.ko` 是否已加载？（`/proc/modules` 里有没有 `backup`）
- 如果没加载，**为什么**？（`insmod` 报什么错？依赖缺失？vermagic？）

**⇒ 如果原厂模块能加载，问题就解决了，不用写 stub。**

### 第 2 步：只有在原厂模块真的加载不了时，才写 stub
而且要**严格照原厂的调用**：
```c
osal_reg_sync_msg_queue_cb(0x3E014D, 200, NULL, 0x3E014D);
osal_reg_msg_queue_cb     (0x3E0150, 204, NULL, 0x3E0150);
```
**不要**传 `&bss_obj` 当 r1（那是把指针当大小用），**也不要**传非 0 的 r2
（内核版对 `r2==0` 会走 `0x30dc` 返回 `-1024`，但原厂就传 0 —— 这个矛盾说明
**原厂调的可能不是我反汇编的那个内核符号**，需要再确认符号解析）。

### 第 3 步：确认 0x3E014D 的服务端**本该是谁**
- 如果是 `backup.ko` → 加载它即可
- 如果不是 → 找出真正注册它的模块（用 `osal_stat_msgq` 的客户端计数 + 反查注册者）

---

## 四、诚实说明：我没能确定回调 ABI

我尝试从 `osal_uipc.ko` 里找"内核如何调用 `cb+0x14`"的派发点，**没有找到可靠的调用序列**
（搜到的 `[rX,#0x14]` 都是 mmap 相关代码）。

**⇒ 回调 ABI 我给不出实证结论，不要基于猜测去实现。**

**替代方案（更稳）**：不要自己写回调，**直接用原厂 `backup.ko`**（第 1 步）。
这也符合"改得越少越安全"的原则。

---

## 五、一个矛盾必须记下来

内核版 `osal_reg_sync_msg_queue_cb`（`osal_uipc.ko` @ `0x3068`）：
```asm
3074: tst r0,#0x8000
3088: bne 30dc           ; bit15 置位 → -1024
308c: cmp r2,#0
3090: beq 30dc           ; r2==0 → -1024
30dc: r4 = 0xFFFFFC00    ; -1024
30e0: return
```

**但原厂 `backup.ko` 传的就是 `r2=0`。**

⇒ 二者不可能同时成立。可能原因：
1. `backup.ko` 的 `bl` 符号解析到**另一个** `osal_reg_sync_msg_queue_cb`（不在 osal_uipc.ko 里）
2. 存在两个版本（wrapper / 实现）
3. 我对 `30dc` 是错误路径的判断有误

**⇒ 这一条要你们在 QEMU 里用 `cat /proc/kallsyms | grep reg_sync` 核实** ——
看 `osal_reg_sync_msg_queue_cb` 到底解析到哪个地址、有没有多个同名导出。
