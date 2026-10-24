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

---

## 七、⚠️ 重大更正：本文 §一 的"gperm 是死代码"结论【错误】

**我的扫描命令有 bug，给出了一个假的零。实际情况是 gperm 有 6 个真实调用点。**

### 7.1 直接事实（`objdump | grep`，可复现）

```bash
objdump -d -M force-thumb --section=.text libObj.so | grep -E "blx\s+3(a7e0|b0ac)\s*<GRM_gperm"
```

```
108e1e:  blx 3a7e0 <GRM_gpermRectblit@plt>
1125d0:  blx 3b0ac <GRM_gpermFillrect@plt>      ← fillrect 确实被调用
117df6:  blx 3a7e0 <GRM_gpermRectblit@plt>
117f0a:  blx 3a7e0 <GRM_gpermRectblit@plt>
126b6e:  blx 3a7e0 <GRM_gpermRectblit@plt>
22a134:  blx 3a7e0 <GRM_gpermRectblit@plt>
```

**fillrect 1 次、rectblit 5 次 ⇒ 不是死代码。**

### 7.2 我的错误根因

```bash
plt=$(objdump -d --section=.plt ... | grep -oE "^[0-9a-f]+ <...@plt>:" | awk '{print $1}')
objdump -d --section=.text ... | grep -cE "blx?\s+$plt "    # ← 坏命令
```

两个 bug 叠加：
1. `$plt` 该是 `3b0ac`，实际含前导零 `0003b0ac`（objdump 操作数没有前导零）
2. 更糟：`$plt` 实际是**空值**（grep 模式没匹配上）⇒ 模式退化成 `blx?\s+ ` ⇒ **恒返回 0**

**⇒ 一个坏掉的命令，产出了一个看起来非常确定的零。**

### 7.3 ★ 两条路径其实收敛于同一点

```
GraphicsImage::clear(img, col)          @0x108C52
  → 0x108BFC: bitmap = img->[0x18]，GetSubRectangle 取 rect
  → 0x108B90 构造 FillRectParam 风格参数
  → 0x112550(bitmap, &color, &rect, flag)
       ↑ 此函数在 0x1125D0 处 blx GRM_gpermFillrect@plt
```

**⇒ `GraphicsImage::clear` 最终调用 `gpermFillrect`。**

**完整真实链路：**
```
ux::gfxeng::*  →  ux::core::GraphicsImage::clear / copy
               →  GRM_gpermFillrect / GRM_gpermRectblit
               →  0x1156C0 构造 GPU 命令（同步，会等 WriteLockBits(-1) 的 condvar）
```

### 7.4 结论修正

| 曾经说 | 实际 |
|---|---|
| gperm 完全不被调用 | ❌ **6 次调用** |
| gperm 是死代码 | ❌ |
| a7c2 的 software_gpe 方案不适用 | ❌ **仍然适用**（同层接口） |
| 应改到 GraphicsImage 层 | ⚠️ 可作补充，但 **gperm 是主路径** |

**⇒ 实现应在 `gperm` 层做（覆盖全），`FillRectParam` 结构全部有效。**

### 7.5 ★ 方法论教训（最重要的一条）

> **"用一个返回零的检查去证明某物不存在"时，必须先用已知存在的东西验证这个检查。**

**零值是最危险的输出 —— 它看起来像确定的结论，但可能只是工具坏了。**

**正确顺序**：先用一个**已知有命中**的样本（如 `WriteLockBits` 的 4 个调用点）验证工具能给出非零，
再用它去测未知项。**工具自检是最便宜的一步，而我跳过了它。**

---

## 八、完整链路（三条线合并，全部经 objdump 直接事实验证）

```
App
 └→ ux::gfxeng::*                              （120 个导入符号）
     └→ ux::core::GraphicsImage::clear / copy   ← appFw UND 导入 ⇒ 可 interpose
         ├→ GRM_gpermRectblit                   @0x108E1E（在 clear 函体内，objdump 标注 clear+0x1ae）
         └→ 0x108BFC → 0x108B90 → 0x112550
              └→ GRM_gpermFillrect              @0x1125D0
                  └→ 0x1156C0 构造 GPU 命令（每次 new(0x78)）
                      └→ WriteLockBits(bmp, -1, NULL) → pthread_cond_wait  ★ 会阻塞
```

### 8.1 gperm 的 6 个调用点（objdump 直接事实）

