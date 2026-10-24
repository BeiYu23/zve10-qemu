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
