# GRM_bitmap* / GRM_fbrm* 分析（ZV-1F libObj）

> 分工交付：移植侧解 `GRM_gperm*`（怎么画），本文解 `GRM_bitmap*` + `GRM_fbrm*`（画到哪）。

## 零、先说一个坑

**所有 `GRM_*` 符号地址都带 Thumb 位（奇数值）。反汇编要 `addr & ~1`。**

例：`GRM_screenGetOnBitmap` 符号值 `0x1153D9`，实际代码在 `0x1153D8`。
用原值反汇编会得到完全错位的指令（我第一遍就踩了）。

## 一、★ 重要简化：YUV 路径和 screenGetOnBitmap 全是空桩

ZV-1F 上这 7 个函数**全部只是 `return 0`**：

```asm
1153d8  GRM_screenGetOnBitmap        : push {r7,lr}; movs r0,#0; pop {r7,pc}
1153f8  GRM_fbrmInitializeYUV        : 同上
115400  GRM_fbrmTerminateYUV         : 同上
115420  GRM_fbrmPrepareUpdateYUV     : 同上
115430  GRM_fbrmUpdateYUV            : 同上
115438  GRM_fbrmFillrectYUV          : 同上
115440  GRM_fbrmRectblitYUV          : 同上
```

**⇒ 整条 YUV 帧缓冲路径在 ZV-1F 上未被实现。软件 GPE 不需要实现它们。**

> 注：`GRM_fbrmSetScreenPreference`(88B) 和 `GRM_fbrmCheckUpdateFinished`(24B)
> 有真实实现，但我这轮未展开（见 §四 待办）。

## 二、★ bitmap 是带 vtable 的 C++ 对象

`GRM_bitmap*` 全是 `BMP_*` 的**薄封装**，真正实现和结构体在 `BMP_*` 层。

ZV-1F libObj 只导出 3 个 `BMP_*`：

| 函数 | 地址 | 大小 |
|---|---|---:|
| `BMP_Create` | `0x118F18` | 220 |
| `BMP_WriteLockBits` | `0x119494` | 48 |
| `BMP_WriteUnlockBits` | `0x1194C4` | 48 |

### 2.1 `BMP_WriteLockBits` / `WriteUnlockBits`（拿可写像素指针）

```c
BMP_WriteLockBits(void *bmp) {
    assert(bmp != NULL);              /* 断言行号 0x11b = 283 */
    vtable = *(void**)bmp;            /* [bmp+0x00] = vtable */
    fn = *(void**)(vtable + 0x4C);    /* ★ vtable+0x4C = WriteLockBits */
    return fn(bmp);
}

BMP_WriteUnlockBits(void *bmp) {
    assert(bmp != NULL);              /* 行号 0x123 = 291 */
    vtable = *(void**)bmp;
    fn = *(void**)(vtable + 0x50);    /* ★ vtable+0x50 = WriteUnlockBits */
    fn(bmp);
    return 0;
}
```

**⇒ 实用结论：不需要知道完整结构体，`BMP_WriteLockBits(bmp)` 就能拿到可写像素缓冲。**

### 2.2 `BMP_Create`（对象大小与参数）

```asm
118f58: movs r0, #128        ; ★ operator new(0x80) —— Bitmap 对象 128 字节
118f5a: blx  operator new
```

参数布局（帧基址换算后）：
```
r0 = arg0            （断言非 NULL，行号 0x46=70）
r1 = arg1
r2 = arg2
r3 = arg3
[sp_entry + 0]  = arg4
[sp_entry + 4]  = arg5   （按 int16 读，ldrsh）
[sp_entry + 8]  = arg6   （按 int16 读，ldrsh）
[sp_entry + 12] = arg7   （断言非 NULL，行号 0x45=69）
[sp_entry + 16] = arg8
[sp_entry + 20] = arg9
```

**⇒ 至少 10 个参数**（4 寄存器 + 6 栈字）。
`arg5`/`arg6` 是 int16，很可能是 **width / height**；
`arg7`（非空断言）很可能是 **像素缓冲指针或 allocator**。

### 2.3 各 `GRM_bitmap*` 的转发目标

