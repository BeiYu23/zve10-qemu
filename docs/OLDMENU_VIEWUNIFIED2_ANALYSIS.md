# 老菜单 `viewUnified2.so` 分析：构造函数不调用 osal、不导入 pthread

> 问题来自 QEMU 线：老菜单 `initializeConfig` 卡住，怀疑
> `viewUnified2.so` 的 1768 个 `DT_INIT_ARRAY` 构造函数里有调 `osal_*` 导致阻塞。

## 一、结论：构造函数里 **0** 处 osal 调用

```
.init_array:  1768 项（全部有效，落在 .text 内，0x18EF20..0x8D98D8）
全 .text 的 osal_* 调用点:  86 处
★ 落在构造函数内的:        0 处
```

按符号分布：

| 符号 | 调用点 |
|---|---:|
| `osal_valloc_msg_wait` | 28 |
| `osal_free_msg` | 24 |
| `osal_snd_msg` | 18 |
| `osal_snd_sync_msg` | 10 |
| `osal_wai_sem_tmo` | 4 |
| `osal_rcv_msg_tmo` | 2 |
| `osal_dynamic_id` / `osal_sig_sem` | 0 |

## 二、★ `viewUnified2.so` **不导入** 任何 pthread/sem 原语

```
UND 符号里 pthread_* / sem_* : 0 个
唯一的等待原语: osal_wai_sem_tmo
```

**⇒ 它不可能自己直接进入 futex 等待。**

`osal_wai_sem_tmo` 的 4 个调用点全在**非构造函数**：

```
0x428EB4 → 函数 0x428EA0
0x429920 → 函数 0x429908
0x5D4F72 → 函数 0x5D4F3C
0x5D501E → 函数 0x5D5000
```
这些函数的调用者（8 处）**也全部是非构造函数**。

**⇒ viewUnified2 的静态初始化不会阻塞。**

## 三、⚠️ 需要先回答的前提：**那真的是死锁吗？**

**condvar 等待在正常运行时极常见 —— 空闲 worker 线程就是这样：**

| 情形 | 判断 |
|---|---|
| 主线程继续跑 + worker 在 `cond_wait` | **正常**，不是死锁 |
| **主线程也阻塞** | 才是死锁 |

**⇒ 建议先确认三件事，再决定是否深挖：**
1. **主线程（tid == pid）是否也阻塞？**
2. `lr=0x0409CD8D` 对应 libpthread 的哪个函数（`cond_wait` / `sem_wait` / `join`）？
3. 该线程的创建者是谁（App 自起的 worker ⇒ 空闲等待正常）？

**⇒ 若只有 worker 在等，那 `initializeConfig` 卡住的真正原因在别处，
继续追这个等待会白费一轮。**

## 四、工具教训（本轮再次踩到）

**前导零 bug**：`readelf`/`objdump` 的标签是 `0018ca14`，而指令操作数是 `18ca14`。

```
❌ grep -E "blx\s+0018ca14 "     → 恒 0（看起来像"没有调用"）
✅ grep -E "blx\s+0*18ca14 "     → 真实计数
```

**⇒ 本项目第二次踩同一个坑。规则：所有地址匹配必须用 `0*` 前缀，并先做阳性对照。**

---

## 五、★ 两版固件都**没有**直接渲染到 VDF 缓冲的路径

### 5.1 `GRM_fbrm*` 全是空桩

| 函数 | ZV-E10 | ZV-1F |
|---|---|---|
| `GRM_fbrmInitializeYUV` | **8 B** | **8 B** |
| `GRM_fbrmTerminateYUV` | 8 B | 8 B |
| `GRM_fbrmUpdateYUV` | 8 B | 8 B |
| `GRM_fbrmPrepareUpdateYUV` | 8 B | 8 B |
| `GRM_fbrmFillrectYUV` | 8 B | 8 B |
| `GRM_fbrmRectblitYUV` | 8 B | 8 B |
| `GRM_screenGetOnBitmap` | 8 B（return 0） | 8 B（return 0） |
| `GRM_fbrmSetScreenPreference` | 88 B（VDF 命令分发） | 88 B |

**⇒ 整条 YUV 帧缓冲路径在两版固件里都未实现 ⇒ 只能经 GPU 命令出图。**

### 5.2 ⚠️ 一次差点成立的假阳性（重要教训）

**搜"固件里有没有 `0x20312000` 常量"，ZV-E10 libObj 命中 1 处 —— 差点据此断言"有直接引用"。**

**反汇编该位置：**

```
0xD9A990:  F8AA 7216    str.w r7,[sl,#0x216]
0xD9A994:  2031 2000    ← movs r0,#0x31 ; movs r0,#0
```

**⇒ `0x20312000` 是两条 Thumb 指令的字节巧合，不是数据常量。**

**佐证**：objdump 标注里对 `0xD9A994` 的引用数 = **0**
（阳性对照 `0xD9B10C` 有 2 处引用，证明检索方法有效）。

### 5.3 ⇒ 规则

> **在 ARM 代码段里按 4 字节搜地址/常量，假阳性率很高（指令立即数会撞）。**
> **搜到之后必须验证：① 该值是否在 `.text` 内（字面量池也可能是巧合）**
> **② objdump 是否有对该地址的引用标注。**

### 5.4 ⇒ 对渲染方案的结论

```
L1：preload 直接 mmap(/dev/mem, 显示缓冲物理地址) 写图案  ⇒ 证明显示链路可控
L2：App 的 gperm 落到那块缓冲（CPU 实现）              ⇒ 需先验证 [bmp+0x08] 与显示缓冲同一
L3：完整 UI 交互
```

**⚠️ L2 的前提**：`swgpe_shim.c` 写入 `[bmp+0x08]`，**必须确认这块内存就是显示器扫描的那块**。
**⇒ 检查方法**：`[bmp+0x08]` 的 VA → `/proc/<pid>/maps` → 物理页 → 对照显示缓冲物理地址。
