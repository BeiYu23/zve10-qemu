# 未验证的最后一条显示路径：`/dev/dmpgles2` 的 mmap

> 来源：反汇编 `grm_gles.ko` 的 `sugilite_mmap_dev`（440 字节）
> 以及 zv-e10-re 的 `tests/test_sugilite_ioctl.py` / `research/firmware/sugilite_ioctl.py`

---

## 一、★ 为什么这条路之前被漏掉

**zv-e10-re 验证了 17 个 ioctl，结论是：**
> "So the ioctl is a thin scalar control surface, not a display door."

**⇒ 他们据此认为 sugilite 不是显示门。**

**但他们**没验 mmap** —— 而 `sugilite_mmap_dev` 是独立的函数（440 字节）。**

**★ 而且他们的 `SIDECAR_GUI_PROPOSAL.md` 假设的是 `/dev/fb0`
（后来被 `DISPLAY_SURFACES.md` 推翻："the framebuffer hunt is closed"）。

**⇒ 那份提案是**设想**，不是验证过的方案。**

---

## 二、★★★ `sugilite_mmap_dev` 的实际行为（反汇编）

```asm
sugilite_mmap_dev:
  1424: size = vm_end - vm_start
  1428: cmp  size, #512        (0x200)
  142c: cmpne size, #33554432  (0x2000000 = 32MB)     ← ★★★
  1448: cmp  size, #131072     (0x20000 = 128KB)
  ...
  ldr  r7, [r2, #172]          (0xac)
  ... remap_pfn_range ...      ← 映射物理内存
```

**★ 只接受三个尺寸：**

| 尺寸 | 猜测用途 |
|---|---|
| **512 B** | 寄存器窗口 |
| **128 KB** | 命令/描述符环 |
| **★★★ 32 MB** | **帧缓冲 / 共享内存** ← 与 stream 区域同尺寸 |

**★ 两条映射路径（从错误串看出）：**
```
"sugilite_mmap_dev bar remap fail"      ← PCIe BAR
"sugilite_mmap_dev shmem remap fail"    ← ★ 共享内存
"!!! GPE HW: bar remapped 0x%08x->0x%p size %x"   ← 成功时会打这条日志
```

**★ 用 `remap_pfn_range` —— 把物理页映射到用户空间。**

---

## 三、★★★ 这意味着什么

**如果 mmap 32MB 映射的是 GPU 的帧缓冲/工作区，那么：**
- **读它 ⇒ 能看到真实画面数据**
- **写它 ⇒ 可能直接影响显示**

**⇒ 这才是"CPU 可见的显示面"可能藏的地方。**

**★★★ 而 A7M3（同代 SoC）能玩贪吃蛇，最可能就是走了这类路径。**

---

## 四、待执行的实验（工具已编好）

```
zvegisk_gpumap   （8,932 B，已编译）
  ① open("/dev/dmpgles2", O_RDONLY)
  ② mmap 三个尺寸（512B / 128KB / 32MB），只读
  ③ 报每种的：头 64 字节、非零比例、唯一字节数
  ④ 再试 O_RDWR + 32MB 可写映射
```

**★ 全程只读（最后一步只验证 PROT_WRITE 映射能否建立，不写数据）。**

**判据：**
- **32MB 映射出来如果是高熵数据/有结构 ⇒ 值得继续**
- **全零 ⇒ 这条路也死了**

---

## 五、如果这条路通了，drawing 方案就变了

**不再是"hook 原厂绘制 API"，而是：**
```
mmap /dev/dmpgles2 (32MB)
  → 得到 GPU 的显存映射
  → 直接写像素（需要知道格式/stride）
  → 显示
```

**⇒ 这才是真正意义上的"自己画"。**

**★ 但前提是：① mmap 能成功 ② 映射的确实是显示面 ③ 格式能解出来。**
