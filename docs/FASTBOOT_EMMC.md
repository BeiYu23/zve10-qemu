# 硬件 fastboot / eMMC 通道：从注入到真机救砖（2026-10-22）

> 本文档记录 2026-10-22 一晚完成的硬件通道闭环：BootROM 串口注入 → USB
> fastboot payload → stage2 eMMC **读+写** → **真机救砖成功** → 看门狗根因
> 定位与修复。实现与全部源码在工具库 **zv-e10-toolkit**（本地
> `/root/fw/zv-e10-toolkit`，GitHub: `dino2048/zv-e10-toolkit`），本文只留
> 结论与关键技术事实。

## 里程碑（全部真机验证）

| # | 里程碑 | 证据 |
|---|--------|------|
| 1 | USB 枚举成功：payload 以 `18d1:4ee0` 出现，`getvar:version` → `OKAY0.4` | toolkit `fastboot/docs/usb-success.md` |
| 2 | stage2 eMMC 读取：识别（OCR `0xC0FF8080`、RCA=1、CID/CSD）+ SDM2 分区表 + `nflasha10` FAT 引导扇区逐字节正确 | `emmc-success.md` |
| 3 | 写路径打通：CMD24 单块 + CMD25 多块，"死空间写图案→读回→还原"验证 | `write-success.md` |
| 4 | **真机救砖**：`/etc/ld.so.preload` size 字段 20→0（净变化 1 字节，六道门校验），相机恢复正常 UI | `fix-record.md`、`preload-scan.md` |
| 5 | 看门狗根因：App 经 `/proc/wdt` 武装的 TIMER-IP WDT 跨 XRESET_REQ 幸存，~3m40s 杀死会话；payload 启动即关 | `wdt-fastboot-exit.md` |

## 关键技术事实（后来者必读）

1. **扇区寻址，不是字节寻址**。本机 CMD17/24 的参数是 512 B 扇区号：
   `nflasha10` 用户区起点 `0x0AC00000` = block `0x56000`。早期救援 payload
   按字节地址 + DMA 描述符尝试（`aux_fix_t1..t34`），数据阶段始终没通，
   根因就是这两个假设都错了。正确路径是 **PIO**：CMD17/24 + block count +
   等 buffer-ready 后从数据端口读/写。

2. **XRESET_REQ 只复位 AP，不清外设域**。这带来两个后果：
   - MISC_RSTREQ_EN 与正在倒计时的 WDT 定时器**幸存**（见下）
   - eMMC 控制器状态跨复位保持，识别序列（CMD0/1/2/3/7/13）每次注入后重跑即可

3. **看门狗（3m40s 自动退出的根因）**：App 通过 `/proc/wdt` 的
   `WDT_SET_START` ioctl 武装 TIMER-IP 看门狗（`0xF2402000`，WD 时钟
   1 MHz，超时经 MISC RSTREQ `0xF2915040` 一带发复位请求）并周期性重装。
   真机 cmdline 带 `wdt.timeout=500`；内核态看门狗默认 OFF（`wdt.mode`
   未置位）⇒ 是 App 在用。修复 = payload 第一条指令执行内核自己的
   `WDT_SET_STOP`：清 RSTREQ_EN、停定时器、清中断
   （`0xF2915048←0x10; 0xF2402000←0; 0xF2402004←0x11`）。
   ILCE-7M4-RE / Powerful6700 均无此处理——ZV-E10 是已知唯一案例。

4. **注入预算 8 KB**：BootROM 串口直注窗口 8,192 B。当前 payload
   6,516 B（GNU 构建）/ 7,908 B（clang），stage2 另走运行时下发（v3 为
   1,860 B），不受此限。

5. **安全模型**：stage2 默认只读，参数块 `[2]=1` 才写（老脚本永远触发
   不了写）；识别未达 TRAN 态拒绝写；每次真实修改遵循 ground-truth
   纪律（先取证、写后逐字节读回、终检全量扫描）。

## 救砖实战摘要

相机因 preload 指向坏 agent 变砖（见 `ANTIBRICK_SYSTEM.md` 的机制分析）。
本次用新通道执行了 10-18 在 QEMU 里端到端验证过的"档 2"修复：

- BPB 驱动（无固定偏移）扫描根目录，精确匹配到 `ld.so.preload`
  （entry 35，LFN，簇 393，size=20）
- 六道硬门：boot/BPB 校验 → 三块 ground-truth 逐字节比对 → 本会话写路径
  健康探针 → 全根目录精确匹配重扫 → 写（与 GT 恰差 1 字节，两次读回）→
  终检（FAT 链、邻块、全根目录 delta=1 字节）
- 净变化：`0x0AC00C7C` 一处 `0x14`→`0x00`；相机断电重启后正常出 UI

恢复手段三重：`restore_preload.py`（GT 块写回）、相机侧
`cp ld.so.preload.orig ld.so.preload`、整分区镜像。

## 这条通道的意义

- **与磁盘系统完全独立**：只要主板能上电、电池在位就能注入 ⇒ 分区级
  救援通道（任意分区读/写/回滚），不再依赖相机能启动
- **解锁菜单移植的刷写路径**：ZV-1F 新菜单移植（见 `ZVE10_PORT_ROADMAP.md`）
  需要重建 `nflasha15`（/usr，300 MiB）——单块写太慢，stage2 v3 已加
  CMD18/CMD25 多块 + ~25 MHz 数据时钟（多块自检待下次硬件会话）
- **双通道互为兜底**：USB 服务模式（系统正常时）↔ 硬件 fastboot（任何时候）

## 下一步（按序）

1. 硬件会话：stage2 v3 多块自检（`stage2_selftest.py`）+ 看门狗修复验证
   （旧 payload 抓武装现场 → 新 payload 会话 >5 分钟）
2. Step 0 基线：`n15_check.py` 抽样比对真机 `/usr` vs 10-16 整盘镜像
3. QEMU 路线 A：hybrid `/usr` 镜像挂进固件自带 qemu profile，提前暴露
   PRM/RTLD 接缝问题
4. 真机最小步刷写（QEMU 验证后）

## 相关文档

- `ANTIBRICK_SYSTEM.md` —— 防砖体系与 preload 事故机制分析
- `GEN1_BOOT_HARDWARE_TYPEC.md` —— Type-C / RP2040 接线
- `zve10-uart-boot/` —— 本通道的前身（UART 载荷实验）
- toolkit 的 `fastboot/docs/` —— 五份成功记录 + 硬件手册 + 协议文档
