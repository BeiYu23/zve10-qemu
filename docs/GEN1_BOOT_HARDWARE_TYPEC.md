# ZV-E10 一代 × RP2040 启动桥：参考资料的接口是 Multi，不是 Type-C

> 问题：买了 RP2040 Pico + Type-C 接线板，ZV-E10 一代；参考的 A7M4 那套资料**有没有 Type-C 版本**？
> 结论先说：**没有。** 他们的自制刷机线是 **Sony Multi 接口**（micro-USB 形状的 10 针配件口）引出板 + **RP2040-Zero**；
> 而 **ZV-E10 一代根本没有 Multi 接口**，所以那根线在你这台机器上**无处可插**。
> 本文所有结论都对着本地真源码/官方手册核过，不是印象。

---

## 1. 参考资料里的硬件到底是什么

来源：`<build>/src/sony-a7m4-cf`（`Erlla/sony-a7m4-custom-firmware`）
+ `/tmp/ilce7m4`（上游 `DavidBuchanan314/ILCE-7M4-RE`）。

| 项 | 内容 | 证据 |
| --- | --- | --- |
| 相机端接口 | **Sony Multi（配件口，micro-USB 形状的 10 针）** | `docs/wiring.md:5`「需要 RP2040-Zero、可正确引出 Sony Multi 信号的插头/转接板」；上游 README「blasting the right bits down the RX pin of the **accessory connector**」 |
| 桥板 | **RP2040-Zero**（Waveshare），不是 Pico | `docs/wiring.md:22`、上游 `tools/uart_boot/Makefile:1` → `FQBN := rp2040:rp2040:waveshare_rp2040_zero` |
| 信号/引脚 | `MULTI_RST=27`、`MULTI_TX=28`、`MULTI_RX=29`、GND | 上游 `tools/uart_boot/uart_boot.ino:23-25` |
| 电平 | 相机侧约 **1.8 V**，桥默认 **`-DOPEN_DRAIN`**（不推挽） | `docs/wiring.md:16`、`Makefile:5` |
| 相机 USB 口的作用 | 只用来跑 **fastboot**（普通 USB 线接 PC） | `docs/wiring.md:5`「相机 USB 用于 fastboot，RP2040 USB 用于启动桥；两者作用不同」 |
| 实物 | 一张图 = Multi 公头引出板 → GND/D+/D−/5V → USB-A；另一张 = 同一个 Multi 引出板 → RP2040-Zero 的 GP27/28/29/GND | 上游 `images/multi_pinout.png`、`images/multi_breakout.jpg`（已逐张看过） |

**关于 Type-C：**

- 中文/英文文档、脚本、方法章节里搜 `type-c` / `usb-c` / `typec` → **零命中**（全仓库 grep）。
- 上游仓库里唯一出现 "Type-C" 的地方是 `payloads/fastboot/src/usb_phy.c` 的注释
  「TCA — the **Type-C alt-mode controller**, at DWC3 base + 0x10000」——那是 **A7 IV 自己的 USB3 PHY**
  （CXD90057 的 DWC3 + Type-C alt-mode 控制器），跟刷机线的接线**无关**。
- 整套里出现的 USB-C 只有一个：**RP2040-Zero 板子自己的 USB-C 母座**（用来跟 PC 讲 bridge 协议）。

⇒ 你想要的是"Type-C 版刷机线"，**他们没做，也没写**。

---

## 2. 为什么这套线接不到 ZV-E10 一代上

### 2.1 ZV-E10 一代没有 Multi 口（Sony 官方手册原文）

| 机型 | 侧边端子（Sony 官方帮助指南） | 有没有 Multi |
| --- | --- | --- |
| **A6400** | 「8. **Multi/Micro USB Terminal**」、10. HDMI micro jack | ✅ 有（所以那套线本来能插 A6400 这一代） |
| **ZV-E10** | 「11. (Microphone) jack、12. **USB Type-C terminal**、13. Charge lamp、14. **HDMI micro jack**、15. (Headphones) jack」+ 顶部「3. Multi Interface Shoe」 | ❌ **没有** |

（ZV-E10 手册：`helpguide.sony.net/ilc/2070/v1/...`；A6400 手册：`helpguide.sony.net/ilc/1810/v1/...`）

注意两个容易被名字骗的点：

