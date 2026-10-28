# 屏幕首次出内容之后的两个问题：内存与 ld 崩溃

## 一、★ 内存：**129 MB 是保真的，不要调大**

真机快照实测：

```
MemTotal:  128,796 kB   (≈129 MB)
MemFree:     2,904 kB   (≈2.8 MB)      ← 真机本身就几乎没空闲内存
```

⇒ **QEMU 里 129 MB 与真机完全一致，调大反而失真。**
⇒ 而且真机 MemFree 只有 2.8 MB —— **这套系统本来就是"内存极度紧张"下运行的**。

### 但这也解释了 64 MB 窗口 OOM

真机 `mem=1G@0` 却只有 129 MB MemTotal ⇒ **约 900 MB 是启动时就被 carve-out 掉的保留区**
（GPU / CP / `wbimem=` 那几条）。

**⇒ GPU 窗口（0x08000000 起）几乎必然落在这块"保留物理区"里，不属于 MemTotal。**

**所以正确做法不是加大内存，而是：**

```
fake_gles 初始化时用 memblock_reserve 预留 0x08000000 起的一段（如 16~64 MB），
让这块【不进入内核可用内存】⇒ MemTotal 仍 ≈129 MB（保真）
再 remap_pfn_range 恒等映射给用户态 ⇒ 窗口可用，且不吃 MemFree
```

与真机行为完全同构：**窗口来自预留物理区，不占 MemTotal。**
而现在的 fake_gles 用 `vmalloc` 分配 ⇒ **真吃了 16 MB**，把本就只剩 2.8 MB 的余量吃光。

## 二、ld-2.11.2 崩溃：大概率是**分配失败**，不是"dlopen 返回 NULL 未判空"

### 反汇编事实

```
lr = ld+0x4E9F   → 调用点在 0x4E9A: blx 0x11D40
                   调用前：r0 = fp(地址)  r1 = 0  r2 = size，且前面有 tst r2,#2
pc = ld+0x11D70  → 落在 0x11D40 这个函数内部
```

`0x11D40` 一带是**加载器内部的匿名函数**（`ld` 只有 dynsym），签名形如
`f(addr, 0, size, flags)` —— **与 mmap/段映射高度吻合**。

⇒ **若 mmap 因内存不足返回失败而调用方未判空，就会正好在这里崩。**
这与"MemFree 只剩 2.8 MB + 我们又多吃 16 MB"完全自洽。

### a7c2 有没有"dlopen 未判空"的记录？——**没有**

翻了他们的 `menu-observe.c`：`dlopen` interposer 只做三件事：
1. **记录 `BEGIN` / `LOADED` / `FAILED`**（每个 dlopen 都记）
2. **剥掉 `RTLD_DEEPBIND`**（flags & 8）——
   注释写明：*"RTLD_DEEPBIND hides a library's osal IPC calls from these hooks; strip it."*
3. 对特定库（libObj / libgpe / libInfraKikiLogGenerator）在成功加载后挂钩

**没有对失败返回 NULL 做任何保护** ⇒ 说明他们那条线上没遇到这个问题。

### 两条可直接照搬的经验

1. **用 dlopen interposer 记录全部 LOADED/FAILED** ——
   崩溃前最后一条 `FAILED` 就是可疑对象（你怀疑的 `libInfraSetupFile.so` 可以这样确认）
2. **剥掉 `RTLD_DEEPBIND`** —— 否则那个库会**绕过我们的所有 preload 钩子**
   （包括 fake_gles 相关的路径），可能出现"明明 hook 了却不生效"

## 三、建议的下一步（按顺序）

| # | 动作 | 判据 |
|---|---|---|
| 1 | fake_gles 的窗口改用 **memblock_reserve 预留物理区**（不要 vmalloc） | MemTotal 仍 ≈129 MB，`MemFree` 不因窗口而下降 |
| 2 | 加 dlopen/LOADED/FAILED 日志（照 a7c2），并在 fake dlopen 里剥 `RTLD_DEEPBIND` | 崩溃前最后一条 FAILED 指向具体库 |
| 3 | fake_gles open/mmap 里把窗口 **memset 0** | 花屏→黑屏成立 ⇒ 扫描输出路径正常 |
| 4 | 再追 GPE/GRM 绘制命令是否落到该窗口 | 真内容出现 |

> 花屏本身是**好消息**：说明扫描输出（scanout）在读我们映射的窗口了，
> 只是里面是未初始化数据。**清黑就能确认这条链路是对的。**

## 四、为什么"预留区"这个改法特别重要

真机 `MemTotal 129 MB` 但 `mem=1G` ⇒ **这套系统的内存布局本来就是"少量可用 + 大量保留"**。
如果我们在 QEMU 里把可用内存调大，会出现：
- App 的 `/proc/meminfo` 读到的值与真机不同 ⇒ 行为可能分叉
- 掩盖本应出现的资源约束（真机上 App 必须在 2.8 MB 余量下工作）

⇒ **保真优于宽松。** 需要空间时，去"保留区"要，而不是去"可用内存"要。
