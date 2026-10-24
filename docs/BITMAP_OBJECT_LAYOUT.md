# Bitmap 对象与 info 块布局（从 vtable 反推，软件 GPE 所需）

> 方法：从 `BMP_Create` 的构造函数（`0x11B6C0`）读出 vtable 指针，
> 再按槽位取出各方法的真实实现并反汇编。
> **本文把"取像素 / 取 stride / 取矩形"三件事全部落实到了具体偏移。**

## 一、vtable 定位

```
BMP_Create @0x118F18:
  118f5a: blx operator new(0x80)          ; Bitmap 对象 128 字节
  118f72: bl  0x11B6C0                    ; ★ 构造函数

构造函数 0x11B6C0:
  11b6dc: ldr  r3,[r5,r3]                 ; r3 = *(GOT) = vtable
  11b6e2: adds r3, #8                     ; ★ vtable + 8（标准 C++：前两字是 offset-to-top / RTTI）
  11b6e6: str  r3,[r4, #0]                ; obj->[0x00] = vtable + 8
  11b6fa: blx  calloc(1, 0x34)            ; ★ 52 字节 info 块
  11b700: str  r0,[r4, #4]                ; obj->[0x04] = info
```

**vtable 实体 = `0x34AE30`**，`obj->[0x00]` 存的是 `0x34AE38`（= vt+8）。
所有槽位偏移都**相对 `obj->[0x00]`**。

## 二、vtable 槽 → 真实实现

| 槽 | 实现地址 | 语义 | 输出 |
|---:|---|---|---|
| `+0x14` | `0x11B455` | **GetSubRectangle** | `*out = info + 0x0C` |
| `+0x18` | `0x11B425` | **getRowPixelCount** | `*out = info + 0x14` |
| `+0x30` | `0x11B319` | **getPhysicalAddress** | `*out = info->[0x24]` |
| `+0x4C` | `0x11BB69` | **writeLockBits**（取锁 + 返回像素） | 见 §四 |
| `+0x50` | `0x11B585` | writeUnlockBits | — |

### 实现体（都是极短的转发）

```asm
; +0x14 GetSubRectangle @0x11B454
11b458: cbnz r1, .ok        ; assert(out != NULL) 行 285
11b46e: ldr  r3,[r0,#4]     ; r3 = obj->info
11b470: adds r3, #12        ; ★ info + 0x0C
11b472: str  r3,[r1,#0]     ; *out = info+0x0C

; +0x18 getRowPixelCount @0x11B424
11b43e: ldr  r3,[r0,#4]
11b440: adds r3, #20        ; ★ info + 0x14
11b442: str  r3,[r1,#0]     ; *out = info+0x14

; +0x30 getPhysicalAddress @0x11B318
11b31e: ldr  r3,[r0,#4]     ; info
11b320: ldr  r3,[r3,#36]    ; ★ info->[0x24]
11b322: str  r3,[r1,#0]     ; *out = info->[0x24]
```

## 三、★ `info` 块布局（52 字节，`calloc(1,0x34)`）

| 偏移 | 内容 | 依据 |
|---:|---|---|
| `+0x0C` | **矩形** `{int16 x, y, w, h}`（8 字节，占 +0x0C..+0x13） | GetSubRectangle 返回 `info+0x0C`；`gpermFillrect` 从中读 `+4`=w / `+6`=h（int16） |
| `+0x14` | **rowPixelCount（行像素数 = 以像素计的 stride）** | getRowPixelCount 返回 `info+0x14` |
| `+0x24` | **物理地址** | getPhysicalAddress 读 `info+0x24` |

> 其余字段（`+0x00..+0x0B`、`+0x18..+0x23`、`+0x28..+0x33`）待确认，**像素格式很可能在其中之一**。

## 四、Bitmap 对象布局（0x80 = 128 字节）