```
108e1e: blx 3a7e0 <GRM_gpermRectblit@plt>   ← 在 GraphicsImage::clear 体内
1125d0: blx 3b0ac <GRM_gpermFillrect@plt>   ← 在 0x112550 内（clear 的子路径）
117df6: blx 3a7e0 <GRM_gpermRectblit@plt>
117f0a: blx 3a7e0 <GRM_gpermRectblit@plt>
126b6e: blx 3a7e0 <GRM_gpermRectblit@plt>
22a134: blx 3a7e0 <GRM_gpermRectblit@plt>
```

### 8.2 关键结构（三条独立证据交叉验证）

| 项 | 值 | 证据 |
|---|---|---|
| image 的内部 bitmap | `[image + 0x18]` | `0x109260: ldr r0,[r4,#24]`（sys_getInternalObject）+ clear 实现同位置 |
| rect 结构 | `int16 x@+0, y@+2, w@+4, h@+6` | ① gpermFillrect 读 +4/+6；② clear 读 +4/+6；③ geo+0x0C 是 geo+0x04 副本 |
| bitmap 像素指针 | `[bmp + 0x08]` | 两处独立代码路径返回它 |
| bitmap 几何块 | `[bmp + 0x04]` | 构造函数 `str r0,[r4,#4]` |
| stride（像素计） | `geo + 0x18` = `align_up(宽, max(a,8))` | `0x11B7D4` |
| bpp | `table[geo + 0x1C]` = `[_,2,4,2,1,2,2,2,4,4]` | `0x119654` 查表 + rodata @0x29D88E |
| 物理地址 | `geo + 0x24` | getPhysicalAddress |

## 九、最终实现规格（五项，全部有直接事实支撑）

```
① interpose GRM_gpermFillrect / GRM_gpermRectblit     ← 汇聚点，覆盖全部绘制
     · 从 FillRectParam+0x00 / RectBlitParam+0x00 取 bitmap
     · 用 [bmp+0x08] 像素、geo+0x18 stride、table[geo+0x1C] bpp 做 CPU 填充
     · mode ∉ {1,2} ⇒ 不绘制（照抄真机）
② interpose GRM_bitmapWriteLockBits                   ← 中性化 arg1==-1 的 cond_wait
③ GRM_fbrmCheckUpdateFinished → return 0              ← 唯一完成信号
④ GRM_bitmapCreateFromPhys：读 phys → 映射 → 填 [bmp+0x08]
⑤ 首次绘制用纯绿，读 2 字节定格式：
     0x07E0 → RGB565   0x07C0 → RGBA5551
     0x03E0 → ARGB1555 0x0F00 → RGBA4444
```

**备选/交叉验证**：`GraphicsImage::clear` / `copy`（App 层，参数更简单，
但只覆盖 App 直接发起的绘制；libObj 内部路径不覆盖）。

## 十、本文档的错误记录（保留以免重犯）

本文 §一 曾错误断言"gperm 是死代码"，§七 已更正。
根因是一个**未自检的 grep 命令**给出了恒为 0 的结果。

**⚠️ 教训：用"返回零的检查"证明某物不存在时，必须先用已知存在的东西验证该检查。**

---

## 十一、完备性证明：拦 gperm 覆盖 100%

**方法**：查真正的 GPU 命令构造器 `0x1156C0`（每次填充 `new(0x78)` 建命令对象的那个）
在 `.text` 里有多少调用者。

```
1179cc: bl 1156c0          ← 全 .text 仅此 1 处
```

**⇒ `gpermFillrect` 的范围是 `0x1178A4 – 0x1179DE`，`0x1179CC` 落在其内。**

**⇒ 所有 GPU 命令提交都汇聚到 `gpermFillrect` 的 `0x1179CC` 一处
⇒ 拦截 `gpermFillrect` + `gpermRectblit` 覆盖 100%，无绕过路径。**

### `clear` 侧的两条入口（同时到达两个 gperm 函数）

```
GraphicsImage::clear  @0x108C53，真实范围 0x108C53..0x108E4D（506 B）
  ├─ 体内 0x108E1E 直接 blx → GRM_gpermRectblit
  └─ 尾部 → 0x108BFC（独立函数，位于 clear 之前）
              → 0x108B90 → 0x112550 → 0x1125D0 blx → GRM_gpermFillrect
```

## 十二、⚠️ 工具可信度：`st_size` 不可信，`st_value` 与 objdump 标注可信

排查中曾误判函数归属，根因需要精确记录：

| 字段 | 可信 |
|---|---|
| 符号 `st_value`（地址） | ✅ 可信 |
| **符号 `st_size`（大小）** | ⚠️ **可能被截断（如 `clear` 的 size=16，实际 506 B）** |
| **objdump 的 `+0xNNN` 标注** | ✅ **可信**——它按"下一个符号值 − 当前符号值"计算 |