| GRM 函数 | 转发到 |
|---|---|
| `GRM_bitmapGetPhysicalAddress` | `0x1193D4` |
| `GRM_bitmapGetRowPixelCount` | `0x1198BC` |
| `GRM_bitmapRelease` | `0x119524` |
| `GRM_bitmapWriteLockBits` | `BMP_WriteLockBits`（PLT，外部） |
| `GRM_bitmapWriteUnlockBits` | `BMP_WriteUnlockBits`（PLT，外部） |
| `GRM_bitmapGetSubRectangle` | 先 `0x1149B8(arg1)` 转换，再 `0x1192B4` |
| `GRM_bitmapCreateComposite` | 返回一个常量（疑似空/占位实现） |

## 三、★ 对软件 GPE 的实际含义

**`GRM_screenGetOnBitmap` 返回 0（空桩）** ⇒ 软件渲染器**不能靠它拿屏幕位图**。

⇒ 正确路径是：**在 `GRM_gpermFillrect` / `GRM_gpermRectblit` 里，从参数中取目标 bitmap**，
用 `BMP_WriteLockBits(bmp)` 拿到可写像素，CPU 绘制，再 `BMP_WriteUnlockBits(bmp)`。

**⇒ 因此最关键的结构仍然是 `gperm*` 的参数**（移植侧在解的那两个），
`bitmap*` 这边已经足够支撑：只要拿到 bitmap 指针，就能 `WriteLockBits` 拿到像素。

## 四、待办（我可以继续）

1. `GRM_fbrmSetScreenPreference`(88B) / `GRM_fbrmCheckUpdateFinished`(24B) 的真实实现
2. `0x1193D4` / `0x1198BC` / `0x119524` / `0x1192B4` 的内部（可反推 bitmap 完整布局）
3. `BMP_Create` 10 个参数的确切语义（需结合调用点交叉验证）

## 五、★ bitmap 虚表槽位映射（补全 §四.2）

所有 `GRM_bitmap*` 最终都走**虚函数分派**：`vtable = *(void**)bmp`，再按固定偏移调。

| vtable 偏移 | 槽位号 | 语义 | 观察来源 |
|---|---:|---|---|
| `+0x14` | 5 | 未知（GetSubRectangle 用） | `0x1192B4` |
| **`+0x18`** | 6 | **getRowPixelCount**（out 参数） | `0x1198BC` |
| **`+0x30`** | 12 | **getPhysicalAddress** | `0x1193D4` |
| **`+0x4C`** | 19 | **writeLockBits → 返回可写像素指针** | `0x119494` |
| **`+0x50`** | 20 | **writeUnlockBits** | `0x1194C4` |
| `+0x58` | 22 | 未知（Release 第二步） | `0x119524` |
| `+0x64` | 25 | 未知（Release 第一步） | `0x119524` |

**⇒ 至少 26 个虚函数槽**（最大偏移 0x64 = 索引 25）。

### 调用模板（移植侧可直接照抄）

```c
static inline void *bmp_vfn(void *bmp, unsigned off) {
    void *vt = *(void **)bmp;
    return *(void **)((char *)vt + off);
}

/* 取可写像素（软件绘制入口） */
void *pixels = ((void *(*)(void *))bmp_vfn(bmp, 0x4C))(bmp);
/* ... CPU 绘制 ... */
((void (*)(void *))bmp_vfn(bmp, 0x50))(bmp);

/* 取物理地址 / 行像素数 */
((void (*)(void *, unsigned *))bmp_vfn(bmp, 0x30))(bmp, &phys);
((void (*)(void *, int *))bmp_vfn(bmp, 0x18))(bmp, &rowpx);
```

### 由此得到的一条替代方案

bitmap 是**抽象基类 + 虚表**，所以除了拦截 `GRM_gperm*`，
还可以**把自己的 CPU 实现挂成一个兼容虚表的 bitmap 对象**（填满 26 槽，至少 `+0x4C`/`+0x50`/`+0x18`/`+0x30`）。
后者更"贴近真机结构"，但工作量更大；**建议先用拦截 `gperm*` 的路线**。

## 六、`GRM_fbrmSetScreenPreference` 与 `CheckUpdateFinished`（移植侧点名要的）

### 6.1 `GRM_fbrmSetScreenPreference(int pref)` @ `0x115304`

```asm
115306: subs r1, r0, #1
115310: cmp  r1, #3
115312: bhi  115328            ; pref-1 > 3 即 pref ∉ {1,2,3,4} → 默认/错误路径
115314: tbb  [pc, r1]          ; 4 项跳转表
```

| pref | 命令码 |
|---:|---|
| 1 | `0xE800` |
| 2 | `0xE802` |
| 3 | `0xE803` |
| 4 | `0xE801` |

