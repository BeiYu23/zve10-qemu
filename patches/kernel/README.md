# 内核补丁说明

## 三个独立补丁（可 `patch -p1`）

| 文件 | 目标 | 作用 |
|------|------|------|
| `kconfig-pcie-qemu.patch` | `arch/arm/mach-cxd900x0/Kconfig` | 让 `CXD900X0_PCIE2_DME` 依赖 `PCI`（配合打开 `CONFIG_PCI`） |
| `tty-visible-qemu.patch` | `drivers/tty/serial/amba2.c` | **QEMU 下强制 console `txrx_enable=1`** ⇒ 机身 App 的输出可见 |

> 这两个补丁已用 `patch -p1 -R --dry-run` 在本项目使用的源码树上**校验通过**。

## `adrs_conv.c`：请直接使用完整文件

`adrs_conv_qemu.c` 是**完整的、修改后的** `arch/arm/mach-cxd900x0/adrs_conv.c`，
请**直接覆盖**源码树里的同名文件（而不是打补丁）。

原因：这个文件在本项目里被**多次修改**，早期的增量补丁
（`adrs-conv-fallback.patch.orig-session`，保留作历史记录）已不能干净应用。

### 改了什么

Sony 的地址换算函数在"无法判断"时**返回 0**，导致调用方（`udate`/`upm`/`usbg_sen`
等模块）解引用 NULL，引发一串内核 oops。修改后：

- `pass_through()` 命中时返回 `VA_DDR0`
- 兜底分支做**正确换算** `pa + VA_DDR0`（而不是一律返回 `VA_DDR0`）

> ⚠️ 注意：第二种写法是本项目踩过的坑 —— 早期为了"消错"一律返回 `VA_DDR0`，
> 结果让 `backup.ko` 拿到错误地址，害得后面多追了好几轮。
> **任何"为消错而加的兜底"都必须 A/B 对照。**
