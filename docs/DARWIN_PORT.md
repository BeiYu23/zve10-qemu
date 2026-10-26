# 给 ZV-E10 的 fastboot payload 加 Darwin 支持

> 2026-10-26。目标：让 fastboot 会话不再被 Darwin 电源 MCU 掐断，
> 这样刷 300 MB 的 /usr 才有可靠的退路。

## 一、为什么需要它

```
Darwin（系统电源 MCU）经 SIO ch0 @ 0xF1005000 维护 4 个递减计数器
位置：0x200026FC，归零即杀整机
★ BootROM 上下文里没有任何代码会重置它们
```

**实测后果：会话 5～9 分钟被掐；AP 未能正常启动时更短（我们见过 ~6 秒）。**

**⇒ 如果刷 /usr 失败，相机起不来 ⇒ Darwin 不被重新武装 ⇒ 每次会话只有几秒
  ⇒ 恢复 300 MB 要几十次往返、数小时。**

**喂它的方法：任何一次 SIO 交换（**读也算**）。**

## 二、来源

从 `DavidBuchanan314/ILCE-7M4-RE` 移植：

```
payloads/fastboot/src/darwin.c      281 行
payloads/fastboot/include/darwin.h   64 行
payloads/fastboot/src/fastboot.c     cmd_darwin_state / cmd_darwin_peek
```

该实现转写自 7M4 AP loader 在 `0xFE04DD3C` 的事务原语。

## 三、两机差异（只 3 处）

| 项目 | 7M4 (CXD90057) | ZV-E10 (CXD90045) |
|---|---|---|
| SIO 基址 | `0xF1005000` | **相同** |
| GPIO 基址 | `0xF101D000` | **`0xF2910000`** |
| GPIO 端口步长 | `0x1000` | **`0x100`** |
| **XCS 端口/位** | port 4, bit 2 | **未知（运行时可设）** |

**包格式、CRC-16/CCITT-FALSE、命令 0x12/0x16、VWDT 地址全部沿用。**

## 四、与 7M4 的两点设计差异

### 1. 引脚运行时可设，不在启动时碰 GPIO

7M4 把 XCS 写死，并在 `main()` 里调 `darwin_init()`。
ZV-E10 的引脚未知，所以：

```c
static int g_pin_port = -1, g_pin_bit = -1;   /* -1 = 未配置 */
```

**`darwin_init()` 在未配置时直接返回 ⇒ payload 启动行为与不含 darwin 的版本**完全一致**。**
**只有显式执行 `oem darwin pin:<port>:<bit>` 才会动 GPIO。**

**理由：错误引脚上设成输出并驱动，可能和外部器件冲突。**

### 2. 不加周期性 keepalive

7M4 也没有。喂狗由**主机**定时发 `oem darwin` 完成。

## 五、命令

```
oem darwin                     读 VWDT 计数器与重装字节（读 = 喂狗）
oem darwin pin:<port>:<bit>    设定 XCS 并初始化 SIO
```

**GPIO 寄存器查看直接用现成的 `oem peek:0xf2910000+0x100*N:0x50`。**

## 六、找 XCS 引脚的方法

7M4 的结构给了判据：

```c
#define SIO_PINS  (BIT(0) | BIT(1) | BIT(3))   /* clk/data/第3根 */
#define XCS_BIT   2
```

**⇒ SIO 的三个引脚和 XCS 在**同一个端口**。找出"bit 0/1/3 已被切换成 SIO 功能"的端口，
该端口 bit 2 就是 XCS。**

**步骤（全部只读，除最后一步）：**

```
① oem peek:0xf1005000:0x20            确认 SIO 已初始化
② oem peek:0xf2910000+0x100*N:0x50    N=0..7 逐端口看 DIR(0x04)/FUNC(0x34/0x38)
③ 找 bit0/1/3 符合 SIO 功能的端口      → 该端口 bit2 即候选
④ oem darwin pin:<port>:2             设定
⑤ oem darwin                          能读到 counters 就说明引脚对了
```

## 七、体积问题（关键工程约束）

**串口注入的实测上限（`pico/PROTOCOL.md`）：**
```
8 KB / 2.17 s 通过；10 KB / 2.65 s 返回 0x2004（失败）
```

**首次尝试 10,488 B ⇒ 会被拒绝。逐项压缩：**

| 步骤 | 大小 | 措施 |
|---|---:|---|
| 初版 | 10,488 | darwin.c + libgcc 除法 |
| 去掉除法 | 9,784 | `%` → 掩码/计数器（`DWC3_EVENT_BUFFERS_SIZE=4096` 是 2 的幂） |
| 静态链接 | 8,148 | `-static -no-pie` 去掉 `.interp/.dynsym/.rel.dyn/.dynamic` 共 1,630 B |
| 精简命令 | **7,612** | 去掉 `cmd_darwin_peek` / `cmd_darwin_gpio`（`oem peek` 可替代） |

**最终 on-wire ≈ 7,682 B = 7.5 KB ⇒ 安全。**

## 八、构建适配（本机 Linux）

原 Makefile 是给 macOS + clang + arm-none-eabi 写的。改动：

```make
CC      := arm-linux-gnueabihf-gcc      # 系统带 15.2.0
LD      := arm-linux-gnueabihf-gcc      # 用 gcc 驱动链接
LDPOST  := -lgcc                        # 必须放在目标文件之后
LDFLAGS := -T link.ld -nostdlib -static -no-pie -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--build-id=none
CFLAGS  := -march=armv7-a -mthumb ...   # 去掉 --target=
```

**注意：`-lgcc` 放 `LDPOST` 而不是 `LDFLAGS`，否则静态库顺序错、符号解析不到。**

## 九、产物存档

```
fastboot/payloads/fastboot-6516-orig.bin    6,516 B  无 darwin（已知可用）
  sha256: 0a2092aeac20429cfaa91446ea9b965218844893299f61d8ebea215d73ccd5a6
fastboot/payloads/fastboot-7612-darwin.bin  7,612 B  含 oem darwin
  sha256: 9b4b77950b17a4a5683892ab1152f68514034c6faaec0ff1848ef5f5a98a201a
```

**原版必须在手边 —— 新版若枚举不出来，立刻回灌原版。**

## 十、尚未验证

```
✗ 新版 payload 从未在真机注入过
✗ XCS 引脚尚未确定 ⇒ oem darwin 尚未跑通过一次
✗ 引脚确定后需验证：读到的 counters 是否合理（0xFF=禁用，0x00=未启用）
```

**⇒ 下一步：注入新版 → 跑 GPIO 探测 → 定引脚 → `oem darwin` 验证。**
