# `GRM_gperm*` 接口结构（软件 GPE 替换所需）—— 分工表我方部分

> 按与 `session-6d5bc1eb` 的分工：本文负责 **gperm\* / screenGetOnBitmap**，
> `GRM_bitmap*` 与 `GRM_fbrm*` 由对方解。

## 一、两版固件的 GRM 底部接口**完全一致**

| 函数 | ZV-1F libObj | ZV-E10 libObj | 大小 |
|---|---|---|---:|
| `GRM_gpermFillrect` | `0x1178A5` | `0x683F4D` | **314** |
| `GRM_gpermRectblit` | `0x1179DF` | `0x684087` | **824** |
| `GRM_screenGetOnBitmap` | `0x1153D9` | `0x685A21` | **8** |

⇒ 同一套 shim 可同时服务老菜单与新菜单。

## 二、`GRM_gpermFillrect` —— 4 个参数，第 3 个是参数结构体

```c
int GRM_gpermFillrect(void *a, void *b, const FillRectParam *p, uintptr_t c);
```

### `FillRectParam` 布局（从 `[r4,#offset]` 的读取实证）

| 偏移 | 类型 | 含义 | 证据 |
|---:|---|---|---|
| `+0x00` | `void*` | **目标 bitmap** | `ldr r0,[r4,#0]` → `GRM_bitmapGetSubRectangle` |
| `+0x04` | `uint8` | 颜色字节 0 | `ldrb r3,[r4,#4]` → 局部 `[r7+28]` |
| `+0x05` | `uint8` | 颜色字节 1 | `ldrb r3,[r4,#5]` → `[r7+29]` |
| `+0x06` | `uint8` | 颜色字节 2 | `ldrb r3,[r4,#6]` → `[r7+30]` |
| `+0x07` | `uint8` | 颜色字节 3 | `ldrb r3,[r4,#7]` → `[r7+31]` |
| `+0x08` | `int16` | `x0` | `ldrh [r7+48]`（来自 `param[2]`） |
| `+0x0A` | `int16` | `y0` | `ldrh [r7+50]` |
| `+0x0C` | `int16` | `x1` | `ldrh [r7+52]` |
| `+0x0E` | `int16` | `y1` | `ldrh [r7+54]` |
| `+0x10` | `uint32` | **模式枚举**（见下） | `ldr r3,[r4,#16]`；`cmp #1` / `cmp #2` |
| `+0x14` | `uint8` | 标志/alpha | `ldrb r3,[r4,#20]` → `[r7+32]` |

> 颜色 4 字节的通道顺序（RGBA / ARGB / BGRA）需再确认：
> 4 个字节被**原样**复制进局部结构，未做位运算 ⇒ 是内存字节序直传。

### `+0x10` 模式的实证

```asm
117996: ldrb r3,[r4,#20]
11799c: ldr  r3,[r4,#16]
11799e: cmp  r3, #1
1179a0: beq  1179a8        ; mode==1 → 局部[36] = 0
1179a2: cmp  r3, #2
1179a4: bne  1179d4        ; 其它 → 直接返回 0（不绘制！）
1179a8: str  r0,[r7,#36]   ; r0==0
1179ac: mov  r3,#1
1179ae: str  r3,[r7,#36]   ; mode==2 → 局部[36] = 1
```

⇒ **`mode` 只接受 1 或 2；其它值函数直接返回 0，不绘制。**

### 内部流程与最终绘制调用

```
memset(&local, 0, 0x30)                       ; 局部 48 B
GRM_bitmapGetSubRectangle(p->bitmap, &r7+0x4c) ; 取子矩形（宽高在 +4/+6，均为 int16）
sub_117838(&rect, x0, y0)                      ; 坐标规范化
sub_11775C(...)                                ; 矩形求交/裁剪
sub_114C0C(p->bitmap, &r7+0x40)                ; 取某模式值 → tbb 跳转表
... GRM 内部取锁/引用 ...
最终:  bl 0x1156C0                             ; ★ 真正绘制
         r1 = arg1, r2 = arg2, r3 = &local
         stack[0] = arg4(c), stack[4] = local[120], stack[8] = p->bitmap
```

⇒ **软件实现只需接管 `GRM_gpermFillrect` 本身**，按其语义把矩形填进 `p->bitmap` 的像素缓冲；
不需要复刻内部那些中间层。

## 三、`GRM_gpermRectblit` —— 同样 4 参，参数结构含**两个 bitmap**

```c
int GRM_gpermRectblit(void *a, void *b, const RectBlitParam *p, uintptr_t c);
```

实证读取：

