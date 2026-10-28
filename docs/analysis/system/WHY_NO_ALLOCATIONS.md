# 为什么看不到任何管理器分配：信号量卡在它们**之前**

> 回答 QEMU 侧："NEW(0x28/0x58/0xAC/0x70) 和 pipe() 一次都没出现"

---

## 一、结论：不用查 tid 门控 —— 指令顺序已经排除了这种可能

`0x8A2EE0` 从入口到第一个分配，**中间夹着那个无限等待的信号量**：

```asm
8a2eee: str  r3,[r0,#0x28]        ; app+0x28 = 0x7FFFFFFF
8a2ef0: mov  r4,r0
8a2ef2: str  r6,[r0,#0x24]        ; app+0x24 = 0
8a2ef4: bl   0x8A2E70             ; ★ 名字初始化（决定 initializeConfig 走不走）
8a2efc: str  r0,[r4,#0x14]        ; app+0x14 = config
8a2f06: bl   0x8A2A80             ; 日志 "P wait"
8a2f0a: mov  r1,#-1               ; timeout = 无穷
8a2f0e: r0 = 0x83037A             ; 信号量 id
8a2f10: blx  osal_wai_sem_tmo     ; ★★★ 阻塞点
8a2f14: ...
8a2f18: bl   0x8A2A80             ; 日志
8a2f1c: ldr  r0,[r4,#0x14]        ; 取 config
8a2f20: ldr  r3,[r3,#0x50]        ; config 虚方法 +0x50
8a2f22: blx  r3
8a2f26: blx  pipe                 ; ★ pipe 在这里
8a2f40: movs r0,#0x28             ; ★ 第一个 new(0x28) 在这里
```

**⇒ `pipe()` 在 `0x8A2F26`，第一个 `new(0x28)` 在 `0x8A2F40`，
   而信号量等待在 `0x8A2F10` —— 全都在它之后。**

**⇒ "没有 PIPE、没有任何管理器分配" 的唯一解释：
   线程根本没通过 `0x8A2F10` 那个信号量。**

不需要去掉 tid 门控去验证 —— **在这条路径上，任何线程都不可能先分配再等信号量。**

---

## 二、回看历史：这次的 ASI 运行很可能**没有同时越过信号量**

| 运行 | 名字 | 信号量 | 结果 |
|---|---|---|---|
| 早期手工调 `0x8A2EE0` | ❌ 空 | 卡住 | 停在 HSEM |
| 手动 signal 一次 | ❌ 空 | ✅ 越过 | **崩在 0x14**（名字空 → config 错） |
| 改用 ASI(0) | ✅ `"X"` | ❓ **未确认** | **无分配、无 pipe** ⇒ 疑似又卡在信号量 |

⇒ **两次修正可能是"分开验证"的，没有叠加。**

**⇒ 正确的组合是：`Application_System_Init(0)` + 越过信号量 `0x83037A`。**

---

## 三、★ 推荐做法：interpose `osal_wai_sem_tmo`，对这一个 id 直接放行

```c
int my_wai_sem_tmo(u32 id, int timeout) {
    if (id == 0x83037A) {
        // 可选：先 usleep 几十毫秒模拟"服务就绪"的时序
        return 0;                 // 0 = 成功
    }
    return real_wai_sem_tmo(id, timeout);
}
```

**为什么比"事后 signal"更可靠**：
- `timeout = -1` ⇒ 无限等待；**signal 有时序竞争**（线程可能还没进入等待就 signal 了，或等待已完成又重入）
- interpose 是**确定性放行**，不依赖时序
- 你们之前已经验证过"返回 0 → 不再出现 HSEM → 继续执行"

**放行后立刻会发生的事**（可作判据）：
1. `0x8A2F18` 再发一条日志
2. `0x8A2F1C` 取 `app+0x14` 的 config，调它的虚方法 `+0x50`
   - **这一步要求 config 真实存在** ⇒ 名字必须是 `"X"`（ASI 已保证）
3. `0x8A2F26` **`pipe()`** ⇒ 你们应该开始看到 PIPE 记录
4. `0x8A2F40` `new(0x28)` ⇒ 开始看到管理器分配
5. `0x8A2F94` `new(0x70)` ⇒ ViewManager 分配
6. `0x8A2FA6` ctor ⇒ **`pVM` 被写入**

**⇒ 只要能看到 PIPE 记录，就说明信号量真的越过了。**

---

## 四、`pVM` 读法复核（你计划里的第 2 项）

`pVM` 地址推导链（可逐步复核）：

```
DataflowInfraMsg... 不相关
ViewManager 用法：
  0x8A4F5A: ldr r3,[pc,#40]      → 字面量 0x4B8C（GOT 槽偏移）
  0x8A4F5E: ldr.w r3,[r9,r3]     → r3 = *(GOT_base + 0x4B8C) = &pViewManager
  0x8A4F62: str r5,[r3,#0]       → *(&pViewManager) = this
```

- `GOT_base` = `0x13D7C28`（由 `0x61229C` 处的 `add r3,pc` + 字面量 `0x00DC5984` 算得）
- `0x13D7C28 + 0x4B8C` = **`0x13DC7B4`**（该槽的重定位就是 `R_ARM_GLOB_DAT → 0x149221C`）
- ⇒ **`pViewManager` 变量地址 = `0x149221C`** ✓（与 `readelf -r` 一致）

**读法建议**：`*(u32*)(libObj_base + 0x149221C)` 是对的。
交叉验证可用 `viewManagerIf::getModelInfo(...)`（`0x140E95`）—— 若它内部对 pVM 判空后返回错误码，
就能反证 pVM 是否为 0。
