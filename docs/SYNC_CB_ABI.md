# `osal_reg_sync_msg_queue_cb` 的完整 ABI（内核模块实证）

> 回答 QEMU 侧：注册签名、回调签名、应答怎么写、r2==0 返回什么。

---

## 一、★ 注册签名（`osal_uipc.ko` @ `0x3068`，ARM 模式）

```asm
3074: tst  r0,#0x8000        ; ① qid 不能带 bit15
3078: r4 = r1                ;    r4 = r1
307c: r6 = r2                ;    r6 = r2
3080: r8 = r3                ;    r8 = r3
3084: r7 = uxth(r0)          ;    qid & 0xFFFF
3088: bne  30dc              ;    bit15 置位 → 错误
308c: cmp  r2,#0
3090: beq  30dc              ; ② r2 == 0 → 错误
3094: bl   alloc_cb
30a8: str  r6,[r5,#0x14]     ;    ★ cb+0x14 = r2（回调函数）
30b0: str  r8,[r5,#0x18]     ;    ★ cb+0x18 = r3（上下文）
30b8: str  r5,[r4,#0]        ;    ★ ★(r1) = cb   ⇒ r1 是【输出指针】
30bc: bl   reg_sync_msg_queue_cb
```

**⇒ 签名：**
```c
int osal_reg_sync_msg_queue_cb(
    unsigned int qid,        // r0，bit15 必须清零
    void       **cb_out,     // r1，回填 cb 结构地址（必须非 NULL）
    void      (*cb_fn)(),    // r2，必须非 NULL ★
    void        *ctx         // r3，回调上下文
);
```

**⇒ 你们之前推断的 `(qid, void **cb_out, cb_fn, ctx)` 是【对的】。**

### `r2 == 0` 的返回（原始字节实证）

`.text` 文件偏移 = vaddr + **0x34**：
```
[va 0x30EC] = 0xFFFFFC01 = -1023   （另一种失败）
[va 0x30F0] = 0xFFFFFC00 = -1024   （30DC 的返回码）
```

**⇒ `r2 == 0` 返回 `-1024`（失败）。**

---

## 二、★★ 回调 ABI（派发点在 `__default_cb` @ `0x2434`）

```asm
2434: __default_cb(r0=msg, r1=cb, r2=lock):
2440: r6 = r1                 ; r6 = cb
2444: r4 = r0                 ; r4 = msg
2450: bl  osal_unlock(r2)
2454: r3 = [r6, #0x14]        ; ★ 回调函数
2458: r0 = r4 + 12            ; ★★ arg0 = msg + 0xC
245c: r1 = [r6, #0x18]        ; ★ arg1 = ctx
2460: blx r3                  ; cb(msg+12, ctx)
2468: bl  osal_lock(r2)
2470: return 0
```

**⇒ 回调签名：**
```c
void cb(void *reply_buf, void *ctx);
// reply_buf = 内核消息体 + 0xC
// ctx       = 注册时传的 r3
```

### 应答怎么写

**回调收到的 `reply_buf` 就是应答缓冲区** —— 直接把内容写进去即可。
**应答长度由注册时的 `size` 决定**（原厂 backup 传的是 **200/204**）。

⇒ **所以注册时必须传一个够大的 size**（buffer 不够会被截断或越界）。

---

## 三、cb 结构布局（由注册代码实证）

| 偏移 | 内容 |
|---|---|
| `+0x00` | 链指针 |
| `+0x08` | 标签（注册时置 0） |
| `+0x14` | **回调函数指针**（注册参数 r2） |
| `+0x18` | **上下文 ctx**（注册参数 r3） |
| 其余 | 内核内部字段 |

---

## 四、★ 与原厂 `backup.ko` 调用的矛盾（必须记录）

原厂 `backup_osal_init`（`backup.ko` @ `0x50C0`，正确偏移重读）：

```
osal_reg_sync_msg_queue_cb(0x3E014D, 0x000000C8(200), 0x00000000(NULL), 0x3E014D)
osal_reg_msg_queue_cb     (0x3E0150, 0x000000CC(204), 0x00000000(NULL), 0x3E0150)
```

用一个签名看，这等于 `cb_out = 200`（非法指针）、`cb_fn = NULL` ⇒ **必然失败**。

**⇒ 二者不能同时成立。可能：**
1. 原厂 `bl` 解析到的**不是** `osal_uipc.ko` 里那个（例如跨版本/另一模块导出同名符号）
2. `backup_osal_init` 的失败是**被容忍的**（但代码里 rc≠0 会 `b` 到错误路径返回 -15）
3. 我对 `backup.ko` 的参数寄存器归属仍有偏差

**⇒ 建议在 guest 里用 `cat /proc/kallsyms | grep osal_reg_sync` 核实实际地址与是否唯一。**

---

## 五、给 QEMU 的结论

**用你们推断的签名注册，回调按 `cb(void *reply_buf, void *ctx)` 实现，往 `reply_buf` 写应答。**

```c
static void *g_cb_out = NULL;

static void backup_sync_cb(void *reply_buf, void *ctx) {
    // ★ 往 reply_buf 写一条合法应答；长度由注册 size(200) 决定
    memset(reply_buf, 0, 200);
    // 至少写一个"成功"返回码 —— 具体字段需按 bootin 的解析再定
}

// 注册（size 必须够大，cb_fn 必须非 NULL）
osal_reg_sync_msg_queue_cb(0x3E014D, &g_cb_out, backup_sync_cb, (void *)0x3E014D);
```

**⇒ bootin 崩溃的修法：不要再"只注册不实现"，在 cb 里写一条合法应答。**
**先保证 `reply_buf` 被写成非全零且长度合法**，再逐字段对齐 bootin 的解析。
