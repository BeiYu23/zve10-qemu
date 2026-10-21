# ZV-E10 QEMU 菜单可操作 —— 移植路线图（a7c2-qemu 参考路线）

> 本文件是**上下文锚**：每隔几轮更新一次状态。所有步骤的结论也写入 QEMU_BOOT_PROGRESS.md。
> 参考：https://github.com/chocotan/a7c2-qemu （MIT；docs/tech/01..12）
> 本地参考 clone：/mnt/build/tmp/a7c2q ；已拷入 /root/fw/zvport/src（MIT 源文件：gpe-raster.c、gpe-sprite.c、gpe-copy.h、gpe-sprite.h、sony-lz77.c）

## 目标与边界
- 目标：ZV-E10（CXD90045/ARMv7，内核 3.0.27）在 QEMU 中显示原厂界面并能操作菜单。
- 边界：只跑 QEMU；不刷机、不改真机；一切可回滚；单 QEMU 运行、结束 kill。
- 现状锚点（2026-10-21）：
  - 已打通：真内核/initrd/模块、dmac patch（LDEC_EXIT 不再卡）、FK8（0x8f013a 假应答）、
    viewUnified2 init_array（529 次 add / 328 唯一）、openView（static=pVM 后 ret=0）、
    VDF 显示通道（bionz-streamfb，phys 0x20312000）、QMP 调参（info registers / x/ VA 读取）。
  - 已知遗留：UI 线程推进但无 GPE 提交（ZZGPEREG=28 不变）、VDF 无像素；
    2 节点环形链表（dedup 后 lowlog 不复现）；部分服务缺失（UIPC sync 超时）。

## 里程碑（M1→M5）
- **M1：服务面摸底**：枚举 im.elf 所有 osal_snd_sync_msg/snd_msg 的 qid、返回码、调用者（FK8 基线），
  找出仍缺失/超时的服务；对照 a7c2 服务清单（CapID/SA/HDMI/DMM/GUI model/CP/date/display info）。
  出口判据：得到"缺失服务清单 + qid"表。
- **M2：最小服务伪造**：在 preload 中按表逐个 fake（返回成功/构造合法响应），直到 UI 线程
  不再等待缺失服务（PC 采样不再卡在等待路径）。
  出口判据：UI 线程出现新的推进（PC 进入 view/create_screen 或 GRM 调用）。
- **M3：软件 GPE → VDF**：hook libObj 的 GRM_* 导出（Fill/Copy/Sprite），用 gpe-raster/gpe-sprite
  渲染到 VDF 物理 0x20312000；用 streamfb/screendump 验证像素。
  出口判据：VDF 出现非 test pattern 像素。
- **M4：资源解码**：确认 Lz77Decoder/资源路径是否依赖 /dev/ldec；必要时用 sony-lz77.c 软件解码替换。
  出口判据：资源解码成功计数（无失败）。
- **M5：输入注入**：定位 App 输入队列（已知 InputServant qid 0x14a0429；a7c2 用 App32 32B 消息），
  注入 MENU 键并验证画面变化。
  出口判据：MENU 键被原 App 消费（画面变化/日志）。

## 当前任务（进行中）
- [M1] 服务面摸底：写 c190 探针（基于 c169/FK8）：interpose osal_snd_sync_msg + osal_snd_msg，
  记录 (qid, ret, lr) 前 N 条并按 qid 汇总；跑 120–180s，输出"qid→ret 分布"。
- [M3 预备] 移植 gpe-raster/gpe-sprite 到 ARMv7 编译（-mfloat-abi=hard 当前工具链），
  写最小自测（纯内存 fill/copy）。
- [M4 预备] 找 libObj 中 /dev/ldec 使用者与 Lz77Decoder 调用点（静态反汇编定位）。

## 关键事实/地址（持续补充）
- qid：0x3e014d backup、0x8f013a ulog（FK8 假应答）、0x14c0424 grm_ma、0x14a0429 InputServant、0x94016a、0x80003b、0x20 无效。
- libObj 基址每次运行变化（如 0x59d4000 / 0x59cf000）；偏移以 libObj_cur.so 为准。
- VDF phys：0x20312000（640×480，streamfb 呈现）。
- viewUnified2 RX/RW 偏移公式：RW=RX+0xB80000；init_array=RX+0xB80AF0（1768）；_init=RX+0x185CC8。
- 我们的 preload 注入方式：bootin 包装脚本 + /lib/libgrmlog.so（pthread worker + dlsym）。

## 记录纪律
- 每完成一个实验：更新本文件"当前任务/关键事实"，并在 QEMU_BOOT_PROGRESS.md 追加一节（含产物路径、判据、结论）。
- 所有临时文件放 /mnt/build/tmp；单 QEMU；结束 pkill -f qemu-system-arm 并确认。

