# ZV-E10 fastboot 可行性评估与实施计划

## 为什么 fastboot 值得做

一旦在 BootROM 串口启动阶段把 USB 栈跑起来，主机就能：
```
fastboot peek / poke / exec   任意内存读写执行
fastboot partition dump       读分区（含 eMMC 全盘）
fastboot flash                写分区
fastboot dumpbrom             导出 BootROM
```
⇒ 不依赖相机上任何软件，彻底解决变砖。

## 参考实现（A7M4 / CXD90057）

上游 `ILCE-7M4-RE/payloads/fastboot/` 有完整实现：
```
src/           USB PHY、DWC3、gadget、fastboot 协议
fastboot.ld    链接脚本（AArch64）
cetus/         另一套（含 ospi/spi/uart/mbox 驱动）
```
⚠️ 它是 **AArch64**；ZV-E10 是 **ARMv7 (CXD90045)** ⇒ 不能直接编译，但协议层与流程可移植。

## 三个必须先解决的前置条件

### ① 载荷大小（最关键，未解决）
```
fastboot payload 至少 5 KB
我们的注入目前 >1.2 KB 就失败
但同族 A7C 实测能发 5,149 B（2 条 MEM_WRITE：4086 + 1063）
⇒ 说明 ROM 支持，问题在我们注入侧
⇒ 优先排查：载波同步波形（笔记：慢块 slow=12 → 250 µs，必须落在 225.4–293.6 µs）
```

### ② USB 控制器基址与 PHY 初始化
```
可从两处挖：
  a) 设备自带明文 loader（EXBL，入口 0xFE047000）里的 USB 相关初始化
  b) 内核源码：linux/drivers/usb/dwc3/ + arch/arm/mach-cxd900x0/
     （本机有 /mnt/build/src/a7c-kernel/linux）
需要确认：DWC3 基址、USB PHY 基址、时钟/复位寄存器
```

### ③ ARMv7 侧实现
```
选项 A：移植 ILCE-7M4-RE 的 fastboot 源码（改寄存器层 + 汇编入口）
选项 B：自己写最小实现：DWC3 gadget + 只支持 peek/poke/flash
建议从 B 开始（更可控），先做能枚举成 USB 设备，再加命令
```

## 分阶段计划

| 阶段 | 目标 | 验收 |
|---|---|---|
| S1 | 修好载波同步，成功发送 5 KB payload | 看到 payload 的 1 秒签名闪灯 |
| S2 | 确认 DWC3 基址 + PHY 初始化，让 SoC 被主机枚举 | 主机 `lsusb` 看到 Sony 设备 |
| S3 | 实现 gadget + fastboot 握手 | `fastboot devices` 能列出设备 |
| S4 | 实现 peek/poke | 能读写 SRAM |
| S5 | 实现 eMMC 读写（复用 loader 里的 MMC 序列） | `fastboot partition dump` 成功 |
| S6 | 写回修复（清 ld.so.preload 的 size 字段） | 相机正常开机 |

## 风险

```
· USB PHY 初始化若缺少参考，调试成本很高（可能需要试错寄存器序列）
· 5 KB 载荷上限若无法突破，fastboot 路线彻底不可行
  ⇒ 所以 S1 是 go/no-go 关卡
```

## 立即可做的

```
1) 载波固件改成笔记要求的慢块波形（250 µs）→ 用 1.4 KB / 5 KB 载荷验证
2) 在内核里定位 DWC3/USB PHY 基址（离线可做）
3) 从 loader 反汇编里找 USB 初始化代码（loader 有 USB 相关字符串可定位）
```
