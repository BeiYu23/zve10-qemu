# ZV-E10 (CXD90045) BootROM 隐藏串口启动 — 协议与载荷开发笔记

> 本目录只包含**原创**的脚本、载荷源码与文档。
> **不含**任何 Sony 固件、BootROM dump、loader 或分区镜像；需要者请自行从自己的设备导出。

## 这是什么

Sony ZV-E10（CXD90045, ARMv7-Cortex-A9, Linux 3.0.27）的 BootROM 存在一个隐藏的
**串口引导模式**：SoC 复位后，BootROM 会从 `MISC_MODEREG` 的 `BOOT_SERIAL_IN` 引脚
按位采样串行数据，把它当作"引导记录"解析，最终可以把任意代码写进 SRAM 并跳转执行。

本仓库记录了逆向出的**物理层 / 记录层 / 校验层**细节，以及在我们自己的机器上
实测成功的部分（payload 可执行、SDHCI 命令链可跑通）。

## 硬件接线（实测可用）

| 信号 | 接到 |
|---|---|
| `BOOT_SERIAL_IN` | 相机 USB-C 的 **D− (A7)**，经 470Ω |
| `XRESET_REQ` | 相机 USB-C 的 **D+ (A6)**，经 470Ω |
| CC1 / CC2 | 各 **5.1kΩ 下拉到 GND**（分开，不短接、不上拉）|
| VBUS | 外部稳定 5V |
| GND | 共地 |

发送端：RP2040（Pico），PIO 位翻转 + DMA，`open-drain` 输出。

## 串口参数

```
48000 baud, 8O1（8 数据位 + 奇校验 + 1 停止位）
起始位 + 8 数据位 + 奇校验位 + 停止位 = 11 bit/字节
ROM 侧用 GPIO 位采样（时序表步长 = 84 tick ≈ 20.83 µs）
```

## 记录格式

```python
body   = struct.pack("<IHI", 10 + len(payload), rtype, value) + payload
record = body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
```

| type | value 含义 | 行为 |
|---|---|---|
| `0x0000` | 解锁值 V | 设置解锁值；成功后置位内部 flag0 |
| `0xFFFF` | 入口地址 | 写入 `[0xFE00500C]` 并置位 flag1；ROM 随后 `bx` 到该地址 |
| 其他 | 目标地址 | 把 payload 字节**直接写入**该地址（无地址合法性检查）|

发送顺序（A7C/7M4 家族实测有效）：

```
SET_UNLOCK(V) → MEM_WRITE ... → SET_ENTRY(entry) → SET_UNLOCK(V)
```

**必须整串连续发送**（记录之间不留间隙），否则 ROM 会以 `0x2004`（记录不完整）/
`0x3001`（接收超时）结束本次串口启动。

## 解锁 token

从相机 boot0（EXBL 头）偏移 `0x200` 处读取 8 字节：

```
V = 0x8CB102DC    C = 0xA86DA25E
crc32(V || C) == 0xFFFFFFFF
```

`SET_UNLOCK` 的 value 必须等于 V，否则 ROM 报 `0x2002`。

## 现象：ROM 的错误灯码

ROM 通过 GPIO0 bit0 闪灯报告错误：

```
长闪次数 = ((code >> 12) & 0xF) - 1
短闪次数 = code & 0xFFF
```

| 长 | 短 | code | 含义 |
|---|---|---|---|
| 0 | 13 | `0x100D` | 奇偶校验错（收到了数据但解不对）|
| 1 | 2 | `0x2002` | token 不匹配 |
| 1 | 3 | `0x2003` | CRC 校验失败 |
| 1 | 4 | `0x2004` | 记录不完整 / 同步失败 / 字节超时 / `len > 0x1000` |
| 1 | 5 | `0x2005` | 跳转发生且入口返回 |
| 2 | 1 | `0x3001` | 接收超时 |
| 0 | 2 | `0x1002` | 串口启动未被接受，退回正常启动后失败 |

## 已实测确认

* 记录层 + 解锁 + `SET_ENTRY` 跳转 **可用**：我们写入的 payload 会执行
  （以"1 秒长闪 + 3 次快闪 + 循环签名"证实）。
* payload 在 SRAM 中可自由使用（我们放在 `0xFE020000` 和 `0xFE021000` 均可）。
* SDHCI（eMMC 控制器）基址 `0xF0304000`，初始化顺序、命令标志位：
  * `CMD0`：命令寄存器写 `0x0000`（**bit4 必须为 0**，否则控制器挂死）
  * `CMD1`：`0x02`（R1），需重试直到响应 bit31 置位
  * `CMD2`：`0x05`（R2 + CRC）
  * `CMD3`：`0x0E`（R6 + CRC + 索引）
  * `CMD7`：`0x0E`/`0x0F`（R1/R1b）
  * `CMD17`：`0x1E`（R1 + CRC + 索引 + 数据），需 `BLKSIZE=512`、`BLOCK_COUNT=1`、`TRANSFER_MODE=0x0012`
  * 时钟：先清 `CLKCTL` bit2 → 设 400 kHz 分频 → 轮询 bit1 稳定 → 再置 bit2
  * **不要**访问厂商寄存器 `0xF0304204` / `0xF03044FC`（触发异常）
* 一次串口启动能接收的载荷总量约 **1.2 KB**（1212 B 可、1264 B 不可，含分条发送也受限）。
  用 `-Os` 编译可把"初始化 + 读块"压到 ~950 B。

## 未解决

* `CMD17` 的数据阶段：命令可发、数据就绪位不可靠，读取会挂死（疑似看门狗复位）。
* 因此尚未实现"读 eMMC 根目录 → 定位文件 → 写回"的完整修复链。
* eMMC 使用 Sony 私有 `mmc_mmcp` 驱动（内核日志 `eMMC: HS200 mode`），
  完整的读块序列可从设备自带的明文 loader（EXBL，加载 `0xFE040000`，入口 `0xFE047000`）中提取。

## 目录

```
scripts/   注入脚本（Python，串口驱动 RP2040 桥 + 记录生成）
payload/   ARM payload 源码（start.S + main.c + linker.ld + Makefile）
rp2040/    RP2040 桥固件（PIO 位翻转 + DMA + 命令协议）
docs/      详细逆向笔记（寄存器、函数地址、时序、错误码）
```

## 合规

仅用于恢复**自己拥有**的设备。所有固件数据需自行从设备导出，本目录不提供任何 Sony 二进制。
