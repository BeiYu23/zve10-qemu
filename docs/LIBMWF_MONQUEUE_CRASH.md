# 崩溃点精确定位：`MonQueue::GetQueueRemain()` 收到 NULL `this`

> QEMU 侧内核钩子给出 `pc=libMWF+0x323BE`、`lr=+0x32407`。
> 本文给出反汇编级结论 + 一个可直接检查的全局变量地址。

---

## 一、崩溃指令

```
000323be <MonQueue::GetQueueRemain()>:      ← pc 正是这里，函数第一条指令
   323be:  6803   ldr  r3, [r0, #0]         ← ★ 崩溃：r0 == NULL ⇒ 读地址 0x0
   323c0:  b580   push {r7, lr}
   323c2:  af00   add  r7, sp, #0
   323c4:  681a   ldr  r2, [r3, #0]
   323c6:  6843   ldr  r3, [r0, #4]
   323c8:  681b   ldr  r3, [r3, #0]
   323ca:  429a   cmp  r2, r3
   323cc:  d902   bls.n 323d4
   ...
```

`r0` 是 C++ 的 `this`。`ldr r3,[r0,#0]` 读 0x0 ⇒ **`this` 为 NULL**，与 `address=0x0 trap=0x0e` 完全吻合。

## 二、调用者：`MonQueue::IsNearlyFull()`，传进去的 this 就是 NULL

`lr = 0x32407 = 0x32406 | 1`（Thumb 位），说明 `blx` 在 **0x32402**：

```
000323fc <MonQueue::IsNearlyFull()>:
   323fc:  b598   push {r3, r4, r7, lr}
   323fe:  af00   add  r7, sp, #0
   32400:  4604   mov  r4, r0            ; r4 = this（备份）
   32402:  f7eb e820  blx GetQueueRemain()  ; ★ r0 未被修改 ⇒ 传的就是入口的 this
   32406:  2840   cmp  r0, #64
   32408:  f200 80e0  bhi.w ...
   3240c:  4620   mov  r0, r4
   3240e:  blx GetQueueRemain()          ; 又调一次
```

⇒ **`MonQueue::IsNearlyFull()` 在入口收到 `this == NULL`**，
不是它内部算错，而是**调用它的人传了空指针**。

## 三、`MonQueue` 是 MWF 的**监视器/调试控制台**子系统

libMWF 里成组出现的符号：

```
MonQueue   MonCtrl   MonLib   MonFlt   MonFil   MonDrv
Monitor::Init()   Monitor::ExecCmdPrefix/Help/HelpMsg/Mmap
MonDrv::Init()    MonDrv::Putchar(int)    MonDrv::mp_lockBufStatus
字符串: "MonTskInput"
```

`MonQueue` 构造函数需要 **4 个参数**：

```
MonQueue::MonQueue(unsigned long*, unsigned long*, unsigned char*, unsigned long)
```

⇒ 它**必须被显式构造**（两个缓冲区指针 + 缓冲 + 长度）。
**全局/成员指针没构造就是 NULL** —— 这与本次崩溃完全一致。

## 四、★ 可直接检查的全局变量

```
符号: MWF::ObjIf_Mcmd::mp_mon      vaddr 0x45874   size 4   （libMWF+0x45874）
符号: MonDrv::mp_lockBufStatus     vaddr 0x46d14   size 4
符号: Monitor::a_CmdBuf            vaddr 0x47878   size 512
```

**建议崩溃时打印 `*(uint32_t *)(libMWF_base + 0x45874)`**：
- 若为 **0** ⇒ 监视器对象从未创建（`MonDrv::Init()` / `Monitor::Init()` 没跑或失败）⇒ 根因确定
- 若非 0 ⇒ 监视器对象在，但其中的队列指针另有问题，再顺藤摸瓜

## 五、★ 关键判断：这是**两条线共有**的问题，不是新菜单特有

| 符号 | ZV-E10 libMWF | ZV-1F libMWF |
|---|---|---|
| `MonQueue::GetQueueRemain()` | `0x32373` | `0x323bf` |
| `MonQueue::IsNearlyFull()` | `0x323b1` | `0x323fd` |

**两版 libMWF 含完全相同的 MonQueue 代码，整体只差 0x4C 字节。**

⇒ 所以"老菜单补丁镜像也崩在同一处"是**必然的**：
**这是 MWF/平台层共有的初始化缺口，与菜单新旧无关。**

⇒ 也意味着：**修好它，两条线同时受益**。

## 六、下一步（QEMU 侧）

1. **打印 `libMWF+0x45874`** 的值（最直接的一条判据）
2. 寄存器 dump 里看 **r0**（应为 0）以及 **r4**（IsNearlyFull 里的 this 备份，也应为 0）
3. 崩溃前 **open/dlopen 序列**：重点看 `MonDrv::Init()` 期间是否打开了某个设备/文件而失败
4. 若要，我可以继续把 **PLT 查表解出来**，定位"谁调用 `IsNearlyFull()`"
   （调用走 PLT，需要把 `.plt` 条目与 `R_ARM_JUMP_SLOT` 槽位对齐，我可以做）
