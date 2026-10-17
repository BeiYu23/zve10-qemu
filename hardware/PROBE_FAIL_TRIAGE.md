# 「探测阶段失败」排查预案

> 用于 `uart_boot_probe`（方波探测）没有出现「13 次短闪」时的逐级排查。
> 目标：**用最短时间判断是"引脚不对"、"时序不对"、还是"ZV-E10 根本没这条路"**

---

## 第 0 步：先确认"到底该闪几次"（★ 重要情报，已从固件挖出）

### A. 更新器（updater）的错误闪烁码 —— 已从固件完整挖出

**来源：`updater.img_unpacked/config/common.src`**
```sh
blink_error() {
    echo "[FIRMUP NG] : Error $1"
    uc_portctl.elf -b $1 -n 0 &     # ★ -b <code> = 按错误码闪烁
    ...
    uc_portctl.elf -b $1 -n 1 &     # POWER LED 同样
}
```

**`uc_portctl.elf` 的用法（从二进制字符串挖出）：**
```
-b [x] -n xx : Set Blink as [x:HEX] [xx:LED NO. SET]     ← ★ x 是【十六进制位图】
-i [x] -n xx : Set Blink as [x:Hz]
-p [x] -n xx : Set Pulse Flash as [x:Hex]
-c 1 -n xx   : LED ON      -c 0 -n xx : LED OFF
```

**已知错误码（`common.src` 原文）：**
| 常量 | 值 | 含义 |
| --- | --- | --- |
| `ERROR_INVALID_FIRMWARE` | **41** | 固件无效 |
| `ERROR_MPR_ERASE` | **45** | MPR 擦除失败 |
| `ERROR_POWER_IC_TYPE` | **47** | 电源 IC 类型异常 |
| `ERROR_FSYS_FORMAT` | **48** | 文件系统格式化失败 |
| `ERROR_UINCOMP` | **54** | 更新未完成 |
| `ERROR_NAND_MOUNT` | **72** | 挂载失败 |
| `ERROR_NAND_UNMOUNT` | **73** | 卸载失败 |
| `ERROR_SYSTEM` | **87** | 系统错误 |
| `FATAL_ERROR` | **99** | 致命 |

**⚠️ 注意：`-b` 的 x 是【十六进制位图】，所以 `-b 41` 会按 `0x41` 的 bit 图闪烁。**
（具体"闪几次"取决于 uc_portctl 内部如何把 bit 图变成脉冲 —— 未反汇编确认）

### B. BootROM 的「13 次短闪」= 另一个机制

**来源：上游 `uart_boot_probe.ino` 注释原文：**
```
This program makes the camera fail to boot, with a "13 short flashes" error code.
This corresponds to error code 0x100D, which indicates a parity error.
This demonstrates that we're reaching the serial bootloader code.
```
**⇒ ★「13 次短闪」是【BootROM 自己】的错误显示（不是 updater 的 blink_error）**
**⇒ 含义：BootROM 收到串行启动前导码后，后续数据奇偶校验失败 ⇒ 报 0x100D**
**⇒ 这正是我们**想要**的结果：它证明「打进了 BootROM 的串行启动代码」✓**

**⚠️ 但我们不能假设 ZV-E10 的闪法一定和 a7M4 一样（可能错误码/闪法不同）**
**⇒ 所以：**让用户记录每次尝试的准确闪法（几次快闪 / 几次慢闪 / 有无长闪）**

---

## 第 1 步：方波完全没效果（相机正常开机 / 毫无反应）

```
可能原因              如何确认                              怎么办
────────────────────────────────────────────────────────────────────────────
① RX 引脚不对          示波器看该脚有没有【周期性方波】         换脚继续扫
   （最常见 ✗）        （RP2040 输出 233us 方波，示波器应能看到）
                       若看不到 ⇒ 转接板/线/GND 有问题 ✗
② GND 没接好           量 GND 与相机外壳是否通（0Ω）           重新接 GND
③ 相机没上电           USB 有没有供电？电池有没有装？          确认供电
④ 方波时序不对         试不同频率: 200us / 233us / 250us / 300us   改 ino 里的 delayMicroseconds
   （BootROM 可能      （上传的 233us 对应 ~2145bps）
     对前导码有要求）
⑤ ZV-E10 确实没有      试完所有候选脚都不行                     接受现实 ⇒ 转第 3 步
   BootROM 串行启动
```

**★ 示波器配置建议（老菜单已给）：Auto 触发 · 1V/div · 100µs/div**

---

## 第 2 步：相机行为异常（黑屏 / 别的闪法）

```
⇒ 可能打到了【别的信号脚】（CC / D+ / D- 等）
⇒ 立即拔电池（无害，可恢复）
⇒ 换脚继续
★ 但也要记录：如果闪法很规律 ⇒ 可能是别的错误码 ⇒ 有价值的情报
```

---

## 第 3 步：13 闪出现了，但灌 payload 后重启仍循环

```
可能原因                如何判断                          怎么办
────────────────────────────────────────────────────────────────────────────
① unlock 偏移不对       ⚠️ 无法直接判断（与"payload 跑飞"现象相同）  第 2 次: 用 -u 0x2CF25621 版
   （最可能 ✗）                                                   仍不行 ⇒ 第 3 步再试其它偏移
② payload 落点不对      同上                                    若怀疑 ⇒ 用 PIC 版 payload（代价大）
③ payload 跑飞          同上                                    同上
④ payload 跑了但没找到   同上                                    读 SRAM 结果码（需 TX）或分析 eMMC 布局
⑤ 修复成功但相机还有    重启后能进菜单 ⇒ 已成功 ✓               不用管
   别的问题
```

### ★ 如何区分"unlock 失败" vs "payload 跑了但没修好"

