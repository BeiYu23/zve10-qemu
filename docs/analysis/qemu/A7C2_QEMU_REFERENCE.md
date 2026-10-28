# a7c2-qemu 技术文档要点与 ZV-E10 移植计划

参考：https://github.com/chocotan/a7c2-qemu （docs/tech/01..12，本地 clone: /mnt/build/tmp/a7c2q）

## 1) 它的架构（结论）
- QEMU 11.1.2：`-machine virt,secure=off -cpu cortex-a35 -smp 1 -m 24G(高 DDR 布局)`
  + DTB 修补（chosen bootargs / initrd 起止）
  + `-device ramfb`（显示桥）+ 自定义 `sony-ui-input` 设备（按键注入）
  + 少量 stub（cxd bus / spacc / hwtimer）
- guest：**原厂内核 + 原厂 DTB + 自建 cpio initramfs**
  - initramfs 的 /init 只挂 devtmpfs 起 shell；宿主经串口逐条下发 setup（模块/seed/服务开关）
  - 模块依赖序：devno → osal_utm → osal_uipc → utimer → backup(Backup_load=1) →
    stream/stream2 → ulogio → dmm(DmmCfg.bin) → grm_ma → upm → udate + seed-date-clock
- 用户态：**LD_PRELOAD menu-observe.so** 到 bootin.elf/im.elf
  - 伪造设备/服务（CapID、SA 地址、HDMI、DMM、GUI 模型、RM 文件媒体、SD、音频、CP、加密等）
  - 软件 GPE（gpe-raster.c/gpe-sprite.c/sony-lz77.c）：受限 Fill/Copy/RGBA4444 sprite/A8 字体/nearest 缩放
  - LZ77：**QEMU_SOFTWARE_LZ77** 用 sony_lz77_decode 绕过 /dev/ldec
  - headless OSD：在 LIRO 边界接 notifyDraw→drawFramebuffer→CmdGpeFlush→callback，
    产出 640×480 RGBA4444 容器（HOSDAP02），写专用 scanout 区，经 fw_cfg 配 ramfb DMA
- 输入：宿主 GTK → 控制通道 → guest inject-key（原 OSAL API 发 32B App 消息，
  raw→canonical 表 1120 对）；键值表（UP/DOWN/MENU/ENTER/REC/MODE 等）已校准
- 首设：语言/日期由原 GUI 操作验证；拍摄 controller 屏 id=0x343 为到达判据
- 当前边界：取景仍黑（CMOS 未模拟）、照片/录像未通过；menu/HUD 可操作

## 2) 对 ZV-E10 的直接可复用点（优先级）
1. **软件 LZ77 解码**（sony-lz77.c）替代 ldec/硬件解码：我们 UI 资源解码很可能就是问题源
   （我们 patch 了 dmac 让 ldec_exit 返回，但真实解码数据未验证）。
2. **软件 GPE 光栅化**（gpe-raster/sprite）：把我们 App 提交的 GPE 命令直接渲染到 VDF 物理地址
   `0x20312000`（我们的 bionz-streamfb 已在显示该区域）→ 不需要 ramfb/fw_cfg。
3. **服务开关/伪造**思路：在 im.elf 里用 preload 替换 osal_snd_msg/osal_snd_sync_msg 的目标 qid 应答
   （我们已有 fake 0x8f013a 的经验），按他们的服务清单逐个补齐（date/DMM/GUI model/CP/HDMI…）。
4. **输入注入**：用原 OSAL API 向 App 队列发 32B 消息（我们已有 InputServant qid 0x14a0429 线索，
   还需要找到 App32 队列/raw 键码表）。
5. **首设/日期**：seed-date-clock 等价物（我们 udate.ko 已加载，但需确认日期服务初始化成功）。

## 3) 与现有 ZV-E10 工程的差异
- 我们机器是自定义 `cxd90045`（ARMv7/3.0.27 内核），不是 virt/cortex-a35：保持现机器，仅借 guest 侧做法。
- 我们已有：真内核+真 initrd+真模块、dmac patch、FK8、viewUnified2 init_array、openView、VDF streamfb；
  缺的是：UI 绘制链路（从未产生 GPE 提交）与资源解码正确性。
- 结论：下一步优先级 = ①软件 LZ77 ②服务伪造最小集 ③软件 GPE 到 VDF ④输入注入。

## 4) 下一步实验（单 QEMU）
A. 确认资源解码路径：在 im.elf 内 hook 解码入口（LIRO/ldec 相关），对比解码输出与文件原始字节；
   若失败，接入 sony_lz77_decode 软件解码。
B. 在 libObj 的 GRM_* 导出处做 GOT hook（我们已有 GRM 入口偏移），把 Fill/Copy/Sprite 命令
   软件渲染进 VDF 0x20312000；用 bionz-streamfb 验证像素。
C. 按 a7c2 服务清单补齐 UIPC 应答（从我们的 UIPCARG qid 统计出发，逐个 fake）。
D. 输入注入：定位 App 输入队列 qid/消息布局，先实现 MENU 键。