然后 `bl 0x11F0F8(cmd)`。其它值走 `0x115328` 的日志/错误路径。

**⇒ 语义：屏幕输出偏好 → 4 个 fbrm 命令之一。软件 shim 可直接返回成功（或按需实现）。**

### 6.2 `GRM_fbrmCheckUpdateFinished()` @ `0x115370`

```asm
115374: bl   0x11F4BE           ; 查 VDF 显示状态
115378: ldr  r3, [0x115384]     ; = 0xFFFFD8F1 = -9999
11537a: cmp  r0, #1
11537c: movne r0, r3            ; 状态 != 1 → 返回 -9999
115380: moveq r0, #0            ; 状态 == 1 → 返回 0
```

而 `0x11F4BE`（`vdf_if_port_init` 区域，即**显示端口层**）：
```asm
11f4c4: r0 = sub_11F438()
11f4ca: sub_1251E4(r0, &out)    ; VDF 接口查询
11f4ce: r0 = out
11f4d0: rsbs r0, r0, #1         ; r0 = 1 - out
11f4d4: it cc → r0 = 0          ; out > 1 时归 0
```
**⇒ 返回 1 当且仅当 `out == 0`；否则 0。**

**⇒ 合起来：`CheckUpdateFinished()` 只在 VDF 状态为 0 时返回 0，其余返回 `-9999`。**

**⇒ shim 建议：直接 `return 0`**（表示"更新已完成"），不必接 VDF。

## 七、★ 屏幕位图从哪来 —— 答案

**关键事实：`appFw.so` 直接导入 `GRM_bitmapCreate` / `GRM_bitmapCreateFromPhys`（UND 引用）。**

**⇒ App 自己创建位图，不从 `GRM_screenGetOnBitmap` 取。**
（后者的桩也就不构成问题 —— 真机同样返回 0。）

**⇒ 因此软件绘制的"目标缓冲"来源是 `GRM_bitmapCreate*`，而它在本文档覆盖的这一半里。**

### 由此得到的实现链

```
App 调 GRM_bitmapCreate(...) → BMP_Create 分配 0x80 对象 + 像素缓冲
App 调 GRM_gpermFillrect(bmp, ...) → shim 用 BMP_WriteLockBits(bmp) 拿像素，CPU 填充
App 调 GRM_gpermRectblit(dst, ..., src) → shim 用两边 WriteLockBits 做块拷贝
App 调 GRM_fbrmCheckUpdateFinished() → shim 返回 0
```

**⇒ 即：`GRM_bitmapCreate` 应让真实 `BMP_Create` 正常跑**（它自己 malloc，内存是真实的），
**只有在 `gperm*` 里才换成 CPU 实现**。这样位图对象、像素缓冲、物理地址全部由真实代码提供。

## 八、★ bitmap 对象真实布局（构造函数 `0x11B6C0` 实证）

```c
Bitmap_ctor(bitmap, arg1, arg2, arg3)
{
    [bitmap+0x00] = vtable;            // vtable 地址 + 8（ARM C++ ABI 常规）
    [bitmap+0x04] = 0;                 // 稍后被 calloc 的几何结构覆盖
    [bitmap+0x08..0x20] = 0;
    [bitmap+0x70..0x7C] = 0;

    geo = calloc(...);
    [bitmap+0x04] = geo;               // ★ +0x04 = 几何结构指针
    assert(geo);

    [geo+0x00] = 0x00ABCDEF;           // ★ 魔数（AB CD EF，不是格式 ID）
    [geo+0x04] = 0;  [geo+0x06] = 0;   // int16
    [geo+0x08] = arg1;                 // int16  ← 维度 1
    [geo+0x0A] = arg2;                 // int16  ← 维度 2
    [geo+0x1C] = arg3;
    [geo+0x20] = <栈参数1>;
    [geo+0x2C] = <栈参数2>;
    [geo+0x30] = <栈参数3>;
    [geo+0x0C..0x13] = {两字}

    pthread_mutex_init(bitmap + 0x24, NULL);   // ★★ +0x24 = mutex
    pthread_cond_init (bitmap + 0x40, NULL);   // ★★ +0x40 = condvar
}
```

### ★★ 8.1 关键风险：`WriteLockBits` 可能【阻塞】

bitmap 里带 **mutex（+0x24）和 condvar（+0x40）** ⇒ 典型是 **GPU fence 同步**：
`WriteLockBits` 等 GPU 空闲 → 返回可写指针；`WriteUnlockBits` 交还并 signal。

