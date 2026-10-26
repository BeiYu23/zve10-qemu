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

---

## 六、`MonDrv::Init` 完整路径（388 字节全解）

```asm
MonDrv::Init:                                     ; libMWF+0x311A5
  311aa: r0 = 28
  311b0: blx operator new(28)                     ; ① 分配 MonCtrl（28 B）
  311b8: blx MWF::ObjLock::ObjLock()              ; ② 构造锁
  311c6: str r5, [r3, #0]                         ; ③ ★★★ 写 MonCtrl 全局（最先！）
  311cc: str r8, [r3]  (r8=0)                     ; ④ 另一全局 = 0
  311d0: Backup_read_4Byte(0x00FA0028) → r6       ; FixedAreaTop（物理偏移）
  311d8: Backup_read_4Byte(0x00FA005D) → r9       ; FixedAreaSize（长度）
  311e2: open("/dev/mem", 0x1002)                 ; ⑤
  3120c: if (fd == -1) → printf → return          ; 失败点 A
  31230: mmap(0, r9, RW, SHARED, fd, r6)          ; ⑥
  31234: if (ret == -1) → printf → return         ; 失败点 B
  31244: r9 = 映射基址 + 0x53400                  ; ⑦ 监视器缓冲区基址
  3127a: 写全局 = 基址
  31294: MonQueue(base+4, base+0x10, 0x400)       ; 入队 1 KB
  312bc: MonQueue(base+8, base+0xC, 0x7BF0)       ; 出队 31,728 B
  312c6: close(fd) → return
```

**★★★ MonCtrl 全局在 `+0x311C6` 写入，早于 open/mmap
⇒ 后续任何失败都不影响它的有效性。**

### 诊断字符串

```
 FixedAreaTop  : %.8X\r\n
 FixedAreaSize : %.8X\r\n
 Physical Addr : %.8X\n
 Logical  Addr : %p\n
 Mon Buf Size  : %.8X\n\n
ERROR : Failed to open stream driver!
ERROR : Failed to mmap!
```

### 为什么在 QEMU 里 mmap 必然失败（且无害）

**`Backup_read_4Byte` @ libbackup+0x1234（48 B，薄包装）：**

```asm
  123e: r2 = 0xFFFFFFFF          ; ★ 默认值
  1244: str r2, [r3,#-4]!        ; 先写输出变量
  ...   → backup_sync_msg(...)   ; 232 B 同步消息
```

**★ `libbackup.so` 全部 API 都是 44~52 字节的薄包装，
真正的消息往返在 `backup_sync_msg`(228 B) / `backup_async_msg`(300 B)。**

**⇒ 备份服务不存在 ⇒ 返回 0xFFFFFFFF ⇒ mmap 长度/偏移非法 ⇒ 失败 ⇒ 提前返回
⇒ **但 MonCtrl 已建好 ⇒ SetEventSender 不再崩。**

### 若要让监视器队列也建立

**stub `Backup_read_4Byte`：**

| key | 含义 | 建议值 | 依据 |
|---|---|---|---|
| **0x00FA0028** | **FixedAreaTop** | **0x035A5000** | cmdline `klog.addr` |
| **0x00FA005D** | **FixedAreaSize** | **0x20000** | cmdline `klog.size`，≥ 需要的 0x8000 |

**★ 队列共需 `0x410 + 0x7BF0 = 0x8000`（32 KB）。**
**★ 备选：`boottime=0x20000@0x03585000`。**

### 附：`/setting/Backup.bin` 已完整提取

**位置：nflasha2 偏移 0xAE0 目录项，FAT12，起始簇 0x14，1,235,836 字节。**
**★ 文件里**不含** 0xFA0028/0xFA005D 字面量
⇒ ID→偏移映射在备份服务端，不在文件里
⇒ 只能靠 stub `Backup_read_4Byte` 改这两个值。**

---

## 七、★★★ 修正与最终结论（QEMU 线实测反馈后）

### 我的 `.bss` 假说被证伪

**QEMU 线实测：`dlopen` 返回后、任何初始化之前，`.bss` 是干净的**
（`echo=0 / q1=0 / q2=0 / pfx=0`）⇒ **加载器确实清零了 `.bss`。**
**`isEchoBackOn == 1` 是之后被置上的 —— 因为 App 启动参数带 `debug`
（`im.elf debug s g r u l i a lazy-global`），**真机上也是 1**。**

**⇒ 所以真机上 MonCtrl **必须**存在，`MonDrv::Init` 不是它的来源。**

### 5 处 `MonCtrl` 构造点的真实属主