| 偏移 | 内容 | 依据 |
|---:|---|---|
| `+0x00` | **vtable 指针**（= `vt+8`） | 构造函数 `str r3,[r4,#0]` |
| `+0x04` | **info 块指针**（`calloc(1,0x34)`） | 构造函数 `str r0,[r4,#4]` |
| **`+0x08`** | **★ 像素缓冲指针** | `writeLockBits` 里 `ldr r3,[r4,#8]`；另一槽 `*out = obj->[0x08]` |
| `+0x0C` / `+0x10` / `+0x14` / `+0x18` | 初始为 0 | 构造函数清零 |
| `+0x1C` | 锁计数 | `writeLockBits` 里 `ldr/adds/str [r4,#0x1C]` |
| `+0x20` | 另一计数 | `str r3,[r4,#32]` |
| `+0x24` | **`pthread_mutex`** | `add r6, r0, #0x24` → `pthread_mutex_lock` |
| `+0x40` | **`pthread_cond`** | `add r0, r4, #0x40` → `pthread_cond_wait` |
| `+0x70..+0x7C` | 初始为 0 | 构造函数清零 |

## 五、★ 软件绘制可直接用这套偏移（不再需要虚函数调用）

```c
/* GRM_gpermFillrect(a, b, const FillRectParam *p, c) */
void *bmp  = p->bitmap;                       /* p + 0x00 */
void *info = *(void **)((char *)bmp + 0x04);  /* info 块 */
void *pix  = *(void **)((char *)bmp + 0x08);  /* ★ 像素基址，不必调虚函数 */
int   rowpx = *(int *)((char *)info + 0x14);  /* ★ 行像素数 = 像素计 stride */
struct { short x, y, w, h; } *r = (void *)((char *)info + 0x0C);  /* 定位图整体矩形 */

/* 目标矩形用 p->x0,y0,x1,y1（p+0x08..0x0E）；颜色 p->color[4]（p+0x04..07）
   注意 p->mode（p+0x10）只接受 1/2，否则真机直接 return 不绘制 */
```

**⇒ 这比走 vtable 更简单，而且完全基于固件自身的数据结构（App 创建的 bitmap 就是真实对象）。**

## 六、仍需确认的一项

**每像素字节数（bpp）** —— `rowpx` 给的是"像素数"，像素基址有了，
但要从像素坐标算字节偏移还需要 bpp：

```
byte_offset = (y * rowpx + x) * bpp
```

**建议实测**：让 App 画一次已知纯色，dump `pix` 前 64 字节看重复单元：
- 2 字节重复 ⇒ RGB565 / RGBA5551，`bpp=2`
- 4 字节重复 ⇒ RGBA8888，`bpp=4`

**一次运行即可定，且顺带确定颜色通道顺序**（例如纯黑会出现 `00 00 00 FF` 或 `FF 00 00 00`）。

---

## 七、geo 块完整布局与 stride 公式（第二轮实证）

### 7.1 `BMP_Create` 的初始化调用 `0x11B774`

```asm
11b7a2: ldr  r5,[r0,#4]        ; r5 = geo（info 块）
11b7a4: adds r1, r5, #4        ; src = geo + 0x04
11b7a6: add  r3, r5, #12       ; dst = geo + 0x0C
11b7aa: ldmia r1,{r0,r1}       ; 读 geo+0x04..0x0B
11b7ac: stmia r3,{r0,r1}       ; ★ 写到 geo+0x0C..0x13  —— 【拷贝】
11b7b2: str  r8,[r4,#12]       ; obj->[0x0C] = arg1
11b7be: strh r3,[r5,#20]       ; geo+0x14 = 0（int16，稍后由 align 填）
11b7c0: strh r3,[r5,#22]       ; geo+0x16 = 0（int16，保持 0）
11b7c2: ldrsh r0,[r5,#8]       ; r0 = geo+0x08 （int16，宽度）
11b7c6: bl   0x119D04          ; align_up(width, max(arg,8))
```

**⇒ `geo+0x0C` 是 `geo+0x04` 的【副本】**，所以：