**⇒ 在没有 GPU 的 shim 里直接调真 `BMP_WriteLockBits`，可能永久阻塞。**

**⇒ 建议：连 `BMP_WriteLockBits` / `BMP_WriteUnlockBits` 一起 interpose，
做成"立即返回缓冲 / 立即解锁"，绕开 mutex+condvar。**

### 8.2 `WriteLockBits` 的返回值语义

- 虚函数以 **单参数** `fn(bitmap)` 调用，返回值在 **r0** 直接返回
- `GRM_bitmap*` 外层那几个"转换"函数（`0x11497C` / `0x114982` / `0x11498E` / `0x1149B8` …）
  实测全是 **空操作**：`push {r7,lr}; add r7,sp,#0; pop {r7,pc}` ⇒ **原样透传**

**⇒ 结论：`WriteLockBits` 返回的是【标量指针】（像素基址），不是含 stride/format 的结构。**

### 8.3 `GetSubRectangle` 输出结构（确认移植侧的观察）

`0x1192B4` 用 `vtable+0x14` 写输出结构。结合移植侧"`+4` 宽、`+6` 高（int16）"的观察：

```
+0x00  int16  x
+0x02  int16  y
+0x04  int16  width    ← ★
+0x06  int16  height   ← ★
```

**⇒ 宽高不用另找，`GetSubRectangle` 就给。**

### 8.4 stride / 像素格式

**构造函数没有用常量设置 stride/format**（`geo+0x04/+0x06` 初始化为 0），
说明它们由 `BMP_Create` 里那个 `0x11B774` 初始化调用按参数填入。

**⇒ 静态拿不到，必须运行时取。** 建议在 shim 里打印：
```
bitmap 指针、[bitmap+0x04] 的几何结构前 0x40 字节、WriteLockBits 返回值、其前 64 字节
```
一次运行即可同时确定 stride 与通道序。

---

## 九、给移植侧的最终实现建议（合并两半）

```c
/* ① 让真实 BMP_Create 跑，位图对象与像素缓冲都是真的 */
/* ② interpose 掉锁，避免等 GPU fence */
void *my_writeLock(void *bmp)   { return real_writeLock(bmp); }   /* 若真实现不阻塞则直接用 */
void  my_writeUnlock(void *bmp) { /* 立即返回 */ }

/* ③ gpermFillrect 全程 CPU 绘制 */
void my_gpermFillrect(void *a, void *b, const FillRectParam *p, uintptr_t c) {
    u32 mode = *(u32*)((char*)p + 0x10);
    if (mode != 1 && mode != 2) return;          /* ★ 照抄真机：其它值不绘制 */
    void *bmp = *(void**)((char*)p + 0x00);
    int  *r   = (int*)((char*)p + 0x08);          /* x0,y0,x1,y1 四个 int16 */
    u8   *col = (u8*)((char*)p + 0x04);
    void *pix = bmp_vfn(bmp,0x4C)(bmp);
    int   rowpx; bmp_vfn(bmp,0x18)(bmp, &rowpx);  /* 行像素数 */
    /* 按 stride = rowpx * bpp 逐行填 col */
    bmp_vfn(bmp,0x50)(bmp);
}
```

## 十、回答移植侧的两个请求

### 10.1 `GRM_fbrmSetScreenPreference` **不含像素格式**（否定的）

实测它就是 VDF 命令分发器：

```c
GRM_fbrmSetScreenPreference(int pref)
{
    if ((pref - 1) > 3) goto 错误/日志路径;      /* pref ∉ {1,2,3,4} */
    cmd = table[pref - 1];                        /* 1→0xE800 2→0xE802 3→0xE803 4→0xE801 */
    return sub_11F0F8(cmd);
}

sub_11F0F8(int cmd)
{
    if ((cmd - 0xE800) > 3) return 2;             /* 越界返回 2 */
    sub_11DE70();
    sub_124DE4();                                  /* vdf_if_port_init 区域 */
    sub_123322(r0, cmd);                           /* rsrc_port_init 区域 */
    return 0;
}
```

**⇒ 这是"屏幕偏好 → VDF/资源端口命令"的路由，里面没有任何 bpp/format 枚举。**

### 10.2 `GRM_bitmapGetSubRectangle` 机制确认（闭环）

