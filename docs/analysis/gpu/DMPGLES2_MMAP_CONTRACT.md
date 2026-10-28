# 真 `dmpgles2` 的 mmap 契约（`grm_gles.ko` 反汇编实证）

> 结论先行：**`/dev/dmpgles2` 最多只映射 128 KB**。QEMU 侧"映射 16/32/256 MB 窗口"
> 与真驱动契约**根本不符**。

## 一、`sugilite_mmap_dev`（`grm_gles.ko` 0x13EC）的分支

```asm
1420: ldmib r1,{r1,r3}        ; r1 = vma->vm_start, r3 = vma->vm_end
1424: rsb  r3, r1, r3         ; r3 = size = end - start
1428: cmp  r3, #512           ; 0x200
142c: cmpne r3, #33554432     ; 0x2000000 (32MB)
1430: movne r5,#0 / moveq r5,#1
143c: bl   __bug_noarg        ; ★ 512B 或 32MB ⇒ 直接 BUG()
1448: cmp  r3, #131072        ; 0x20000 (128KB)
144c: bne  14d8               ; 不是 128KB ⇒ 走另一条路
```

### 接受的两条路径

| size | 行为 |
|---:|---|
| **0x4000（16 KB）** | `remap_pfn_range` 固定 **pfn `0xF1000` = 物理 `0xF1000000`**，长度 16 KB |
| **0x20000（128 KB）** | 从 `vmalloc` 缓冲**逐页**映射：`vmalloc_to_pfn` + `remap_pfn_range`（每页 4096），要求 **`vm_pgoff == 0`** |
| 0x200（512 B） | **`BUG()` —— 内核直接崩，不是返回错误** |
| 0x2000000（32 MB） | **`BUG()`** |
| 其它 | 错误返回 |

### 关键地址解读

- `pfn 0xF1000 → 物理 0xF1000000`：这是 **SoC 外设/寄存器区**（`0xF0xxxxxx`），
  与我们 QEMU 机型里 `CXD90045_DDRC_BASE(i) = 0xf0104000 + i*0x1000` 同一地址空间
  ⇒ **16 KB 那条映射的是 GPU 寄存器文件**，不是显存。
- 128 KB 那条映射的是 **vmalloc 命令环（ring）**。

## 二、`dmm.ko` / `grm_ma.ko` **没有 mmap**

```
dmm.ko     : 未定义 remap_pfn_range / vmalloc_to_pfn；仅导入 vmalloc
             有 dmm_ctrl_AllocUnit{New,Reuse,Share,Rendezvous}Hdlr / dmm_ext_ctrl_AllocExtent
grm_ma.ko  : 无 mmap / remap 符号；有 ma_malloc_impl / mspace_malloc / ma_dmm_open_send_recv
```

⇒ **大块显存由 `dmm.ko`（按 unit/extent 分配）与 `grm_ma.ko`（MA 堆）通过 ioctl 管理，
二者都不向用户态映射任何东西。**

## 三、★ 因此

**真机架构下，App 永远拿不到 16/32/256 MB 的映射窗口。**

QEMU 侧现在做的"把 0x08000000 起一大片 MAP_FIXED 给 App"，
是为了让 stub 驱动跑通而**人为构造**的模型 ——
它能出画面（说明 scanout 链路对），但**与真驱动契约不符**，
所以 App 的后续地址计算会持续越界（窗口末 +6 MB 且随窗口大小移动）。

### 建议的方向

1. **`dmpgles2` 的 fake 严格按真契约**：
   - 收到 `0x4000` ⇒ 映射一块替身页当作寄存器窗口（真机是 `0xF1000000` 外设区）
   - 收到 `0x20000` ⇒ 给 128 KB 环（`vm_pgoff` 必须为 0）
   - 收到 `0x200` / `0x2000000` ⇒ 真机是 `BUG()`，**fake 不该返回成功**
   - 其它尺寸 ⇒ 按真机返回错误，让上层自己处理
2. **大缓冲走 `dmm.ko`**：让 `AllocUnit*` / `AllocExtent` 返回真实可用的地址，
   而不是靠 dmpgles2 的窗口"假装"