**验证**：objdump 标 `clear@@Base+0x1ae` = `0x108C53 + 0x1AE = 0x108E01`，
与符号值区间法所得范围 `0x108C53..0x108E4D` 一致。

**⇒ 正确表述：不要用 `st_size` 判断函数边界；用符号值区间（或等价地相信 objdump 的 `+0xNNN`）。**

**⚠️ 不要因为 `st_size` 不可信就弃用 objdump 标注** —— 那会增加出错概率。

## 十三、本次协作犯过的错，统一归类

| 错 | 本质 |
|---|---|
| 用 `blx?\s+$plt `（变量为空）扫调用点 | 对**命令的具体形式**判断错 |
| 用 `st_size` 判断函数归属 | 对**字段语义**判断错 |
| 用 `mov.w r1,#-1` 出现 182 次当阻塞调用数 | 用**模式计数**代替调用点计数 |
| "函数体内扫不到阻塞原语 ⇒ 异步" | 用**过滤器结果**代替全路径行为 |
| "0 日志 ⇒ shim 坏" | 用**无法区分假设的观测**当证据 |

**共同点：每次都发生在"我以为我知道这个字段/命令/观测是什么意思"的地方。**

**⇒ 对策：任何"零值"或"不存在"的结论，先做阳性对照（用已知存在的样本验证手段本身）。**

---

## 十四、函数大小判定：三种方法的可信度（含两次互相纠错）

### 14.1 实测结果

| 函数 | 入口 | `st_size` | 下一符号 − 入口 | **序言/尾声配对** | 谁对 |
|---|---|---:|---:|---:|---|
| `GRM_gpermFillrect` | `0x1178A4` | 314 | 314 | — | **`st_size` ✅** |
| `GRM_gpermRectblit` | `0x1179DE` | **824** | 5434 | **824** ✅ | **`st_size` ✅** |

**`gpermRectblit` 的序言/尾声证据：**

```
序言 0x1179DE:  stmdb sp!, {r4,r5,r6,r7,r8,r9,sl,fp,lr}      ← 9 个寄存器
尾声 0x117D12:  ldmia.w sp!, {r4,r5,r6,r7,r8,r9,sl,fp,pc}    ← 9 个，完全对应
真实大小 = (0x117D12 + 4) − 0x1179DE = 0x338 = 824 ✅
```

### 14.2 ⇒ "下一符号 − 入口"方法会**高估**

```
0x1179DE ── gpermRectblit（止于 0x117D12，824 B）
0x117D16 .. 0x118F18 ── 无符号 static 函数区（约 4600 B）
0x118F18 ── BMP_Create
```

**中间夹无符号 static 函数时，该方法严重高估。**

### 14.3 ⇒ 调用点归属需要重新认定

```
0x117DF6  在 gpermRectblit 内？ False  → 属于无名 static 函数
0x117F0A  在 gpermRectblit 内？ False  → 属于无名 static 函数
```

**不影响拦截方案**（6 个调用点全部走 PLT，拦入口全覆盖），
但解释了"6 个里 2 个来自无名区"。

### 14.4 ★ 方法可信度总表（本项目的最终结论）

| 方法 | 可信度 |
|---|---|
| `st_value` | ✅ 可信（**注意带 Thumb 位，比较前须 `& ~1`**） |
| `st_size` | ✅ **本例准确**；但不能预设它一定截断 |
| 下一符号值 − 入口 | ❌ **夹 static 函数时高估** |
| **序言/尾声寄存器配对** | ✅ **ground truth**，不依赖符号表 |
| objdump 的 `+0xNNN` | ✅ 可信（基于 `st_value`） |

### 14.5 方法论三层（本项目最终沉淀）

1. **阳性对照**：任何"零值/不存在"结论，先用已知存在的样本验证手段本身
2. **观测-假设唯一性**：一个观测若不能区分两个假设，它就是无效证据
3. **★ ground truth 优先**：字段推断互相矛盾时，回到代码结构/重定位表本身判断

> 第 3 层是本轮新增 —— 当两个"看起来都合理"的派生方法互相矛盾时，
> 不要选一个信，而要找一个**不依赖符号表**的事实（如序言/尾声配对）。

---

## 十六、★ 函数边界的权威来源：`.ARM.exidx`（本项目最终采用）

### 16.1 方法

