# ZV-E10 UART 物理入口研究

> 只记录固件/内核证据，不把 GPIO 编号误当成主板测试点。

## 1. SoC UART0 的确定映射

来源：`arch/arm/mach-cxd900x0/uart.S`。

```c
#define CONSOLE_UART_NO 0
#define UART_PORT       2
#define UART0_TXD       (1 << 9)
#define UART0_RXD       (1 << 8)
```

内核初始化顺序：

```c
GPIO2.PORT: 清 bit 9/8，把 GPIO2[9]/GPIO2[8] 切到 UART0 功能
OCTRL2.OEN: bit 9 置 1（TX 输出）
OCTRL2.IEN: bit 8 置 1（RX 输入）
OCTRL2.PUD1: bit 9/8 置 1（关闭上下拉）
UART0 @ 0xF2000000: 115200 8N1
```

因此 CXD90045 的逻辑信号是：

| 信号 | SoC GPIO | GPIO bank | 方向 | 逻辑串口 |
| --- | --- | --- | --- | --- |
| `DEBUG_UART0_TXD` | GPIO2[9] | `0xF2910200` | 相机输出 | UART0 TX |
| `DEBUG_UART0_RXD` | GPIO2[8] | `0xF2910200` | 相机输入 | UART0 RX |

## 2. 相关寄存器地址

内核定义：

```c
CXD900X0_MACRO_BASE = 0xF2910000
CXD900X0_GPIO_BASE  = 0xF2910000
CXD900X0_OCTRL_BASE = 0xF2912000
GPIO_REG(ch, reg)    = (ch << 8) + GPIO_##reg
CXD900X0_OCTRL(ch)   = OCTRL_BASE + (ch << 8)
```

代入 GPIO2/OCTRL2：

```text
GPIO2 base          = 0xF2910200
GPIO2.PORT          = 0xF2910230       （清 bit 8/9 选择 UART0）
OCTRL2 base         = 0xF2912200
OCTRL2.OEN_SET      = 0xF2912204       （置 bit 9）
OCTRL2.IEN_SET      = 0xF2912214       （置 bit 8）
OCTRL2.PUD1_SET     = 0xF2912244       （置 bit 8/9，关闭上下拉）
UART0               = 0xF2000000
```

这些是 SoC 内部地址，**不等于外部连接器针脚**。要找到物理点，仍需 boardview、拆机走线或维修治具资料。

## 3. loader 侧的交叉证据

`/system/ldr_drv.bin`（ZV-E10 的 `nflasha3` loader）是 ARM Thumb flat binary，包含：

```text
字符串: "uart read error."
字符串: "Loader-Driver ver:"
字符串: "Loader-Driver rev:"
```

反汇编/常量扫描还发现：

```text
文件偏移 0x0DFC 附近：literal 0xF2912000（OCTRL block）
文件偏移 0x12D0 附近：literal 0xF2910000（GPIO block）
```

在 0x0D70 附近的 Thumb helper 会以 `port << 8` 加这些基址，再写 `+0x44/+0x48/+0x54/+0x58` 等 GPIO/OCTRL bank 寄存器；这和 CXD90045 内核的 GPIO/OCTRL 布局一致。它证明 loader 确实在操作同一套 GPIO 控制器，但目前没有从 flat binary 可靠恢复出“板上哪一个测试点”。

## 4. 当前物理结论

```text
A8/SBU1、B8/SBU2 已经用方波探针尝试，均无效果。
054c:0994 是充电 HID，不是已确认的恢复接口。
因此不能继续把 USB-C SBU 当成已证实的 UART RX。
```

下一步应优先向维修人员索取 `SY-1121` boardview/治具点位，目标关键词是：

```text
GPIO2[8] / DEBUG_UART0_RXD / UART0_RXD
GPIO2[9] / DEBUG_UART0_TXD / UART0_TXD
BOOT_SERIAL_IN / BOOT_EXT / BOOT_SIMPLE / BOOT MODE
```

## 5. 电气注意

```text
UART 空闲线约为 1.8 V 域（以实测为准）。
先用高阻示波器/逻辑探头只读确认 TX/RX，不接 RP2040 输出。
确认 RX 后才使用电平转换或开漏桥；不要把 3.3 V 推挽直接接到相机脚。
```
