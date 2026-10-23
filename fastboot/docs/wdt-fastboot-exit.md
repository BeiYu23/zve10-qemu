# fastboot ~3 分 40 秒自动退出：根因与修复（2026-10-22）

## 现象

- fastboot 注入后 **~3 分 40 秒（≈220 s）** 自动从 USB 消失，与 payload
  在做什么无关（空闲等待、读取中都会发生），时间高度确定。

## 根因：幸存的 App 看门狗

CXD900x0 的看门狗机制（内核 `arch/arm/mach-cxd900x0/wdt.c`）：

- **TIMER-IP WDT**：`CXD900X0_WDT_TIMER = 0xF2402000`（CXD90014 定时器布局，
  WD 时钟 1 MHz），超时后通过 MISC 块的 **RSTREQ** 发复位请求
- **MISC_RSTREQ**（MISC 基址 `0xF2915000`，regs-misc.h）：
  `RSTREQ=+0x40`，`SET=+0x44`，`CLR=+0x48`，`STAT=+0x50`；使能位 `EN=0x010`
- 用户态通过 `/proc/wdt` 的 `WDT_SET_START` ioctl 直接武装它：
  `cxd900x0_wdt_count = arg`（**1 MHz 时钟下的原始微秒数**），App 周期性重装
- 真机 cmdline 里有 `wdt.timeout=500`（内核态看门狗默认 OFF，
  `wdt.mode` 未置位 ⇒ 是 App 在用 ioctl 武装）

**GP27 XRESET_REQ 脉冲只复位 AP 子系统**：外设域的 `MISC_RSTREQ_EN`
和正在倒计时的 WDT 定时器**幸存**。注入后 payload 正常运行，~220 秒后
（App 最后一次重装的时刻起算）RSTREQ 触发整机复位，USB 设备消失。

## 证据链

1. 现象时间（3:40 ≈ 220 s）确定、与 payload 活动无关
2. 相机 cmdline 带 `wdt.timeout=500`；内核 wdt 驱动提供
   `/proc/wdt` ioctl（`WDT_SET_START` 接受任意微秒数计数）
3. GP27 复位是 XRESET_REQ 外部请求，只复位 AP；MISC/定时器块在外设域
4. payload 从未触碰 `0xF2402000`/RSTREQ，且我们已知该复位不清它们
   （救砖手册的复位注入流程对 Pico 侧 UART/时序零影响可佐证外设域状态保持）

## 修复（payload 启动即杀看门狗）

`fastboot/src/reset.c` 新增 `wdt_kill()`，复刻内核 wdt 驱动的
`WDT_SET_STOP`（wdt.c:379-382）：

```c
write32(MISC_RSTREQ_CLR /* 0xF2915048 */, MISC_RSTREQ_EN /* 0x10 */);
write32(WDT_TIMERCTL   /* 0xF2402000 */, 0);
write32(WDT_TIMERCLR   /* 0xF2402004 */, TMCLR|TMINTCLR /* 0x11 */);
```

`main()` 的**第一条调用**就是 `wdt_kill()`（先于 LED/定时器/USB 初始化）。
`oem reboot`（system_reset）不受影响：它直接置位 EN|ASSERT。

## 其他机型对照

- ILCE-7M4-RE（CXD90057）payload **不处理这个看门狗**——其 Virtual WDT
  （darwin）是 BootROM 刷写计数器，另一回事；7M4 用 Multi 口 +
  XRESET_REQ GPIO 驱动复位，全盘 dump 能跑很久 ⇒ 90057 平台上
  App 未武装此 WDT 或复位语义不同
- Powerful6700（A6700，CXD90057 代）源码中无看门狗处理 ⇒ 同上
- **ZV-E10 是唯一已确认 App 武装 TIMER-IP WDT 且跨 XRESET_REQ 幸存的案例**

## 验证流程（下次注入时）