```asm
1179e4: mov  r4, r2              ; p
1179ea: mov  sl, r0              ; arg1
1179ec: mov  fp, r1              ; arg2
1179f2: mov  r9, r3              ; arg4
1179e8: movs r2, #156            ; memset 的 156 B 局部结构
1179fc: ldr  r0,[r4,#0]          ; p->[0]  → GRM_bitmapGetSubRectangle  ⇒ 目标 bitmap
117a06: ldr  r0,[r4,#12]         ; p->[0xC] → GRM_bitmapGetSubRectangle ⇒ 源 bitmap
117a28: ldr  r0,[r8],#4          ; 顺序读 p->[0],[4],[8]...
```

| 偏移 | 含义（已确定 / 推断） |
|---:|---|
| `+0x00` | **目标 bitmap**（确定） |
| `+0x0C` | **源 bitmap**（确定） |
| `+0x04`,`+0x08` | 目标/源坐标或裁剪参数（**待细化**） |

> 结构共约 156 B（局部 memset 长度），比 FillRect 的 48 B 大得多 —— 说明 Rectblit 参数更多。

## 四、★ `GRM_screenGetOnBitmap` 在**两版固件里都是 stub**

```asm
00685a20 <GRM_screenGetOnBitmap>:      ; ZV-E10
  685a20: push {r7, lr}
  685a22: movs r0, #0
  685a24: add  r7, sp, #0
  685a26: pop  {r7, pc}                ; ★ 直接 return 0
```

ZV-1F 的 `0x1153D8` **完全相同**（同样是 8 字节、return 0）。

⇒ **它不是"取屏幕位图"的实现，而是一个返回 NULL 的平台桩。**

**含意（对 QEMU 线很重要）**：
1. **不要去实现/期待 `GRM_screenGetOnBitmap` 返回有效位图** —— 真机上它也返回 0
2. App 必须自己处理 NULL（真机 ZV-1F 同样如此）
3. 屏幕位图真正的通路在 `GRM_fbrm*` / `GRM_bitmapCreate*` 那两组（由对方在解）

## 五、给软件 shim 的实现要点（我方结论）

1. interpose 的符号名（两版相同）：
   ```
   GRM_gpermFillrect / GRM_gpermRectblit
   ```
2. 只需按 §二/§三 的语义**直接在 CPU 上填充像素**，无需复刻内部中间层
3. **`mode != 1 && mode != 2` 时按真机行为"返回 0 不绘制"** —— 不要自己发挥
4. `GRM_screenGetOnBitmap` 保持返回 0（与真机一致）
5. 位图内存的真身取决于 `GRM_bitmapCreate*` / `GetPhysicalAddress` / `WriteLockBits`
   —— 那部分需要与 `session-6d5bc1eb` 的结论合并

---

## 六、与 `session-6d5bc1eb` 的 bitmap 结论合并 —— 软件绘制已可实现

### 6.1 两半拼起来

我方（`gperm*` 参数结构）＋ 对方（`BMP_*` 取像素接口）：

```c
/* GRM_gpermFillrect(void *a, void *b, const FillRectParam *p, uintptr_t c) */
BMP0 = *(void**)((char*)p + 0x00);      /* 目标 bitmap */
pix  = BMP_WriteLockBits(BMP0);         /* 直接拿到可写像素缓冲 */
/* rect   = p+0x08 (x0,y0) / p+0x0C (x1,y1)  int16 */
/* color  = p+0x04..0x07  四个字节，原样内存序 */
/* mode   = p+0x10  只接受 1 / 2；其它 ⇒ 直接 return 0 不绘制 */
BMP_WriteUnlockBits(BMP0);
```

⇒ **不需要解完整 bitmap 布局**，只要有 bitmap 指针 + `WriteLockBits` 即可绘制。

### 6.2 对方补充的关键事实

- `BMP_WriteLockBits(bmp)`：`vtable = *(void**)bmp`；`fn = *(void**)(vtable + 0x4C)`；`return fn(bmp)`
- `BMP_WriteUnlockBits(bmp)`：`*(void**)(*(void**)bmp + 0x50)`
- `BMP_Create`：`new(0x80)` = **Bitmap 对象 128 字节**，≥10 个参数
- **7 个 `GRM_fbrm*YUV*` 与 `GRM_screenGetOnBitmap` 全是 `return 0` 空桩**
  ⇒ 整条 YUV 帧缓冲路径在 ZV-1F 上未实现，软件 GPE 不需要实现它们

### 6.3 仍需确认的一项：颜色字节顺序

`GRM_gperm*` 没有任何外部导入者（只有 `libObj` 内部经 PLT 调用），
⇒ **静态找调用点成本高**（PLT 条目间还夹着字面量，布局不规则）。

**⇒ 更可靠的做法：在 shim 里运行时把 `FillRectParam` 的前 0x18 字节原样打印出来。**