| 偏移 | 类型 | 含义 |
|---:|---|---|
| `+0x04` | int16 | 基准矩形 `x` |
| `+0x06` | int16 | 基准矩形 `y` |
| `+0x08` | int16 | **宽 w** |
| `+0x0A` | int16 | **高 h** |
| `+0x0C` | int16 | 当前矩形 `x`（= +0x04 的副本） |
| `+0x0E` | int16 | 当前矩形 `y` |
| `+0x10` | int16 | **当前宽 w**（GetSubRectangle 返回 `geo+0x0C`，读 `+4` = 这里 ✓） |
| `+0x12` | int16 | **当前高 h**（读 `+6` = 这里 ✓） |
| `+0x14` | int16 | **rowPixelCount（像素计 stride）** |
| `+0x16` | int16 | ★ **疑似像素格式**（创建时置 0，与 a7c2 的 `format` 语义吻合） |
| `+0x24` | u32 | **物理地址** |

### 7.2 ★ stride 公式（`0x119D04` 实证）

```asm
119d06: subs r0, #1            ; r0 = width - 1
119d08: cmp  r1, #8
119d0c: movge r4, r1           ; r4 = max(align, 8)
119d0e: movlt r4, #8
119d12: adds r0, r0, r4        ; r0 = width - 1 + a
119d16: bl   <div>             ; r0 = (width-1+a)/a
119d1a: muls r0, r4            ; * a
119d1c: sxth r0, r0
```

即：

```c
int a = (align >= 8) ? align : 8;
stride_pixels = ((width - 1 + a) / a) * a;      /* = align_up(width, a) */
```

**⇒ 若宽度本来就是 8 的倍数，则 `stride == width`。**

### 7.3 ★ 像素格式的推论

`BMP_Create` 初始化时把 `geo+0x16` 置 **0**，而 a7c2 的实现里：

```c
unsigned int bpp = format ? 4 : 2;          /* gpe-raster.c */
```

⇒ **`format == 0` ⇒ `bpp = 2` ⇒ 16 bpp。**

而 a7c2 的显示路径实现是 **RGBA4444**：

```c
pack(r,g,b,a) = (r & 0xF0) << 8 | (g & 0xF0) << 4 | (b & 0xF0) | (a >> 4);
/* R[15:12] G[11:8] B[7:4] A[3:0] */
```

**⇒ 最可能的组合：`geo+0x16` = format = 0，bpp = 2，格式 RGBA4444。**

> 仍需一次运行实测确认（打印 `geo+0x16` 与像素字节模式）。

## 八、⚠️ 锁/死锁问题与"更彻底的绕过法"

bitmap 对象里有：

| 偏移 | 内容 |
|---:|---|
| `+0x1C` | 锁计数 |
| `+0x20` | 另一计数 |
| `+0x24` | **pthread_mutex** |
| `+0x40` | **pthread_cond** |

`writeLockBits`（槽 `+0x4C` @`0x11BB68`）会 `pthread_mutex_lock`，
并在 `0x11BBA8` 处 `pthread_cond_wait` —— **这是 GPU fence 同步，没有 GPU 时可能永久阻塞。**

### ★ 绕过法：根本不调 `writeLockBits`

像素指针**直接可读**：

```
obj->[0x08] = 像素缓冲指针
```

两条独立代码路径都返回它：
- `writeLockBits` 成功路径：`11bbfa: ldr r3,[r4,#8]` → `str r3,[r9]`
- 另一个槽（`0x11B326`）：`ldr r3,[r0,#8]` → `str r3,[r1]`

**⇒ 在纯软件渲染场景下没有 GPU 竞争，直接读 `obj+0x08` 即可，
既避免死锁、又不需要 interpose 锁函数。**

（仍建议 shim 里把 `WriteUnlockBits` interpose 成空操作，防止 App 其他路径真去加锁。）

---

## 九、★★ bpp 的来源已确定：它是缓冲区大小公式的第三个因子

`0x11B774` 初始化函数的完整算术（实证）：