1. 用**旧** payload（git 历史版）注入 → 枚举后立即
   `oem peek:0xf2402000:0x10` + `oem peek:0xf2915040:0x10`
   ⇒ 应看到 TMST 位在跑、CMP 巨大计数、RSTREQ_EN 置位（**武装现场**）
2. 手动 `oem poke:0xf2915048:0x10` + `oem poke:0xf2402000:0x0` +
   `oem poke:0xf2402004:0x11` ⇒ 会话应活过 3:40（** mitigation 证明**）
3. 换**新** payload（wdt_kill 内置）注入，保持会话 >5 分钟不再退出
   ⇒ 修复生效；`peek` 确认 WDT CTL=0、RSTREQ EN=0
4. 之后 Step 0/1（stage2 自检 + nflasha15 基线）可在同一会话内完成，
   不再受 3:40 限制

## 构建（GNU 工具链，含本次修复的 payload）

见 [write-success.md](write-success.md)。新 payload 6,516 B（clang 版
7,908 B；GCC 代码更紧凑），串口启动预算 8,192 B 余量 1,676 B。
GNU 构建需 `libgcc_shim.c`（raise() stub）+ `LDPOST=<libgcc.a>`。

## ★★ 更正（2026-10-23 晚）：真机看门狗**是开启的**

前一版复核曾断言「`wdt.mode=0` ⇒ 内核从不装载、AP 看门狗双重关闭」。
**该结论错误**，原因是引用了错误文件：

| 文件 | 内容 | 性质 |
|---|---|---|
| `/root/fw/real_cmdline.txt` | 只有 `wdt.timeout=500`，`console=ttyAM0,115200n8` | ❌ **QEMU 实验改过的副本**（console 已被改成串口） |
| `zve10_delivery/nflasha1.img` @0x66C4C0 | `wdt.mode=0 xrstreq=0` | 只是 cmdline **模板**，会被覆盖 |
| **`/setting/KEMCO.TXT`**（nflasha2，bootloader 真正读的） | **`xrstreq=1 wdt.mode=1`** | ✅ **权威来源** |
| 真机 `/proc/cmdline`（`a_p0/01_cmdline.txt`，10-16 实抓） | `xrstreq=1 wdt.mode=1` | ✅ 与 KEMCO.TXT 一致 |

⇒ 正常开机时 **`wdt.mode=1`**：`wdt_init()` 会装载、`wdt_setup_counts()` 会计算
（`wdt.timeout=500` ⇒ `(1000+1)*500` 个 1 MHz tick = **500.5 ms**），
`cpu_wdt_start()` / `cxd900x0_wdt_start()` 都会启动；
**`xrstreq=1`** ⇒ `cxd900x0_xrstreq_setup()` 把 `MISC_RSTREQ_EN` **置位**。

### 但这仍然不是 5~9 分钟的杀手（实测依据）

- 若 WDT 在注入后仍在跑，它只会在 **500 ms** 内复位，而我们观测到的是分钟级
- 注入后实测 `WDT TIMERCTL=0 / TIMERCMP=0 / TIMERREAD=0 / RSTREQ=0`
  （连续两个会话都是全 0）⇒ **BootROM 串口启动路径把它停掉/清了**
  （ROM 进入串口下载前必然要清干净外设状态）
- 而会话仍然在 5~9 分钟后死 ⇒ 死因另有所在（见
  [darwin-power-mcu.md](darwin-power-mcu.md) / [power-idle-exit.md](power-idle-exit.md)）

### 保留 `wdt_kill()` 的真正理由（现在更充分了）

`wdt_kill()` 在注入后是 **no-op**（本来就已经 0），但它是**防御性保险**：
若将来改用不经 ROM 的注入方式（例如冷启动 `--no-reset`、或 ROM 不清 WDT 的变体），
看门狗会是**武装状态且超时仅 500 ms**，那时 `wdt_kill()` 就是唯一救命的一行。
