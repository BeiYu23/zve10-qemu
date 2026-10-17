# CXD90045 BootROM / UART 公开资料调研

> 调研时间：2026-10-17 · 结论：**重大发现**（找到了 SoC BGA 引脚表）

---

## ★★★ 一、核心成果：拿到 a6400 官方维修手册（同芯片 CXD90045）

**来源**：`ILCE6400_serviceManual_FULL.pdf`（24.8 MB，完整维修手册含电路图）
```
下载地址: https://photo-parts.com.ua/parts/IMG_STORE/Sony%20A6400%20PCB/ILCE6400_serviceManual_FULL.pdf
已存: /tmp/ILCE6400_serviceManual_FULL.pdf
文本: /tmp/a6400_manual.txt（pdftotext 提取，3.26 MB）
```

**为什么重要**：**a6400（ILCE-6400）与 ZV-E10 使用同一颗 SoC（CXD90045 / CXD900X0 家族）**
⇒ 电路图中的 **SoC BGA 引脚定义、信号名、外部连接**，对 ZV-E10 有**极高参考价值**

---

## ★★★★ 二、SoC BGA 引脚表（从电路图提取）

| BGA 球位 | 信号名 | 含义 | 对我们 |
| --- | --- | --- | --- |
| **`G18` / `L19`** | **`BOOT_SERIAL_IN`** | **BootROM 串行启动数据输入** | ★★★★★ **这就是灌 payload 的线！** |
| **`AL24`** | **`BOOT MODE`** | **启动模式选择** | ★★★★ 决定进 eMMC 启动 还是 串行/工厂模式 |
| **`P4`** | **`BOOT_SIMPLE/SENS_XCS2`** | BOOT_SIMPLE（简单启动模式） | ★★★ 另一个模式选择 |
| `A8` | `XSYS_RST` | 系统复位 | ★★ 可用作复位控制 |
| `L11` | `XRESET_REQ` | 复位请求 | ★★ |
| `K3` | `RESET` | 复位 | ★★ |
| `M15` | `SIR_RXD` | 红外接收 | 无关 |
| `H18` | — | — | — |

**⇒ ★ 关键结论：**
```
CXD90045 的 BootROM 串行启动 = 「BOOT MODE/BOOT_SIMPLE 置到串行模式」+「数据从 BOOT_SERIAL_IN 输入」
⇒ 与我们用 a7M4 方案做的事【完全一致】✓
⇒ 而且这解释了 a7M4 的 Multi 口为什么能救砖：Multi 口把 BOOT_SERIAL_IN / BOOT MODE / RESET 引出来了
```

---

## ★★★ 三、a6400 的 MULTI 连接器引脚图（电路图原文）

**MULTI 连接器 CM4000（a6400 有 MULTI 口；ZV-E10 没有，但信号名通用）：**

| Pin | 信号 | 备注 |
| --- | --- | --- |
| 3 | `USB_D+` | USB 数据 |
| 4 | `USB_ID` | USB OTG ID |
| 5 | `GND` | 地 |
| 6 | `D_3.1V/LANC_DC` | 电源/LANC |
| 7 | `XRESET_REQ` | **复位请求** |
| **8** | **`UART_TX/LANC_SIG`** | **DEBUG_UART0_TXD**（相机 → 外部） |
| **9** | **`UART_RX/BOOT_IN`** | **DEBUG_UART0_RXD**（外部 → 相机） |
| **10** | **`AD_JACK_IN`** | ★ **`BOOT_SERIAL_IN`**！ |
| 11 | `ROUT/XAELOCK` | 音频输出 |
| 12 | `LOUT/XSHUT` | 音频输出 |
| 13 | `VIDEO` | 视频 |
| 15 | `XPWR_ON` | 电源开 |

**⇒ ★★ 极其重要：`BOOT_SERIAL_IN` 在 MULTI **pin 10**，而不是 pin 9（UART_RX）！**
```
pin 8  = UART_TX（相机输出，可用于【看日志】）
pin 9  = UART_RX（普通串口接收）
pin 10 = BOOT_SERIAL_IN（★ BootROM 串行启动专用输入）
```

**⇒ 这解释了为什么"探测"要接在特定的脚上 —— 接 pin 9（UART_RX）可能打不进 BootROM！**

---

## ★★ 四、其他有用信息

### DEBUG UART
```
DEBUG_UART0_TXD / DEBUG_UART0_RXD  → MULTI pin 8 / pin 9
⇒ 如果 ZV-E10 能找到这两个信号的测试点 ⇒ 就能看到控制台输出 ✓
⇒ 而且固件里已确认: 串行启动模式下会 `ud_cons.elf on`（打开控制台）✓
```