```python
# .ARM.exidx: 每项 8 字节；第 1 个字是 prel31 偏移
for i in range(size // 8):
    w = u32(exidx_fo + i*8)
    v = w & 0x7FFFFFFF
    if v & 0x40000000: v -= 0x80000000        # prel31 符号扩展
    fn = (exidx_va + i*8 + v) & 0xFFFFFFFF
# 函数范围 = entries[i] .. entries[i+1]
```

**⇒ ARM EHABI 规定：每个函数由【下一条 exidx 条目的起点】界定。**
**⇒ 编译器生成，不依赖符号表 ⇒ 权威。**

### 16.2 实测（libObj `.ARM.exidx` @ `0x317668`，17601 项）

| 函数 | 入口 | 下一条目 | **exidx 长度** | dynsym |
|---|---|---|---:|---:|
| `GRM_gpermFillrect` | `0x1178A4` | `0x1179DE` | **314** | 314 ✅ |
| `GRM_gpermRectblit` | `0x1179DE` | `0x117D18` | **826** | 824（差 2 字节对齐）✅ |

### 16.3 两个无名调用点的真实归属

```
exidx 条目序列：0x117D18(284) → 0x117E34(264) → 0x117F3C

0x117DF6  属于 0x117D18（284 B）
0x117F0A  属于 0x117E34（264 B）
```

**⇒ 两个不同无名函数各自调 `gpermRectblit@plt` ⇒ 库内部有两处复用路径。**

### 16.4 ★ 判定方法可信度最终表

| 想知道 | **原始来源（权威）** | 不可靠的近似 |
|---|---|---|
| 函数范围 | **`.ARM.exidx`** | 下一 dynsym 符号（夹 static 时高估） |
| 调用关系 | **重定位表 + objdump** | — |
| 数据结构布局 | **反汇编里的 `ldr/str` 偏移** | 命名/类推 |
| 符号可否拦截 | `UND` 导入 + `JUMP_SLOT` + 无 `-Bsymbolic`/`VERDEF` | 外部 import 数量 |
| 某函数是否被调用 | **objdump 里对入口/PLT 桩的 `bl/blx`** | 模式计数 |

**⇒ 共同点：这些都是【编译器/链接器生成】的事实，不是从别处派生的。**

### 16.5 方法论最终沉淀（五层）

1. **阳性对照**：零值/不存在结论，先用已知存在的样本验证手段本身
2. **观测-假设唯一性**：观测不能区分两个假设时即为无效证据
3. **观测点位置**：优先因果链【上游】的观测（下游观测多因一果）
4. **ground truth 优先**：派生方法冲突时，回到原始来源（exidx / 重定位表 / 反汇编偏移）
5. **不要过度泛化**：一个字段"可能不可信"不等于"一定不可信"（`st_size` 本例就是对的）

---

## 十七、⚠️ 调用图更正（用 `.ARM.exidx` 重新判定）

### 17.1 exidx 判定结果（独立解析 + 工具化）

```
0x108C52  GraphicsImage::clear 入口 → 0x108C52..0x108C62  (16 B)   ★纯尾调用
0x108BFC  clear 尾调的函数        → 0x108BFC..0x108C52  (86 B)
0x108C84  无名函数               → 0x108C84..0x108E4C  (456 B)
0x108E1E  gpermRectblit 调用点    → 属于 0x108C84..0x108E4C  ← ★不在 clear 内
0x112550                          → 0x112550..0x1125EC  (156 B)
0x1125D0  gpermFillrect 调用点    → 属于 0x112550..0x1125EC
0x1179DE  GRM_gpermRectblit 入口  → 0x1179DE..0x117D18  (826 B)
0x117DF6  调用点                  → 属于 0x117D18..0x117E34 (284 B)
0x117F0A  调用点                  → 属于 0x117E34..0x117F3C (264 B)
```

### 17.2 ⇒ 正确调用图（**取代 §八**）

```
GraphicsImage::clear (0x108C52, 16 B, 纯尾调用)
  → 0x108BFC (86 B)   读 [img+0x18] → GetSubRectangle
    → 0x108B90 (108 B)
      → 0x112550 (156 B) → GRM_gpermFillrect

0x108C84 (456 B, 无名) → GRM_gpermRectblit     ← 独立入口，不经 clear
```

**⇒ 收回"`clear` 同时调两个 gperm 函数"的说法 —— 它只调 `gpermFillrect`。**
**⇒ 拦截方案不受影响（两者都经 PLT，拦两个入口全覆盖）。**

### 17.3 ⚠️ 我犯的错：**同一个错误方法用了两次**

| 时间 | 说法 | 方法 |
|---|---|---|
| 早先 | `gpermRectblit` 区域 5434 B | 下一 dynsym 符号 ❌ |
| 之后 | `0x108E1E` 在 `clear` 体内 | **同一方法** ❌ |

