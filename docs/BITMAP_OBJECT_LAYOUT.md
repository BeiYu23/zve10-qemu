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
