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