**更严重的是：我还"验证"了它** —— 用同样算出的区间 `0x108C53..0x108E4D`
看到 `0x108E1E` 落在里面就认为确认了。**那不是验证，是自我印证。**

### 17.4 ★ 方法论第 8 条（本轮新增）

> **用方法 A 得出的结论，不能用方法 A 自己再验证一遍。验证必须是【异源】的。**

**这类错误比"用派生近似当权威"更难自查** —— 因为"我验证过了"这句话本身会带来虚假信心。

### 17.5 方法论 8 条总表

1. 阳性对照：零值/不存在结论，先用已知存在的样本验证手段本身
2. 观测-假设唯一性：观测不能区分两个假设时即为无效证据
3. 观测点位置：优先因果链上游的观测
4. ground truth 优先：派生方法冲突时回到原始来源
5. 不要过度泛化：一个字段"可能不可信"不等于"一定不可信"
6. 预测状态机前，先枚举所有**写**该状态的位置
7. 模型有未验证的初始状态前提时，不得当结论
8. **★ 验证必须异源，不能自我印证**

---

## 十八、★ shim 跨固件可复用性验证（ZV-1F ↔ ZV-E10）

### 18.1 两版 gperm 函数：**语义等价，但非字节相同**

| 函数 | 大小 | 差异字节 | 差异性质 |
|---|---:|---:|---|
| `GRM_gpermFillrect` | 314 / 314 | 36 (11.5%) | **全是 4 字节 `bl`/`blx`（f0/f7 前缀）** |
| `GRM_gpermRectblit` | 826 / 826 | 68 (8.2%) | 同上，17 段 |

**⇒ 差异仅为相对调用目标（两库布局不同），**代码逻辑完全相同**。**

**⚠️ 措辞要准确**：是"语义等价"，**不是"字节相同"** ——
不能用 md5 比对证明等价。验证方法必须是"差异段的指令类型分析"。

### 18.2 ★ bpp 表完全相同

```python
kBppByFormat[10] = [0,2,4,2,1,2,2,2,4,4]   # 16 位元素
ZV-1F  @ 0x29D88E
ZV-E10 @ 0xFD6E7A      ← 字节完全相同
```

**⇒ `swgpe_shim.c` 里的 `kBppByFormat` 在 ZV-E10 上原样有效。**

### 18.3 老菜单的 gperm 同样经 PLT 被调用

```
ZV-E10 libObj:  gpermFillrect 桩 0x108390 → 1 个调用点
                gpermRectblit 桩 0x10C774 → 7 个调用点
（对照 ZV-1F：1 和 5）
```

**⇒ 两条线都可 interpose；shim 按符号名生效，与地址无关。**

### 18.4 ⇒ 结论

**`swgpe_shim.c` 可直接用于老菜单**，唯一需确认项：
`GRM_fbrmCheckUpdateFinished` 在老菜单是否存在、语义是否相同。

---

## 十九、★ 第 10 类错误：**替身/探针的失败返回值会改变被测系统的控制流**

### 19.1 三个实例（同一个坑）

| 桩 | 失败返回值 | 后果 |
|---|---|---|
| 本项目的 `CreateFromPhys` 探针 | 返回 0 | **制造了要诊断的空指针** |
| QEMU 线的 fake_gles | 返回 `-EINVAL` | **制造了启动链断裂（appFw 从未加载）** |
| 老菜单线同类改动 | 同类 | — |

### 19.2 规则

> **替身/探针不是透明观测者 —— 它的失败返回值会改变被测系统的控制流。**
> **任何诊断 stub 的失败路径，必须让上层走【最保守 / 最接近成功】的分支，
> 而不是错误分支。** 即使数据是假的，也比让系统走进从未设计过的错误处理好。

### 19.3 为什么要单独列为一类

**前 9 条大多关于"如何读别人的东西"；这一条关于"如何写自己的东西"。**
**⇒ 自产的 bug 最容易被忽略，因为我们会本能信任自己刚写的代码。**

---

## 二十、⚠️ 一条工具边界（补记）

**`.ARM.exidx` 在 `libObj.so` 上可靠，但在 `appFw.so` 上粒度很粗：**

```
appFw: 0x1FBFB40 → exidx 给出的"函数" 0x1F4A9F8..0x2017D08 = 840,464 字节
```

**840 KB 的"函数"不合理 ⇒ appFw 的 exidx 覆盖不均。**
**⇒ 在 appFw 上判断地址归属，exidx 只能作参考，需配合序言/尾声或其它方法。**