已知 UI 常见色（纯黑 `0x00000000`、白、菜单高亮的特定色）几乎立刻能反推出通道顺序：
- 若打印到 `00 00 00 FF` ⇒ 说明第 4 字节是 alpha（RGBA 序）
- 若打印到 `FF 00 00 00` ⇒ ARGB 序

⇒ 一次运行即可确定，比静态逆推更快。

### 6.4 仍需 bitmap 布局提供的信息

虽然 `WriteLockBits` 能拿到指针，但要**正确写入像素**还需要：

| 需要 | 为什么 | 来源 |
|---|---|---|
| **stride / pitch** | 逐行跳转 | bitmap 布局（对方 §4.2） |
| **像素格式**（RGB565 / RGBA5551 / RGBA8888） | 颜色编码 | 同上 |
| **width / height** | 边界裁剪 | 同上（`BMP_Create` 的 arg5/arg6 疑为宽高） |
| WriteLockBits 返回的是**像素基址**还是**含 stride 的结构** | 决定怎么写 | 同上 |

⇒ **对方 §4.2（bitmap 完整布局）现在是软件绘制的唯一阻塞项**，值得优先。

---

## 七、`RectBlitParam` 字段访问全表（老菜单 rectblit 有 7 个调用点，需要它）

### 7.1 从 `GRM_gpermRectblit`（`0x1179DE`，826 B）提取的参数访问

| 偏移 | 访问 | 用途（已确认/推断） |
|---:|---|---|
| **`+0x00`** | `ldr` | **目标 bitmap**（送入 `GRM_bitmapGetSubRectangle`）✅ |
| **`+0x0C`** | `ldr` | **源 bitmap**（同上）✅ |
| `+0x20` | `ldr` | 标量（模式/标志） |
| `+0x24` | `ldrb` | 字节标志 |
| **`+0x26`** | `ldrh` → local `0x4C` | ★ 矩形字段 1 |
| **`+0x28`** | `ldrh` → local `0x4E` | ★ 矩形字段 2 |
| **`+0x2A`** | `ldrh` → local `0x50` | ★ 矩形字段 3 |
| **`+0x2C`** | `ldrh` → local `0x52` | ★ 矩形字段 4 |
| **`+0x2E`** | `ldrh` → local `0x54` | ★ 矩形字段 5 |
| `+0x30` | `ldr` | 标量 |
| **`+0x34..0x37`** | 4×`ldrb` | **颜色 4 字节**（与 FillRectParam 同位置）|
| `+0x3C` | `ldr` → local `0x58` | 标量 u32 |
| `+0x40` | `ldr` → local `0x5C` | 标量 u32 |
| `+0x48` | `ldr` | 标量 |
| `+0x4C` | `ldrb` | 字节标志 |

### 7.2 关键观察：5 个 u16 被**连续**搬进 GPU 参数块

```asm
117c66: ldrh r3,[r4,#38] ; +0x26
117c68: ldrh r1,[r4,#40] ; +0x28
117c6a: ldrh r2,[r4,#42] ; +0x2A
117c70: ldrh r3,[r4,#44] ; +0x2C
117c76: ldrh r1,[r4,#46] ; +0x2E
   → 依次写入 local 0x4C / 0x4E / 0x50 / 0x52 / 0x54
117c7c: ldr  r3,[r4,#60] ; +0x3C → local 0x58
117c8a: ldr  r3,[r4,#64] ; +0x40 → local 0x5C
```

**⇒ 顺序保留 ⇒ 这 5 个 u16 语义上是"同一组矩形参数"。**

**最可能的语义**（尚未实测确认）：
`+0x26`=dst x, `+0x28`=dst y, `+0x2A`=src x, `+0x2C`=src y, `+0x2E`=w（或 h）

**依据**：目标/源 bitmap 的宽高已由 `GetSubRectangle` 分别取得（存 local `+0x08/+0x0A` 与 `+0x20/+0x22`），
所以这 5 个值更可能是**偏移与尺寸**，而非整幅位图尺寸。

### 7.3 ⚠️ 建议：不要静态猜测，用一次运行时日志定案

**做法**（与纯绿定格式同一套路）：

```c
int GRM_gpermRectblit(void *a, void *b, const u8 *p, u64 c)
{
    static int n = 0;
    if (n++ < 3) {
        /* 打印 param 前 0x50 字节 */
        for (int i = 0; i < 0x50; i += 16) log_hex(p + i, 16);
    }
    ...
}
```

**⇒ 配合已知的屏幕几何（640×480 / stride 1280）与位图尺寸，
很容易反推哪几个字段是坐标、哪几个是尺寸。**

**⇒ 静态能确定的只有"哪些字段被读"，语义必须实测。**