```c
GRM_bitmapGetSubRectangle(void *bitmap, void *rect_out)
{
    rect_out = sub_1149B8(rect_out);      /* 空操作转换 */
    0x1192B4(bitmap, rect_out);
    return sub_11497C(...);               /* 空操作转换 */
}

0x1192B4(bitmap, arg)
{
    assert(bitmap != NULL);               /* 行号 0xD4 = 212 */
    vtable = *(void**)bitmap;
    fn = *(void**)(vtable + 0x14);        /* 虚函数槽 +0x14 */
    fn(bitmap, arg);                       /* r0=bitmap, r1=输出结构，原样传递 */
}
```

**⇒ 输出结构由 `vtable+0x14` 的**具体实现**填写，静态看不到字段。**
**⇒ 移植侧在 `gpermFillrect` 调用点观察到的 `+4=width / +6=height`（int16）是实证，比静态更强 —— 可以闭环。**

### 10.3 ★ format/stride 的新解法：打 `GRM_bitmapCreate` 的参数

`BMP_Create` 的初始化 `0x11B774` 里：

```asm
11b790: cbnz r1 → assert(r1 != NULL)   ; 行号 0x8a = 138
11b7a2: r5 = [bitmap + 4]              ; geo
11b7a4: r1 = r5 + 4
11b7a6: r3 = r5 + 12
11b7aa: {r0,r1} = *(geo+4)             ; 读 geo+4/+6（两个 int16，宽高）
11b7ac: *(geo+0x0C) = {r0,r1}          ; 复制到 geo+0x0C/+0x0E（工作副本）
11b7b2: [bitmap + 0x0C] = r1(arg1)
```

**⇒ 没有任何"格式 → 常量"的映射表；格式/stride 完全来自调用方传的参数。**

**⇒ 最优解不是推像素，而是：interpose `GRM_bitmapCreate`，把 10 个参数全打印出来。**

App 建位图时格式值是固定的（比如恒为 1 或 2），**一次运行直接读到**，
比"画一个纯色再反推重复单元"更直接、更确定。

**⇒ 建议顺序改为：① 打 `GRM_bitmapCreate` 参数 → ② 若还需确认，再 dumper 像素。**

## 十一、★★★ `writeLockBits` 的阻塞条件（精确定位）

实现 `0x11BB68`：

```c
writeLockBits(bitmap=r4, arg1=r5, arg2=r9)
{
    pthread_mutex_lock(bitmap + 0x24);        /* 加锁 */
    [bitmap + 0x1C]++;                        /* 引用计数 */

    if (arg1 == 0)          goto 返回;         /* ★ arg1 == 0 → 直接返回，不等待 */
    if (arg1 != 0xFFFFFFFF) goto 另一条路径;   /* 其它值 */

    /* ★★ arg1 == -1 (0xFFFFFFFF) */
    sub_11BAF4(bitmap);
    if (r0 != 0) goto 出错;
    pthread_cond_wait(bitmap + 0x40, bitmap + 0x24);   /* ★★★ 永久阻塞点 */
    ...
}
```

### ⇒ 结论一：只有 `arg1 == -1` 才会阻塞

**`writeLockBits(bitmap, 0, ...)` 不会被阻塞。**

### ⇒ 结论二：`BMP_WriteLockBits` 会把 `r1` 原样透传（不设置它）

```asm
119494 BMP_WriteLockBits:
  119498: cbnz r0, 1194ae      ; assert(bitmap)
  1194ae: ldr  r3, [r0, #0]    ; vtable
  1194b0: ldr  r3, [r3, #76]   ; +0x4C
  1194b2: blx  r3              ; ★ r0=bitmap, r1 = 调用者传进来的值，未修改
```

**⇒ `BMP_WriteLockBits` 实际是【2 参数】：`(void *bmp, int timeout)`，第二个参数决定是否阻塞。**

**⇒ ★ 调用它时必须显式传 `0`，绝不能让它变成 `-1`（或未初始化栈值）。**

### ⇒ 由此给出的安全写法

```c
void *pix = ((void *(*)(void *, int))BMP_WriteLockBits)(bmp, 0);   /* 永不阻塞 */
```

**比"完全绕开锁"更稳** —— 保留了引用计数和互斥，只是不等待 GPU。

## 十二、★ `[bmp+0x08]` 能否直接读？（更正移植侧的结论）

- 构造函数 `0x11B6C0`：`str r5,[r4,#8]`，其中 `r5 = 0` ⇒ **`[bmp+0x08]` 被清零**
- 初始化 `0x11B774`：**没有设置 `[bmp+0x08]`**，但设置了 `[bmp+0x0C] = arg1`