- **「Multi Interface Shoe」是顶上的热靴**（给闪光灯/数字麦克风用的），**不是**「Multi/Micro USB Terminal」。
  上游灌位用的 UART RX 在配件口的第 7/8 针，热靴上没有这条线，社区也没有把它当串口用的资料。
- ZV-E10 连 LANC/串口那层中间件都没带：`libInfraLanc.so`、`libInfraDial.so`、`libInfraSircs.so`
  在 libIMDB 表里**有条目但文件不存在**（A6400/ZV-1F 同样不带）。

### 2.2 你手上的 Pico 与文档要求的板子不一样

文档要 **GP27/GP28/GP29**；而**标准 Raspberry Pi Pico 的排针没有引出 GP29**：

> Pico 数据手册：`GPIO29 — IP Used in ADC mode (ADC3) to measure VSYS/3`

即 GP29 在 Pico 上被 VSYS/3 分压占用，不在 40 pin 上；RP2040-Zero 才把 GP0–GP29 全引出。
要用 Pico 就得改 `uart_boot.ino` 里的 `#define MULTI_RX 29`（重映射到空闲脚）再重编——
但这一步在 §2.3 的问题解决之前没有意义。

### 2.3 就算有 Multi 口，A7M4 那套方法对 ZV-E10 也基本不适用

| 维度 | A7 IV（资料的目标机） | ZV-E10 一代（你的机器） |
| --- | --- | --- |
| SoC | CXD90057 / CXD90058，**ARM64** | CXD90045，**ARM32** |
| 内核 | 4.9.337 | 3.0.27_nl-rt106+ |
| fastboot payload | AArch64 裸机 payload（已开源） | **没有**对应 payload，bootrom/payload 都不是一套 |
| eMMC 加密 | 分区 **AES-XTS 加密**，要从 boot0 推密钥 | 实测分区是**明文** ext2/vfat（不需要解密这一环） |
| 串口 boot 入口 | Multi 口的 RX 引脚灌位 | **没有可插的 Multi 口** |

所以：资料里"解密码、签名、FL2/FL3"那一大套，对 ZV-E10 能用的只有**方法论**；
硬件入口（Multi 口灌位）在 ZV-E10 上不存在。

---

## 3. 好消息：ZV-E10 平台自己确实有 Serial / USB boot（相机侧证据）

| 证据 | 内容 |
| --- | --- |
| `/system/ldr_drv.bin`（nflasha3） | 字符串 `uart read error.` / `Loader-Driver ver:` / `Loader-Driver rev:` ⇒ **loader 有 UART 读取通道** |
| updater `is_valid_boot.sh` | `ud_get_boot_device.elf` 返回 **1=OneNAND / 2=MS / 3=USB Boot**；USB Boot 分支会 `ud_cons.elf on` 打开控制台 |
| `startupdate.sh` | 存在 `/root/IAMLDUDWRT` ⇒ `UPDATE_TYPE=Serial`（**串口下载写入**模式） |
| `/usr/kmod` | 有 **主机侧** USB 驱动：`xhci-hcd.ko`、`usb-storage.ko`、`usbnet.ko`、`rndis_host.ko`、`asix.ko`、`usbhid.ko`、`cdc-*.ko`；同时也有 gadget 侧（`usbg_*`、`g_mass_storage`、`libcomposite`）⇒ 相机两种角色都具备 |
| `usb_insmod.sh` | 正常模式就装 `usb_extcmd.ko`（工厂 EXTCMD 通道）、`usb_portMonitor.ko`、`usbg_sen.ko` |

⇒ **平台支持"串口/USB 下载"这件事**，缺的是"从哪个物理点进去"：
ZV-E10 没 Multi 口，最可能是**主板测试点/工厂治具触点**（要拆机），或者走 **USB-C 的 USB Boot 路径**（触发条件未知）。

---

## 4. 你买的 Type-C 接线板现在能干什么（以及一个大坑）

- **相机 USB-C 是"设备口"**（固件里是 `usb_darwin` 控制器 + `usbg_*`/`libcomposite` gadget 栈）。
  想让 **Pico 跟相机用 USB 通信，Pico 得当主机**（Pico-PIO-USB），主机侧必须：
  在 CC 上给 **Rp**（典型 56 kΩ 上拉到 5 V）并**提供 VBUS**。