```asm
11b7c2: ldrsh r0,[r5,#8]        ; r0 = geo+0x08  （宽）
11b7c6: bl    0x119D04          ; align_up(宽, max(arg,8))
11b7d4: strh  r0,[r5,#24]       ; ★ geo+0x18 = 对齐后宽  (int16)
11b7d8: ldrsh r0,[r5,#10]       ; r0 = geo+0x0A  （高）
11b7dc: bl    0x119D20          ; align_up(高, max(arg,8))
11b7e4: strh  r0,[r5,#26]       ; ★ geo+0x1A = 对齐后高  (int16)

11b802: ldrh  r2,[r3,#24]       ; r2 = 对齐后宽
11b804: ldrh  r5,[r3,#26]       ; r5 = 对齐后高
11b806: ldrsh r3,[r7,#6]        ; ★ r3 = 栈参数（= 每像素字节数 bpp）
11b80a: smulbb r5, r5, r2       ; r5 = 高 × 宽
11b80e: muls  r5, r5, r3        ; ★ r5 = 宽 × 高 × bpp  = 缓冲区字节数
```

### 结论

| 项 | 值 | 位置 |
|---|---|---|
| **对齐后宽（= 像素计 stride）** | `align_up(width, max(a,8))` | **`geo+0x18`**（int16） |
| **对齐后高** | `align_up(height, max(a,8))` | **`geo+0x1A`**（int16） |
| **缓冲区字节数** | `geo+0x18 × geo+0x1A × bpp` | 计算式 |
| **bpp** | **调用方传入的参数**（`BMP_Create` → `0x11B774` 的第 7 个栈参数 `[r7+6]`） | 不是常量 |

**⇒ 全路径没有"格式值 → 常量"的映射表；bpp 由 `GRM_bitmapCreate` 的调用参数决定。**
（这一条验证了 `session-6d5bc1eb` §10.3 的判断。）

### ★ 由此得到一个**确定**的 bpp 求法（不再需要反推像素）

```
bpp = 像素缓冲字节数 / (geo+0x18 × geo+0x1A)
```

像素缓冲的分配大小可以从两处取得：
- `BMP_Create` 里分配像素缓冲那次调用的 size 参数
- 或运行时：`WriteLockBits` 返回的指针 + 已知 buffer 大小

**⇒ 这是"确定性"求法，比"画纯色看重复单元"更可靠。**

### ⚠️ 需要修正我之前的一处说法

我先前说 `getRowPixelCount` 返回 `geo+0x14` 就是 stride。
但实测 **对齐后宽写的是 `geo+0x18`**，而 `geo+0x14` 在初始化时被置 0
（可能由后续调用 `0x119DD0` / `0x11A5D8` 填）。

**⇒ 实现时请优先读 `geo+0x18` 作为 stride；`geo+0x14` 作为交叉验证。**
两者应当相等（若 `geo+0x14` 最终=filled），不等时以 `geo+0x18` 为准。

---

## 十、★★★ 最终答案：`format → bpp` 是**查找表**，ZV-1F 的完整映射已解出

> 本节**推翻我在 §7.3 用 `geo+0x16` 推断 bpp 的猜测**（该字段恒为 0，不能承载格式信息）。
> bpp 由 `geo+0x1C` 的格式值经查找表得出。

### 10.1 调用链

`0x11B774` 初始化函数里：

```asm
11b7e6: ldr  r0,[r3,#28]       ; r0 = geo+0x1C  ← ★ 格式值
11b7e2: adds r1, r7, #6        ; &local
11b7e8: bl   0x119654          ; bpp = f(format, &out16)
...
11b806: ldrsh r3,[r7,#6]       ; r3 = bpp（上面算出的局部值）
11b80e: muls r5, r5, r3        ; 缓冲区字节数 = 对齐宽 × 对齐高 × bpp
```

### 10.2 `0x119654`：断言 + 查表

```asm
119654: cmp  r0,#0
11965a: bgt  11966e            ; assert(format > 0)   行 0x170
11966e: cmp  r0,#9
119670: ble  119686            ; assert(format <= 9)  行 0x171
119686: ldr  r3,[pc,#36]       ; 表地址（GOT/PC 相对）
11968a: ldrh r3,[r3, r0, lsl #1]   ; ★ table[format]，16 位元素
119690: strh r3,[r1,#0]        ; *out = table[format]
```

**⇒ 格式合法范围是 `1..9`，超出会 `__assert_fail`。**

### 10.3 ★★ ZV-1F 的完整映射表（`.rodata` @ `0x29D88E`）

