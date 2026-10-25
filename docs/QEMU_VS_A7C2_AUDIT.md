# 我们的 QEMU 环境 vs a7c2-re —— 对照审计

> 参照：`chocotan/a7c2-re` 的 `qemu-sony-native.patch`（11 文件 / 30 KB）
> 本文基于我们 `patches/qemu/bionz.patch` + 基础 `hw/arm/bionz.c`（1333 行）

## 一、结论先说

**我们的机器侧比 a7c2 更完整；真实的短板在【guest 侧 shim 层】。**

两边走了相反的投资路线：

| | a7c2 | 我们 |
|---|---|---|
| 机器模型 | 标准 `virt` + 4 个最小设备 | **自定义 `bionz`（完整 SoC 模型）** |
| 外设 | 用 stub 顶掉 | **真实建模** |
| Guest shim | **`menu-observe.so`：3159 行 + 8 个模块** | 零散、逐步补 |

## 二、机器侧逐项对比

| 能力 | a7c2 | 我们 | 判定 |
|---|---|---|---|
| 启动协议 | DTB + `/chosen/bootargs` | **ATAGS + bootloader 跳板（MACH_TYPE 3497）** | ✅ 各自正确 |
| 内存 | `virt` 默认 + 预留 | **DDR0 1G + DDR1 256M + WBI 保留区（对齐真机 KEMCO.TXT）** | ✅ 我们更准 |
| 定时器 | **自建 `sony-hwtimer`** | **✅ 已有 `CXD90045_HWTIMER` ×4 @0xf2403000（IRQ 153–156）** | ✅ 我们更完整 |
| 中断控制 | GIC 别名到 Sony 真实地址 | **IRQ 矩阵 32×16 + GPIO IRQ ×16 + ARM_CPU_IRQ** | ✅ 架构不同，各自成立 |
| L2 缓存 | 未建模 | **PL310 @0xf8002000** | ✅ 我们有 |
| DMA | 未建模 | **DMA330** | ✅ 我们有 |
| eMMC | 未建模 | **emmc0** | ✅ 我们有 |
| 时钟/复位 | 未建模 | **SUGI clk0/pll0/rst0–3** | ✅ 我们有 |
| 加密加速 | `sony-spacc-stub` | **❌ 无** | ⚠️ 缺口 |
| CXD 总线 | `sony-cxd-bus-stub` | **❌ 无** | ⚠️ 缺口 |
| 显示 | ramfb + fw_cfg | **`bionz_vip` + `bionz_streamfb` + vsync IRQ** | ✅ 各自成立 |
| **输入注入** | **`sony-ui-input`（GTK → guest）** | **❌ 完全没有** | ❌ **明确缺口** |

## 三、三个缺口

### ① ❌ 输入注入（最明确的缺口）

`hw/arm/bionz.c` 里没有任何 keyboard / input / QemuInputHandler。

**影响**：即使菜单渲染出来，也**无法操作**。
**参照**：a7c2 的 `hw/input/sony-ui-input.c` + `ui/gtk.c` 改造 —— 主机按键 → chardev → guest 输入队列。
**优先级**：中（当前卡点不需要，但"能显示"之后立刻需要）。

### ② ⚠️ SPACC（加密加速）桩

a7c2 有 `sony_spacc_stub.c`。我们没有。
**影响**：若固件在启动路径访问 SPACC 寄存器而未建模，可能卡住或走异常分支。
**优先级**：低—中，**建议先用 QEMU 的未实现访问日志确认是否真的被访问**。

### ③ ⚠️ CXD 总线桩

a7c2 有 `sony_cxd_bus_stub.c`。我们没有。
**影响**：同类，取决于固件是否访问。
**优先级**：低，同样先确认是否被访问。

## 四、战略含义

**a7c2 把全部精力投在 guest 侧：`menu-observe.so` 一个 .so 里塞进 8 个模块**
（主框架 + CAPID/SA/HDMI/DMM 四个服务 + GPE 光栅/精灵 + LZ77 + headless OSD）。

**我们把精力投在机器侧：1333 行的 SoC 模型。**

**⇒ 对当前卡点（`grm_init` 的 worker 回调不返回）而言，机器侧完全不是瓶颈 ——
瓶颈在 guest 侧的"服务替身"是否按原契约应答。**

**⇒ 后续投资应该倾斜到 guest shim，而不是继续加设备模型。**

## 五、建议动作

```
① 【即做】回调链修复（cb 补 signal q+0x68，持 q+0x50）—— 见 GRM_REQUEST_QUEUE.md
② 【即做】L2：GRM_bitmapCreate + gperm CPU 实现（移植 a7c2 的 gpe-raster/gpe-sprite 为 32 位）
③ 【先确认再决定】查 QEMU 未实现访问日志里有没有 spacc / cxd_bus 地址
④ 【显示通了之后】补输入注入（照 a7c2 的 sony-ui-input 思路）
⑤ 【不要做】为了对齐 a7c2 而把 bionz 改成 virt —— 我们的模型是优势，不是负担
```