**⇒ "完全不调虚函数、直接读 `[bmp+0x08]`" 有风险 —— 它可能仍是 0。**

**两点更正**：
1. 绘制前必须**先确认 `[bmp+0x08] != 0`**；若为 0，说明像素指针是 `writeLockBits` 过程中才建立的
2. **`[bmp+0x0C] = arg1`**（初始化里设的）—— arg1 很可能是像素缓冲或其描述符，**值得一并打印**

**⇒ 推荐：先打印 `[bmp+0x08]` / `[bmp+0x0C]` 的实际值再决定走哪条路。**

## 十三、`info` 块字段语义（补充移植侧的发现）

| 偏移 | 内容 |
|---|---|
| `+0x0C` | **矩形 `{int16 x, y, w, h}`**（`GetSubRectangle` 返回它的地址） |
| `+0x14` | **rowPixelCount**（像素计 stride；`getRowPixelCount` 返回它的地址） |
| `+0x18` / `+0x1A` | 初始化算出的派生值（与宽高相关） |
| `+0x24` | **物理地址** |

**注意**：这几个 getter 通过 out 参数返回的是【字段地址】，不是数值：
`out = info + 0x14` ⇒ 需再 `*(int*)out` 取值。

## 十四、`[bmp+0x08]` 的性质确认 + 对 `geo+0x16` 格式推论的更正

### 14.1 `[bmp+0x08]` 确实是【像素虚拟地址】字段

`0x11B34E` 处与 `info+0x24`（物理地址）**成对维护**：

```asm
11b340: ldr  r3,[r0,#8]      ; 读当前 [obj+8]
11b344: cmp  r1,r3
11b348: bne  ...             ; 不匹配 → 另一分支
11b34a: ldr  r3,[r0,#4]      ; info
11b34c: subs r1,r2,r1
11b34e: str  r2,[r0,#8]      ; ★ [obj+8] = r2（新的 VA）
11b352: ldr  r2,[r3,#0x24]   ; info+0x24 = 物理地址
11b354: adds r2,r2,r1        ; 物理地址同步偏移
```

**⇒ `[bmp+0x08]` = 像素 VA，`info+0x24` = 对应物理地址，二者由一个函数保持对应关系。**

**⇒ 移植侧"直接读 `[bmp+0x08]`"的方向是对的。**

### 14.2 但它不是"创建时无条件设置"的

- 构造函数：`str r5,[r4,#8]`，`r5 = 0` ⇒ **清零**
- 初始化 `0x11B774`：**不设置 `+0x08`**
- 只有 `0x11B34E` / `0x11BDC2` 这两处会写它，且都有前置条件

**⇒ 建议 hybrid 写法（两条都覆盖）**：

```c
void *pix = *(void **)((char *)bmp + 0x08);
if (!pix) pix = ((void *(*)(void *, int))BMP_WriteLockBits)(bmp, 0);  /* 传 0，不阻塞 */
```

### 14.3 ★ 更正：`geo+0x16` 恒为 0，不能作为格式依据

全 bitmap 代码区（`0x11B000-0x11C200`）内，对 `geo+0x16`（偏移 22）的写入**只有一处**：

```asm
11b7c0: strh r3,[r5,#22]      ; 其中 r3 = 0（11b7b2 前已置 0）
```

**⇒ 该字段永远是 0，不会取其它值 ⇒ 它不可能是"会变化的像素格式字段"。**

**⇒ "`format == 0` ⇒ bpp = 2 ⇒ RGBA4444" 这个推论缺少实证支撑** ——
它依赖 a7c2（**另一台相机**）的 `gpe-raster.c` 里 `format ? 4 : 2` 这一行，
而 ZV-1F 的 bitmap 代码里没有对应的格式映射表（§10.3 已查证）。

**⇒ bpp / 通道序必须实测，不能由 `geo+0x16` 推出。**

### 14.4 实测建议（一次运行定三项）

```
① 读 [bmp+0x08]、[bmp+0x0C] 的初值
② 读 geo+0x14（stride，像素计）
③ 让 App 画一个纯色后 dump pix 前 64 字节
   · 2 字节重复 → 16bpp（565 / 5551 / 4444 需再看位域）
   · 4 字节重复 → 32bpp（RGBA/BGRA 需再看字节序）
```

