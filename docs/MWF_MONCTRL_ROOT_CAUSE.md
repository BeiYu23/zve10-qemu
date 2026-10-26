# `MWF::ObjIf::SetEventSender` 崩溃的根因与两条解

> 给 QEMU 线的离线分析结果。崩溃 pc = libMWF+0x26332（`MWF::ObjIf::SetEventSender+0x22`），
> 故障地址 0x4 ⇒ `r9 == 4`（MonCtrl 全局是垃圾）。

---

## 一、根因：`MonDrv::Init()` 才是建 MonCtrl 的地方

**`MonDrv::Init` @ libMWF+0x311A5（388 B，**dynsym GLOBAL**）：**

```asm
MonDrv::Init:
  311aa: r0 = 28 (0x1C)              ; ★ MonCtrl 对象 = 28 字节
  311b0: blx operator new(28)
  311b6: r5 = r0
  311b8: blx MWF::ObjLock::ObjLock()
  311c6: str r5, [r3, #0]            ; ★★★ 写入 MonCtrl 全局指针
  311d0: blx Backup_read_4Byte       ; 之后才读配置
```

**★★★ 关键：全局指针在 `+0x311C6` 就写好，**早于**可能失败的后半段。**

**⇒ 先调 `MonDrv::Init()`，`r9` 就不会是垃圾。**

---

## 二、推荐做法：`MWF::Initialize()`（导出 + 幂等）

**`MWF::Initialize` @ +0x20A09（160 B，dynsym GLOBAL）：**

```asm
  20a14: r5 = ldb [r3]          ; 读"已初始化"标志
  20a1a: bne 20a8a              ; ★ 已初始化则整体跳过 ⇒ 幂等
  20a1e: strb #1, [r3]
  20a20: blx MWF::MwfCommon::Init()
  20a24: blx Monitor::Init()    ; → MonLib::Init / MonFil::Init / MonFlt::Init
  20a2a: operator new(64) → MWF::ObjThread(...) → StartThread()
  20a4e: blx ObjLog_Mcmd::Init()
  20a52: blx MWF::RsrcMcmd::Init()
  20a56: blx MWF::InhMcmd::Init()
  20a5a: blx MWF::ObjIf_Mcmd::Init()
  20a5e: blx MWF::ObjIf::Init()
  20a62: blx EventIdFactory::Initialize(0x83e8, 0x800)
```

**★ 但 `Monitor::Init` @+0x325B5 只调 `MonLib::Init` / `MonFil::Init` / `MonFlt::Init`，
**没有**调 `MonDrv::Init`；全库搜 `311a4` 也只有标签本身，**无任何 bl 调用点**。
⇒ 推断它经函数指针表调用，那张表在 QEMU 里没走通。**

**调用顺序建议：**
```
① MWF::Initialize()    // 幂等
② MonDrv::Init()       // 显式补建 MonCtrl
```

---

## 三、兜底：崩溃点前本来就有守卫，2 字节即可跳过

**`SetEventSender` @+0x26310：**

```asm
  26320: ldr r3,[r4,r3]      ; GOT → 守卫全局指针
  26324: ldrb r2,[r3,#0]     ; 读守卫字节
  26326: cbz r2, 2634a       ; ★★ 为 0 → 跳过整个日志块
  2632e: ldr.w r9,[r4,r3]    ; MonCtrl 全局
  26334: ldr.w r0,[r9]       ; ← 崩溃点
  26338: blx MonCtrl::ConPrintfMessage   ; 第 1 次
  26346: blx MonCtrl::ConPrintfMessage   ; 第 2 次
  2634a:                     ; cbz 目标
```

**⇒ `+0x26326` 两个字节 `82 b1`（`cbz r2,0x2634a`）改成 `10 e0`（`b.n 0x2634a`），
一次跳过两次调用。**

**★ 但全库有 **46 处** `ConPrintfMessage` 调用 ⇒ 只 patch 这一处后面还会在别处崩。
⇒ 补建 MonCtrl 比 patch 彻底。**

---

## 四、守卫变量的候选

**libMWF 里的单字节标志（OBJECT / GLOBAL）：**
```
_ZN6MonLib12isEchoBackOnE   0x476D0   1 字节   ← 最像
_ZN6MonLib11cmdLineModeE    0x476CC   4 字节
```
**GDB 验证：读 `MWFBASE+0x476D0`，若为 1 则置 0，看崩溃是否消失。**

---

## 五、附带发现

- **`MonCtrl::ConPrintfMessage` 是 dynsym GLOBAL（+0x30F6D，54 B）
  ⇒ 造 sink 太麻烦时，直接 hook 成 `bx lr` 空函数即可。**
- **`ObjIf::GetInstance` @+0x26A4D 不是建单例，是**按名字查表**：
  表项 108 字节/条，名字在 `+5`，名字上限 50 字符。
  ⇒ 可用来确认某个 ObjIf 是否注册成功。**
- **`MonCtrl` ctor `_ZN7MonCtrlC2EPKcb` @+0x30C95（32 B）也导出。**
