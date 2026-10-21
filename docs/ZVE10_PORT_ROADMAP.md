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
