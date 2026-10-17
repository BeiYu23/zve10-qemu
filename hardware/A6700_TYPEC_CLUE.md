# A6700 Type-C 连接图对 ZV-E10 的参考边界

> 来源：用户提供的 A6700 当前验证接法图片。以下是对图片的技术拆解，不把 A6700 专属地址直接套到 ZV-E10。

## 图中验证接法

```text
独立充电器 +5V  → 测试板 VBUS
+5V ──约 56kΩ── A5 (CC1)
+5V ──约 56kΩ── B5 (CC2)

Pico GP28 ──470Ω── USB-C A 侧 D−（A7，靠近 A8）
分析仪 CH0 ──────── D− 线
分析仪 CH1 ──────── Pico GP28 侧
充电器 GND、测试板 GND、Pico GND、分析仪 GND 全部共地
```

这和之前只把 GP28 接到 A8/B8 的实验有本质区别：

1. 数据线是 **D−/A7**，不是 SBU1/SBU2；
2. CC1/CC2 通过约 56kΩ 接 VBUS，测试板模拟 USB 主机的 Rp；
3. VBUS 由独立、限流的 5V 充电器提供；
4. Pico 数据线上还有 470Ω 串联保护；
5. 先用分析仪确认 Pico 侧和相机侧确实看到 48 kbaud 波形。

## 可以迁移到 ZV-E10 的部分

当前 `<repo>/aux_fix/rp2040/uart_boot/uart_boot.ino` 已具备：

```text
PIO + DMA
48000 baud
每字节 8 数据位 + 奇校验 + 1 停止位（8O1）
OPEN_DRAIN
GP28 输出
```

源码中的 `build_bits()` 明确构造了 11 bit 帧：起始位、8 个数据位、奇校验位、停止位；`serialboot.pio` 用 PIO 状态机通过改变 GPIO 方向实现开漏传输。

因此，若 ZV-E10 的 USB Boot/Serial 入口与 A6700 同类，**物理层实验应优先试 D−/A7 + 正确 CC/VBUS 条件**。

## 不能直接迁移的部分

| A6700 图中信息 | 对 ZV-E10 的状态 |
| --- | --- |
| A6700 ARM64 裸机 payload | **不能直接用**；ZV-E10 CXD90045 是 ARMv7 |
| unlock `0x2CF25621` | ZV-E10 未证实；当前 payload 仍保留 bypass + A7IV 变体 |
| A6700 的 GPIO17 `dpdm_connect_to` | **不能直接用**；ZV-E10 的 GPIO/MUX 配置不同 |
| 成功后 `18d1:4ee0` fastboot | ZV-E10 当前 payload 不实现 USB fastboot，只直接修 nflasha10 |
| A6700 具体 USB MUX 电平 | ZV-E10 只有 `usb_darwin`/`usb_portMonitor` 驱动线索，尚未确认相同 MUX |

## ZV-E10 固件侧对应线索

ZV-E10 自己的 updater 会加载：

```text
usb_darwin.ko
usb_portMonitor.ko
usbg_sen.ko
usb_extcmd.ko
usbg_storage.ko
```

`usb_portMonitor.ko` 的 `/proc/udm/usb_portMonitor` 接口有两个命令：

```text
1 : Switch USB Selector to Me
2 : Switch USB Selector to BusPowerIC
```

模块内部通过 `gpio_set_data_bit()` 和 backup 参数切换 USB selector，但目前没有证据证明它就是 A6700 图中的 DP/DM MUX，也没有找到 ZV-E10 的 `dpdm_connect_to` 或 GPIO17 定义。

## 如果继续做 D− 实验，必须满足

```text
1. 相机不要同时接 PVE/电脑主机，避免两个 VBUS/主机角色冲突。
2. 使用独立、限流的 5V USB 供电；不要把 5V 接到 Pico 3.3V。
3. CC1/CC2 各使用约 56kΩ Rp 到 VBUS，确认测试板不是只有 5.1kΩ Rd 的设备板。
4. GP28 到 D−/A7 之间串 470Ω；没有电阻时不要照图直接把 3.3V 推挽接入。
5. Pico、相机、分析仪共地。
6. 使用正式 `uart_boot.uf2` + 主机 `flash_aux_fix.py`，不是只发 233µs 探针方波。
7. 成功判据看 ZV-E10 payload 的 LED：1 秒自检 → 3 快闪心跳 → 结果循环；不期待 `18d1:4ee0`。
```

## 当前结论

```text
A8/B8 失败不能否定 USB Boot，因为此前没有按 A6700 图配置 CC Rp/VBUS，也没有试 D−/A7。

下一次最有信息量的实验：
  正确 USB 主机电气条件 + D−/A7 + uart_boot.uf2 + 最终 ARMv7 payload

但这仍是“平台相似性实验”，不是已证实的 ZV-E10 入口；A6700 的 GPIO17、ARM64 payload、fastboot PID 都不能照搬。
```