3. 若必须维持窗口模型，至少要知道**这是 QEMU 专有构造**，别拿它去对齐真机

## 四、对 "mmapI1/I2/I3" 假设的回应

真驱动要求 **每次 mmap 的 `vm_pgoff == 0`** 且**只给 128 KB**，
⇒ **App 若需要更大区域，必然要多次 mmap**。
⇒ 多次调用是**设计预期**，不是异常。

若 App 期望多次 mmap 得到的区域**相邻**，那 fake 必须：
- 尊重 `MAP_FIXED` 或
- 一次映射一大片后按切片依次返回相邻地址

这能解释"崩点 = 窗口末 + 0x6000E4"：**App 以为紧邻的下一个 128 KB 块已就位。**

---

## 五、补充实证（第三轮反汇编）：硬约束清单

### 1. 环缓冲是**单块** `vmalloc(128KB)`，驱动 init 时只分配一次

```asm
sugilite_dev_init+0xa8:
    1610: mov  r0, #131072      ; 0x20000
    1618: bl   vmalloc           ; ★ 只此一次
```
```asm
sugilite_dev_init:
    15b4: mov  r1, #0xf1000000  ; ★ 寄存器基址【固定】
    15c0: mov  r3, #0x4000      ;   16 KB
          bl   ioremap
```

### 2. 128KB 路径**要求 `vm_pgoff == 0`**

```asm
1450: ldr  r6, [r4, #72]     ; r6 = vma->vm_pgoff
1454: lsls r6, r6, #12       ; r6 = pgoff << 12
1458: bne  1520              ; ★ 非 0 ⇒ 错误返回
```

### 3. 16KB 路径**不检查 pgoff**，无条件 `remap_pfn_range(pfn=0xF1000, 0x4000)`

```asm
14d8: ldr  r3, [r4, #24]
14e0: mov  r2, #0xf1000      ; pfn → 物理 0xF1000000
14e4: orr  r3, r3, #0xe4000  ; vm_flags
14f0: bic  r3, r3, #0x3c     ; vm_page_prot: 清低 6 位（non-cached / 非可执行）
```

### ★★ 结论：**"相邻切片"模型是错的**

由于环是**单块**、且 **pgoff 必须为 0**：

> **每次 128KB mmap 都映射【同一块】128KB 环的不同 VA（别名），
> 不是从 arena 里切出相邻的新块。**

⇒ 因此**不能**用"arena 顺序切片、返回相邻 VA"来实现 —— 那与真驱动语义不同。
⇒ App 也不可能在真机上做 `base + i*0x20000` 跨多块算术（那些块都是同一块环）。

### ★★★ 由此对"窗口末 + 0x6000E4"的重新解释

既然真驱动只给 128KB，那么 App 之所以会访问到"窗口末 + 常量"，
最可能是 **我们的 fake 通过 mmap 尺寸/ioctl 返回值告诉了 App 一个远大于 128KB 的缓冲**，
App 于是按这个尺寸算偏移，走进了真实驱动根本不会有的区域。

⇒ **正确做法不是给更大的窗口，而是把契约改回 128KB**：
- `0x20000` ⇒ 映射那唯一 128KB 环
- `0x4000` ⇒ 映射替身寄存器页
- 其它尺寸（尤其 `0x200` / `0x2000000`）⇒ **按真机拒绝**（真机是 `BUG()`）
- ioctl 上报的尺寸也必须是 128KB

### 那画面从哪来？

真机 App **不直接写显存** —— 它把绘制命令填进 128KB 环，由 GPU 渲染。
⇒ **要用软件渲染就必须在【用户态 GPE 层】实现**，这正是 a7c2 的做法：
`software_gpe` / `GpermFillRect` / `GpermBitBlit` / `Flush` 全部替换为 CPU 绘制。

⇒ **"fake 一个大窗口让 App 直接画"与真机架构不符，长期走不通；
   照 a7c2 在 GPE 层软件绘制才是正路。**
