# a7c2-re 的显示服务契约（DMM / OSD）与本机可移植性

> 素材：`a7c2re/report/2026-10-17-qemu-native-camera/runtime/analysis/`
> **重要前提修正见 §0。**

---

## §0 ★ 先纠正一个前提：a7c2 **不是**用 QEMU 设备模型

我此前说"他们写了图形设备模型"，方向对但**落点错了**。真实架构是：

```
QEMU（基础机器，qemu-sony-native.patch）
   └─ guest 内：LD_PRELOAD 一个 shim  →  menu-observe.so
        ├─ 拦截 dlsym 本身（改写固件的符号解析）
        ├─ 拦截 osal_reg_msg_queue_cb / osal_snd_msg / osal_snd_sync_msg
        │   / osal_valloc_msg_wait / osal_free_msg / osal_rcv_msg_tmo
        ├─ ★ 拦截 GPE 绘图 API → 软件实现，直接写帧缓冲
        │     mpgpe::GpermFillRect / GpermBitBlit / FillRect / BitBlit
        ├─ 拦截 WidgetSystem::notifyDraw / GraphicsEngine::drawFramebuffer 取帧
        └─ 用 dmm_service_reply() 回答显存分配请求
```

启动方式（`run-menu-probe.sh`）：
```
--observe --initramfs --load-dmm --dmm-service --software-gpe
--firmware-identity --display-info-service --seed-power-on
```

⇒ **"软件 GPE" 是 guest 侧的 API 级拦截，不是 QEMU 里的寄存器级设备模型。**
⇒ 对我们是**好消息**：我们这十几轮已经在写 guest 内探针/钩子，工具链和方法都是现成的；
   写 QEMU 设备模型（1,300 行 C + 寄存器语义）反而是更难的那条。

### ★ ZV-E10 有对应的接口族（本机实测）

| A7 C II（a7c2-re 拦截的） | **ZV-E10（libObj 导出，可同样拦截）** | 大小 |
|---|---|---|
| `mpgpe::GpermFillRect` | **`GRM_gpermFillrect`** @ `0x683F4D` | 314 B |
| `mpgpe::GpermBitBlit` | **`GRM_gpermRectblit`** @ `0x684087` | 824 B |
| bitmap addref/lock/physical | `GRM_bitmapWriteLockBits` / `GRM_bitmapWriteUnlockBits` / `GRM_bitmapGetPhysicalAddress` / `GRM_bitmapGetRowPixelCount` / `GRM_bitmapGetSubRectangle` / `GRM_bitmapCreate` @ `0x684C19` | — |
| framebuffer 获取 | **`GRM_screenGetOnBitmap`** @ `0x685A21` | 8 B |
| （YUV 路径） | `GRM_fbrmInitializeYUV` / `UpdateYUV` / `FillrectYUV` / `RectblitYUV` / `SetScreenPreference` | — |

⇒ **ZV-E10 的图形调用面是 `GRM_*` 一族（17 个导出），完全可以照 a7c2 的做法在 guest 内软件实现。**

---

## §1 DMM 契约（`dmm-service.c/h`，完整可复现）

**形态**：40 字节请求 → 40 字节应答的**消息服务**（由 shim 在 `osal` 消息层回答）。

### 请求布局（`q`，size 必须 ≥ 40）

| 偏移 | 宽 | 字段 | 说明 |
|---|---:|---|---|
| `+0` | u16 | **op** | 仅 `0x20..0x23`，否则返回 `-1024` |
| `+4` | u32 | **magic** | 必须 `0x63a`，否则 raw `-3` |
| `+8` | u32 | **handle** | `(generation << 10) \| index` |
| `+20` | u16 | echo | 原样回填到应答 `+6` |
| `+22` | u16 | **option** | 见下表 |

### 应答布局（`r`）

| 偏移 | 宽 | 内容 |
|---|---:|---|
| `+0` | u16 | `op \| 0x8000` |
| `+2` | u16 | **converted status**（0 = 成功；`-0x201→-1`，`-0x105/-0x205→-5`） |
| `+6` | u16 | echo（= 请求 `+20`） |
| `+8` | u16 | `1` 表示后面带回地址 |
| `+10` | u16 | **raw status** |
| `+12` | u32 | handle（`0x20` 成功时为新句柄，否则回填请求值） |
| `+16` | u32 | magic `0x63a` 回显 |
| `+24` | u32 | CP 地址低 32 位 |
| `+28` | u32 | CP 地址高 32 位 |
| `+32` | u32 | 大小 `0x80000`（512 KiB/槽） |

### 操作语义

| op | 名称 | 语义 |
|---|---|---|
| `0x20` | **ALLOC** | `option∈{0,1}`；找空闲槽（**最多 2 槽**），gen+1，`handle=(gen<<10)\|idx`，清零 0x80000 字节；`option==1` 时带回地址 |
| `0x21` | **RETAIN** | `option∈{0,1}`；`open_count++`（255 封顶）；带回地址 |
| `0x22` | **RELEASE** | `option` 必须 0；`open_count--`；已为 0 时返回 `1` |
| `0x23` | **FREE** | `option∈{0,2}`；`option==2` 先减一次；`open_count` 归零则释放槽；返回 0 或 3 |