## 进度更新（2026-10-21 18:00）
- [M1] 已派 init-unlocker：c190 服务面摸底（osal_snd_sync_msg/snd_msg qid→ret 汇总，180s）。
- [M4 预备] 已完成：
  - 确认 libObj 内含 `/dev/ldec` 字符串（0xfce5c2, rodata）与 `ux::core::Lz77Decoder` RTTI；
    说明 UI 资源解码很可能走 Lz77Decoder→/dev/ldec（与 a7c2 文档一致）。
  - 参考代码已拷入 `/root/fw/zvport/src`（MIT）：gpe-raster.c/gpe-sprite.c/gpe-copy.h/gpe-sprite.h/sony-lz77.c；
    已用 ARMv7 工具链编译通过（test_render）。
  - 已写 c191 preload（`/root/fw/zvport/src/c191_preload.c`，/mnt/build/libgrmlog-c191.so）：
    interpose open/open64/ioctl 记录 /dev/ldec、/dev/liro、/dev/dmpgles*、stream 的打开与 ioctl；FK8 fake 0x8f013a。
    注意：c191 未含 init_array（pVM 不会出现），仅用于观察“正常启动阶段是否打开 ldec/liro”。
  - [M4 下一步] 需要把 open/ioctl 记录并入 c169（FK8+IA）基线后再跑 180s：看 IA 后资源解码是否打开 /dev/ldec、ioctl 返回码。

## M1 结果（2026-10-21 18:10，c190/c190b，180s，单 QEMU）
- 汇总（hex qid / n / ret）：见下表；libObj base=0x59CE000（c190b）。
```
0x8F013A n=763  ret0=763            ← FK8 fake 生效
0x3E014D n=6727 ret0=6727           ← backup 正常
★0x94016A  ret=-1020                ← 缺失（首 run）
★0x924127 n=23  ret0=0 retM1020=23  ← 全部超时
★0x1010227 n=2  ret0=0 retM1020=2   ← 全部超时
★0x13E8402 n=1  ret0=0 retM1020=1   ← 超时
0xC881BB n=3 ret0=3 ; 0x12182ED n=1 ret0=1 ; 0x12282F6 n=4 ret0=4
0xCA8192 n=2 ret0=2 ; 0x25906BE n=3 ret0=3 ; 0x14C0424 n=69 ret0=69 ; 0xCF016D n=14 ret0=14
```
- 异步（snd_msg）全部 ret0，无 -1020。
- 静态线索：
  - 0x1010227 调用者 lr 偏移 ≈ 0x8C0E41（libObj，靠近 infra_indctr 区）；
  - 0x13E8402 调用者偏移 ≈ 0x408645，且该 qid 常量也出现在 0x408710 附近（同一功能域）；
  - 0x924127 常量出现在 0x8C17A4 / 0x89AE3C 等。
- 结论：M1 完成，缺失服务 = 0x94016A、0x924127、0x1010227、0x13E8402（另有 0x8F013A 已 fake）。
- 下一步（c191，已派）：对这 4 个 qid 假返回 0，并打印消息头（前 8 word），跑 240–300s 看 pVM/ML/ZZ/VDF/新阻塞。

## M2 进展（c191，2026-10-21 18:15）
- 对 4 个缺失 qid 假返回 0：全部变 ret0（0x924127 23 次、0x1010227 2 次、0x13E8402 1 次、0x94016A）；
- pVM 非 0（i=36 起 1,425,040），但 **ZZGPEREG 仍 28、screen 未变、无 ML**；
- **出现新超时 0x25906BE（n=1,-1020）** ⇒ 服务依赖链往后推进一层，需继续解。
- 消息头线索：
  - 0x924127: `w0=0x00920111 w1=0x00000403 w2=1 w3=0x395 w4=5`
  - 0x94016A: `w0=0x009400E1`
  （首 word 形如 0x00xx00XX，像服务内部 message type）
- 下一步（c192，已派）：**auto-fake** —— 先真实调用，凡 ret==-1020 一律改返 0 并记录，
  一次扫出全部缺失服务；观察 pVM/ML/ZZGPEREG/VDF 是否解锁绘制。

## M2 后半（c193 auto-fake，2026-10-21 18:25）
- auto-fake（真实调用后 ret==-1020 改返 0）后：**同步超时全部清空**（0x94016A/0x1010227/0x924127/0x13E8402）；
- 仍 **ML=0、ZZGPEREG=28、VDF blank**；pVM 非 0；无 ring；
- **发现 app 内部 abort/assert**（探针捕获 abort，lr=libObj+0x64029B）；
- abort 栈 LR 换算（以 base=0x59D1000）得到候选偏移：
  - 0x64129a → `ux::core::GraphicsImage::sys_getInternalObject+0x14e`
  - 0x85748e → `libra::String::operator=+0x3658`
  - 0x85c34c → `libra::ImageData::getWidth+0x1a46`
  - 0x64031e → `ux::core::Log::write+0x178e`
  - 0x69d4e2 → `BMP_Create+0xada`
- 判读：abort 发生在**图像/位图资源创建链路**（BMP/ImageData/GraphicsImage），
  与 a7c2 文档"资源 LZ77 解码失败导致显示异常"高度吻合 ⇒ **M4（资源解码）很可能是当前真正阻塞点**。