### 复位相关
```
XSYS_RST (A8) / XRESET_REQ (L11) / RESET (K3)
⇒ MULTI pin 7 是 XRESET_REQ ⇒ 这就是 a7M4 方案里 GP27 控制的 RESET ✓
```

### 电源 IC 类型（固件）
```
POWER_IC_CHARON_CAM=1, POWER_IC_CA=4, POWER_IC_PIROSHKI=5, POWER_IC_CHARON_DSC=6
⇒ ZV-E10 大概率也是 CA 或 Charon 系列
```

---

## 🎯 五、对 ZV-E10 的行动建议

```
① 维修手册的 SoC 引脚表是【同一颗芯片】⇒ G18/L19(BOOT_SERIAL_IN)、AL24(BOOT MODE)、P4(BOOT_SIMPLE)
   在 ZV-E10 主板上【同样的 BGA 位置】⇒ 只要找到这些球引出的测试点/过孔！
② 用户大佬说"工厂用 Type-C 口写完整数据"
   ⇒ 说明 ZV-E10 把 BootROM 相关信号【引到了 USB-C 连接器】或附近的测试点
   ⇒ 重点查: USB-C 的 SBU1/SBU2（A8/B8）——之前已测 A8 无信号，但可能时序/模式不对
③ ★ 关键问题: ZV-E10 是否需要【先拉 BOOT MODE 到串行模式】才能用 BOOT_SERIAL_IN？
   ⇒ a6400 上 BOOT MODE(AL24)/BOOT_SIMPLE(P4) 是模式选择
   ⇒ 如果 ZV-E10 上电默认是 eMMC 启动，那必须先强制 BOOT 模式，否则灌数据没用 ✗
   ⇒ ★ 这可能就是"13 闪不出现"的真正原因！
```

---

## 六、搜索过程中的其他线索

