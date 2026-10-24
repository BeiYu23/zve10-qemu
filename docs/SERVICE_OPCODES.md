# backup 服务协议：opcode 表与请求布局（内核实证）

> 来源：backup.ko::backup_osal_call_func（0x51FC，2024 字节）ARM 反汇编。

## 一、请求分发机制

```asm
520c-522c: r3 = *(u32*)r0          ; opcode = 请求前 4 字节（小端）
5230: r2 = r3 + 1
5234: cmp r2, #38                  ; opcode 范围 0..37
5238: ldrls pc, [pc, r2, lsl #2]   ; 跳转表（表在 0x5240，索引 = opcode+1）
523c: b 59a8                       ; 越界 -> default
```

## 二、opcode 到处理函数

| opcode | 处理函数 | 语义 |
|---:|---|---|
| 0 | Backup_get_datasize | 取数据大小 |
| 1 | Backup_get_datasize_multi | 批量取大小 |
| 2 | Backup_get_attribute | 取属性 |
| 3 | Backup_read | 读数据 |
| 4 | Backup_read_4Byte | 读 4 字节 |
| 5 | Backup_read_multi | 批量读 |
| 6+ | default (0x59A8) | 不支持 |

这是标准的“先查大小、再读数据”文件服务。

## 三、请求结构布局（从回调收到的 reply_buf 起算）

| 偏移 | 宽 | 字段 |
|---|---:|---|
| +0x00 | u32 | opcode |
| +0x18 | u32 | arg0（多为 offset/句柄） |
| +0x1C | u32 | arg1（多为 length） |

证据：opcode 3 的处理器这样取参

```asm
5364: r2 = byte[r0+25]; r3 = byte[r0+24]   ; le16(r0+24)
5374: r3 = byte[r0+26]
5380: r3 = byte[r4+28]; r1 = byte[r4+29]
5388: r0 = le32(r0+24)                     ; arg0 = req[24..27]
5390: r1 = le32(r4+28)                     ; arg1 = req[28..31]
539c: bl Backup_read(arg0, arg1)
```

所以日志里的 rb[0]=3 就是 opcode 3（Backup_read），rb[0]=0 是 opcode 0（get_datasize）。

## 四、响应怎么写

内核 __default_cb（0x2434）：

```asm
2460: blx r3          ; 调用 cb(reply_buf, ctx)
2464: mov r0, r5      ; 之后只用 lock handle
246c: mov r0, #0
```

内核忽略回调的返回值，只把 reply_buf 的内容当作应答。
所以应答必须写进 reply_buf，长度由注册时的 size(200) 决定。

## 五、关键判断：backup 与 initializeConfig 是两条不同的路

- backup 队列（0x3E014D）：bootin 用它读备份数据（op0 查大小、op3 读数据）
  - 修好应答能让 bootin 走下去（已经做到 trap=0）
- initializeConfig：走的是 dlopen("viewUnified2.so") + initializeViewConfig()
  （见 CONFIG_DLOPEN_MECHANISM.md），不用 backup 队列

所以把 backup 应答写对，不会让 initializeConfig 拿到 EXIT，除非 bootin 的数据依赖它。
QEMU 侧重回 ioctl 抓包看现在等哪个 qid 的方向是对的。

## 六、建议的最小合法应答

```c
static void backup_sync_cb(void *rb, void *ctx) {
    u32 op = *(u32*)((char*)rb + 0x00);   /* 先保存 opcode！ */
    u32 a0 = *(u32*)((char*)rb + 0x18);
    u32 a1 = *(u32*)((char*)rb + 0x1C);
    memset(rb, 0, 200);
    switch (op) {
    case 0:  /* get_datasize: 返回 0 表示空 */ break;
    case 3:  /* Backup_read: 填 a1 字节合法内容 */ break;
    default: break;
    }
}
```

注意：rb[0] 既是输入 opcode 又是输出位置，回调里必须先保存 opcode 再 memset。
日志里 rb[0]=3 是在进入时读到的，说明这个理解成立。

## 七、/proc/kallsyms 核对

只为了解那个矛盾（原厂传 r2=0 却应失败）。既然已经用正确签名跑通，这个矛盾可以
暂时搁置，不影响继续推进，优先级低。