- 下一步（c194，已派）：在 c193 基础上加
  1) open/open64/ioctl 记录 /dev/ldec、/dev/liro、/dev/dmpgles2、stream；
  2) interpose `BMP_Create`（libObj 导出）记录 (args,ret)；
  3) abort 栈扩展（256 words）+ /proc/self/maps 全量 dump，定位 0x04097146 属库；
  4) 180s，报告 ldec/liro 活动、BMP 返回码、abort 栈与 maps。

## 显示通道自检（c195，ab_v151）— 2026-10-21 18:28
- 目的：验证"我们直接写 VDF 物理 0x20312000，bionz-streamfb 能否显示"（决定软件 GPE 输出方案）。
- 实现：`/root/fw/zvport/zvobs.so`（ARMv7 preload，替换镜像内 /lib/libgrmlog.so）：
  - constructor 建 pthread；15s 后 open("/dev/mem") + mmap2(0x20312000, 640*480*4)；
  - 填蓝/红 16px 竖条纹（32bpp），打印 `ZVOBSP loaded / PAINT mapped / PAINT ok`。
- 镜像：`/mnt/build/ab_v151.img`（= ab_v95 + n7 替换 libgrmlog.so + dmac 由 initrd_q4 提供）。
- 判据：串口出现 PAINT 标记；QMP screendump 在 30s/50s 是否从 test pattern 变成条纹。
- 若成立 → 软件 GPE 直接写 VDF 即可，无需 ramfb/fw_cfg。

## 显示通道确认（c195b，2026-10-21 18:45）
- **VDF 直写成功**：用户态 mmap /dev/mem phys 0x20312000 并填 32bpp 蓝/红竖条纹，
  QMP screendump T=30s 读到 16px 条纹颜色正确 → **bionz-streamfb 通道可用，软件 GPE 直接写 VDF 路线成立**（无需 ramfb）。
- 注意：单独用 zvobs.so 替换 /lib/libgrmlog.so 会因**缺 fa keys（fa0028/fa005d）**在 5.5s MonQueue 崩；
  双 preload（原 c105 探针 + zvobs）可用。
- c194 关键发现：
  - 未观察到 /dev/ldec、/dev/liro 被打开（资源解码不走 ldec？或未到阶段）；
  - `BMP_Create(a=0x5AB20, b=640, c=480, d=1) → ret=0（失败）` ← 黑屏/assert 的直接嫌疑；
  - 0x04097146 属 **libpthread-2.11.2.so**（0x04094000-0x040A4000），是 futex/spin 热点，不是 ld.so。

## M3 新计划：用户态 GRM/BMP 垫片（a7c2 风格）
- 目标：在 preload 里接管 libObj 导出的 GRM_* 与 BMP_Create 相关路径，用自管 bitmap + 软件光栅，最终让 screen bitmap 指向 VDF。
- 第一步（c196，取证）：interpose 并记录
  - `BMP_Create`
  - `GRM_bitmapCreate` / `GRM_bitmapCreateComposite` / `GRM_bitmapGetPhysicalAddress` / `GRM_bitmapGetRowPixelCount` / `GRM_bitmapWriteLockBits` / `GRM_bitmapWriteUnlockBits`
  - `GRM_gpermFillrect` / `GRM_gpermRectblit` / `GRM_screenGetOnBitmap`
  记录参数/返回值（前 40 次），保持转发；看 BMP_Create 失败卡在哪个 GRM 调用。
- 第二步：用真实实现替换（fake bitmap 结构 + fill/copy + screen→VDF）。

## M3 推进（c197，2026-10-21 18:55）
- c197（仅 BMP_Create C interposer，基于 c193）：
```
BMPC a=0x55468 b=640 c=480 d=1 ret=0xFFFFD8F1 (-9999)
AAB3 lr=libObj+0x64029B → abort（约 9–10s）
```
- c194 同一调用 ret=0（run 间不同但都失败）⇒ **BMP_Create(640x480) 失败是早期 abort/黑屏直接原因**。
- c196 的 naked 汇编 hook 方案失败（app 线程全退），改用已导出符号的 C interposer。
- 关键符号（libObj 导出）：
  - `BMP_Create` (0x69ca08)
  - `_ZN2ux6resmgr15ResourceManager11createImageERKN5libra16CreateImageParamEPNS2_9ImageDataE` (0x61082c)
  - `_ZN2ux6resmgr15ResourceManager7requestEjPFvPvPN5libra9ImageDataEES2_S5_h` (0x6105a8)
  - `_ZN5libra9ImageDataC1Ev` (0x85a616) / detach / sys_getImageData / getPixelFormat / getResID / getWidth / getHeight
  - `_ZN2ux4core13GraphicsImage21sys_getInternalObjectEPv` (0x64114c) / clear / copy
- 下一步（c198，已派）：C interpose 上述函数记录参数/返回，定位 -9999 来源分支。
- 另：`ab_v153.img` = ab_v95 + 带 fa keys 的 zvobs 单 preload（可验证不崩 + PAINT）。