| 来源 | 内容 | 有用度 |
| --- | --- | --- |
| [ILCE-7M4-RE](https://github.com/DavidBuchanan314/ILCE-7M4-RE) | a7M4 串行启动完整逆向（我们已用） | ★★★★★ 已用 |
| [photo-parts a6400 PCB](https://photo-parts.com.ua/parts/IMG_STORE/Sony%20A6400%20PCB/ILCE6400_serviceManual_FULL.pdf) | **a6400 完整维修手册** | ★★★★★ **本次核心成果** |
| [iFixit ZV-E10 拆解](https://zh.ifixit.com/Teardown/Sony+ZV-E10+%E6%8B%86%E8%A7%A3%E4%BB%A5%E6%8E%A5%E8%A7%A6+SD+%E5%8D%A1%E6%A7%BD/200047) | 只到 SD 卡槽，无测试点 | ★ |
| [Qiita: Sony MULTI 端子](https://qiita.com/morohe/items/8ffe963c0880b51d17dd) | MULTI 端子说明 | ★★ |
| manualslib | 有 a6400 手册但被 Cloudflare 拦（403） | ✗ |
| FX10 bootloader 日志 | 是 Cypress FX10（USB 芯片），与索尼相机无关 | ✗ |

**⇒ 未找到：ZV-E10 专用的 UART/测试点公开资料（这类信息基本在维修圈内部）**

---

## 七、下一步（如果在真机上验证）

```
1. 找 ZV-E10 主板的 G18/L19 附近测试点（需要显微镜/点位图）
2. 或者: 找 USB-C 连接器到 SoC 的走线，看 SBU1/SBU2 是否接到 BOOT_SERIAL_IN
3. ★ 最关键: 确认 ZV-E10 的 BOOT MODE 引脚状态 —— 是否需要外部拉高/拉低才能进串行模式
   ⇒ 如果能拿到 ZV-E10 的点位图（维修站有），这一步就解决了
```

---

## ★★★★ 八、完整 BOOT 信号图（重大补充）

**从 a6400 电路图提取的 SoC 启动/调试相关引脚（CXD90045 同一颗）：**

| BGA 球位 | 信号名 | 含义 | 对救援的价值 |
| --- | --- | --- | --- |
| **`G18` / `L19`** | `BOOT_SERIAL_IN` | **串行启动数据输入** | ★★★★★ 灌 payload 的线 |
| **`N8`** | **`BOOT_EXT/SENS_XCS`** | **BOOT_EXT = 外部启动（串行/工厂模式）** | ★★★★★ **可能就是强制串行模式的脚！** |
| **`P4`** | `BOOT_SIMPLE/SENS_XCS2` | BOOT_SIMPLE 模式 | ★★★★ 模式选择 |
| **`AL24`** | `BOOT MODE` | 启动模式选择 | ★★★★ |
| **`AH23`** | `BOOT_NVM/SA_TDI` | 从 NVM(eMMC) 启动 / **JTAG TDI** | ★★★ 正常启动选它；JTAG 用 |
| **`AH24`** | `BOOT_SBB/SA_TDO` | / **JTAG TDO** | ★★★ **JTAG 调试接口存在！** |
| `AJ12` | `BOOT_MTM/PERST_1` | 多测试模式 | ★★ |
| `A8` | `XSYS_RST` | 系统复位 | ★★ |
| `L11` | `XRESET_REQ` | 复位请求 | ★★ |
| `K3` | `RESET` | 复位 | ★★ |

### ★★★ 关键推论

```
1. SoC 有【多个启动模式 strapping 引脚】:
   BOOT_EXT(N8) / BOOT_SIMPLE(P4) / BOOT MODE(AL24) / BOOT_NVM(AH23) / BOOT_MTM(AJ12)
   ⇒ 板上一定有成对的电阻把这些脚拉到 VCC 或 GND（决定上电默认启动模式）

2. 正常启动 = BOOT_NVM（从 eMMC 启动）
   工厂串行启动 = 可能把 BOOT_EXT(N8) 拉到某个电平 + 数据走 BOOT_SERIAL_IN(G18/L19)
   ⇒ ★ 这就是"工厂治具(pogo pin)"接触的测试点！

3. 还有 JTAG（SA_TDI/SA_TDO）⇒ 另一条救援路径
   ⚠️ 但 JTAG 需要知道 TAP 状态机和指令寄存器 ⇒ 工作量大
```

### ★★ 这解释了"13 闪不出现"的可能原因（完整版）

```
原因 1: 打错了信号
  · UART_RX (pin 9) ≠ BOOT_SERIAL_IN (pin 10)
  · 必须打到 BOOT_SERIAL_IN 才能进 BootROM 串行启动

原因 2: 没有强制启动模式
  · 上电默认可能是 BOOT_NVM（eMMC 启动）
  · 必须先让 BOOT_EXT/BOOT_SIMPLE/BOOT MODE 处于"串行/工厂"状态
  · 否则即使 BOOT_SERIAL_IN 上有数据，SoC 也不会理

原因 3: 复位时序不对
  · BootROM 只在复位后很短的窗口内检查串行启动
  · a7M4 方案用 GP27 精确控制 RESET 就是在抓这个窗口

⇒ ★ 所以我们需要的物理点可能是【3 个】：BOOT_SERIAL_IN + 某个 BOOT strap + (可选)RESET
```

---

## 九、关于 ZV-E10 维修手册（本次专门搜索的结果）

**⇒ 未找到公开的 ZV-E10 维修手册 / 点位图**

尝试过的途径：
```
· photo-parts.com.ua 的 "Sony ZV-E10 PCB" 目录     ⇒ 404（只有 a6400 等旧机型）
· 多个搜索引擎（中/英/俄文关键词）                  ⇒ 只找到拆解文章、用户手册、维修店广告
· manualslib / elektrotanya                        ⇒ 无 ZV-E10
· "9-888-xxx" 索尼手册编号格式                      ⇒ 无结果
```

**唯一有用的副产品：**
```
ZV-E10 主板料号 = SY-1121
（来源: 一个土耳其零件销售页 "Sony Zv-e10 Ana Tahta Anakart Sy-1121"）
⇒ 可用这个料号在维修圈找点位图
```

**⇒ 但 a6400 手册已经给了 **SoC 级**的全部信息（引脚名、BGA 球位、信号含义）**
**⇒ 这些在 ZV-E10 上 **完全相同**（同一颗芯片）**
**⇒ 唯一缺的是 ZV-E10 的**板上走线**（信号从 BGA 引到哪里）—— 这只能靠点位图或实测**

### 建议向维修大佬提的问题（用 BGA 球位问，最精准）
```
1. "ZV-E10 主板上 G18/L19 (BOOT_SERIAL_IN) 有没有测试点？"
2. "BOOT_EXT (N8) / BOOT_SIMPLE (P4) / BOOT MODE (AL24) 在板上是什么状态？（有电阻拉到哪？）"
3. "工厂治具（pogo pin）接触的是哪几个点？"
4. "有没有 ZV-E10 的 boardview（点位图）？料号 SY-1121"
```