**理论上：payload 会在 SRAM `0xFE02F000` 写 `[magic "ZV10"][code]`**
```
0xA5=成功  0xE1=没找到  0xE2=MMC失败  0xE3=读失败  0xE4=写失败  0xE5=校验失败
```

**⚠️ 但 BootROM 串行启动协议是【只写】的 —— 没有读命令 ✗**
**⇒ 所以要看 SRAM 结果码，必须有【相机 TX】或【别的读通道】**

### ★★ 一个可行的替代观测路径（重要发现）

**我从固件里挖到：串行启动模式下控制台会被打开！**
```
文件: initrd/usr/bin/UdtrMain.sh
    #console control
    test -f /root/IAMLDUDWRT       && ud_cons.elf on
    test -f /setting/updater/debug && ud_cons.elf on
⇒ IAMLDUDWRT = "I AM LD UD WRT" = 串行启动标记
⇒ ★ 串行启动模式下 console 打开 ⇒ 如果我们能找到【相机 TX】，就能看到输出 ✓
⇒ 这与"正常启动 console 关闭"（AMBAUART_CONSOLE_ENABLE_DEFAULT=0）形成对比 ✓
```

**⇒ 所以：如果能同时找到 RX + TX（或者 TX 与 RX 是同一组差分/复用），观测问题就解决了。**

### ★★★ 关于"可观察的物理副作用"（最终版）

**已经找到并验证 ZV-E10 的错误指示 LED：**

```
真实 cmdline: error.led=0,0          ⇒ port=0, bit=0
GPIO 基址:    0xF2910000
设输出:       0xF2910004
LED 亮:       0xF2910048（WDATA+CLR，低有效）
LED 灭:       0xF2910044（WDATA+SET）
```

**最终 payload 的 LED 反馈：**

```
1 次 1 秒长闪       = LED 自检
3 次 80ms 快闪      = payload 已启动（已通过 unlock + SET_ENTRY）
等待 1 秒
2 次 500ms 长闪     = 结果帧标记
N 次 150ms 短闪     = 结果码
停 3 秒后无限重复   = 不会因为错过一次而丢失结果
```

| 短闪次数 | 结果 | 说明 |
| --- | --- | --- |
| 1 | `0xA5` | 成功/幂等 |
| 2 | `0xE1` | 没找到，零写入 |
| 3 | `0xE2` | MMC 初始化失败 |
| 4 | `0xE3` | 读块失败 |
| 5 | `0xE4` | 写块失败 |
| 6 | `0xE5` | 校验失败，尝试恢复 |

**QEMU 已验证：**正常路径改 1 字节、干净路径零写入、幂等路径零新增改动；三条路径都到达循环播报出口。

| 方案 | 当前状态 | 结论 |
| --- | --- | --- |
| **LED 自检/心跳/结果循环** | ✅ **已实现并通过 QEMU** | 真机上数 LED 判断 payload 是否启动及结果 |
| 主动触发 WDT 复位 | ⚠️ 未验证 | WDT=0xF2402000 在 QEMU 未映射，暂不访问 |
| 驱动对焦马达 | ✗ | 需知道马达寄存器 |
| 写 eMMC 标记供事后读 | ✗ | eMMC 焊死，事后无法读取 |

**真机判读：**
```
完全没长闪/快闪 ⇒ payload 没跑（物理脚、unlock 或入口问题）
看到自检/心跳但没有结果帧 ⇒ payload 在 LED/存储初始化阶段异常
看到结果帧 + N 短闪 ⇒ payload 已跑完并给出明确结果
```

---

## 第 4 步：全都失败

```
⇒ 停手。不要磨主板（等群里大佬给出"哪两根线"的确定答案）
⇒ 选项:
   · 送修（索尼官方/第三方）
   · 等大佬的 Type-C 引脚答案（他说"工厂用这个口写完整数据"）
   · 找 ZV-E10 的 BootROM dump（有 dump 就能分析 unlock 逻辑和真实引脚）
```

---

## 附：本次挖到的固件级情报汇总

| 情报 | 来源 | 价值 |
| --- | --- | --- |
| 错误码表（41/45/47/48/54/72/73/87） | `config/common.src` | 若能看到闪法可反推错误类型 |
| 闪烁编码 = `-b <HEX 位图>` | `uc_portctl.elf` 字符串 | 理解闪法含义 |
| **串行启动标记 = `/root/IAMLDUDWRT`** | `UdtrMain.sh` / `execute_prod.sh` | ★ 串行启动模式的特征 |
| **串行启动时 `ud_cons.elf on`（打开控制台）** | `UdtrMain.sh` | ★★ 有 TX 就能看到输出 |
| 工厂模式 = `MODE_PROD`（`getCurrentMode`） | `UdtrMain.sh` | 理解工厂流程 |
| 电源 IC 类型: 1/6=Charon, 4=CA, 5=PIROSHKI | `common.src` | ZV-E10 可能是 CA 或 Charon |
| 串行启动路径会调 `uc_caboot.elf` | `UdtrMain.sh` | CA 电源 IC 的特殊启动流程 |

### ★ 一个值得注意的猜想

```
IAMLDUDWRT 这个文件名 = "I AM LD UD WRT"
                        ↑    ↑  ↑
                        LD   UD  WRT
⇒ 可能 = "I AM (boot)LOADER / UPDATE(r) / WRITE(r)" ？
⇒ 它是 BootROM 串行启动后【由下载的程序创建】的标记文件
⇒ 也就是说: 如果有人能把固件灌进去，BootROM 就会建立这个文件 ⇒ 进入更新器 PROD 模式
⇒ ★ 这暗示: 索尼的"工厂基板制作"流程 = 串行启动 → 灌入更新器 → 更新器写完整固件
   ⇒ 与我们的路线【完全一致】✓✓
```
