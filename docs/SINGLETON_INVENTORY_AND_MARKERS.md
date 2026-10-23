# App 侧单例盘点表 + `0x8A2EE0` 进度探针

---

## 一、★ 先解决一个逻辑矛盾

**QEMU 侧观测**：`pvm=0`（55 秒采样，全程不变），同时 App 线程活着、`viewUnified2.so` 已 mmap。

**但反汇编证明**：`0x8A2EE0` 里 0x8A2FA6 调用 `ViewManager::ctor`（`0x8A4E28`），
而该构造函数**最后一条语句**就是 `str r5,[&pViewManager]`（`0x8A4F62`）。

⇒ **如果 `0x8A2EE0` 完整跑完，`pVM` 不可能还是 0。**

**所以只有三种可能：**
1. `0x8A2EE0` **没有跑到 0x8A2F94/0x8A2FA6**（中途提前返回或走了别的分支）
2. `ViewManager::ctor` 被调用但**在写 pVM 之前失败/返回**
3. `pVM` 的地址或读取方式有偏差（`0x149221C` 是从 GOT 槽 `0x13DCB78` 解出来的，值得复核）

**这个矛盾比"缺哪个单例"更优先** —— 先确定 `0x8A2EE0` 到底走到哪一步。

---

## 二、★ `0x8A2EE0` 的进度探针（一次运行定位）

在下列地址各放一个计数探针（或打印），即可二分出停在哪：

| 地址 | 事件 | 若未命中说明 |
|---|---|---|
| `0x8A2EEE` | `str r3,[this+0x28]` | 函数根本没进 |
| `0x8A2F0E` | `osal_wai_sem_tmo(0x83037A)` 等待点 | — |
| `0x8A2F26` | `pipe()` | 信号量之后卡住 |
| `0x8A2F4A` | `new(0x28)` | — |
| `0x8A2F6C` | `new(?)` → `[app+0x0C]` | — |
| `0x8A2F72` | `new(0x58)` → `[app+0x08]` | — |
| `0x8A2F82` | `new(0xAC)` → `[app+0x04]` | — |
| **`0x8A2F94`** | **`new(0x70)` ← ViewManager 分配** | 提前退出了 |
| **`0x8A2FA6`** | **`bl ViewManager::ctor`** | 分配失败 |
| **`0x8A2FAC`** | **`str r5,[app+0]` 保存** | — |

**判据**：
- 命中到 `0x8A2F94` 但没到 `0x8A2FA6` ⇒ `operator new(0x70)` 失败（堆不够/被 hook）
- 命中到 `0x8A2FA6` 但 `pVM` 仍 0 ⇒ **ctor 内部提前返回**（可能在 0x8558C4 那 5 次虚调用里）
- 连 `0x8A2F4A` 都没到 ⇒ 卡在 `pipe()` 或更早

---

## 三、App 侧单例盘点表（全部地址已实证）

| 单例 | 变量地址 | 是否存在创建者 |
|---|---|---|
| `GraphicsEngine` | `0x145CAB8` | ❌ 需手工（已解决） |
| `WidgetSystem` | `0x145CB1C` | ❌ 需手工（已解决） |
| **`pViewManager`** | **`0x149221C`** | 由 `ViewManager::ctor`（`0x8A4E28`）写入 |
| `ResourceManager`（弱指针） | `0x145CA64` | 有惰性创建器 `0x610184` |
| `ResourceManager` 引用计数 | `0x145CA6A`（u16） | 初始化入口 `0x610F74` |
| `InputService::m_pMyInstance` | `0x1459548` | 需查（当前为 0） |
| `InfraInput` | `0x1492524` | ✅ 自带 create（`malloc(0x7a)`） |
| `ViewIdSoTable` | `0x1492450` | ✅ `__cxa_guard` 惰性构造 |
| `ModelIdSoTable` | `0x14920CC` | ✅ `__cxa_guard` 惰性构造 |
| `ModelTree` | `0x149222C` | ✅ `__cxa_guard` 惰性构造 |
| App 名字缓冲区 | `0x14920E8` | 由 `Application_System_Init` 填（默认 `"X"`） |

**⇒ 盘点时只需看前 6 个**（后 4 个自建，不该为 0）。

---

## 四、关于 Q1：主循环在等什么

`0x8A3104`（线程体调的"主循环"）**内部没有任何等待原语**
（无 `osal_*`、无 `nanosleep`/`usleep`/`select`/`poll`）。

⇒ 它是**"跑一轮就返回"的轮询体**，真正的阻塞在它调用的函数里
（如 8 次 `0x8A2A80`、`clock_gettime`、`ScalarAppfwIF::IsViewModeDiademEv` 等）。

⇒ **"线程空闲在 futex"是事件循环的正常表现，不是卡点。**
   真正要查的是"为什么没有活干"—— 而按 §一，先确认 `0x8A2EE0` 是否真跑完。

**`ScalarAppfwIF` 定义在 `libObj.so`（`0x8A6740`）**，由 `viewUnified2/6.so` 导入
⇒ 它**不是**缺失的外部服务，App 侧自带。
