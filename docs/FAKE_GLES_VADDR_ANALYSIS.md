# `0x083360E4` 数据中止的真因：`fake_gles` 返回的地址**在进程里没有映射**

> 崩溃：`address=0x083360E4 trap=0x0e`，`r0 == r4 == 0x083360E4`，`pc=0x13674B96`。

---

## 一、先回答两个疑问

### 1. `libdmpNative` / `libDMPGlslc` **不存在**

全 `ZV-1F /usr/lib` 搜索 `dmpnative|dmpgl|glslc` = **0 命中**。
DMP 的 native 代码**在 `libObj.so` 里**：

```
libObj.so : "dmpNative" 134 处, "InternalCmd" 17 处
appFw.so  : 0 / 0
```

⇒ `0x13xxxxxx` **不是** libdmpNative/libDMPGlslc。

### 2. `0x13xxxxxx` 属于谁：用容量先排除一圈

```
混合镜像 /usr/lib 总容量   = 97 MB
最大单库                  = appFw.so 50 MB
pc 与 lr 相差 0x9900B      = 612 KB ⇒ 若同库，该库 ≥ 612 KB
pc 距 0x04000000          = 246 MB
```

**97 MB 的库总量装不出 246 MB 的偏移** ⇒ `0x13xxxxxx` **不是"顺序排布的第 N 个库"**。

⇒ 更可能是：**mmap 向下分配（top-down）**，或该地址落在**别的映射区**（堆/匿名映射/线程栈）。
**不要靠猜 —— 让 maps dump 直接给出覆盖 pc 的那一行即可。**

> 建议：dump 时**只保留 `r-xp` 段**（段数会大幅减少），或**单独打印覆盖 `pc` 与 `lr` 的那两行**，
> 比把上限从 220 抬到 400 更省事，也不会再被截断。

---

## 二、★ 真正的线索：`0x083360E4` 是"没映射的 RAM 地址"

```
0x083360E4 = 131 MB，落在 DDR0（0x0–0x40000000）内
trap 0x0e = 数据中止 / 翻译错误
r0 == r4 == 0x083360E4   ← 同一个指针，说明它是被"传进来"的
```

⇒ 该地址**在机器层面是 RAM**，但**在这个进程的页表里没有映射** ⇒ 一访问就翻译错误。

### `grm_gles.ko` 是怎么把内存交给用户态的

```
导入符号里明确有： remap_pfn_range   vmalloc   vmalloc_to_page   vmalloc_to_pfn
```

⇒ 真驱动的做法是：**内核 `vmalloc` 一块缓冲，在 `.mmap` 里用 `remap_pfn_range`
把它映射进调用进程的地址空间**，进程拿到的是**自己可用的虚拟地址**。

**而 `fake_gles` 若只是"编一个数"返回（物理地址或常量），进程地址空间里根本没这页**
⇒ App 第一次读写就 `trap 0x0e`。

**⇒ 这就是 `address=0x083360E4` 的成因，与库无关。**

---

## 三、修法（按代价排序）

| # | 做法 | 说明 |
|---|---|---|
| **A（推荐）** | `fake_gles` 实现**真正的 `.mmap`**：`vmalloc`/`alloc_pages` 一块 + `remap_pfn_range` 映射进 vma，ioctl 只返回**句柄/偏移**，地址由 mmap 得到 | 与真驱动同构，App 拿到的一定是已映射地址 |
| **B** | ioctl 里直接 `vmalloc` + 手工在 `current->mm` 里插 vma | 等价但更脏，A 更稳 |
| **C** | 若 App 是"ioctl 拿物理地址 → 自己 mmap `/dev/dmpgles2`" | 那 `fake_gles` 必须提供对应的 `.mmap` 文件操作，否则同样失败 |

**判据**：修好后 `Physical Addr : 200EB400` 之外应再出现类似的 **Logical/虚拟地址**打印，
且 App 对该地址的读写不再 trap。

---

## 四、给 maps dump 的建议

1. **只 dump `r-xp` 段**（可执行映射，数量远少于全部段）
2. 或者**只打印覆盖 `pc` / `lr` 各自所在的那一行**（两行就够）
3. 上限 220→400 只是延后截断，不如过滤

有了覆盖 pc 的那行，就能一眼看出是 appFw.so（50 MB，且崩溃线程正是 THREAD_APP）还是别的库。
