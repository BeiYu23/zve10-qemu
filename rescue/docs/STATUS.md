# ZV-E10 救砖 —— 当前状态

> 最后更新：2026-10-17（软件侧已 100% 待命，全部堵在物理引脚）

## 一、一句话状态

```
✅ 软件侧：全部就绪（payload / LED 反馈 / 记录流 / 灌入脚本 / 协议规格 / 排查预案）
✗ 硬件侧：相机上仍未找到可用的 RX/USB 恢复入口
⚠️ 待验证：ZV-E10 的 unlock 偏移是否也是 0xFE02F398
```

## 二、已完成（有证据）

### 故障与修复方案
| 项 | 结论 | 证据 |
| --- | --- | --- |
| 故障原因 | `/etc/ld.so.preload`（20 B）指向被换成 `gotdump.so` 的 `/etc/zve10_agent.so` ⇒ 所有进程开机即崩 | 另一对话自述 + 现象吻合 |
| 修复原理 | 清 `ld.so.preload` 目录项的 size 字段（+28..+31 = 0）⇒ 文件变 0 字节 ⇒ glibc 读到空 preload 列表 | 已在 QEMU 验证 |
| **实际改动量** | **1 个字节** | 12MB 分区字节级 diff = 1 |
| 权威偏移 | 用户区 `0x0AC00000`（块 `0x56000`）· `.img` 内 `0x0B000000` | 分区表 + md5 三重交叉验证 |
| 干净镜像 md5 | `3a75a7c02c80420ec48b763164b02fbc` | — |

### 软件件（全部已验证）
| 文件 | 大小 | 说明 |
| --- | --- | --- |
| `aux_fix/payload.bin` | **6,744 B** | ★ 最终版（扫 32 块 + 零写入保护 + 幂等 + LED 自检/心跳/循环结果） |
| `aux_fix/boot_stream.bin` | **7,018 B** | ★ 第 1 次尝试记录流（bypass，19 条） |
| `aux_fix/records_unlock_a7iv.bin` | **6,996 B** | 第 2 次尝试记录流（`-u 0x2CF25621`，18 条） |
| `aux_fix/flash_aux_fix.py` | — | 精简灌入脚本，支持 `--retry` |
| `aux_fix/gen_records.py` | — | 记录流生成器 |
| `aux_fix/records.h` | — | C 数组（供 MCU 直接用） |
| `aux_fix/LED_RESEARCH.md` | — | LED 地址证据链和其它 LED 值汇总 |

### 三条安全保证（QEMU 实测）
| 场景 | 行为 | 主机侧验证 |
| --- | --- | --- |
| 正常修复 | 找到 → 清 size → 读回验证 | **只有 1 个字节变化** ✓ |
| 重复运行（幂等） | `size already 0 ⇒ nothing to do` | 不再写 ✓ |
| **找不到目标** | **`NOT FOUND ⇒ 零写入`** | **改动字节数 = 0** ✓✓ |

### 最终版 LED 观测

```
GPIO: port 0 bit 0，来自真实 cmdline error.led=0,0
点亮 0xF2910048 · 熄灭 0xF2910044 · 设输出 0xF2910004

1 次 1 秒长闪 = LED 自检
3 次快闪       = payload 已启动
2 次长闪 + N 短闪，3 秒后重复 = 结果
  N=1 成功/幂等，N=2 没找到，N=3 MMC，N=4 读，N=5 写，N=6 校验
```

**最终版 QEMU：**正常路径 1 字节变化 ✓；幂等路径新增改动 0 字节 ✓；干净路径零写入 ✓；三条路径均进入 LED 无限播报出口且无崩溃。

### 协议（已纠正过的易错点）
```
SET_UNLOCK = 0x0000     ← 不是 0xfffe！
MEM_WRITE  = 0x0001     ← 不是 0x0000！
END_MARKER = 0x0123
SET_ENTRY  = 0xFFFF     ← value = 入口地址；只"设置"，跳转在最后 END_MARKER
详见 PROTOCOL.md
```

### 关键地址证据
```
内核 arch/arm/mach-cxd900x0/include/mach/platform.h:
  CXD900X0_ESRAM_BASE = 0xfe000000    CXD900X0_ESRAM_SIZE = 16MB
  VA_BOOTRAM          = IO_ADDRESS(0xFE000000)     ← ★ Sony 自己命名为 BOOTRAM
⇒ CXD90045 的 BootRAM/BootROM 与 a7M4 同址 ⇒ payload 落点 0xFE020000 可沿用
```

## 三、唯一缺口：物理引脚

```
现状:
 · 用户有 USB-C 全针引出板 ✓ + 正点原子 DM40C 示波器 ✓
 · SBU1(A8) / SBU2(B8) 已试 ⇒ **均无效果** ✗
 · 相机 boot 循环时 USB 枚举 `054c:0994`（约 1.7 秒一轮、设备约存活 127ms）
 · `054c:0994` 已确认是 Sony USB Charger 充电 HID（1 字节占位 report），没有已知可利用恢复通道
 · 维修圈 `ff00/ff01` 更可能属于进入 `054c:0336` 后的工厂 Service/EXTCMD 协议，不是 0994 充电身份
 · PVE 容器能看到枚举但无 `/dev/bus/usb` 直通；USB 捕获工具需在 PVE 宿主机运行
 · 用户【不愿拆机】✗
 · 相机没有 Multi 口 ⇒ 无现成 RX/TX/RESET

群里的维修大佬确认:
 ★ "ZV-E10 通过 Type-C + 小板可以救砖，索尼工厂就是用这个口写完整数据，
    工厂程序叫【基板制作】"
 ★ "ff00 = 写 / ff01 = 读，是相机里史诗级最高权限，升级模式不开机也能用"
 ⇒ 证明串行引脚确实在 Type-C 上 ✓ 方向正确 ✓
 ⇒ 但具体哪两根线，大佬还没说 ✗
```

**当前最具体的物理目标：** ZV-E10 内核把 UART0 映射到 GPIO2[9]=TX、GPIO2[8]=RX；对应 SoC GPIO bank 为 `0xF2910200`。这仍需要 `SY-1121` boardview/治具点位才能转成主板测试点。

## 四、待验证项

| 项 | 风险 | 应对 |
| --- | --- | --- |
| ZV-E10 的 unlock 偏移是否 `0xFE02F398` | 中 | 第 1 次试 bypass；失败换 `-u 0x2CF25621` |
| payload 落点是否 `0xFE020000` | 低（有内核 BOOTRAM 证据） | 若错 ⇒ payload 跑飞 ⇒ 但零写入 ⇒ 无害 |
| ZV-E10 是否支持 BootROM 串行启动 | **未知（最大不确定性）** | 用 `uart_boot_probe` 的「13 闪」判定 |

## 五、执行顺序（物理点打通后）

```
① RP2040: GND + GP28→相机 RX（GP27→RESET 可选）
② 烧 `uart_boot.uf2`（48000 baud / OPEN_DRAIN / GP27-29）
③ `python3 flash_aux_fix.py --port COMx --no-reset --retry 10`（无 RESET/循环重启时）
④ 观察 1 秒长闪 + 3 快闪，再数循环结果码
⑤ 断电重启相机
⑥ 正常开机 = 成功 ✓✓ / LED 结果为 2~6 = 按码排查 / 完全无自检闪 = payload 没跑
```

**安全：找不到 ⇒ 零写入 · 重复灌 ⇒ 幂等 · 卡住 ⇒ 拔电池恢复**
**禁止：不要磨主板（等大佬给"哪两根线"的确定答案）**
