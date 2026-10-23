# `target=` 决定 im.elf 加载哪些库（IMDB profile 机制）

> 起因：QEMU 侧实测 im.elf 的真实 argv 是
> `/usr/bin/im.elf.real debug target=qemu s g r u l i a lazy-global`
> ——不是"7 选 1"，而是**全部字母作为并列标志**。本文据此重查 IMDB 机制。

## 一、`libIMDB.so` 的 6 个 profile，每个有独立的 enable 位

来自 `data/imdb_table.tsv`（220 条 × 44 B，`.data.rel.ro` 实测导出）：

| idx | profile | bitmask（该 profile 的激活位） |
|---|---|---|
| 3 | `IMDB:default` | **0x00000001** |
| 4 | `IMDB:qemu` | **0x00000006** |
| 5 | `IMDB:nfs` | 0x00000008 |
| 6 | `IMDB:set` | 0x00000010 |
| 7 | `IMDB:USBJ` | 0x00000020 |
| 8 | `IMDB:RE-SUB` | 0x00000040 |

**每个库记录有 `enable` 字段 = 需要哪个 profile 位才装载。** 于是：

```
enable=0x01 → 只在 IMDB:default 下装载
enable=0x02 / 0x04 → 在 IMDB:qemu(0x06) 下装载
```

## 二、★ 后果：`target=qemu` 与 `target=default` 的库集合不同

实测几条关键记录：

| idx | 路径 | enable | 说明 |
|---|---|---|---|
| 130 | `libEMG.so` | **0x01** | 仅 default |
| 131 | `libAdjustUIProxy.so` | 0x04 | qemu |
| 138 | `libInfraMediaPwrMgr.so` | 0x02 | qemu |
| 141 | **`libBizInitializer.so`** | **0x01** | **仅 default** |
| **143** | **`appFw.so`** | **0x01** | **仅 default —— 而 ZV-E10 上此文件不存在** |
| 144 | `subapp.so` | 0x01 | 仅 default |

⇒ 若真机跑的是 `target=default`：
  - `appFw.so`（idx=143）会被**尝试装载但文件不存在** ⇒ 必然失败；
  - 真机却正常工作 ⇒ 说明**该条失败被容忍**（或该 entry 被其它位再过滤）。
⇒ 若 QEMU 跑 `target=qemu`：
  - `appFw.so` 与 `libBizInitializer.so` **根本不在装载列表里** ⇒ 与真机的库集合**不同**。

**这是一个此前未被检查的变量**：QEMU 侧一直用 `target=qemu`（因为环境是 QEMU），
而真机的 `target=` 值未知。

## 三、`IMDB_find_target_bit` 失败时的行为（已反汇编）

`im.elf` 在 `0xaf14` 的函数：

```c
if (target == NULL) return -1;
bit = IMDB_find_target_bit(target);          // 0xaf20
if (bit == 0xFFFFFFFF) {                     // 找不到
    state->field_0x28 = 1;                   // ★ 写一个降级标记
    return;                                  // ★ 直接返回，target 串【不会被记录】
}
state->field_0x24 = target;                  // 记录 target 指针
state->field_0x1c = flags;
if (strncmp(target, "????", 4) == 0)         // 某个 4 字符 target 特殊处理
    flags |= 0x1000;
if (bit == 1) {                              // 特殊位再细分
    if (IMDB_get_entries(...) 的 0x8000 位 == 0)
        bit = (0x4000 位 == 0) ? 1 : 0x80;
    else bit = 0x20;
}
state->field_0x28 = bit;                      // 最终 target bit
```

⇒ **`IMDB_find_target_bit` 返回 -1 时会静默降级**：target 串不记录、bit 置 1、
后续装载走的是"默认那套"。这正好能解释"资源都加载了、view 却不建"。

**便宜的第一步检查**（QEMU 侧）：打印 `IMDB_find_target_bit("qemu")` 的返回值。
若为 -1 ⇒ 整条链走降级路径，一切都能解释。

## 四、`lazy-global` 是 dlopen 模式，不是调试开关

`im.elf` 支持三种：`lazy-global` / `lazy-local` / `now-global`（另有 `nodebug`、
`debug`、`boot=`、`app_argv=`、`fn=/system/av-cam.bin load=1 debug=1`）。

对应标准的：

```
lazy-global = RTLD_LAZY | RTLD_GLOBAL
lazy-local  = RTLD_LAZY | RTLD_LOCAL
now-global  = RTLD_NOW  | RTLD_GLOBAL
```

**真机传的是 `lazy-global`** ⇒ 符号进全局域。
这条正对移植报告里的风险 #2（"若框架用 RTLD_LOCAL 加载，会在运行时报
undefined symbol"）。**QEMU 的探针装载库时必须用 RTLD_GLOBAL，否则行为不等价。**

## 五、给 QEMU 侧的行动顺序（按性价比）

1. **打印 `IMDB_find_target_bit("qemu")` 返回值** —— 若 -1，整条链降级，直接定位
2. **确认真机的 `target=` 是什么** —— 这是新发现的变量，可能改变整个装载集合。
   真机 `/proc/<im.elf pid>/cmdline` 一行就能读到（`tools/z10_capture.sh` 已包含
   全 PID 的 cmdline 抓取）
3. **核对探针 dlopen 用的是不是 `lazy-global`**（RTLD_GLOBAL），不是则改
4. 再上 dlsym hook + `osal_valloc_msg_wait` qid 统计
