# 前人的 QEMU 成果调查（2026-10-24）

> 问题："A7M3 有人做出来了，a7c2-re 也做出来了，为什么我们做不出来？"
> 方法：GitHub API 系统性检索 + 源码级核对。

---

## 一、调查结论：**没有公开的 A7M3 QEMU 项目**

| 搜索 | 结果 |
|---|---|
| `sony+qemu` | 仅 2 个仓库：`ma1co/OpenMemories-CI`、`cokeenet/zve10-qemu`（我们） |
| `a7m3` | 15 个，**全是无关仓库**（土耳其省市数据、个人主页）或**固件封装分析** |
| `ILCE-7M3` / `cxd90027` | 无 QEMU 项目 |
| A7M3 相关真实项目 | `ayuanjc/sony-a7m3-firmware-analysis`（2026-10-23，9 KB）—— **只做 `Update_ILCE7M3V404.exe` 封装链与 UFU 容器静态分析，不涉及模拟** |

⇒ **A7M3 的 QEMU 模拟没有公开实现**（可能有私有/未开源的工作）。

---

## 二、真正存在的两个 QEMU 成果

### A. `ma1co/qemu` — CXD4108（老 Cyber-shot）

- 作者即 **PMCA-RE / fwtool.py** 的作者 ma1co
- `hw/arm/bionz.c`（47 KB）**已含 5 个平台的机器模型**：
  `cxd4108` / `cxd4115` / `cxd4132` / `cxd90014` / **`cxd90045`**
- **`cxd90045` 就是 ZV-E10 的 SoC！** 但该机器只到**启动级**：
  DDR0/DDR1/SRAM/BootROM、A9MPCORE、bionz_gpio×18、bionz_bootcon(串口)、
  SDHCI+eMMC、UART×4、hwtimer、sio —— **没有图形/显示设备**
- `OpenMemories-CI` 用它跑 DSC-T100 / DSC-G3 的自动化测试，
  **CI 里有真实界面截图**（clock / playback / playback_home）

**它为什么能出界面**：`bionz.c` 的 CXD4108 部分提交历史显示，ma1co 逐件补齐了：
```
framebuffer → blit engine → JPEG decoder → image resize engine
→ IPC(scalar) → sysv timer → ADC/analog bus → external NAND → cam_sync → audio
```
⇒ **显示栈是"一件件设备模型堆出来的"**，不是靠改固件。

### B. `chocotan/a7c2-re` — A7C II（新菜单）

1,303 行图形/显示设备模型：
```
gpe-raster.c 90   gpe-sprite.c 113   dmm-service.c 113
headless-osd.c 326   ramfb-viewer.c 305   sony-lz77.c 72
hdmi-format-service.c 284
```
成果：**原固件菜单可操作**（方向键/确认/MENU/拨盘）、像素与 AP 快照逐像素一致。
**未完成**：拍摄主界面、实时取景、拍照录像。

---

## 三、★ 我们的差距（对照 `oss/patches/qemu/`）

| | 前人 | 我们 |
|---|---|---|
| QEMU 补丁 | `bionz.c` 47 KB（5 平台）+ 图形设备若干 | `bionz.patch`(11 KB) + `sd-cid.patch`(7 KB) |
| 内容 | 启动 + **framebuffer/blit/JPEG/resize/GPE/DMM/OSD/ramfb** | **只有启动**（`cxd90045_build_atags` / `bootloader` / `load_wbi`） |
| 图形设备计数 | 多项 | **gpe/dmm/osd/ramfb/lz77 全部为 0** |

⇒ **我们的固件显示栈永远不会初始化**，因此 `GraphicsEngine` /
`ViewManager` / `WidgetSystem` 的创建路径**根本不会被执行**。
这解释了此前十几轮静态分析里"libObj 内找不到引擎创建点"的困惑 —— **不是固件缺组件，
是我们没给它期待的硬件。**

---

## 四、可直接利用的东西

1. **`ma1co/qemu` 已有 `cxd90045` 机器模型** → 可与我们的 `bionz.patch` 逐行对比，
   补上我们缺的 SoC 侧设备（GPIO 版本、bootcon、sio、hwtimer 等）
2. **`ma1co/qemu` 的 CXD4108 图形部分**（framebuffer/blit/JPEG/resize）可作**方法论模板**
3. **`chocotan/a7c2-re` 的 GPE/DMM/OSD/ramfb 源码就在本机**：
   `a7c2re/report/2026-10-17-qemu-native-camera/runtime/analysis/`

---

## 五、注意事项（不能盲目搬）

- CXD90045（ZV-E10，A9 cortex-a5）与 CXD4108（ARM926/ARM11）、CXD90057（A7C II）**代际不同**
- 图形引擎可能根本不是同一套 IP：`chocotan` 的 GPE 模型是 **A7C II 的**，
  ZV-E10 的显示通路未必同构
- 前人成果均**未达到"完整相机"**：ma1co 停在老 Cyber-shot 的菜单/回放；
  chocotan 明确写"不是完整相机模拟器"

---

## 六、给决策的一句话

**"他们做出来了"= 他们写了 1,000~2,000 行设备模型。我们没有写，所以没出来。**
这不是方法差异，是工作量差异。
