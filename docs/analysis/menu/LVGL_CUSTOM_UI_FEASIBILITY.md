# LVGL 自绘 UI 可行性 —— 完整技术路径

> 问题：「能不能开机使用 LVGL 自绘制 UI 然后使用触摸屏/按键读取？」

---

## 一、结论

**可行，但不能走常规路线。**

| 路线 | 可行性 | 原因 |
|---|---|---|
| `/dev/fb0` + LVGL fbdev 驱动 | ❌ | **fb0 是 ENXIO 空壳，内核没注册 framebuffer** |
| `/dev/dmpgles2` + LVGL GLES2 后端 | ❌ | **固件里零 GLES/EGL 库**，GPU 是私有 SUGI 接口 |
| **GRM 位图 API + LVGL 自定义 flush** | ✅ | **API 全部导出，可 dlopen 调用** |

---

## 二、★★★ 关键发现：Sony 的位图 API 是**导出符号**

**`libObj.so` 的 dyn-syms 里（可直接 `dlopen`+`dlsym`，无需 hook）：**

```
GRM_bitmapCreate            0x684c19  120 B
GRM_bitmapWriteLockBits     0x684ead   16 B    ← 拿 CPU 可写地址
GRM_bitmapWriteUnlockBits   0x684ebd   16 B    ← 提交改动
GRM_bitmapGetPhysicalAddress 0x684e6d  16 B
GRM_bitmapGetRowPixelCount  0x684fd7   16 B
GRM_bitmapGetSubRectangle   0x684ddd   28 B
GRM_bitmapCreateComposite   0x684d95   12 B
GRM_gpermRectblit           0x684087  824 B    ← 位图→屏幕
GRM_gpermFillrect           0x683f4d  314 B    ← 矩形填充
GRM_screenGetOnBitmap       0x685a21    8 B    ← 当前屏幕位图
GRM_fbrm*                   0x685xxx   8-88 B ← framebuffer manager (YUV)
BMP_Create                  0x69ca09  220 B
```

**⇒ 这是一套完整的「分配位图 → 写像素 → blit 到屏」接口。**

---

## 三、LVGL 的自定义显示驱动映射

**LVGL 支持自定义 `flush_cb`（把 LVGL 渲染结果推到屏幕）。
这套 API 正好对得上：**

```c
/* ① 初始化：创建一个和屏幕一样大的位图 */
GRM_bitmapCreate(&screen_bmp, width, height, BMP_PIXELFORMAT_*, ...);

/* ② LVGL 刷新回调 */
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* 拿到位图的可写地址 */
    void *dst = GRM_bitmapWriteLockBits(screen_bmp, area);

    /* 把 LVGL 渲染的这块区域拷进去（格式转换如果需要） */
    memcpy(dst, px_map, ...);

    /* 提交 + blit 到屏幕 */
    GRM_bitmapWriteUnlockBits(screen_bmp);
    GRM_gpermRectblit(target_bmp, screen_bmp, area);

    lv_display_flush_ready(disp);
}
```

**★★ `GRM_screenGetOnBitmap` 只有 8 字节 ⇒ 很可能是个 thunk，直接返回当前屏幕位图
⇒ 我们的 blit 目标就是它。**

---

## 四、输入怎么拿

| 方式 | 状态 |
|---|---|
| `/dev/input/event*` | ❌ **ENODEV 空壳**（`/proc/bus/input/devices` 为空）|
| `input.ko` | ✅ 由 `init` 加载，走 Sony 的 `udif` 框架（`udif_driver_register`）|
| **`APPFWINFRA_INPUT_GETEVENT`** | ✅ libObj 内部 API —— **要 hook 或找导出符号** |
| `/dev/gpio` | ✅ 存在（777），按键可能直连 GPIO |
| `/dev/uinput` | ✅ 存在 —— **可以注入虚拟按键** |

**⇒ 输入需要进一步逆向：先看 `APPFWINFRA_INPUT_GETEVENT` 是否导出，否则 hook `infra_input`。

**★ 摸清之后，LVGL 的 `lv_indev` 读按键队列即可。**

### 触摸屏

**`libObj.so` 里有 `InputService::setResourceTpSettings(int,int,int)`（TP = Touch Panel）
⇒ ZV-E10 有触摸屏，走的是 `InputService`，同样需要 hook。**

---

## 五、★★★ 但有个更简单的中间方案

**在实现完整 LVGL 之前，先用 `GRM_gpermFillrect` 证明"我们能在屏幕上画东西"：**