| format | bpp | 说明 |
|---:|---:|---|
| **1** | **2** | 16bpp（565 / 5551 / 4444 之一） |
| **2** | **4** | 32bpp（RGBA8888 类） |
| **3** | 2 | 16bpp |
| 4 | **1** | 8bpp（调色板） |
| 5 | 2 | 16bpp |
| 6 | 2 | 16bpp |
| 7 | 2 | 16bpp |
| 8 | 4 | 32bpp |
| 9 | 4 | 32bpp |

> 表后紧跟 `0x4D42`（= `'B','M'`，BMP 文件魔数），旁证取表位置正确、且表长恰为 10 项。

### 10.4 ⇒ 实现时这样取 bpp（不需要再猜）

```c
static const uint8_t bmp_bpp_by_format[10] = {0, 2, 4, 2, 1, 2, 2, 2, 4, 4};

int format = *(int32_t *)((char *)geo + 0x1C);    /* ★ 格式值 1..9 */
if (format < 1 || format > 9) return;             /* 真机会 assert */
int bpp = bmp_bpp_by_format[format];
int stride = *(int16_t *)((char *)geo + 0x18);    /* 像素计 */
int ah     = *(int16_t *)((char *)geo + 0x1A);
/* 缓冲区字节数应为 stride * ah * bpp，可据此自检 */
```

### 10.5 仍需实测的只有"通道位序"

bpp 已确定，但 16bpp 具体是 RGB565 / RGBA5551 / RGBA4444，
以及 32bpp 是 RGBA / BGRA，仍需一次实测确认（画已知纯色后看字节）。

**⇒ 但 bpp 本身不再是未知项 —— 它会直接由 `geo+0x1C` 的格式值决定。**

---

## 十一、★★★ 重大更正：App 用的是 `GRM_bitmapCreateFromPhys`，位图内存由 **App 提供**

### 11.1 导入符号实证

| 符号 | `appFw.so` 是否导入 |
|---|---|
| **`GRM_bitmapCreateFromPhys`** | **✅ 是（UND）** |
| `GRM_bitmapCreate` | ❌ 否 |
| `GRM_bitmapGetPhysicalAddress` | ❌ 否 |
| `GRM_bitmapGetRowPixelCount` | ❌ 否 |
| `GRM_bitmapGetSubRectangle` | ❌ 否 |
| `GRM_bitmapWriteLockBits` | ❌ 否 |

**⇒ App 只用 `CreateFromPhys` 一个入口创建位图，其余全是库内部使用。**

### 11.2 含意（推翻了"库自己 malloc 像素缓冲"的假设）

```
GRM_bitmapCreateFromPhys(...)  @0x114AF0
    → 5 次 no-op 转换调用（0x11498E / 0x114982 / 0x1149B2 / 0x11499A / 0x1149A6）
    → 重排 9~15 个参数到栈，调用真正的实现
```

**⇒ 像素缓冲的物理地址由 App 传入，不是 `BMP_Create` 分配的。**

**⇒ 所以 `[bmp+0x08]`（VA）必须由"把那个物理地址映射进本进程"建立；
`info+0x24` = App 传入的物理地址。**

### 11.3 对 QEMU 线（`session-41b33827`）的直接价值

之前"fake 一个 0x08000000 大窗口"方向虽被否，
但**"需要能访问 App 指定的物理缓冲"这件事本身是对的** —— 只是机制应该是：

> **在 `GRM_bitmapCreateFromPhys` 里读出 App 传入的物理地址，
> 把它（或它所在的物理段）映射进本进程，再把映射后的 VA 填回 `[bmp+0x08]`。**

⇒ 这能解释 App 为什么会写到 `0x085360E4` 这类地址 —— 那是它自己申请的物理缓冲。

### 11.4 同时否掉一条假设

`appFw` **不导入** `GRM_bitmapGetRowPixelCount`（也不导入 `GetSubRectangle` / `GetPhysicalAddress`）。
⇒ **"死 getter 返回 0 导致 size 查询得 0"这条因果链不成立** —— App 根本没调它。
⇒ stride 只能从 `geo+0x18` 取（内部使用），或 App 自己按 `align_up(宽,8)` 算。
