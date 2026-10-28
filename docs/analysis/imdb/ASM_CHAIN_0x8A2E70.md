# `0x8A2E70` 调用链：为什么"没有信号量记录"其实说明它**没返回**

> 回答 QEMU 侧："`0x8A2EE0` 是否根本没被调用？"

---

## 一、★ 结论：`0x8A2EE0` **确实被调用了**（唯一调用点证明）

用 BL/B.W 全 `.text` 扫描：**`0x8A2E70` 的调用点只有一个** —— **`0x8A2EF4`**，
而它就在 `0x8A2EE0` 内部。

⇒ 你们观测到 `initializeConfig`（由 `0x8A2E70` 在 `0x8A2EA8` 调用，
   LR = `0x8A2EAC`，带 Thumb 位即 `0x8A2EAD`，**与你们的日志完全吻合**）
⇒ **`0x8A2EE0` 必然执行过。**

**所以"没看到 SEMT/PIPE"不是"函数没被调用"，而是"它卡在 `0x8A2E70` 里没返回"。**

## 二、逻辑链（无分支可逃）

`0x8A2EE0` 从 `0x8A2EF4` 到 `0x8A2F10` **没有任何分支**：

```asm
8a2ef4: bl   0x8A2E70        ← 进去之后必须返回才会继续
8a2efc: str  r0,[r4,#0x14]
8a2efe: mov  r0,r6
8a2f00: ldr  r6,[pc]
8a2f02: add  r6,pc
8a2f04: mov  r1,r6
8a2f06: bl   0x8A2A80        ← 日志 "P wait"
8a2f0a: mov  r1,#-1
8a2f0e: ldr  r0,[pc]         ← 0x83037A
8a2f10: blx  osal_wai_sem_tmo
```

⇒ **若 `0x8A2E70` 正常返回，`0x8A2F10` 一定被执行。**
⇒ 你们没看到 SEMT ⇒ **`0x8A2E70` 没有返回。**

## 三、`0x8A2E70` 里哪一步可能不返回

```asm
8a2e70: 名字处理（strncpy 到 this+0x2C）
8a2e84: len = min(strlen(name), 20)
8a2e90: strncpy(this+0x2C, name, len)
8a2e9a: this[0x3F] = 0
8a2ea8: blx initializeConfig        ← ★ 你们在这里有日志
8a2eb2: strncmp("AppConfig.so", name, 20)
8a2ec6: r3 = [GOT]
8a2ec8: blx r3                      ← 尾部调用（else 分支是 0x44ACD9，纯 getter，立即返回）
8a2eca: pop 返回
```

**候选阻塞点只有两个，都在 `initializeConfig` 之内。**

## 四、`initializeConfig` 的结构（只有 40 字节，在 `libObj+0x14BE85`）

```asm
14be86: movs r0, #24
14be8a: blx  operator new        ; ① 分配 24 字节
14be90: bl   0x14BD58            ; ② 构造配置对象（内部还会 new + 一连串初始化）
14be98: str  r4,[r3,#0]          ; ③ 存进全局 getConfig 指向的位置
14be9a: pop 返回
```

⇒ **如果它不返回，阻塞必在 ① 或 ②**：
- ① `operator new(24)` 卡住 ⇒ 堆/内存池问题（你们已在别的路径见过 `new(24)` 正常，概率低）
- ② `0x14BD58` 构造里卡住 ⇒ 需要看它内部（我这里看到它调 `0x14BCE8`、`0x14BC64` 等）

## 五、★ 给 QEMU 的最小验证（一次运行）

**你们已经有 `initializeConfig` 的进入日志，只差"退出日志"。**

```c
void *my_initializeConfig(void) {
    log("INITCFG_ENTER tid=%d", gettid());
    void *r = real_initializeConfig();
    log("INITCFG_EXIT  tid=%d", gettid());
    return r;
}
```

- **只有 ENTER 没有 EXIT** ⇒ 确认卡在里面（再去二分 ① `new(24)` ② `0x14BD58`）
- **ENTER 和 EXIT 都有** ⇒ 那 `0x8A2E70` 之后必然走到 `0x8A2F10`
  ⇒ **问题变成"你们的 SEMT 探针没抓到"，而不是"没执行"**
  ⇒ 这时去查 SEMT 探针本身的参数/tid 读取是否正确

**同一次运行再加一个**：interpose `0x8A2E70` 本身
（`libObj+0x8A2E70` 不是导出符号，但可以用 `operator new(24)` + `getConfig` 变化间接确认）。

---

## 六、方法学提醒

你们这条推断的出发点是"没日志 = 没执行"，但**日志缺失有两种原因**：
1. 代码没执行
2. **代码执行了，但日志探针没抓到**（参数读错、tid 不符、被过滤、时序早于注入）

这一轮我们**已经用"唯一调用点"排除了第 1 种**。所以下一步要验证的是**探针本身的可靠性**。