- 市面上多数「Type-C 接线板」焊的是 **5.1 kΩ Rd**（设备侧下拉）。拿它当主机＝**CC 状态错**，相机不会进数据态。
  买之前/用之前先用万用表量一下 CC–GND 的电阻：
  - ~5.1 kΩ 到 GND ⇒ 设备板（只能当设备用）
  - 无电阻/可自行加 ⇒ 可改成主机（CC 各接 56 kΩ 到 5 V，并注意 VBUS 供电）
- 另一条更"顺"的思路：**相机当主机、Pico 当 USB 大容量设备**（RP2040 自带 USB device，MSC 例程现成），
  对上 `is_valid_boot.sh` 里的 `USB Boot`（ID=3）。这条不需要 Pico 做主机，Type-C 接线板只用来引
  VBUS/D+/D−；但**触发条件完全未知**，需要先做 §5 的第 2 步。

---

## 5. 建议的下一步（按性价比排序）

1. **先别接相机**。量一下 Type-C 接线板的 CC 电阻，确认它是 Rd（设备）还是可以当主机用；
   再确认 Pico 板子上你能自由使用的脚（GP29 不可用）。
2. **把 `USB Boot` / `Serial` 的触发条件搞清楚**（我可以接着做，纯软件、不动硬件）：
   - 反汇编 `/system/ldr_drv.bin`（loader 的 UART 读路径）与 `pformat.elf`；
   - `ud_get_boot_device.elf` 只存在于相机内的 updater 分区（升级包里没有），但 `mf`/`sauu` 里有同类逻辑；
   - 看 `usb_portMonitor.ko` / `usb_extcmd.ko` 判断"插入什么设备会让相机进哪种模式"。
3. **找 ZV-E10 主板 UART 测试点**：需要拆机照/社区资料；这是唯一能复刻 A7M4"灌位"方式的物理点。
   顺便：ZV-E10 分区是明文，一旦有了写入通道，**不需要** A7M4 那套解密/验签绕过。
4. **如果目标就是走 A7M4 那条成熟路线**：需要一台**有 Multi/Micro USB 口**的机身
   （A6400 / A6600 / A7C / A7 IV 都有）。ZV-E10 一代不是这条路的目标机。
5. 手上的 Pico 不浪费：可以先用它做 **USB 侧实验**（当主机扫相机在各 USB 模式下的枚举、
   或当 MSC 设备让相机读），这些都不需要 Multi 口。

---

## 5.5 补充（第二轮）：A7C II / α6700 也只有 Type-C，他们怎么改的？

**先确认用户的前提是对的**（Sony 官方帮助指南端子清单）：

| 机型 | 侧边端子 | Multi 口 |
| --- | --- | --- |
| α6700（ILCE-6700，guide `ilc/2320`） | 麦克风、**USB Type-C**、卡槽、HDMI micro、耳机 | ❌ |
| A7C II（ILCE-7CM2，guide `ilc/2360`） | 麦克风、**4. USB Type-C**、卡槽、耳机、HDMI micro（另有 Multi Interface Shoe 热靴） | ❌ |
| ZV-E10 一代 | 麦克风、USB Type-C、HDMI micro、耳机（+ 热靴） | ❌ |

**但"只有 Type-C 也能改"≠"Type-C 能灌串口"。** 三条证据：

1. A7M4 那条线的入口是 **BootROM 的 UART boot（GPIO bit-bang）**，走配件口 RX —— 上游 `ILCE-7M4-RE/bootrom/README.md` 原话：
   "The AP also has a UART boot mode (bit-banged over GPIO)"、"blasting the right bits down the RX pin of the accessory connector"。
2. 上游 dump 的 **CXD90057/CXD90058 BootROM**（各 48 KB）里扫不到任何 USB 字符串
   （`strings | grep -i usb` 零命中）⇒ **BootROM 没有 USB 栈**，不存在"USB-C 直灌 bootrom"。
3. A7M4 项目自己的文档写明：串行启动传输是进入 fastboot 的**必需硬件**，方法固定绑定 A7M4 6.02。

⇒ A7C2/α6700 的成功案例用的是**另一条管线**，最可能是**存储卡升级**：