```
0x1E910  MWF::EventReceiver::PrintQueue(ParamDump::Filter)      ← 栈上临时，无关
0x1E9E8  MWF::EventReceiver::PrintMessage(bool, ObjMsg*, char*) ← 栈上临时，无关
0x1F238  MWF::InhDP::Initialize()  @0x1F220                     ← 建 3 个
0x1F252  MWF::InhDP::Initialize()
0x1F26C  MWF::InhDP::Initialize()
```

### 各实例落到哪个全局（GOT 解出）

| MonCtrl 实例 | 全局地址 | 备注 |
|---|---|---|
| **`RsrcDP::smp_monErr`** | **0x4586C** | **★ `SetEventSender` 用的就是这个** |
| `InhDP::smp_monErr` | 0x44254 | |
| `InhDP::smp_monWrn` | 0x442B8 | |
| `InhDP::smp_monDbg` | 0x442B0 | |
| `InhMcmd::smp_monAdd` | 0x442D8 | debug=true |

### 完整调用链（全部反汇编确认）

```
MWF::Initialize()                        @0x20A09
  20a1e: strb #1, [0x442DB]              ← ★★★ 幂等令牌，在干活【之前】置 1
  20a20: → MwfCommon::Init()
  20a24: → Monitor::Init()
  20a4a: → ObjThread::StartThread()
  20a4e: → ObjLog_Mcmd::Init()
  20a52: → RsrcMcmd::Init()              @0x2D0CC
            2d0d0: → RsrcDP::Initialize()   @0x2CC29（412 B）
                      → new MonCtrl → RsrcDP::smp_monErr @0x4586C
  20a56: → InhMcmd::Init()               @0x204C8（100 B）
            204ce: → InhDP::Initialize()    @0x1F220
                      → 3× MonCtrl + 1× MonCtrl(debug) → InhMcmd::smp_monAdd
  20a5a: → ObjIf_Mcmd::Init()
  20a5e: → ObjIf::Init()
  20a62: → EventIdFactory::Initialize(0x83e8, 0x800)
```

### ★★★ 真正的 bug：幂等令牌被提前消耗

**令牌在 `MWF_BASE + 0x442DB`（`.bss`，静态值 0）。
`MWF::Initialize()` 在 `+0x20A1E`（第二条指令）就把它置 1，**早于所有实际工作**。**

**⇒ 若从 preload 提前调用且中途失败（卡在 0x20A24~0x20A4E 之间），
**App 自己再调时 `ldrb r5,[r3]; bne 20a8a` 直接跳过**
⇒ `RsrcDP::smp_monErr` 永远为 0 ⇒ `SetEventSender` 永远崩。**

### 修法

**① 推荐：不要从 preload 调 `MWF::Initialize()`，让 App 自己调。**

**② 若必须提前调，调用后把令牌复位：**
```c
MWF_Initialize();
*(volatile unsigned char*)(MWF_BASE + 0x442DB) = 0;  // 归还令牌
```
**⚠️ 会跑两次，某些 `operator new` 会重复分配。**

**③ 直接调叶子（注意：`RsrcDP::Initialize` / `InhDP::Initialize` **都没有幂等守卫**，
调两次会重复分配并覆盖全局，泄漏但不崩）：**
```
RsrcMcmd::Init() → RsrcDP::Initialize()
InhMcmd::Init()  → InhDP::Initialize()
ObjLog_Mcmd::Init() / ObjIf_Mcmd::Init() / ObjIf::Init()
EventIdFactory::Initialize(0x83e8, 0x800)
```

### 关于 QEMU 线新崩的 `libMWF+0x25CC0`

```
0x25CC0 = ~ParamStd_Template<int, Param::Type(7)>()
25cc6: blx MWF::Param::~Param()
```
**⇒ 在析构**构造到一半/被覆盖的对象** —— 与令牌问题同源（下游产物）。
**★ 修好令牌前不要急着修这个。**

### 关于 stub 的 FixedAreaTop/Size 的疑点

**QEMU 线 stub 成 `0x8F000000 / 0x80000`，mmap 成功、MonQueue 建起来了。
但 `+0x53400` 这个偏移暗示真机的缓冲区在某个**特定 RTOS 共享区**
（cmdline 候选：`klog.addr=0x035A5000` / `boottime=0x20000@0x03585000`）。
⇒ 建在普通 DDR 里不会崩，但监视器输出没人读（另一端在 RTOS）。
★ 若目的只是不崩，现状可接受；要让监视器真工作，这两个值需对齐 RTOS 侧。**