### 地址映射（关键）

```
pool 物理基址 = 0x41e000000        （DMM_TEST_PHYSICAL）
每槽大小      = 0x80000             （2 槽共 0x100000）
CP 视图地址   = 物理 - 0x400000000 + index * 0x80000
              ⇒ 槽0 = 0x1e000000，槽1 = 0x1e080000
```

### 错误码

```
返回 -1/-1024/-1021 表示服务级失败；raw 码经 converted 表映射：
  raw 0      → converted 0（成功）
  raw -0x201 → converted -1（池满）
  raw -0x105 / -0x205 → converted -5（索引/句柄非法）
  其它       → 原样
```

### 真机对照清单（你们已有的 `/proc/udm`）

`dmm_info / dmm_extents / dmm_units / dmm_local_que / dmm_global_que` —— 可用于核对
槽位数量、单位大小、队列 ID 是否与上述模型一致。

---

## §2 OSD 契约（`headless-osd.c/h`）

**形态**：不是设备，而是 **UIPC 消息队列消费者**。

### 队列 ID（固定）

```
HOSD_CONTROL     0x8280074d     控制
HOSD_UPDATE      0x8280084d     帧更新
HOSD_META_UPDATE 0x8280084e     元数据更新
HOSD_AP          0x8288076c     AP 侧
HOSD_GUI         0x9088076b     GUI 侧
```

### 启动前置条件（`hosd_claim`）

1. 建一次性 claim 文件（`O_CREAT|O_EXCL`），**失败即拒绝启动**（防止重复消费）
2. `osal_stat_msgq` 必须可用
3. **`/proc/modules` 里不能出现 `liro`** —— 出现即拒绝（避免与真 LIRO 抢队列）
4. 对 3 个队列（CONTROL/UPDATE/META_UPDATE）逐个 `stat`，要求
   `status==0 && pending==0 && clients==0 && state2==0 && state3==0`

### 消息格式

**descriptor（36 字节）** —— 帧描述：

| 偏移 | 宽 | 约束 |
|---|---:|---|
| `+0x10` | u16 | 必须 `0` |
| `+0x14` | u16 | 必须 `640`（宽） |
| `+0x16` | u16 | 必须 `480`（高） |
| `+0x18` | u16 | 矩形 x，≤640 |
| `+0x1a` | u16 | 矩形 y，≤480 |
| `+0x1c` | u16 | 宽，≤640−x |
| `+0x1e` | u16 | 高，≤480−y |

**update（8 字节）**；`pending == 0x1005` 是"保留/重试"状态（需要 `pixel_source_seq` 非 0）。

**像素**：`640 × 480 × 2` = **614,400 字节**（RGBA4444），另有 128 字节头。

### 帧身份链（通过 `/proc/self/mem` 读 guest 结构）

```
base + 0x2ebde0 → mgr
mgr  + 8        → fb
fb   + 0x68     → on        （必须 == fb+0x20）
fb   + 0x70     → off       （必须 == fb+0x40）    ← 二者可互换
on   + 0        → bitmap
on   + 0x10     → id
on   + 0x18     → physical
fb   + 8        → u16[2] = 宽, 高
fb   + 12       → format    （必须 == 1，RGBA4444）
fb   + 0x98     → semaphore id
bitmap 的 vptr 必须 == base + 0x26d210
要求：640×480，physical ∈ [0x410000000, 0x500000000)
```

> ⚠️ `base+0x2ebde0` / `base+0x26d210` 是 **A7 C II 的 libObj 偏移**，
> ZV-E10 上必须重新推导（我们已有 `GRM_screenGetOnBitmap` 等入口可用）。

### 取帧加锁顺序（必须严格照做，否则画面撕裂/死锁）

```
wait_sem(sid)
→ identity 校验
→ addref(bitmap)
→ read_lock(bitmap, 0, &bits)
→ try_raster()
→ memcpy(pixels, bits, 614400)
→ unlock_raster()
→ read_unlock(bitmap)
→ release(bitmap)
→ signal_sem(sid)
```
任一步失败 → **不 ACK**，并把消费者标记为 poisoned（后续不完整状态永不确认）。

---

## §3 给 ZV-E10 的落地建议（按性价比）

| 步 | 做什么 | 判据 |
|---|---|---|
| 1 | guest shim 拦截 `GRM_gpermFillrect` / `GRM_gpermRectblit`，**只记录参数并返回成功**（不画） | 固件是否继续往下走 |
| 2 | 补 `GRM_bitmap*` 的最小可用实现（create / physical / rowpixels / writelock） | 是否开始有人要 bitmap |
| 3 | 回答 DMM 请求（照 §1 的 40 字节协议） | 显存请求是否被接受 |
| 4 | `GRM_screenGetOnBitmap` 拿到帧缓冲地址 → 导出到 ramfb | 画面是否出现 |
| 5 | 真的软件光栅化（fill/blit） | 像素是否正确 |

**核心判据不变**：固件是否**自己**调用 `ViewManager::ctor`（`libObj+0x8A4E28`）。