- α6700 的官方升级方法标题就是 [「ILCE-6700 Ver.2.00 固件升级操作方法**(使用存储卡升级)**」](https://service.sony.com.cn/download/firm/00003046.html)。
- A7C II 的官方固件本体就是 `BODYDATA.DAT`（Ver 2.01，**1,139,929,560 B**，UFU 容器）——
  见 [ChambersXDU/sony-a7c2-research 的验证记录](https://github.com/ChambersXDU/sony-a7c2-research/blob/master/docs/ilce-7cm2-2.01.md)。
- 日版"改中文/多语言"这类，多数是**官方跨区固件**（带索尼签名）→ 不需要任何硬件，Type-C 只是普通 USB 口。

**对 ZV-E10 一代的重要更正（我上一轮说窄了）**：gen1 **也有**存储卡/文件输入这条管线：

| 证据（ZV-E10 固件内） | 内容 |
| --- | --- |
| `/usr/lib/libupdaterufp.so` | 字符串 **`BODYDATA.DAT`**、`LENSDATA.DAT`、`/setting/updater/dat4`、`/tmp_updater/Firm.dat` |
| 升级器 initrd | `sdfileinput.elf`、`sauu`、`libsdfileinputupdaterapi.so`、`execute_ufp.sh`（按模式跑 `sauu -m/-s` 或 `sdfileinput.elf`） |
| `is_valid_boot.sh` | 启动源：**2 = MS Boot（从存储卡启动升级器）**、3 = USB Boot |
| `UdtrMain.sh` | Minor / Service / **VersionSkip(mode6)** / Serial(`/root/IAMLDUDWRT`) 等模式 |

**但拦路的是验签**：`libsdfileinputupdaterapi.so` 里的符号与 A7M4 被 patch 的那份**完全同名**：

```
Updater::SignatureVerification::verifyKey(char const*)     ← A7M4 方法 1 打掉的就是它
Updater::SignatureVerification::VerifySignature(...)
Updater::SDUUDConditionChecker::isVersionOK() / isModelOK()
PEM_read_RSA_PUBKEY · SHA1_* · SHA256 · RSA_verify
```

⇒ 官方包能过；**改过的包过不了**，除非先打掉 `verifyKey`（A7M4 方法 1，ARM32 版就是 `mov r0,#0; bx lr`），
而这需要一条**写入通道**去改 `nflasha1` 里的升级器 initrd —— A7M4 的写入通道正是 Multi 口的串行启动/fastboot。
ZV-E10 没有 Multi 口，但固件里存在 **MS Boot / USB Boot / Serial** 三条候选入口，这才是该继续查的点。

**待确认**：用户看到的 A7C2/α6700 "改成功"具体是（a）日版改语言、（b）去录像时限/解锁、
还是（c）真正的自定义固件。三者对 ZV-E10 的可行性完全不同——(a) 可能不需要硬件，
(c) 必须先把写入通道找出来。

---

## 5.6 关于"他们都是从 Type-C 的 RX/TX 改的"这个说法

**我查不到支持它的公开资料**，但把能核实的技术事实列出来，你可以自己判断：

| 事实 | 证据 |
| --- | --- |
| A7M4 公开资料里的串口启动是 **BootROM 的 UART boot，bit-banged over GPIO**，接的是**配件口（Multi）的 RX 脚** | 上游 `ILCE-7M4-RE/bootrom/README.md`、主 README |
| CXD90057/58 的 BootROM dump 里**没有任何 USB 字符串** ⇒ BootROM 没有 USB 栈 | `strings CXD9005{7,8}_*_0xFFFF0000.bin \| grep -i usb` 零命中 |
| CXD90045（ZV-E10 同 SoC）的**控制台/调试 UART0 是挂在 GPIO 上的**：`UART_PORT=2`、`UART0_TXD=(1<<9)`、`UART0_RXD=(1<<8)`（由 `CONFIG_CXD900X0_UART_IO_INIT` 把 GPIO2[9]/GPIO2[8] 切成 UART 功能） | `<build>/src/a7c-kernel/linux/arch/arm/mach-cxd900x0/uart.S` |
| ZV-E10 的 USB-C 是 **USB 3.2 Gen 1 / SuperSpeed 5Gbps**（官方指南）⇒ Type-C 的 **SS 差分对（SSTX/SSRX）被 USB 占用**，物理上不能再当 UART | ZV-E10 帮助指南 "SuperSpeed USB 5Gbps (USB 3.2)" |
| 因此 Type-C 上"空着"的只有 **SBU1(A8) / SBU2(B8)**（相机不用 DP alt mode）与 CC1(A5)/CC2(B5) | Type-C 标准引脚定义 |

**结论**：如果真有人从 Type-C 走串口，**只可能是 SBU1/SBU2**（或内部测试点），不可能是 SSTX/SSRX；
但我没有找到任何公开的 A7C II / α6700 / ZV-E10 用 Type-C SBU 做串口启动的记录，
所以这个说法**目前无法证实**。要证实/证伪，最直接的就是拿万用表量（见下）。

**用你手上的 Type-C 接线板做判定（不用拆机）**：

1. **关机、拔电池**状态下量电阻：CC1/CC2 对 GND（相机是 UFP 时约 5.1 kΩ）；SBU1/SBU2 对 GND 是否悬空。
2. 装电池、**开机**，量各脚电压：UART 空闲是**高电平**（相机侧约 1.8 V 域）。
   重点看 **SBU1(A8)、SBU2(B8)** 是否稳定在 ~1.8 V，以及复位/开机瞬间有无跳变。
3. 若量到可疑的 1.8 V 串口线：方向要注意——A7M4 的桥是**灌相机的 RX**（host → 相机），
   所以必须先确认哪根是相机的 RX。电平上必须用 **OPEN_DRAIN**（借相机侧上拉）或电平转换，
   **绝对不要用 3.3 V 推挽**。
4. 另一条更省事的核实：找 **ZV-E10 主板拆机照/服务手册**，看有没有丝印标着 `TX/RX/GND/TP` 的测试点
   （工厂治具触点通常就在这些位置）。

**需要你提供**：你看到"从 Type-C 的 RX/TX 改"的**出处**（帖子/图/视频链接）。
我按着出处去核对到底接的是哪几根、用的什么触发方式——这决定 ZV-E10 能不能照抄。

---

## 5.7 测量作业单（要量哪些、期望值、怎么判读）

**准备**

- 接线板必须是**公头能插进相机**（相机那边是 Type-C 母座）；如果是母座板，就用一根 USB-C 公对公/公头转接。
- 万用表：电阻档 + 直流电压档。**黑笔固定接 GND**（Type-C 的 A1/A12/B1/B12）。
- **先不要接 RP2040**，裸板量完再决定接线；量的时候别让表笔滑动（VBUS 短路最危险）。

**Type-C 引脚对照（24 针）**

| 信号 | 针脚 |
| --- | --- |
| GND | A1 A12 B1 B12（+ 外壳） |
| VBUS | A4 A9 B4 B9 |
| CC1 / CC2 | **A5 / B5** |
| D+ / D− | A6 A7（B6 B7 是同一对，一般只走一组） |
| SBU1 / SBU2 | **A8 / B8** |
| SS 差分对 | A2 A3 A10 A11 / B2 B3 B10 B11（TX1± RX2± TX2± RX1±） |

### 第 1 步：断电量电阻（关机、拔电池）

> ⚠️ **别用"通断档/蜂鸣档"测 CC**：通断档只判断"是否低于阈值"（通常几十欧，好表到 ~1 kΩ），
> **5.1 kΩ 远超阈值 ⇒ 不会响、显示 `OL`**。所以"不响"是正常现象，但它**证明不了** CC 上有 5.1 kΩ。
> 判断必须用**电阻档**（20 kΩ 或自动量程）读数值。只有真的"响了"才说明短路（≈0 Ω，多半是表笔滑到一起）。
> 带电量电阻/通断都是错的：读数无意义，还可能干扰 CC 检测电路。

| 量哪里 | 期望 | 判读 |
| --- | --- | --- |
| CC1 / CC2 → GND | **≈5.1 kΩ** | 相机是 **UFP（设备）**：只有当主机给 VBUS + CC 上拉(Rp) 时它才会出 USB |
| CC → GND | ≈0 Ω | 短路/板子问题 |
| CC → GND | 悬空(OL) | 少见，记下来 |
| SBU1 / SBU2 → GND | 通常 OL | 若只有几十~几百 Ω ⇒ 这两根在板上接了东西（有戏） |
| D+ / D− → GND | 高阻 | 相机侧 USB PHY 未供电时应当如此 |
| VBUS → GND | 不短 | 一定确认，避免后面灌电出事 |
| SBU1↔SBU2、D+↔D− | 不短 | 排除板上短路 |

### 第 2 步：上电空载（装电池开机，不接电脑）

逐个脚量 **对 GND 的直流电压**，记稳态值：

- **VBUS（A4/A9/B4/B9）应为 0 V** —— 相机是设备，不输出 5 V。看到 5 V 说明接反/板子异常。
- **D+/D− 通常 ~0 V** —— 没检测到 VBUS 前，USB 设备不会上拉 D+。
- **重点：SBU1(A8) / SBU2(B8)**
  - 稳定在 **~1.8 V** ⇒ 疑似相机侧串口 **TX**（或其它 1.8 V 逻辑），是第一嫌疑；
  - ~0 V 且不是悬空 ⇒ 可能是相机的 **RX**（等外部驱动）；
  - 一直是 0/悬空 ⇒ 这两根不参与。

### 第 3 步：上电 + 接电脑，各模式再来一遍

把相机 `USB连接` 依次切 **海量存储 / MTP / 电脑遥控 / USB流式传输**，每个模式量一遍 SBU/D+/D−/CC：

- 目的是把"USB 工作状态"与"空闲"区分开；相机不用 DP alt mode，所以 USB 工作时 SBU 应仍空闲。
- 顺带自检：接上电脑后 D+/D− 会出现约 3.3 V 的 USB 信号（均值/跳变），说明你的测量点没接错。

### 第 4 步：复位瞬间抓波形（决定性一步）

串口启动是"**复位后很短时间内**灌数据"，静态量电压不够：

- 有示波器/逻辑分析仪最好；没有就把候选脚接到 RP2040 的一个 GPIO，复位后连续采样 1–2 秒。
- 判据：**复位后几十 ms~几百 ms 内**，候选线上是否出现 **~48 kbaud 级别的数据波形**（A7M4 的链路波特率就是 **48000**）。只有静态电平、没有数据 → 不是串口。
- 上游 `tools/uart_boot_probe` 就是为这个判定写的：给 RST 一个脉冲，然后在一根线上持续发垃圾数据，
  **如果相机报"13 次短闪"（错误码 0x100D，parity error），就证明你打进了串口 bootloader**。
  这是"我找对针了吗"最快的验证法（相机异常就拔电池，上游原话：别慌，拔电池即可恢复）。

### 关键数字对照

| 项 | 值 |
| --- | --- |
| A7M4 串口启动链路波特率 | **48000** |
| 相机信号电平 | 约 **1.8 V**（桥必须 OPEN_DRAIN 或电平转换） |
| 老机身 Multi 口 UART | 3.3 V 域（flowshutter 记录），**不要和 BootROM 的 1.8 V 混为一谈** |
| 灌的方向 | A7M4 灌的是**相机的 RX**（host → 相机），两根候选线要判方向 |

### 回填模板（量完发我，我来判读+定下一步）

| 引脚 | 断开时 R→GND | 开机电压 | 接电脑时 | 复位瞬间有无波形 |
| --- | --- | --- | --- | --- |
| CC1 (A5) | | | | |
| CC2 (B5) | | | | |
| SBU1 (A8) | | | | |
| SBU2 (B8) | | | | |
| D+ (A6) | | | | |
| D− (A7) | | | | |
| VBUS | | | | |

---

## 6. 引用与复现

- 参考资料（本地）：
  - `<build>/src/sony-a7m4-cf/docs/wiring.md`、`docs/wiring.en.md`、`README.md`
  - `/tmp/ilce7m4/README.md`、`tools/uart_boot/uart_boot.ino`、`tools/uart_boot/Makefile`、`images/multi_pinout.png`
- 官方手册（判断接口用）：
  - ZV-E10：`https://helpguide.sony.net/ilc/2070/v1/en/print.pdf`（端子清单见 "Parts of the camera"）
  - A6400：`https://helpguide.sony.net/ilc/1810/v1/en/print.pdf`（有 `Multi/Micro USB Terminal`）
  - Pico：`https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf`（GPIO29 = VSYS/3 检测）
- 一键复查：

```sh
grep -rniE "type-?c|usb-?c\b" <build>/src/sony-a7m4-cf /tmp/ilce7m4 | grep -v '\.git/'
sed -n '1,25p' <build>/src/sony-a7m4-cf/docs/wiring.md
strings -a <build>/fw/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha3_unpacked/ldr_drv.bin | grep -i uart
cat <build>/fw/unpacked_zve10/firmware.tar_unpacked/0400_updater/dev/nflasha1_unpacked/boot/initrd.img_unpacked/usr/bin/is_valid_boot.sh
```
