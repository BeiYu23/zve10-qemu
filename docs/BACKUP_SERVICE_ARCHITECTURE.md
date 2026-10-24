# Backup 服务架构：应答方在用户态，不在内核

> 依据：`backup.ko` / `libbackup.so` / `osal_uipc.ko` 反汇编（ZV-E10 固件）
> 关联：`docs/BACKUP_KEY_TABLE.md`（BK4 文件格式）、`docs/A7C2_REPO_FULL_ANALYSIS.md`

## 结论

**`0x3E014D` 等队列的应答方是【用户态 `libbackup.so`】，不是内核模块 `backup.ko`。**

内核模块的注册调用全部失败（传 `cb = NULL`，被内核拒绝）；用户态库注册同一队列并带真实回调。

## 证据

### 1. `osal_reg_sync_msg_queue_cb` 拒绝 NULL 回调

```asm
3068 <osal_reg_sync_msg_queue_cb>:
  3074: tst  r0, #32768      ; qid bit15
  3088: bne  30dc            ; ⇒ 错误
  308c: cmp  r2, #0          ; cb == NULL ?
  3090: beq  30dc            ; ⇒ 错误
  30dc: → 0xFFFFFC00 (-1024)
  30cc: → 0xFFFFFC01 (-1023) ; alloc 失败
```

### 2. `backup.ko` 四处注册全部传 NULL

| 函数 | 位置 | qid | cb |
|---|---|---:|---|
| `backup_osal_init` | 0x50E0 | `0x3E014D` (sync) | NULL |
| `backup_osal_init` | 0x5118 | `0x3E0150` (async) | NULL |
| `Backup_reg_cb` | 0x2E4C | `0x3E414E` (async) | NULL |
| `backup_senser_init` | 0x5DD4 | `0x3E0166` (sync) | NULL |

⇒ 全部 `-1024`，内核模块一个队列都没注册成功。

### 3. `libbackup.so` 注册 `0x3E414E` 且带真回调

```asm
1a12: ldr  r2, [pc,#44]     ; cb
1a14: ldr  r0, [pc,#36]     ; 0x003E414E
1a16: adds r1, r4, #4
1a18: add  r2, pc           ; ⇒ cb = 0x182D
1a1c: blx  osal_reg_msg_queue_cb
```

## 架构

```
backup.ko     只负责数据：filp_open/vfs_read /setting/Backup.bin → 核内建表
libbackup.so  负责协议：在调用进程内注册队列 + 用户态回调
              回调里调 Backup_read / Backup_nread（PLT → 内核模块取数据）
```

⇒ "服务端"在**调用者自己的进程里**，这解释了 `current->comm = bootin.elf` / `sen.elf`。

## 真模块崩溃点 0x91C 的解释

`backup_suspend+0x828` = `0x91C`：

```asm
8ac <sub_8AC>:                       ; 警告路径
  8cc: bl  osal_valloc_msg_wait(0x8F0139, &msg, 4, 1)
  8dc: mov r3, #14
  8ec: strh r3, [msg]
  8fc: bl  osal_snd_sync_msg(0x8F0139, msg)
  904: ldrb r4, [msg, #2]            ; ★ 由应答方写入的状态字节
  918: cmp  r4, #0
  91c: strbeq r4, [r4]               ; ★ r4==0 ⇒ 写地址 0 ⇒ NULL deref
```

**`0x8F0139` 是 ulogio 的队列**（引用者含 `osal_ulogio.ko`、`libosal_ulogio.so`）。
应答方把 `msg[2]` 写成非零即不崩。

⇒ 所以"真模块 + 真文件"不是走不通，是**缺 `osal_ulogio.ko`**。

## 建议顺序

1. 加载 `osal_ulogio.ko`（+ `libosal_ulogio.so`）
2. 去掉内核 backup stub，换真 `backup.ko`
3. 确认 `/setting/Backup.bin` 在 VFS 可见（1,235,836 B，格式见 `BACKUP_KEY_TABLE.md`）
4. 观察 `bootin`/`sen` 的请求是否被 `libbackup.so` 自己应答

⇒ 若成立，`nread` 循环自然消失，且**无需再猜任何应答格式**。
