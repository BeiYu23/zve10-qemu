# libObj 单例地图：谁自建、谁要手工造（QEMU 攻坚用）

> 目的：把"崩一次补一个"变成"一次看全"。全部地址基于 `libObj.so`
> （md5 `12cf982d…`，已与真机逐字节核对）。

---

## 一、六个单例，只有两个需要手工构造

| 单例 | getter | 单例变量 | 自建能力 | 结论 |
|---|---|---|---:|---|
| `ux::gfxeng::GraphicsEngine` | `0x61229C` | `0x145CAB8` | **❌ 无** | **手工构造**（QEMU 侧已完成） |
| `ux::wgtsys::WidgetSystem` | `0x616FE4` | `0x145CB1C` | **❌ 无** | **手工构造**（含自指链表头，见 §三） |
| `ux::resmgr::ResourceManager` | `0x610218` | `0x145CA64` | ✅ **有惰性创建器** | **调一次即可**（见 §二） |
| `ViewIdSoTable` | `0x8A69BC` | — | ✅ `__cxa_guard` 惰性构造 | 无需处理 |
| `ModelIdSoTable` | `0x8A0170` | — | ✅ 同上 | 无需处理 |
| `ModelTree` | `0x8A65D0` | — | ✅ 同上 | 无需处理 |
| `InfraInput` | `0x8C22A0` | — | ✅ 自带 create（`malloc(0x7a)`） | 无需处理 |

**⇒ 只需要手工处理 2 个**，而 GraphicsEngine 已经解决。**WidgetSystem 是最后一个**。

---

## 二、★ ResourceManager：有惰性创建器，但被"引用计数"挡住

### 弱指针 + 惰性创建

`ResourceManager::sys_getInstance()`（`0x610218`）语义：

```
r0 = *(0x145CA64)          ; 弱指针
if (r0 == 0) return NULL   ; ← 未创建就返回空（没有回退对象！）
else 返回计算后的地址
```

**⇒ `res = 0` 的直接含义：`*(0x145CA64) == 0`，即 ResourceManager 从未被创建。**

### 创建器存在 @ `0x610184`

```asm
610184: push {r4,r5,r7,lr}
        r5 = r0                       ; 唯一参数
        r4 = &0x145CA64               ; 弱指针地址（PC 相对）
        r3 = [r4]
        if (r3 != 0) return 1         ; 已有 → 直接成功
        ; ---- 创建路径 ----
        bl 0x854670                   ; 分配
        bl 0x63FB14
        [r4] = r0                     ; ★ 写入弱指针
        bl 0x8558C4                   ; 初始化
        ...
        return 1/0
```

### 但它的调用者被引用计数短路 @ `0x610F74`

**`0x610F74` 正是 `uxengine_init`（`0x6271D4`）调用的第一个函数**（`6271f4: bl 0x610f74`）。

```asm
610F74: stmdb sp!,{r4-r10,lr}
        r3 = &refcount                 ; PC 相对 → 0x145CA6A
        r2 = *refcount
        if (r2 != 0) {                 ; ★★ 已非零 → 只加计数，直接返回
            r2 += 1
            *refcount = r2
            return                     ;   不创建！
        }
        *refcount = 1
        bl 0x640398
        bl 0x610184                    ; 创建 ResourceManager
        bl 0x6131D4                    ; 后续初始化
        ...
```

### ★★ 给 QEMU 的直接假设：`res=0` 可能是"引用计数被毒化"

```
布局：0x145CA64 = ResourceManager*   (4 字节)
      0x145CA6A = 引用计数            (2 字节，紧跟其后)
```

**若 `*(u32*)0x145CA64 == 0` 而 `*(u16*)0x145CA6A != 0`**，就说明：
> 之前某次（可能崩溃/半途而废的）调用已经把计数置 1，但实例没建成
> ⇒ 之后每次 `0x610F74` 都只加计数、永不创建 ⇒ **`res` 永远 0**

**验证与修复（一行的事）**：
```c
u32 ptr = *(u32*)(libObj_base + 0x145CA64);
u16 cnt = *(u16*)(libObj_base + 0x145CA6A);
// 若 ptr==0 && cnt!=0：
*(u16*)(libObj_base + 0x145CA6A) = 0;      // 清计数
// 然后重新走初始化（uxengine_init 或直接调 0x610F74）
```

---

## 三、WidgetSystem：必须手工构造，且 `+0x484` 要自指链表头

（前一轮已给出，此处存档）

`0x627554` 插入逻辑要求 `container+4` / `container+8` 是有效指针：

```c
char *ws = calloc(1, 0x2000);
unsigned int head = (unsigned int)(ws + 0x484);
*(unsigned int *)(head + 4)  = head;   // prev → 自己
*(unsigned int *)(head + 8)  = head;   // next → 自己
*(unsigned int *)(head + 32) = 0;      // count
*(void **)((char *)libObj_base + 0x145CB1C) = ws;
```

**+0x4C** 会收到写入（`[r2+12] = newNode`），所以要保证 `ws+0x484+12` 可写（在 0x2000 块内即可）。

---

## 四、给 QEMU 的建议执行顺序

```
① 读 0x145CA64 / 0x145CA6A → 判断是否"计数毒化"
     是 → 清计数，重跑初始化
     否 → 继续
② 手工造 WidgetSystem（§三）→ 再调 0x8A2EE0
③ 观察 pVM(0x149221C)、res(0x145CA64)、wg(0x145CB1C) 三个值
④ 仍崩 → 记录新的 NULL 单例/偏移，回来找我解
```

**判据仍然是**：固件是否**自己**调用 `ViewManager::ctor`（`0x8A4E28`）。