## 十五、★★ `geo+0x14` 恒为 0 ⇒ `getRowPixelCount` 是【死 getter】

全 bitmap 代码区（`0x11B000-0x11C200`）字段写入统计：

| 字段 | 写入次数 | 值 |
|---|---:|---|
| `geo+0x14`（偏移 20） | **仅 1 处**（`0x11B7BE`） | **恒为 0** |
| `geo+0x18`（偏移 24） | 2 处（`0x11B7D4`、`0x11B960`） | 对齐后宽 |
| `geo+0x1A`（偏移 26） | 2 处（`0x11B7E4`、`0x11B970`） | 对齐后高 |

而 `getRowPixelCount`（vtable `+0x18`，实现 `0x11B425`）返回的是 **`info + 0x14`** 的地址：

```asm
11b43e: ldr r3,[r0,#4]      ; info
11b440: adds r3,#20         ; ★ info + 0x14
11b442: str r3,[r1,#0]      ; *out = info+0x14
```

### ⇒ 结论

**`GRM_bitmapGetRowPixelCount()` 在任何时候都返回 0**（因为 `geo+0x14` 永远不被赋非零值）。

**⇒ 真正的 stride 在 `geo+0x18`（对齐后宽，像素计）。移植侧的修正正确，而且比他想的更彻底：不是"优先读 +0x18"，而是"那个 getter 本身就是死代码"。**

### ★★ 一条可能与 QEMU 线现象相关的联想

QEMU 线最新结论是"崩点模式更像某个 **size 查询返回 0** 导致的堆/缓冲越界"。

**而这里正好有一个确定会返回 0 的尺寸 getter。**

**⇒ 值得验证的假设**：如果 App 内部某处调用 `GetRowPixelCount` 并用结果做地址算术
（如 `addr = pix + y * rowpx * bpp`），**`rowpx == 0` 会让所有行塌缩到同一地址**，
或使字节数计算为 0 —— 与"size 查询返回 0"的现象吻合。

**验证方法**：在 QEMU 侧 interpose `GRM_bitmapGetRowPixelCount`，
打印调用者的 LR 与返回值。若确实有调用且返回 0，这条线就接上了。

## 十六、🚨🚨 严重更正：`writeLockBits` 是【3 参数】，且返回值是状态码不是指针

### 16.1 完整签名（`0x11BB68` 实证）

```c
int writeLockBits(void *bitmap, int timeout /* r1 */, void **out /* r2 */);
```

**返回 `r9`（即 arg2 被清零后的值）⇒ 成功时返回 0，失败返回错误码。**

### 16.2 `timeout == 0` 分支的实际行为

```asm
11bb92: cbz r5, 11bbde        ; arg1 == 0 → 11bbde
11bbde: mov r8, r5            ; r8 = 0
11bbe0: ldr r3,[r4,#28]       ; [bitmap+0x1C]
11bbe4: subs r3,#1
11bbe6: str r3,[r4,#28]       ; ★ refcount 回退（净零，不保留引用）
11bbe8: bl  0x11BAF4          ; 查询状态
11bbec: cbz r0, 11bc06        ; 失败 → 错误路径
11bbee: ldr r3,[r4,#32]
11bbf0: adds r3,#1
11bbf2: str r3,[r4,#32]       ; [bitmap+0x20]++（另一个计数）
11bbf4: cmp r9,#0
11bbf8: beq 11bc1e            ; ★ arg2 == 0 → 直接收尾（拿不到指针！）
11bbfa: ldr r3,[r4,#8]        ; ★ 像素指针
11bbfc: str r3,[r9]           ; ★★ 写到 *arg2（输出参数）
11bc00: mov r9,#0
11bc04: b   11bc1e

11bc1e: pthread_mutex_unlock(bitmap+0x24)
11bc24: mov r0, r9            ; ★★★ 返回值 = r9 = 0
11bc26: ... pop
```

### 16.3 🚨 两条致命后果

**① 返回值是指针的话会拿到 0**

```c
void *pix = ((void *(*)(void *, int))BMP_WriteLockBits)(bmp, 0);
/* pix == 0（这是"成功"状态码），代码会误判为"没有像素"而 return */
```

**② `BMP_WriteLockBits` 不设置 `r2` ⇒ `*arg2` 写到【垃圾地址】**