```c
/* 一个最小的验证：画一个红方块 */
GRM_bitmap *screen = GRM_screenGetOnBitmap();
GRM_gpermFillrect(screen, x, y, w, h, 0xF800 /* RGB565 红 */);
```

**⇒ 如果这个能显示，整条路就通了；LVGL 只是把"画红方块"换成"画整个 UI"。**

**★ 而且这个验证可以做成一个**极小的 .so**（几十行），风险比 LVGL 小得多。**

---

## 六、实现路线

```
Phase 0（关键验证，最小风险）：
   · 写一个 ~50 行的 .so
   · 构造函数里调 GRM_screenGetOnBitmap + GRM_gpermFillrect 画个方块
   · 用 zmagisk 机制 preload 进 im.elf
   · 看屏幕有没有红方块
   ★ 成功 ⇒ 整条路通；失败 ⇒ 需要解参数结构

Phase 1（摸参数结构）：
   · 反汇编 GRM_gpermFillrect / GRM_bitmapCreate 确认参数
   · 用我们的 libgrm.c 里的成果对照

Phase 2（输入）：
   · 摸 APPFWINFRA_INPUT_GETEVENT 或 infra_input
   · 拿到键码/触摸坐标

Phase 3（LVGL 集成）：
   · 交叉编译 LVGL 到 ARMv7
   · 写 flush_cb（用 GRM bitmap API）
   · 写 indev 驱动
   · zmagisk 起一个独立进程

Phase 4（产品化）：
   · cokee-zvegisk UI 菜单
   · 开机选择 + 崩溃回退
```

---

## 七、风险与限制

| 风险 | 说明 |
|---|---|
| **和 im.elf 抢屏幕** | 原厂 UI 也在画，会互相覆盖。需要在原厂帧之后注入，或暂停原厂绘制 |
| **GRM 参数结构未完全解出** | 需要反汇编 + 对照 `libgrm.c` |
| **LVGL 体积** | 交叉编译后约 200-500 KB，放 `/setting` 或 `/log` 没问题 |
| **输入路径未打通** | 目前只知道走 Sony 的 `infra_input` |
| **性能未知** | SUGI GPU + 2D blitter 的能力未测 |

---

## 八、诚实结论

**技术上可行，但**不是"开机跑 LVGL"那么简单**：**

**① 没有 framebuffer ⇒ 必须走 Sony 的 GRM 位图 API（已验证导出）**
**② 没有标准 GL ⇒ 不能用 GPU 加速路径**
**③ 输入要走 Sony 的 `infra_input` ⇒ 需要 hook**

**⇒ 现实的第一小步：50 行画个方块。成功了这个项目就可以推进，
失败了我们就知道卡在参数结构上，回去继续逆向。**

---

## 九、★★★ 实测更新（Phase 2 结果）：高层 API 是空桩

**已在真机上跑过两个探针（崩溃隔离，agent 无恙）：**

### 探针 1：符号解析

```
dlopen(/usr/lib/libObj.so) = 0x22078      ✓ 成功
GRM_gpermFillrect          → 0x4891f4d    ✓
GRM_gpermRectblit          → 0x4892087    ✓
GRM_bitmapCreate           → 0x4892c19    ✓
（七个符号全部解析成功，子进程 EXIT_CODE=0）
```

### 探针 2：实际调用了五个无参函数

```
GRM_screenGetOnBitmap     = 0x00   ← 空桩
GRM_fbrmInitializeYUV     = 0x00   ← 空桩
GRM_fbrmPrepareUpdateYUV  = 0x00   ← 空桩
GRM_fbrmUpdateYUV         = 0x00   ← 空桩
GRM_fbrmTerminateYUV      = 0x00   ← 空桩
```

**⇒ 整个 FBRM / 屏幕接口在 ZV-E10 上被编译成空函数。**

**★★★ 关键推论：**

| 结论 | 说明 |
|---|---|
| **独立进程自绘 UI** | ❌ **不可行** —— 拿不到屏幕位图 |
| **`GRM_gpermFillrect/Rectblit`** | ✅ 是真的，但**需要有效位图** |
| **`GRM_bitmapCreate`** | ✅ 真的，但**11+ 个参数**，只有 im.elf 内部会调 |
| **唯一可行路径** | **在 im.elf 内部拦截（preload / zmagisk）** |

### 下一步的关键动作

**Hook `GRM_bitmapCreate`，记录 im.elf 调用它时的**真实参数** ——
这样 11 个参数的语义就从实际使用中反推出来了，而不必靠猜。**

**★ 有了参数语义，我们就能自己创建位图，然后 `GRM_gpermRectblit` 上去。
⇒ LVGL 的 flush_cb 才真正可实现。**
