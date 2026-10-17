# ZV-E10 LED / GPIO 线索汇总

## 已确认的硬件 LED 映射

| 项 | 值 | 证据 |
| --- | --- | --- |
| GPIO 基址 | `0xF2910000` | `platform.h`: `CXD900X0_MACRO_BASE` / `CXD900X0_GPIO_BASE` |
| GPIO 通道步长 | `0x100` | `regs-gpio.h`: `GPIO_REG(ch, reg) = (ch << 8) + GPIO_##reg` |
| WDATA | `+0x40` | `regs-gpio.h` |
| WDATA SET | `+0x44` | `GPIO_SET=0x04` |
| WDATA CLR | `+0x48` | `GPIO_CLR=0x08` |
| error LED 参数 | `error.led=0,0` | 真实 cmdline、两个 `kemco.txt` 副本一致 |
| LED port/bit | port 0, bit 0 | `params.c` 明确格式为 `PORT,BIT` |
| 设置输出 | `0xF2910004`，写 bit 0 | DIR bank SET |
| 点亮（低有效） | `0xF2910048`，写 bit 0 | `error.c`: `WDATA+GPIO_CLR` |
| 熄灭 | `0xF2910044`，写 bit 0 | `error.c`: `WDATA+GPIO_SET` |

`error.c` 支持第二个参数 `error.led_2`，但真实 cmdline 没有设置它；默认值是 `PORT_UNDEF=0xffffffff`，因此当前没有第二个已确认的 LED 映射。

## 最终 payload 使用的 LED 反馈

```text
1 次 1 秒长闪       LED 自检
3 次 80ms 快闪      payload 已跳转并开始执行
等待 1 秒
2 次 500ms 长闪     结果帧标记
N 次 150ms 短闪     结果码
停 3 秒后无限重复
```

```text
1 = 0xA5 成功/幂等
2 = 0xE1 没找到，零写入
3 = 0xE2 MMC 初始化失败
4 = 0xE3 读块失败
5 = 0xE4 写块失败
6 = 0xE5 校验失败
```

## Updater 使用的其它 LED 值（不是 GPIO 地址）

这些值由 updater 脚本传给 `getledval.elf` 或 `uc_portctl.elf -b`，表示错误/闪烁模式，**不是新的 port/bit 映射**：

| 值 | 出现位置/用途 |
| --- | --- |
| `0x42` | `chkfirm.sh` 的固件/列表类型错误；`us_crc32sumprfile.sh` 的 FIRM |
| `0x46` | `startupdate.sh`、`endupdate.sh`、`us_mkfs.sh` 的 keep-backup 错误 |
| `0x49` | `us_crc32sum_backup.sh` 的 UFP backup 错误 |
| `0x51` | `loader_writer.sh` / `us_mkfs.sh` 的插件错误 |
| `0x52` | `loader_writer.sh` 的坏块错误 |
| `0x53` | `loader_writer.sh` 的配置读取错误 |
| `0x81` | updater CRC 检查中的插件错误分支 |
| `0x82` | 多个 CRC/应用/备份检查中的插件或校验错误 |

Updater 的抽象 LED 通道是：

```text
uc_portctl.elf -n 0   主错误/存储 LED
uc_portctl.elf -n 1   条件满足时的电源 LED
```

`common.src` 的 `blink_error()` 会把错误码传给 `-b`，但这些调用没有暴露额外的 GPIO 物理地址。当前能直接用于 bare-metal payload 的唯一硬件映射仍是 `error.led=0,0` 推出的 port 0 bit 0。

## 结论

没有在 ZV-E10 固件或内核中找到第二个已配置的 LED port/bit。继续猜其它 GPIO 会增加风险；最终 payload 只访问有完整证据链的 port 0 bit 0。
