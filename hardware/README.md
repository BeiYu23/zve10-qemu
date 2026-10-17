# 硬件研究：BootROM / UART / LED / Type-C

本目录收集**物理入口**相关的研究笔记。这些内容决定"救砖时能不能把线接对"，
以及"启动桥硬件能否做出来"。

| 文档 | 内容 |
|------|------|
| [`BOOTROM_RESEARCH.md`](BOOTROM_RESEARCH.md) | CXD90045 BootROM / UART 公开资料调研：启动模式、串口协议、可加载性 |
| [`UART_PIN_RESEARCH.md`](UART_PIN_RESEARCH.md) | 主板 UART 物理入口（焊盘位置、电平、波特率） |
| [`LED_RESEARCH.md`](LED_RESEARCH.md) | LED / GPIO 线索（用于判断开机阶段与故障状态） |
| [`A6700_TYPEC_CLUE.md`](A6700_TYPEC_CLUE.md) | A6700 的 Type-C 连接图对本机型的**参考边界**（同族但不通用） |
| [`PROBE_FAIL_TRIAGE.md`](PROBE_FAIL_TRIAGE.md) | 「探测阶段失败」的排查预案（分阶段判据） |
| [`OPERATOR_CARD.md`](OPERATOR_CARD.md) | 救砖真机操作卡（一页纸，现场照着做） |
| [`README_盲操作容错版.md`](README_盲操作容错版.md) | 盲操作容错版最小修复 payload 的说明（无显示、只有串口时的操作法） |

配套的可运行 payload 源码在 [`../rescue/payloads/`](../rescue/payloads/)，
协议细节在 [`../rescue/docs/`](../rescue/docs/)。

---

## 为什么这些"物理"资料很关键

1. **救砖必须走 BootROM 串口/USB 启动** —— 系统本身已经起不来，
   所以物理入口是唯一通道（见 [`../rescue/README.md`](../rescue/README.md)）
2. **需要能判断"设备现在是哪一阶段"** —— LED 与串口输出是最便宜的判据
3. **同族机型可参考但不可照搬** —— A6700 / A7M4 的接线与协议细节可能不同，
   直接套用有风险

> ⚠️ 拆机与焊接有损坏风险；所有操作请先核对型号、备份数据、并在自己能承担的范围内进行。