```asm
119494 BMP_WriteLockBits:
  1194ae: ldr r3,[r0,#0]      ; vtable
  1194b0: ldr r3,[r3,#76]     ; +0x4C
  1194b2: blx r3              ; r1、r2 均未设置！
```
库里的 `BMP_WriteLockBits` 只传 1 个参数，**`r2` 是调用者的残留值**。
如果恰好非 0，`str r3,[r9]` 会**写到一个随机地址 ⇒ 内存破坏或崩溃**。

### 16.4 ✅ 正确写法（两选一）

**方案 A（推荐，最简单）—— 直接读字段，不调锁**

```c
void *pix = *(void **)((char *)bmp + 0x08);
```

**方案 B —— 按真实 3 参数调用虚函数**

```c
void *pix = NULL;
int rc = ((int (*)(void *, int, void **))VFN(bmp, 0x4C))(bmp, 0, &pix);
if (rc != 0) return;        /* 失败 */
```

**方案 C —— 若一定要用库里的 `BMP_WriteLockBits`（不推荐）**

```c
register void *r2 asm("r2") = &pix;      /* 手工把 r2 设成输出指针 */
int rc = ((int (*)(void *, int))BMP_WriteLockBits)(bmp, 0);
```
**⇒ 脆弱，不建议。**

### 16.5 refcount 的额外结论

`timeout == 0` 路径里 `[bitmap+0x1C]` **先 ++ 后 --（净零）**，所以：

**⇒ 用方案 A 直接读字段，不会造成引用计数失衡（因为根本没进锁路径）。**
**⇒ 之前"必须配对 WriteUnlockBits"的担心，在方案 A 下不成立。**

但 `[bitmap+0x20]` 会 ++（未被回退），**方案 B 需要配对解锁**。

## 十七、像素格式判别：精确预期值表（含对移植侧表格的更正）

### 17.1 四种候选布局的打包公式

```c
RGB565:   ((r>>3)<<11) | ((g>>2)<<5)  | (b>>3)
RGBA5551: ((r>>3)<<11) | ((g>>3)<<6)  | ((b>>3)<<1) | (a>>7)   // R15:11 G10:6 B5:1 A0
ARGB1555: ((a>>7)<<15) | ((r>>3)<<10) | ((g>>3)<<5)  | (b>>3)  // A15 R14:10 G9:5 B4:0
RGBA4444: ((r>>4)<<12) | ((g>>4)<<8)  | ((b>>4)<<4)  | (a>>4)
```

### 17.2 ★ 精确预期值表

| 测试色 (R,G,B,A) | RGB565 | RGBA5551 | ARGB1555 | RGBA4444 |
|---|---:|---:|---:|---:|
| 纯红 (FF,00,00,00) | `0xF800` | `0xF800` | `0x7C00` | `0xF000` |
| **纯绿 (00,FF,00,00)** | **`0x07E0`** | **`0x07C0`** | **`0x03E0`** | **`0x0F00`** |
| 纯蓝 (00,00,FF,00) | `0x001F` | `0x003E` | `0x001F` | `0x00F0` |
| **纯 alpha (00,00,00,FF)** | **`0x0000`** | **`0x0001`** | **`0x8000`** | **`0x000F`** |

### 17.3 ★★ 结论：**只需一个测试色（纯绿）即可唯一确定**

```
测 纯绿 (0, 255, 0, 0)，读 pix 前 2 字节的 16 位值：

  0x07E0  → RGB565
  0x07C0  → RGBA5551   (R15:11 G10:6 B5:1 A0)
  0x03E0  → ARGB1555   (A15 R14:10 G9:5 B4:0)
  0x0F00  → RGBA4444

四种格式的绿值全部不同 ⇒ 一次测试即定案。
```

**若要确认 alpha 位在高位还是低位，再测一次纯 alpha：**
```
  0x0000 → RGB565（无 alpha）
  0x0001 → RGBA5551（alpha 在 bit0）
  0x8000 → ARGB1555（alpha 在 bit15）
  0x000F → RGBA4444
```

### 17.4 ⚠️ 对移植侧表格的更正

移植侧的 5551 行是 `R=0xF800 G=0x03E0 B=0x001F A=0x0001` ——
**这四个值不属于任何单一布局**：

- `R=0xF800` 与 `A=0x0001` 来自 **RGBA5551**（R15:11 / A0）
- `G=0x03E0` 与 `B=0x001F` 来自 **ARGB1555**（G9:5 / B4:0）

**⇒ 那一行混了两种布局。** 按更正后的表使用。

（565 行与 4444 行核实无误。）
