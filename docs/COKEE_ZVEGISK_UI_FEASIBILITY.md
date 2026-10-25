# cokee-zvegisk UI —— 自造 UI 的可行性分析

> 问题：「我们可以造一个 UI 吗？cokee-zvegisk 的 UI」

---

## 一、先给结论

**可以，而且有三条技术路线，难度差别很大。**

| 路线 | 说明 | 风险 | 工作量 |
|---|---|---|---|
| **A. 侧挂 UI** | 保留 `im.elf`，另起进程画到 `/dev/fb0` | 中 | 中 |
| **B. Hook 控件框架** | `LD_PRELOAD` hook `ux::wgtsys::Widget`，插入自己的页面 | 中高（C++ ABI）| 中高 |
| **C. 完全替换 im.elf** | zmagisk `mount --bind` 覆盖，完全自造 | 高 | 极大 |

**推荐顺序：先做 A 的 Phase 1（证明能画出东西），再决定往哪走。**

---

## 二、已核实的技术底座

### ✅ 显示

| 项 | 状态 |
|---|---|
| **`/dev/fb0`** | **存在** —— `libObj.so` 里明确引用 |
| **`/dev/dmpgles2`** | GPU（GLES2）设备节点 |
| **`grm_gles.ko`** | GPU 驱动（在 `/usr/kmod/`）|
| **`grm_ma.ko`** | 2D blitter（memory access）|
| **`/dev/ldec`** | 显示编解码器（面板侧）|
| **`ldec.ko`** | 显示驱动（在 `/root/kmod/`）|

### ✅ 输入

**`libObj.so` 里是完整的控件框架：**
```
infra_input_init
ux::wgtsys::WidgetBase::onKeyEvent(ux::wgtsys::KeyEvent const&)
GEN_Button (setSize / setHighlightPosNotify / setPushAnime / setHoverAnime / calcAlpha)
InputService::setQuadPositionEvent(int,int)
InputService::setResourceTpSettings(int,int,int)     ← TP = 触摸屏
```

**★ 注意：固件里**零** `/dev/input` 引用 ⇒ 按键走 `infra_input` 抽象层，不是原始 evdev。**
**★ 但内核**有** evdev（`evdev_init`、`evdev: no more free evdev devices`）⇒ 底层存在，只是固件没直接用。**

### ⚠️ 已知的不确定性

**zv-e10-re 实测结论：「真机没有 CPU 可见的帧缓存」，`0x20312000` 98.7% 为零，
显示是 descriptor-driven（APL/AIC + XDMAC + HME），面板由私有 `ldec` 驱动。**

**⇒ `/dev/fb0` 很可能是**合成前的离屏缓冲**，写它会被合成器读走，
但**不一定直接显示**。这一点必须实测。**

---

## 三、Phase 1：唯一该先做的事（证明能画）

```
① 读 /dev/fb0 的 fb_var_screeninfo / fb_fix_screeninfo
   ⇒ 分辨率、位深、stride、RGB 排布
② 读当前一帧内容，存成 PPM 看 baseline
③ 写几个明显像素（比如左上角 100×100 纯红）
④ 观察屏幕
   ✓ 有变化 ⇒ /dev/fb0 是活的 ⇒ 路线 A 可行
   ✗ 无变化 ⇒ 说明 fb0 是离屏，需要走 grm/dmpgles2 ⇒ 难度上一个台阶
```

**★ 这一步就够决定整个项目走向。**

---

## 四、路线 A：侧挂 UI（推荐起点）

```
im.elf（原厂，保留 AF/AE/防抖/镜头）   ← 一直跑
zmagisk.so 构造函数据起来的守护进程
  ├── 开 /dev/fb0
  ├── 读 infra_input（hook）或 /dev/input
  ├── 长按某键 1.5 秒 ⇒ 暂停原厂 OSD，画我们的界面
  └── 再按 ⇒ 恢复原厂
```

**优点：原厂功能全保留**
**难点：和 im.elf 抢 `/dev/fb0` 的协调；输入怎么拿到**

### cokee-zvegisk UI 可以有的功能

```
· 画幅选择（16:9 / 3:2 Open Gate / 4:3 / 1:1 / 17:9）
· 变形宽银幕反挤压预览（1.33x / 1.5x / 2.0x）
· 码率滑块（100–200 Mbps）
· 视频辅助：假色 / 波形 / 矢量示波器 / 峰值 / 斑马纹自定义 IRE
· 快门角显示（180° 规则）代替倒数快门
· 电池/存储/温度仪表盘
· 自定义画框线
```

---

## 五、路线 B：Hook 控件框架（最"原生"）

**`LD_PRELOAD` hook `ux::wgtsys::WidgetBase::onKeyEvent` 或 `paint()`，
在 Sony 的菜单体系里插入我们自己的页面。**

**优点：保留原厂导航逻辑，界面看起来是"原生"的**
**难点：C++ name mangling / vtable / ABI 与固件版本强绑定**

**★ 这条路和 zmagisk 完美配合：hook 库就是 `/setting/mode/preload` 指向的 .so。**

---

## 六、路线 C：完全替换 im.elf

**`zmagisk` 用 `mount --bind` 把我们的 `im.elf` 覆盖 `/usr/bin/im.elf`。**

**⇒ 完全控制 UI，但要自己处理：对焦、测光、防抖、镜头通信、
BIONZ X RTOS 通信、录制控制、存储……**

**★ 除非直接链接 `libObj.so` / `libmpr.so` / `av-cam.bin`（BIONZ X RTOS 命令引擎），
否则等于重写整个相机 —— 极大工作量。**

---

## 七、zmagisk 与 UI 的关系（关键优势）

**zmagisk 的构造函数在 `im.elf` 的 `main()` **之前**运行 ⇒ 可以：**

```
① mount --bind 覆盖 /usr/lib/viewUnified2.so     ← 换 UI 引擎
② mount --bind 覆盖 /usr/share/app/global.xdb    ← 换资源索引
③ mount --bind 覆盖 /usr/share/app/LayoutMaster_* ← 换布局
④ 或者干脆起一个自己的 UI 进程
```

**⇒ 换 UI 资源、换控件库、甚至换 im.elf 本身，都可以通过 zmagisk 完成，
且**零 flash 写入**，重启即恢复。**

---

## 八、建议的推进顺序

```
第 1 步（必做）：Phase 1 显示验证
   · 需要 agent 在线（USB 维护模式拉起）
   · 读 fb0 参数 + 写测试像素 + 看屏幕

第 2 步：输入验证
   · 找出按键从哪个设备来（strace im.elf 的 open/read，或 hook infra_input）
   · 能读到键码 ⇒ 交互可行

第 3 步：最小叠加
   · 画一个方块 + 一行文字在 Live View 上
   · 不做菜单，只证明"能叠加"

第 4 步：cokee-zvegisk UI 雏形
   · LVGL 或自绘
   · 长按唤出 / 隐藏

第 5 步：决定是否走路线 B 或 C
```

**★ 每一步失败都能停在原地，且全部零 flash 风险。**
