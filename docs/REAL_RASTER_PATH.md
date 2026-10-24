# ZV-1F 新菜单的真实光栅化路径（推翻 gperm 方案后的正确目标）

> **重大更正**：`GRM_gpermFillrect` / `GRM_gpermRectblit` 在两版菜单里**完全不被调用**。
> 真正的像素原语是 `ux::core::GraphicsImage::{clear, copy}`，由 **appFw 直接导入**。

## 一、`GRM_gperm*` 是死代码（五路证据）

| 检查 | 结果 |
|---|---|
| libObj 内 `GRM_gpermFillrect@plt`(0x3B0AC) 调用点 | **0** |
| libObj 内 `GRM_gpermRectblit@plt`(0x3A7E0) 调用点 | **0** |
| 全 197 个 `/usr/lib/*.so` UND 导入 | **0** |
| 函数体地址作为常量在数据段 | **0** |
| dlsym 字符串 / `im.elf` / `bootin.elf` 引用 | **0** |
| 老菜单 `viewUnified2.so`（14 MB）引用 | **0** |

**对照**：`GRM_bitmapWriteLockBits@plt`(0x3B588) 用同法数出 **4 个调用点** ⇒ 方法有效。

**⇒ a7c2 的 `mpgpe::GpermFillRect` 方案不适用**（A7C II 与 ZV-1F 架构不同）。

## 二、★ 正确的目标：`ux::core::GraphicsImage`

appFw 只导入 3 个：

```
_ZN2ux4core13GraphicsImage5clearEPvRKN6stella6CColorE
_ZN2ux4core13GraphicsImage4copyEPvssssS2_ssssbbb
_ZN2ux4core13GraphicsImage21sys_getInternalObjectEPv
```

**⇒ 以 UND 导入 ⇒ appFw 有自己的 PLT ⇒ LD_PRELOAD 可拦截。**

### 全部导出方法

| 符号 | 地址 | 大小 |
|---|---|---:|
| `clear(void*, CColor const&)` | `0x108C52` | 16 |
| `copy(void*,void*)` | `0x108F9A` | 16 |
| `copy(void*,s,s,void*,s,s,s,s)` | `0x109078` | 50 |
| `copy(void*,s,s,s,s,void*,s,s,s,s,b,b,b)` | `0x108E4D` | 88 |
| `sys_getInternalObject(void*)` | `0x109241` | 52 |

## 三、★★ `clear` 的完整调用链（已解通）

```asm
GraphicsImage::clear(self /*r0*/, col /*r1*/)      @0x108C52
  108c52: movs r2,#1
  108c5e: b.w  0x108BFC                          ; 尾调实现

0x108BFC:
  108c04: mov  r4, r0            ; self
  108c06: mov  r5, r1            ; col
  108c08: ldr  r0, [r0, #24]     ; ★★★ r0 = self->[0x18]  = bitmap 句柄
  108c10: bl   0x1192B4          ; GRM_bitmapGetSubRectangle 转发器（vtable+0x14）
  108c16: ldr  r3, [r7, #4]      ; 返回的子矩形指针
  108c18: ldrh r2, [r3, #0]      ; rect.x
  108c1a: ldrh lr, [r3, #2]      ; rect.y
  108c1e: ldrh r1, [r3, #4]      ; rect.w
  108c20: ldrh r3, [r3, #6]      ; rect.h
  108c44: bl   0x108B90          ; → 构造填充参数
```

```asm
0x108B90:
  108bc0: ldrb r0,[col,#0] → [r7+12]   ; color R
  108bc2: strh rect.x → [r7+4]
  108c04: ...                           ; color G/B/A → [r7+13..15]
  108be6: ldr  r0,[r2,#24]              ; ★ bitmap = img->[0x18]
  108bea: bl   0x112550                 ; ★ 真正的"填色矩形"原语
```

```asm
0x112550(bitmap /*r0*/, &color /*r1*/, &rect /*r2*/, flag /*r3*/, ...)
  11255c: ldrsh r3,[r2,#4]      ; rect.w
  112568: cmp r3,#0 → 早退      ; ★ w <= 0 直接返回
  112570: ldrsh r3,[r2,#6]      ; rect.h
  112576: cmp r3,#0 → 早退      ; ★ h <= 0 直接返回
  112582: memcpy(&local+12, rect, 8)     ; 拷 8 字节矩形
  11258e: memcpy(&local+8, color, 4)     ; 拷 4 字节颜色
```

### 关键新事实

```
img + 0x18  =  bitmap 句柄
rect 结构   =  int16 x, y, w, h
color 结构  =  4 字节 R,G,B,A
```

## 四、★ `clear` 的 CPU 替代（可直接写）

```c
/* img+0x18 = bitmap；bitmap 布局我们已解：
   [bmp+0x08] 像素指针，geo = [bmp+0x04]，geo+0x18 = stride(像素)，
   table[geo+0x1C] = bpp，geo+0x0C = 当前矩形 {x,y,w,h} */
void my_GraphicsImage_clear(void *img, const uint8_t *color)
{
    if (!img || !color) return;
    void *bmp = *(void **)((char *)img + 0x18);
    if (!bmp) return;
    void *geo = *(void **)((char *)bmp + 0x04);
    void *pix = *(void **)((char *)bmp + 0x08);
    if (!geo || !pix) return;

    int format = *(int *)((char *)geo + 0x1C);
    if (format < 1 || format > 9) return;
    static const int bpp_tab[10] = {0,2,4,2,1,2,2,2,4,4};
    int bpp    = bpp_tab[format];
    int stride = *(int16_t *)((char *)geo + 0x18);
    int16_t *r = (int16_t *)((char *)geo + 0x0C);   /* x,y,w,h */
    if (stride <= 0 || r[2] <= 0 || r[3] <= 0) return;

    /* 按 bpp/通道序打包 color，逐行填充 r[0],r[1],r[2],r[3] */
}
```

## 五、仍需解的一项：`copy` 的调用链

`copy`（`0x108E4D`，88 B）是 appFw 用的块拷贝，参数 12 个：
```
copy(dst, dx,dy,dw,dh, src, sx,sy,sw,sh, bool, bool, bool)
```
下一步解它对 `src/dst` 的取值方式（是否同样走 `img+0x18`）以及最终落到哪个原语。

## 六、仍然有效的旧结论

- `GRM_fbrmCheckUpdateFinished → return 0`（appFw 导入，完成信号）
- bitmap 对象/geo 块布局、`geo+0x18` stride、`table[geo+0x1C]` bpp、`[bmp+0x08]` 像素
- `GRM_bitmapCreateFromPhys`：像素物理地址由 App 传入
- `GRM_bitmapWriteLockBits` 的 3 参数 / `arg1==-1` 阻塞语义（**若新路径仍会走到**）
