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

## M3 内部分支定位（2026-10-21 19:05）
- BMP_Create 内 **-9999（0xFFFFD8F1）** 的 4 个 literal 返回点：
  - 0x69D210 / 0x69D27E / 0x69DB2C / 0x69DE10（均为“fprintf 错误日志后返回 -9999”的错误分支）
  - 0x69D27E 错误来自 0x69D27A 调用 **0x767840** 的返回值==0 分支；
  - 0x69DB2C 错误来自 0x69DB0C 的 **vtable 间接调用（+0x64 方法）返回 0**；
  - 0x69DE10 错误来自 **pthread_mutex_init 返回非 0**（0x69DDFE）。
- 另：abort 直接 caller lr=0x64229B 对应 0x642296 `bl 0x860A78`（libra 图像参数/析构域），
  abort 实际在更深调用链中（AAB3 栈）——需要 inline hook 才能精确定位。
- 结论：BMP_Create 失败可能来自 ①0x767840 返回 0 ②vtable+0x64 返回 0 ③mutex_init 失败，
  需 inline hook（mprotect+8 字节跳板）trace 才能确定；已把实现方案交给 init-unlocker。
- inline hook 方案要点（已下发）：
  - patch: `f8df f000`(ldr.w pc,[pc]) + word(stub|1)；trampoline: 原 8 字节 + `f8df f000` + word(addr+8|1)；
  - stub asm: `push {r0-r3,r12,lr}; bl c_logger; pop {r0-r3,r12,lr}; ldr r12,[pc]; bx r12`；
  - mmap RWX 缓冲放 trampoline；mprotect 文本页 RW → patch → 恢复 RX；sys_cacheflush(0x0f0002)。
  - 先 hook：BMP_Create、0x69B38C、0x69B440、0x61082C(createImage)、0x69D27A 目标 0x767840、0x860A78。

## M3 关键进展（c203/c204/c205 计划，2026-10-21 19:30）
- **inline hook 机制成功（单目标）**：c203 hook BMP_Create：
  - patch: `df f8 00 f0` + word(stub|1)；trampoline = 原 8 字节 + 跳回 target+8；mprotect/clear_cache；
  - Thumb stub 必须在独立 .S（`.thumb`），否则 naked asm 编成 ARM → illegal instruction；
  - 仅前 8 字节 PC 无关的函数可 hook（BMP_Create ✓；0x69B38C/0x69B440 ✗ 含 ldr [pc]）。
- c203 观测：BMP_Create 被反复调用 20+ 次，参数固定 `a0=0x5E4C8/0x52E30/0x55468（每次 run 不同，疑似 ctx 指针）, 640,480,1`，
  caller `lr=libObj+0x6F8F85`。
- 反汇编 **0x6F8F80** 调用点：
```
blx BMP_Create@plt
ldr r3,[r6,#8]; cbz r3 → 返回0
cbz r0 (bmp==0) → 6f8fae 另一分支
... log; bl 0x76768C; cbz r0 → **return 4（重试）**；否则 abort
```
  ⇒ 重试循环的原因就是 BMP_Create 返回 -9999（非 0 且无效）。
- **-9999 的 4 个返回点**（literal 池）：
  - 0x69D21C（分支A）、0x69D290（来自 0x767840 返回0，分支B）、0x69DD7C（vtable+0x64 返回0，分支C）、0x69DE4C（pthread_mutex_init 非0，分支D）
- ab_v204.img（Lead 已构建）：把上述 4 个 literal 改为 **-10001/-10002/-10003/-10004**，
  配合 c205 的 exit 日志即可确定命中分支。
- c204 多 hook 失败（加载即 exception），后续保持**单目标 hook**。
- 下一步（c205）：单 hook BMP_Create + exit 日志（记录返回值）+ 限流（防串口洪水）→ 跑 ab_v204 找出 -10001..-10004。

## 上下文锚（2026-10-21 19:40）
- 当前在等：c205（BMP_Create exit 日志）结果 → 用 ab_v204 的哨兵值确定 -9999 具体分支。
- ab_v204.img 已验证 n15 /lib/libObj.so 四个 literal = -10001/-10002/-10003/-10004。
- c205 首次运行未出现 HINST/HOOK（hook 未安装），需队友修 exit-stub 后重跑。
- 一旦确定分支：
  - 分支B(-10002)：0x767840 返回值问题 → inline hook 0x767840 或 patch 判断；
  - 分支C(-10003)：vtable+0x64 方法返回 0 → 需要对应设备/模型对象；
  - 分支D(-10004)：pthread_mutex_init 失败 → preload 里 interpose pthread_mutex_init 强制返回 0（低成本试探）。
- 之后目标：BMP_Create 成功 → UI 创建 screen → 软件 GPE/VDF 输出。

## M3 根因解码（2026-10-21 20:00）
- 用"唯一化全部 -9999 literal"法（c208）确定 BMP_Create 失败来自 **0x69B5A8**（index 24）：
```
69b558: bl 0x7676C4 ; cbz r0, 正常; 否则 log 并返回 -9999
```
- **0x7676C4 的真正失败条件**（反汇编）：
```
0x7676C4: if (ctx==0) return -2
           r = 0x767D7C(ctx)
           if (r) return r
0x767D7C: if (memcmp(*(u32*)(ctx+4), MAGIC, 4) != 0) return -9999
          MAGIC bytes @0x1007834 = 45 00 ca fe  (0xFECA0045)
```
- ⇒ **BMP_Create 的 ctx+4 magic 不匹配**（调用方上下文类型/初始化不对）。
- c209 整函数 patch 0x7676C4 → 早期崩（该函数早期也用）；
- c211 实验（Lead 已构建 ab_v211）：只把 0x767D92 `movne r0,r3` → `movs r0,#0`，
  让 magic 检查总是通过（窄范围），观察 BMP_Create 是否继续、后续是否崩在错误 ctx 上。
- 待办：c211 运行结果；若继续 → 决定修调用方 ctx 还是保留放宽；同时 hook 记录 ctx+4 实际值。

## M3 更精确的失败点（2026-10-21 20:15）
- 重要修正：magic 期望值不是 0xFECA0045。memcmp 目标地址按 Thumb `add r1,pc`（PC=Align2）计算得 **0x1007836**，
  该处字节 = **CA FE FE ED（0xEDFEFECA）**，与 ctx(+4) 的 CAFEFEED **一致** ⇒ magic 检查实际**通过**。
  （0x1007834 的 "45 00 ca fe" 只是 "ImplE\0" 字符串尾部与 magic 前半重叠产生的错觉。）
- 因此 BMP_Create 的 -9999 来自 `0x7676C4` 的 **tail-call 0x7685A8 的返回值**（非 magic、非 -2）。
- `0x7685A8` 反汇编关键：
```
r4 = ctx; r6 = ctx+0x58; lock(r6)
if (*(u32*)(ctx+16) != 16)  → r9 = -9999  → return -9999   ← 主要失败分支
else:
  ... r9 = 0x76858C(...); if (r9) → 跳到最后 return r9（也可能非 0）
  ... 成功则 [r5]=0、ctx+20++，返回 r9(=0)
```
- ctx dump 曾显示 `*(ctx+16)=0x10`（=16），所以可能不是该分支，而是 0x76858C 返回非 0
  （或 dump 的字段索引与 +16 对应有偏差，需再次核对）。
- c213 计划：单 inline hook **0x7685A8**（前 8 字节 stmdb+mov，安全）做 entry+exit 日志：
  - entry 打印 ctx、ctx+0x10、ctx+0x58 lock 状态；
  - exit 打印返回值（0 还是 -9999）；
  - 若返回 -9999 且 ctx+0x10==16 → 说明别处覆盖了 ctx+16 或 dump 偏移有误，再抓完整 32 word。

## M3 hook 机制修正与下一步（2026-10-21 20:30）
- Thumb trampoline 关键 bug 已修：跳 trampoline 必须 `bx (trp|1)`，否则按 ARM 解码 → illegal（c213 崩因，也可能是 c204 多 hook 失败原因）。
- c214（单 hook **0x7685A8** entry+exit，ab_v203 基线）：120s 内仅 1 次：
```
E58 ctx=0x000B4410 c16=0x00000010 lock=0x06D8F188 r1=0 r2=0 r3=0x0DAC4BFC lr=libObj+0x6BCBE1
E58 ret=0x00000000
```
  - 该调用来自另一路径（lr=0x6BCBE1），**不是 BMP 路径**（BMP 路径若走 0x7676C4 tail-call，lr 应在 0x69Bxxx）；
  - 说明 BMP 路径很可能在 0x7676C4 内**提前返回**（未到 tail-call 0x7685A8）。
- 但注意：c208 的“唯一化 literal→index→地址”映射可能有偏（index 24→0x69B5A8），
  因此需用 **BMP_Create 自身 exit 日志** 直接确认实际返回值，再决定路径。
- 下一步（c215，多 hook，两 trampoline 均 thumb|1）：
  1) BMP_Create：entry(参数) + exit(返回值)
  2) 0x7685A8：entry+exit
  观察 BMP 调用期间是否有 E58、以及 BMP_Create 实际返回码（-9999 / -2 / 其它）。

## M3 链路完全确认 & 新根因方向（2026-10-21 20:55）
- c216/c218 双 hook（BMP_Create + 0x7685A8，entry+exit）决定性数据：
```
HBMP_IN  a0=0x5f9d8 640 480 1 lr=libObj+0x6F8F85
HBMP_OUT ret=0xFFFFD8F1 (-9999)
0x7685A8 前5次：早期 layout 初始化，ret=0
第6次（=BMP ctx 0x5f9d8）：r1=0x000e1000 r2=640 r3=0x5fab0 lr=0x69B55D
    E58_OUT ret=0xFFFFFFFF (-1)
```
⇒ 链路：BMP_Create → 0x7676C4 → tail 0x7685A8(ctx) → 返回 **-1** → BMP 视为失败(-9999)。
- 0x7685A8 中 ctx+16==16 满足 → 走 `r9 = 0x76858C(iter)`，非零 → 返回 r9(-1)。
- 0x76858C 关键：
```
r3=[iter+8]; if(!r3) → 返回 -9999
bl 0x768208(iter)      ; 0x768208: osal_snd_sync_msg(id=[iter+12], msg=[iter+8])  ← UIPC 同步消息！
r3=[iter+4]; return [r3+16]     ; ← 返回的结果字段，初始 -1，成功应答后置 0
```
- ⇒ **根因很可能是：我们 auto-fake 的同步消息只返回 0，但没有把“应答数据”写回消息/结果字段，导致 [iter+4]->[+16] 仍为 -1**。
- hook 机制经验（已修）：
  - Thumb 位：`bx (tramp|1)`；
  - exit stub 必须保护返回值（push {r0,r4,r5,r7} → logger → pop）。
- 下一步（c220）：hook `osal_snd_sync_msg`（C interposer，安全）记录 qid/w0..w3/lr，与 BMP_Create 调用做时间关联，
  找出 0x768208 发出的那条同步消息用的是哪个 qid、期望什么应答；随后构造/伪造正确的应答数据。

## M3 c220 结果与下一步（2026-10-21 21:05）
- BMP 窗口内抓到唯一同步消息：
```
BMPWIN qid=0x014C0424 msg=0x046E2FCC lr=0x0613B59B ret=0
pre = 0x00000009 0x609828E0 0x000E1000 0x00000280
post= 同（前4 word 未变）
```
- qid **0x14C0424 = grm_ma 服务**（此前 QSYNC 统计 69 次 ret0，正常服务）；
- 0x768208 反汇编确认原型：`r0=[iter+12]`、`r1=[iter+8]`，`b.w osal_snd_sync_msg` → **只传 (id, msg) 两个参数**（r2/r3 是调用者残留）；
- 0x76858C 完整逻辑：
```
push{r3,r4,r7,lr}; r4=iter; r3=[iter+8]; if(!r3) → return -9999
bl 0x768208(iter)                     ; 发同步消息
r3=[iter+4]; r0=[r3+16]; return       ; 读状态字段（初始 -1，成功应为 0）
```
- 结论：消息发送成功(ret=0)，但 `[iter+4]+16` 未变 0 ⇒ **应答/回调没有把状态写回**（可能服务端处理是异步回调，或我们环境里该服务进程/回调缺失）。
- 下一步 c221（极短窗口 hook）：
  1) hook BMP_Create entry/exit 做 arming；
  2) hook 0x76858C entry：当 armed 时，写 `*(u32*)(*(u32*)(iter+4) + 16) = 0`（诊断性强制成功），记录原值；
  3) 观察 BMP_Create 返回值是否变成功、UI 是否推进（pVM/ML/ZZ/VDF）；
  4) 若成功 → 再研究真正的 reply 回调（grm_ma 服务），而不是长期 patch。

## M3 关键布局解码（2026-10-21 21:25）
- 0x768054 反汇编（iter/msg 构建）：
```
0x768054(iter, id=9, qid):
   iter+12 = qid
   iter+4 = iter+8 = 0
   osal_valloc_msg_wait(&iter+8, 44)      ; 分配 44B 消息
   成功: [iter+8-4] = id(9); iter+4 = iter+8   ; ⇒ iter+4 == iter+8 == msg
0x768098: 断言 iter+4 != 0; msg+0 = halfword 9
0x768208: osal_snd_sync_msg(qid=[iter+12], msg=[iter+8])   ; 两参数
0x76858C: 返回 *(u32*)(msg+16)             ; status 字段在 msg+16
```
- c220 只比较了 msg 前 4 word（0/4/8/12），**未检查 +16**；真实语义是服务把 status 写到 msg+16。
- 因此 BMP 失败根因 = **grm_ma 服务应答未回填 msg+16（保持 -1）**。
- c224（已派）：在 `osal_snd_sync_msg` interposer 里对 `qid==0x14C0424 && msg[0]==9` 写 `msg[4]=0`（即 msg+16=成功），
  不改其它 qid、不用 inline hook；观察 BMP_Create 是否成功、UI 是否推进。

## 状态锚（2026-10-23 00:40）
- 真机救砖由另一会话（`救砖`）负责，用户反馈“相机活了”；本会话继续 QEMU 目标，不混线。
- 最新 QEMU 状态（c220–c225 之后）：
  - BMP_Create(ctx,640,480,1) 仍返回 -9999；
  - 链路已确认：BMP_Create → 0x7676C4 → tail 0x7685A8 → 0x76858C → `osal_snd_sync_msg(qid=0x14C0424, msg)`，
    随后 0x76858C 读 `*(u32*)(msg+16)` 作为状态（初始 -1）→ BMP 视为失败；
  - msg 布局（0x768054 反汇编）：iter+12=qid、iter+4=iter+8=msg（44B，由 osal_valloc_msg_wait 分配）、msg+0=halfword 9；
  - 因此“状态在 msg+16”是确定的。
- 下一步 c226（已派 init-unlocker）：在 osal_snd_sync_msg interposer 中对
  `qid==0x14C0424 && *(u32*)msg==9` 写 `((u32*)msg)[4]=0`（=msg+16），观察 BMP_Create 是否成功、下一个阻塞点。
- 备选方案（若状态修复后仍失败/崩）：**用户态接管 BMP_Create**（PLT 可拦截，c197 已验证），
  返回自建 640x480 32bpp bitmap（对象 0x80B，需按 0x69B38C/0x69B440 的结构构造），
  绘制用软件光栅直接写 VDF 0x20312000（显示通道已在 c195 验证）。

## c226（失败，探针 bug）与 c227 计划（2026-10-23 00:55）
- c226：app 在 `address=0x04026008 / trap_no=0x0e / err=0x81f` 崩 —— **地址落在我们 probe 代码区**，
  与 c213 同型（Thumb/ARM 位或 stub 返回错误）→ 探针 bug，不是 app 问题；本版还叠加了 inline hook，先全部去掉。
- c227（最小安全版）：**不装任何 inline hook**，只在 `osal_snd_sync_msg` C interposer 内：
  - 仅当 `qid==0x014C0424` 且 msg 指针落在 0x04000000–0x0C000000 且 4 字节对齐；
  - 读 `msg[0]&0xffff == 9` 时，把 `msg[4]`（=msg+16，状态字段）写 0 并打印 before 值（限流 6 条）；
  - 其余 qid 完全不碰。
- 判据：出现 `FIX16 before=ffffffff` → BMP_Create 是否变成功 → app 是否推进（pVM/ML/ZZ/VDF）；若崩记录 pc/lr/模块。

## ★ 里程碑 c230（2026-10-23 01:10）
- **根因修正**：c194–c229 的失败大量来自**探针自身**（inline hook 的 Thumb/ARM 位）：
  - c226 崩 address=0x04026008（probe 区，trap 0x0e）；
  - c229b 崩 pc=0x04023952（probe 区，cpsr Thumb:off）。
  ⇒ 采用 **无 inline hook** 的纯 interposer 探针（menuc230.c）。
- grm_ma 位图请求消息已验证（11 word）：
  `w0=9(请求id) w1=结果对象指针 w2=0xE1000(921600=640*480*3) w3=0x280(640) w4=status(-1) w6=0x4D w8=0x22BB2000(帧缓冲phys) w9=0x96000(614400=640*480*2)`
- 强制 w4=0 会命中 `AAS MA_ErrNone == error @BitmapImpl.cpp:199` ⇒ 底层 MA 分配真失败。
- **c230 稳态（低日志、无 hook）**：
```
gfx=523248 res=115579488 wg=525368（全部非 0）
pVM=1380064（i=24 起稳定）→ ViewManager 创建成功 ★
openView("view/SCALAR",0) ret=0
dev=dmpgles2 33953、uipc 19398、ldec 1；assert=0；无崩溃；跑满 150s
screendump = QEMU 测试彩条（VDF 无像素）；ZZGPEREG 写入 = 0
```
- 结论：**初始化链全部打通**，差"开始绘制"（GPE 提交 + VDF 像素）。
- 下一步：
  1) c230 长跑 + 仅日志，看 MA 是否自然成功；
  2) 若 MA 恒失败 → interposer 里替身分配（返回我们 mmap 的 640x480 缓冲区 + status=0 + w6=0）；
  3) 软件 GPE（MIT gpe-raster/gpe-sprite）→ VDF 0x20312000 → streamfb 出画面；
  4) 输入注入（raw MENU=0x1ab）实现交互。

## c231（无 hook 稳态复核，2026-10-23 01:15）
- 同 c230 镜像思路（只改探针为"只 dump id-9 消息、不改任何字段"）：
```
pVM=1384632（稳定到 i=56）、无 assert、无崩溃
但 serial 中 0 条 M9（id-9 → qid 0x14C0424 的消息一条都没发）
```
- ⇒ 在**未触发位图创建路径**的运行里，App 可以自然走到 ViewManager 创建且不 assert；
- 说明 c228b 的 MA 断言只出现在"真的要建 640x480 位图"的运行分支上（run 间路径不同）。
- 结论：当前稳态（c230/c231）= 初始化链通、ViewManager 有；差"触发首批绘制"。
- 下一步：找出 App 何时进入"建屏/绘制"分支（可能需要 ViewManager openView + 事件循环消费，或主循环 tick），
  一旦进入该分支，就用"替身分配应答"让 BMP 成功，然后接软件 GPE → VDF → 输入。

## c232：QMP 寄存器采样定位"为什么不画"（2026-10-23 01:25）
- 在 c230 稳态镜像上用 QMP `info registers` 采样 12 次（T≈95s，PC 分布）：
```
11/12 次: PC=0x040A2146（libpthread-2.11.2.so 代码段 0x04094000–0x040A4000，futex/spin）
 1/12 次: PC=0x601438BC（内核 __schedule 区）
```
- 同 run 统计：
```
pVM=1,417,048（i=52/56 稳定，ViewManager 在）
qid 0x14C0424（grm_ma）消息 207 条 → 分配请求在持续发生
dev=stream 打开次数 = 0  ★ 显示管线从未被打开
ZZGPEREG 写入 = 0        ★ 从未提交 GPE 命令
AAS/assert = 0，无崩溃
```
- ⇒ 结论：App 已"活着"（管理器图/view 表/ViewManager 都在），但**没有进入渲染/显示阶段**：
  既没打开 /dev/stream，也没写 GPE。UI 线程停在 libpthread 的 futex 等待（等事件/等输入）。
- 这解释了为什么强制 BMP status 只会命中 MA 断言：**位图创建并不在真正需要的路径上**（207 条请求里没有触发绘制）。
- 输入链路事实（静态反汇编）：
```
snd_InfraInputEvent_to_InputServant(ev):
   qid = 0x014A0429
   msg = osal_valloc_msg_wait(24)       ; 24 字节事件拷贝
   memcpy(msg, ev, 24); osal_snd_sync_msg(0x14A0429, msg)
InputService::createEvent(key, press) / pushFilterEvent 为进程内 API（m_pMyInstance 已是 0，不可用）
```
- 下一步（下轮执行）：
  1) 实现 **UIPC 输入注入**：构造 24B `_InfraInputEvent` → `osal_snd_sync_msg(0x014A0429, msg)`，
     先试 MENU（a7c2 raw=0x1AB）与方向键（0x3/0x30），观察 ZZGPEREG / dev=stream / 截图变化；
  2) 若注入无反应 → 说明 InputServant 的接收线程不在本进程（或未启动），改为检查 0x14A0429 的接收侧
     （哪条线程 rcv_msg 该 qid）并补齐服务；
  3) 一旦出现 GPE 提交，立刻接软件 GPE 渲染到 VDF 0x20312000 + 输入循环，形成"可交互"闭环。

## c233/c234 输入注入尝试（2026-10-23 02:05）
- c233：向 `0x014A0429`（InputServant）注入 24B 事件（4 种布局，MENU=0x1AB）→ **全部 ret=-1024（无效 id）**
  ⇒ 该队列在本环境**未注册**（InputServant 服务未起来）。
- c234：改注入 `0x005503E0`(a7c2 App32 风格) / `0x011006E9` / `0x0094016A` / `0x014C0424`，32B 布局（key/sub/status/ex/from）
  → **全部 -1024**；只有 app 自己发的 0x14C0424（请求 id 9）正常 ret=0。
- 结论：**不能靠 UIPC 注入输入**（目标队列都不存在）；输入链路需要 InputService 实例（m_pMyInstance=0）或另一个仍在运行的接收者。
- 同时：`/dev/stream` 从未打开（字符串只在 libObj/libInfraMtpServer 出现），`ZZGPEREG` 写入 0 → 显示管线根本未启动。
- 下一步（进行中）：330s 长跑确认是否只是"慢"；若始终不启动，则要找出 App 等待的显示/场景服务（HDMI/DMM/OSD 通知）。

## c235–c237（2026-10-23 02:20）
- **c235（330s 长跑，c230 基线）**：stream 打开 0、ZZGPEREG 0、assert 0、pVM=1417000 稳定 —— 证明**不是慢，而是 UI 绘制路径从未启动**。
- **c236（整数 openView 变体）**：把 static=pVM 后依次调 `viewManagerIf::openView(id,0)`（id=0x75/0x2ca/0x343/0x308/0x431/0x1c6）：
  - 全部返回 `0x159ED0`（1416208，非 0、不崩）；但 stream/GPE 仍为 0。
- **c237（Application_System_Init(1)）**：直接崩（trap_no=1）→ 该参数不合法，保持 ai(0)。
- 结论：我们"手工启动的 UI 线程"能建出管理器图/ViewManager，但**App 自己的 UI 从未真正开始建屏/绘制**；
  这正是当前最后一道门。
- 后续方向（下轮）：
  1) 查 `im.elf` 主线程决定"是否调用 Application_System_Init"的条件（是否依赖 HDMI/传感器/场景服务）；
  2) 若是被服务门控 → 用 preload 伪造该服务状态（参考 a7c2 的 HDMI/DMM/OSD 开关）；
  3) 若是 bootin 参数 → 调整 `target=/dmode=` 组合再试。

## c238–c245（2026-10-23 02:45）：★ 找到 App 自己的启动停点
- c238（dlopen 日志）：启动模块序列
```
libObj → libgpe → libResourceBluetooth → libRsrcHotshoe → libInfraUsbCmn → libUsbControl …(之后不再有)
```
  之后是 `BRDSKIP read id=0xcf0400..0xcf040a`（UsbControl 读配置，我们 shim 返回 0）。
- c240（Backup_read 打 caller）：**热循环 id=0x01070316 被读 2000+ 次（n=0）**，caller = `libObj+0x168333`
  = `BackupManager::Bkup_Read` 内；`Backup_get_datasize(0x1070316) = -1` → 我们 shim 返回 0 → App 永远拿不到数据 → 卡死。
- c241/c242 掩码写错，c243 修正后（camera-profile id 段 `0x0107xxxx` 未知 → 返回 64B 全零 + 成功）：
  - **热循环消失（2059 → 0）**、pVM 仍非 0（1352120）、无 assert、无崩溃；
  - 但 stream/GPE 仍 0，仍不绘制。
- **c244（关键对照）：把探针里的 `Application_System_Init(0)` 去掉** → **pVM=0**、0x14C0424 消息 0 条
  ⇒ **App 自己的启动路径从未调用 UI 初始化**；此前的 pVM 全是我们探针代劳。这是明确结论。
- c245：在 c243 基础上加 dlopen 序列日志（进行中），确认模块加载是否停在 libUsbControl 之后。
- 下一步：锁定"模块加载/初始化停在哪"，优先怀疑 UsbControl / InfraUsbCmn 的初始化依赖
  （USB 设备枚举、或某 UIPC 服务），并按需伪造该状态。

## ★★ c246（2026-10-23 02:50）：Backup 轮询修复后模块加载 8 → 61，定位到下一个卡点
- 在 c243（camera-profile 未知 id 返回全零）基础上，把 dlopen 日志上限提到 220，且**不调用** `Application_System_Init`：
```
模块加载数：8 → 61（libObj/libgpe/libMWF/libbackup/libSaveLoadSettings/libbkdmn/
 libOnDemandLoader/libInfraKikiLog/libMWF/libi2cuipc/libI2CRight/libJiritsu/libAtn/
 libInfraSystemProperty/libInfraCrypt/libInfraSetupFile/libResourceBluetooth/libUsbBusPower/
 libInfraUsbCmn/libUsbControl/libRsrcHotshoe/libInfraMediaCommon/libBusPower/libInfraFsys/
 CameraOnDemandSaver/libInfraHdmi/libAdjModule/libInfraOptionInstall(er)/libDpro_EVF/
 libInfraUpmPro/libfan/libEMG …）
```
- 但 **pVM 仍为 0**（App 自己仍没调 UI 初始化），且 libEMG 之后：
```
EXIT THREAD_INFRA0 pid=145 code=0
ZZFUTEX im.elf.real pid=134 ... upc=0x040A2146
ZZSTK1 含 0x0094016A、ZZSTK2 含 0x00920114   ← 主线程卡在等/发这两个 qid 的消息
```
- ⇒ **新卡点 = 主线程阻塞在 qid 0x94016A / 0x920114 的 UIPC 等待**（这两个正是我们 M1 里"缺失服务"之一）。
- 下一步：在探针里为 0x94016A（+0x920114）实现**最小服务端**：
  `osal_reg_sync_msg_queue_cb` 注册 + 收到请求后回一条"成功"应答（按 a7c2 的 fake service 方式），
  让主线程越过这一步，进而调用 Application_System_Init。

## c247–c248（2026-10-23 03:00）
- **c247**：把 `/bin/bootin.elf` 包装脚本改为**先启动 sen.elf**（官方链里的兄弟进程）再跑 bootin/im.elf：
  - sen.elf 确实跑起来了（249 条相关日志，含 UIPC 活动）；
  - 但 0x94016A 的同步消息**仍然 -1020**（`ret=0xfffffc04`），pVM 仍 0、stream/GPE 仍 0；
  - ⇒ sen.elf 不提供 0x94016A 服务，问题不在"缺兄弟进程"。
- **c248**：从探针主动向 0x94016A / 0x920114 发消息（4 种 payload）→ **全部 -1024（队列不存在）**；
  - ⇒ 0x94016A 是 App **发送**的目标（客户端角色），且**当前无人 serve**；我们用 auto-fake 让它"假成功"，
    但 App 之后仍阻塞（不是这条调用本身造成的死锁）。
- 新线索（im.elf 反汇编）：启动器会打开 `/dev/nflasha2 /dev/nflasha3 /dev/nflasha10 /dev/nflasha15 /dev/nflasha9 /dev/nflasha23`
  等分区读取 **IMDB 配置**（决定加载哪些模块）。若某分区内容异常，加载列表会不完整（我们只看到 61 个模块，未出现 viewUnified2 等 UI 模块）。
- 下一步：
  1) 核对 im.elf 读 IMDB 的分区与返回（可加 open/read 日志），确认"应有模块列表"是否被截断；
  2) 找出主线程真正阻塞点（futex owner / 等待的 qid 应答）；
  3) 若 IMDB 缺项 → 提供正确分区内容或伪造 IMDB 读取结果，让 UI 模块（viewUnified2 等）被加载。

## c249（2026-10-23 03:05）
- 在无代劳探针里加 `open/read` 日志（只针对含 "nflasha" 的路径）：
```
NFO/NFR 均为空 —— App 进程本次运行根本没有 open /dev/nflasha*
```
- 设备访问统计：`dev=uipc 557 / dev=dmpgles2 4 / dev=kikilog 1` —— **从未碰 /dev/dmm、/dev/gpe**；
- ⇒ "IMDB 被截断"假设未能证实（App 没走 nflasha 读取）；显示栈从未被打开。
- 现状判断（重要）：
  1. App 自身启动链止步于 61 个模块之后，主线程 futex 等待；
  2. 我们代劳 `Application_System_Init(0)` 能建出 gfx/res/wg/ViewManager，但**绘制需要 GRM 位图分配**，
     而 grm_ma（内核侧服务，qid 0x14C0424）应答只回 0、不填状态 → BMP_Create 失败；
  3. 强行把 status 写 0 → 命中 `MA_ErrNone == error @BitmapImpl.cpp:199`（底层 MA 分配仍失败）。
- 结论：**核心瓶颈 = grm_ma/MA 位图分配不可用**（内核侧无实体显存分配器），
  这既是"App 自己画不出来"的原因，也是"我们代劳也画不出来"的原因。
- 下一步（两条并行）：
  A) 在探针里实现 **grm_ma 应答替身**：对 id==9 的请求返回"成功 + 我们 mmap 的缓冲区地址 + 清零错误码"，
     让 BMP_Create 拿到一张可写的 640×480 位图；
  B) 若 A 成功但 App 仍不刷屏，则用软件 GPE（MIT gpe-raster）把那张位图内容周期性拷到 VDF 0x20312000，
     至少先看到"有内容"的画面，再逐步接管 App 的绘制命令。

## c250/c251（2026-10-23 03:15）：grm_ma 应答替身实验
- **c250（把 w8=0x20312000 / w9=0x96000 写进应答）→ 直接崩**（THREAD_INFRA0，bad pc，trap 0x0e）：
  `w8/w9` 是 App 自己的指针/长度语义，不能乱改。
- **c251（只把 status=0、error=0，即 w4/w6/w7 清零，保留其余字段）**：
```
GMA 请求只有 6 条（150s），不再有重试风暴 → BMP_Create 很可能已经成功
无 assert、无崩溃、pVM=1353784 稳定
但 dev=stream 仍 0、ZZGPEREG 仍 0 → 仍然没有任何绘制
```
- 重要发现：c251 抓到的 grm_ma 消息里 **w5/w8 本身就是 0x20312000（VDF 物理地址）**，
  说明"位图内存"本来就指向帧缓冲；之前的失败只卡在 status 字段。
- ⇒ 现在 BMP/位图这一关**基本可通过**（只清 status/error 即可），
  剩下的问题是 **App 从未发起"绘制/建屏"流程**（view 打开后没有 render tick，或 render 依赖我们未提供的服务）。

## ★ c253（2026-10-23 03:22）：VDF 显示通道在当前基线**再次实测可用**
- 探针里用 raw syscall 打开 `/dev/mem`、`mmap2(0x20312000>>12)` 640×480×4，逐像素写渐变：
```
串口: PVDF painted
QMP screendump 取样（y=240）：
  x=0 (255,0,0) → x=80 (255,0,10) → x=320 (255,0,40) → x=636 (255,0,79)
```
- ⇒ **guest 用户态可直接把像素送到 VDF 并显示在 QEMU 窗口**（bionz-streamfb 正常）；
- 关键教训：`mmap2` 的 offset 必须是**页号**（0x20312000>>12 = 0x20312），否则 EINVAL（c252 的 mmapfail）。
- 产物：`oss/artifacts/c253/vdf_gradient.png`（截图）、`screen.ppm`、`serial_tail.txt`；
  镜像 `/mnt/build/ab_v253.img`。
- 意义：**"能出画面"已具备**（软件渲染 → VDF → 窗口）。剩下的是让 **App 的 UI 内容**走这条路：
  1) 让 App 发起绘制（BMP 已可通过：只清 status/error，位图本就指向 VDF）；
  2) 若 App 的绘制经 GPE 命令，则在 fake_gles 里把 GPE 命令翻译成 VDF 光栅（gpe-raster 已有）；
  3) 输入闭环（当前 InputServant 队列不存在，需另寻入口）。

## ★ c254–c261（2026-10-23 03:45）：线程清单揭示"UI 线程从未创建"
- c254：`openView(id)` 后直接调 `View::drawDirect()` → **崩**（view 未完成初始化，不能用）。
- c255：`/setting/sen/smode = "UU"`；`/proc/udm/upm_bootmode` **不存在**（openfail）。
- c256/c257：dir 遍历失败（dirent 偏移问题），改用 **tid 扫描** `/proc/self/task/<tid>/{comm,wchan}`（c258–c261）：
```
tid 134 im.elf.real        wchan=futex_wait_queue_me     ← 主线程等 futex
tid 135/136/137/141 uipc_proxy_0..3  wchan=__k_4u_wait_cb
tid 142 mwf_monitor_inp    147 THREAD_INFRA2   148 grm_result   150 grm_gpe
149 InfraAudioDecod  153 AV_DDS  154 InfraAvEffector  155 InfraIndctr
156 UsbBusPower  157 InfraDecoder  158 I-TpMain  159 InfraMovieEdito
160 UsbControl  161 I-TpRecv  162 HotshoeResource  163 InfraMovieEncod
164 BusPower  165 Hotplug  166 I-HotshoeRecv  167 I-SircsMain
168 InfraMovieEncTh  169 InfraStillEncod
```
- **关键结论：没有 `THREAD_APP`（也没有 Application/UI 线程）** ——
  App 的 UI 线程**从未被创建**，所以 pViewManager 一直是 0、永远不绘制。
- 其它证据：`/proc` 可读（探针读 maps/comm/status 都正常），因此不是挂载问题；
  `im.elf` 里有 `imelf_sync_api_cb: err: cmd:0x%x` 与 `/usr/bin/sen.elf &` 等字符串，
  说明 App 有**一套由外部（sync API / 兄弟进程）驱动的启动命令**，我们的 bootin 链从未发送"启动 App"的命令。
- 下一步（下轮）：
  1) 在 im.elf 里定位 `imelf_sync_api_cb` 与命令分发（cmd 表），找出"启动应用/创建 THREAD_APP"的命令值；
  2) 用探针在进程内直接调用该回调（或经 UIPC 发给 im.elf 自己的 sync 队列）触发 THREAD_APP；
  3) 之后按已有路线：BMP（已可通过）→ 软件 GPE → VDF（已验证）→ 输入。

## ★★ c262/c263（2026-10-23 03:50）：找到并直接启动 THREAD_APP —— 它立刻退出
- im.elf 里 THREAD_APP 的启动器：`0xA1B0`（`osal_create_waitable_thread(entry=0x972D, name="THREAD_APP", prio=15)`）。
- c262：探针在进程内直接调 `0xA1B0` → `START_APP call` / `START_APP returned`，不崩；
- c263（启动后扫描线程）：**THREAD_APP 确实被创建（pid=189），但随即 `EXIT THREAD_APP pid=189 code=0`**；
- THREAD_APP 入口（0x972C）逻辑：
```
osal_thread_id(); osal_set_cpu(tid,1);
log("a_thread");
r5 = b8e8(0, 0x08000008, 0, arg);      ← 按 IMDB 目标 0x08000008 跑条目
b8e8(r5, 0x08000009, adac(), arg);      ← 再按 0x08000009 跑条目
log(); return;
```
- ⇒ **应用阶段（0x08000008/0x08000009）在 IMDB 中没有匹配条目**，所以线程"启动→立刻退出"，
  这就是"UI 从未出现"的最终原因（不是崩溃，不是卡死，而是**没有要加载的东西**）。
- 下一步：确认 IMDB 目标过滤来源（`target=` 参数 / dmode / bootmode）——
  很可能我们的 `target=qemu` 在 IMDB 里不存在对应 target 位，导致应用阶段条目被过滤掉；
  尝试 `target=prod` 等真实 target 名，或直接在探针里把过滤掩码改成"全匹配"再跑一次。

## c264（2026-10-23 03:52）：target=prod 无效
- 把包装脚本改成 `im.elf 'target=prod ' dmode=3` → **im.elf.real 直接退出（code=61952 / 0xF200）**，探针都没跑起来；
- ⇒ `target=qemu` 是**正确且被接受**的 target；问题不在 target 名。
- ⇒ 结论收敛为：**IMDB 里应用阶段（0x08000008 / 0x08000009）的条目缺失**，
  或这些条目被 `dmode=3` 之类的过滤条件排除。
- 下轮计划：
  1) 找到 IMDB 数据来源（libIMDB 读哪个文件/分区），把它的条目表 dump 出来，
     确认是否存在 target 位 0x08000008/0x08000009 的条目；
  2) 试 `dmode` 其它取值（0/1/2）看应用阶段是否被启用；
  3) 若 IMDB 确实缺项 → 用探针在 `IMDB_get_entries` 层伪造这些条目（把库路径/入口指向 viewUnified2 等），
     让 THREAD_APP 真正把 UI 模块拉起来。

## ★★ c266–c268（2026-10-23 04:05）：IMDB 表结构解出 + 用 arg=1 触发应用阶段（模块 61 → 78）
- libIMDB.so 的 `imdb_raw`（.data.rel.ro，file 0x64B0）是**静态条目表**，每条 44 字节：
```
[0]=命令/标记  [1]=类型位  [2]=目标位掩码  [3]=参数/ID  [4..10]=字符串/指针字段
```
- 关键条目：
```
idx142: [0]=0x08000008 (阶段8 起点/哨兵)
idx143: [0]=0x20000004 [1]=0x1 [2]=0xFFFFFF9F [3]=0x07FB0083
idx144: [0]=0x20000004 [1]=0x1 [2]=0x40       [3]=0x83
idx145: [0]=0x20000004 [1]=0x1 [2]=0xFFFFFF9F [3]=0x1A9
idx146: [0]=0x20000002 [1]=0x1 [2]=0x7FFFFFFF [3]=0x1CF
idx147: [0]=0x08000009 (阶段9 起点/哨兵)
```
- THREAD_APP 的两次 `b8e8(...)` 过滤条件是 `entry[1] & arg != 0` 且 `entry[2] & globals[40] != 0`；
  **我们之前用 `ta(0)`（arg=0）→ 全部条目被过滤 → 线程空转退出**。
- **c268：改用 `ta((void*)1)`** →
```
START_APP arg=1
dlopen 模块数 61 → 78（新增 libNetService*/libInfraEthernetService/libInfraIPsec/libInfraSKD 等）
随后崩溃：address=0x00000001, trap 0x0e, pc=0x040719cc(libc), lr=0x0409fd77(libpthread)
```
- ⇒ 应用阶段**确实存在并可启动**，现在只是其中某个条目的入口调用参数不对（NULL+1 解引用）。
- 下一步：
  1) 逐条隔离 idx143–146（用不同 arg 掩码，或临时改 entry[1]/[2] 只保留一条）找出崩的是哪条；
  2) 检查 `b448/b4fc` 里 dlopen+dlsym 后调用时的参数来源（entry[36]/[40] 指针是否需重定位）；
  3) 修好后 THREAD_APP 应能跑完 → 它会把 UI 模块（viewUnified2 等）真正拉起来。

## ★★★ c269/c270（2026-10-23 04:10）：应用阶段四条真身解出（UI 启动清单）
- 隔离测试（改 `imdb_raw` 的 entry[1] 只放一条）→ 单独跑 idx143 即崩（address=0x1，且 appFw.so 未加载）；
  崩点在 im.elf 的 IMDB 执行器（b7dc）里，不是模块本身。
- **条目解码结果（这是整条 UI 启动链的"任务清单"）**：
```
idx143: cmd=0x20000004  mask=0xFFFFFF9F  id=0x07FB0083
        dlopen "appFw.so"      → dlsym "Application_System_Init"   ★ UI 主线程入口
        附加 config: "AdjChkConfig.so", "AppConfig.so"
idx144: cmd=0x20000004  mask=0x40        id=0x83
        dlopen "subapp.so"     → "subapp_init" / "subapp_exit"
idx145: cmd=0x20000004  mask=0xFFFFFF9F  id=0x1A9
        dlopen "/usr/local/lib/libVssApp.so" → "vssapp_init" / "vssapp_exit"
        附加: "/usr/local/lib/AdjChkConfig.so", "/usr/local/lib/libVssAppMain.so"
idx146: cmd=0x20000002  mask=0x7FFFFFFF  id=0x1CF
        dlopen "gui.so"        → "DPro_App_UI_init" / "DPro_App_UI_sus" / "DPro_App_UI_resume"
```
- 这与我们已知事实完全对接：`Application_System_Init` 正是此前我们手工调用的函数（ai(0) 能得到 pVM≠0），
  说明**真正的 UI 启动 = 依次执行这四条**（appFw → subapp → VssApp → gui）。
- 执行器（b7dc）用 `ldrh` 取 entry[12] 低 16 位当 id 再查表，解析路径有坑 → 直接调用会崩；
  **因此下轮改用"手工顺序调用"绕过 IMDB 执行器**：
  1) dlopen("appFw.so") + dlsym("Application_System_Init") → 调 0（已验证可出 ViewManager）；
  2) dlopen("subapp.so") + subapp_init；
  3) dlopen("/usr/local/lib/libVssApp.so") + vssapp_init；
  4) dlopen("gui.so") + DPro_App_UI_init；
  5) 之后再走 BMP（已可通过）→ 软件 GPE → VDF（已验证）→ 输入。

## c271（手工顺序调用，失败在探针侧）
- 尝试在探针里依次 `dlopen→dlsym→调用`：appFw.so/Application_System_Init → subapp.so/subapp_init
  → libVssApp.so/vssapp_init → gui.so/DPro_App_UI_init。
- 结果：**探针 worker 根本没跑起来**（无 `Y9 worker` 日志），且 `THREAD_INFRA0` 崩（trap 0x0e）——
  属于探针侧问题（`step()` 里嵌套 dlopen/dlsym 与已有 dlopen interposer 相互干扰），不是 App。
- 下轮改用更稳的方式：
  1) 不自己调 dlopen，而是**直接调用已解析函数指针**（`Application_System_Init` 用 dlsym(NULL,"...") 拿，
     subapp/vssapp/gui 的符号先用 `dlopen` 在**worker 线程启动前**完成，避免与 interposer 竞争）；
  2) 或直接用 `im.elf` 的 `b448`（dlopen+dlsym+调用）函数，按条目字段构造调用（绕开 b7dc 的 ldrh 截断问题）。

## ★ c272（2026-10-23 04:15）：四条 UI 库在 /usr 里**根本不存在**
- 运行期实测（探针直接 open）：
```
/usr/lib/libObj.so            = OK (fd 17)      ← 存在
/usr/lib/appFw.so             = -2 (ENOENT)     ✗
/usr/lib/subapp.so            = -2              ✗
/usr/lib/gui.so               = -2              ✗
/usr/local/lib/libVssApp.so   = -2              ✗
/usr                          = OK
```
- 而在 `/usr` 分区镜像里 `DPro_App_UI_init` / `vssapp_init` / `appFw.so` 这些**字符串确实存在**（约 41.8MB 处），
  但那是 **libIMDB 的条目表**，对应的 `.so` 文件本身缺失。
- 符号核对：`libObj.so` 导出 `Application_System_Init`（✓），但 **不导出** `subapp_init` / `vssapp_init` / `DPro_App_UI_init`。
- ⇒ 结论：**IMDB 的 UI 启动清单是对的，但四个库文件在我们的镜像里缺了三/四个**，
  所以 THREAD_APP 即使被正确触发也无库可加载（之前 arg=1 时 78 个模块能加载，是因为那些在 /usr/lib 里存在）。
- 下轮计划：
  1) 在 `/system`（nflasha3）与其他分区里找 `gui.so` / `subapp.so` / `appFw.so` / `libVssApp.so`（探针直接 open 探测候选路径）；
  2) 找不到就用替代方案：`appFw.so → libObj.so`（导出 Application_System_Init，已验证可用）；
     `gui.so` 的 `DPro_App_UI_init` 若彻底缺失，则用我们自己实现的软 UI（软件 GPE → VDF）替代最后一步；
  3) 目标不变：先把 Application_System_Init + 视图系统跑起来（pVM≠0 已有），再把绘制接到 VDF。

## c273（补充线索）：nflasha15 抽样区是**全零**
- 按文档 `nflasha15 用户区 0x2D800000 ⇒ .img 0x2DC00000` 抽 64MB → **头部全零**，debugfs 打不开（无 ext2 超级块）；
- ⇒ 我们镜像里该位置**不是**真正的 /usr 分区内容（要么偏移不同，要么镜像里该分区为空），
  这解释了为什么运行期 `/usr/lib/appFw.so` 等全部 ENOENT；
- 而 init 日志早有：`mounting1 /dev/dnflasha15 -> /usr ... failed errno(6)` → **/usr 挂载本来就失败**。
- 下轮首要任务：**修正 /usr（nflasha15）分区在镜像中的位置/内容**，让真正的 /usr 挂上（含 appFw.so/gui.so 等），
  这是"UI 库缺失"的根因；其次才是执行 appFw→subapp→VssApp→gui 四步。

## ★ c274/c275（2026-10-23 04:20）：/usr 已挂载且有 319 个库，但 UI 四库确实不存在
- 运行期用 `getdents64` 枚举 `/usr/lib` → **319 个条目**（真实 /usr，含 libObj/viewUnified2/libNetService…）；
  `/usr/local/lib` → 0 个（空目录）。
- 但 **`appFw.so` / `subapp.so` / `gui.so` / `libVssApp.so` 一个都没有**（c272 的 ENOENT 得到确认）。
- ⇒ 结论修正：**IMDB idx143–146 那四条并不属于本机型（ZV-E10）**（它们的 mask 0xFFFFFF9F / 0x40 是给别的 target 位的），
  本机 UI 实际就是 `libObj.so` 里的 `Application_System_Init`（= appFw 的等价物），这与我们早先手工 ai(0)
  能建出 gfx/res/wg/ViewManager 的事实一致。
- 于是路线固定为：
  1) `libObj` 的 `Application_System_Init(0)` 启动 UI 系统（已有 pVM≠0）；
  2) `viewManagerIf::openView(id,0)` 打开视图（返回非 0，不崩）；
  3) 解决"渲染触发"：找到 App 的 draw/commit 入口（`View::drawDirect` 需完整 view 状态；也可试
     `ux::gfxeng::GraphicsEngine::updateResource` / `GRM_screenGetOnBitmap`），配合已可用的 BMP（只清 status/error）
     与已验证的 VDF 通道；
  4) 判定标准：`ZZGPEREG>0` 或 `/dev/stream` 被打开 + 截图像素变化。

## c276/c277（2026-10-23 04:25）：图形栈现状——引擎在，但"屏幕位图"从未创建
- 手工调用结果：
```
GraphicsEngine::sys_getInstance()      = 0x00086830（非 0，引擎在）
GraphicsEngine::updateResource(inst)   = 146128196（返回非 0，不崩）
GRM_screenGetOnBitmap()                = 0x00000000   ★ 屏幕位图不存在
GRM_fbrmSetScreenPreference(0,0)       = -9999        ★ 显示配置设置失败
```
- 即使启用"只清 status/error"的 grm_ma 修法，轮询 `screenGetOnBitmap()` 仍恒为 0（GMA 消息仅 6 条，
  说明 App 自己也没再请求建位图）。
- libObj 导出的图形 API 全表（共 21 个）：
```
GRM_bitmapCreate / bitmapCreateComposite / bitmapGetPhysicalAddress / bitmapGetRowPixelCount
GRM_bitmapGetSubRectangle / bitmapWriteLockBits / bitmapWriteUnlockBits
GRM_gpermFillrect / gpermRectblit
GRM_fbrmSetScreenPreference / fbrmFillrectYUV / fbrmRectblitYUV / fbrmInitializeYUV /
fbrmPrepareUpdateYUV / fbrmUpdateYUV / fbrmTerminateYUV
GRM_screenGetOnBitmap
```
- ⇒ 结论：**"屏幕位图"由 DMM/OSD 侧创建（我们环境没有），所以 App 即使引擎就绪也无处可画**。
  下一步用 `GRM_bitmapCreate` **自己创建一张 640×480 位图**（配合 grm_ma status 修法），
  再用 `GRM_gpermFillrect` 画色块，验证"App 自己的图形栈 → VDF"是否可行；
  若位图物理地址等于 0x20312000（此前消息里 w5/w8 已是该地址），则应能直接在屏幕上看到。

## c279（2026-10-23 04:30）：用户态 BMP_Create 替身首次接通
- 在 preload 里 interpose `BMP_Create(ctx,w,h,fmt)`（这是 libObj 的 PLT 导出，可拦）：
```
FCREATE ctx=0x00060780 w=640 h=480 fmt=1        ← App 启动早期就请求 640×480 位图
FCREATE ok bmp=0x04042aa0 bits=0x0f764008       ← 我们返回自建位图（malloc 640*480*4）
```
- 同 run：`GRM_bitmapGetPhysicalAddress` / `GRM_gpermFillrect` **零调用**，
  且 `pVM=0`（本次 run 与 c230 不同——替身位图改变了初始化路径/时序）。
- 结论：
  1) **BMP_Create 可以完整由用户态接管**（这是绕开 grm_ma 的关键）；
  2) 但 App 拿到位图后没有继续走 `GRM_*` 绘制，说明它还依赖别的检查（如位图字段布局、或后续 GRM 内部函数）；
  3) 需要按 App 期望的位图对象布局填充（0x80 字节、vtable、w/h/stride/phys 等），而不是我们自造的 20 字节结构。
- 下轮：先反汇编 BMP_Create 内部构造（0x69B38C/0x69B440 已经知道分配 0x80 字节 + calloc 52 字节），
  按该布局构造替身对象（vtable 指向原 BMP 类、字段填 w/h/stride/phys=VDF），再观察是否出现 GRM 绘制调用。

## c280/c281（2026-10-23 04:32）：直接走 App 图形 API 的两种结果
- **c280**（只对 id==9 的 grm_ma 请求清 status/error，然后自己调 real `BMP_Create(0x60780,640,480,1)`）：
```
BC = 0x06079a09
BMP = 0xFFFFD8F1 (-9999)   ← 真实 BMP_Create 仍失败
```
  ⇒ 说明 id==9 那条不是该次的分配请求（实际请求形如 `w0=0x00920009`，w2/w3 也不同），我们没修到点上。
- **c281**（把 qid=0x14C0424 的**所有**应答都清 status/error，再建位图）：
  - 探针在 i=46 前崩（trap 0x0e），
  - **但截图不再是 QEMU 测试彩条，而是满屏随机噪声**（(0,0)=黑、(120,240)=(16,0,247)…）：
    说明 App/我们确实往 VDF 区域写了东西，只是写成垃圾（把"没有真实分配结果"当成了成功）。
- 结论：
  1) **必须按请求类型正确构造应答**（status=0 + 合法的 phys/stride/size 结果字段），不能一刀切清零；
  2) grm_ma 请求至少有两种形态：`w0=0x00000009`（w2=0xE1000,w3=0x280,w8=0x22BB2000,w9=0x96000）
     与 `w0=0x00920009`（w2/w3 为 0，w5/w8=0x20312000）——要分别处理；
  3) 下一步：对 `0x00920009` 也补上正确的返回字段（phys=0x20312000、stride=640*2、size=0x96000、status=0、error=0），
     然后再跑，看 App 是否能真正画出内容而不是噪声。

## ★ c282（2026-10-23 04:36）：qid 0x14C0424 其实是**日志/错误上报**服务，不是分配器
- 打印到该 qid 的消息头（前 8 个 word）：
```
GMA 0x00920007 w1=0x6F6D6D6F w2=0x2065006E w3=0x4D203D3D w4=0xFFFFFFF7 w5=0
GMA 0x00920001 w1=0x5F6C6700 w2=0x6D6D6F63 w3=0x65006E6F w4=0x203D3D20
GMA 0x00920009 ... w5=0x20312000 w8=0x20312000
GMA 0x0092000A / 0x00920002 同样带 0x20312000 等
```
- 其中 w1..w3 是 **ASCII 文本**（`"ommo"`、`"n e "`、`"== M"`、`"gl_"`、`"como"`、`"ne"`）→ 典型的
  `"... == error @ Gmm..."` 字符串；w4=0xFFFFFFF7 = **-9**（错误码）。
- ⇒ 该 qid 是 **GRM 的错误/日志上报通道**，不是内存分配服务。
  我们之前"清 status 让它变成功"等于**伪造日志成功**，并没有真正让分配成功 —— 这解释了为什么数据是垃圾。
- 真正的 MA 分配路径需要另找（候选：`/dev/grm_ma` 设备 ioctl、`dmpgles2` 的 0x…8202/0x…8206，
  或 `libra` 内部的 pool）。下轮用 `getdents64`+`open` 探测 `/dev` 下与 grm/ma/dmm 相关的节点，
  并 hook `BMP_Create` 内部实际调用链（0x69B440 之后）确认它是走 ioctl 还是走 UIPC。

## ★★★ c284（2026-10-23 04:45）：App 其实**已经在初始化 UI 并提交 GPE 命令**——是我们把命令丢了
- 给 `open()` 加日志（限前 60 条）后，真相浮出（此前统计的是 ioctl 数，不是 open）：
```
OP /dev/hdmi       fd=8,9
OP /dev/dmpgles2   fd=10,16
OP /dev/stream     fd=10,15,20,21,22      ★ 显示管线被打开多次
OP /dev/mem        fd=6,7,11,12,13,14,17
OP /dev/ldec       fd=27                  ★ 资源解码设备
OP global.xdb / ./lang_cmn.uxc / ./color_cmn.uxc / ../../usr/share/app/image_cmn_43.uxc
```
- 同时 dmpgles2 上持续出现 `0xc0048211`（wait）、`0xc0048212`（kick）、`0xc004820d`（event）——
  **说明 App 正在往 GPE 提交绘制命令，并等待完成事件**。
- 而我们的 `fake_gles`（kernel 模块 /root/fw/fake_gles/fake_gles.c）对这些 ioctl **一律返回 0，
  且 GPE 命令缓冲区（per-open DMA 256KB）里的内容被直接丢弃** → 所以屏幕上永远只有 QEMU 测试图案。
- ⇒ 结论彻底修正：
  1) "App 不画"是错的——**App 在画，命令已经发出**；
  2) 缺的是 **GPE 命令执行器**（把 ring buffer 里的命令光栅到 VDF/显存）；
  3) 这与 a7c2 项目的核心工作一致（他们实现了软件 GPE：gpe-raster/gpe-sprite/sony-lz77）。
- 下轮计划：
  1) 在 `fake_gles` 里加 **ring buffer dump**（kick 时把前 N 字节/命令头打印到内核日志），
     逆出命令格式（opcode/坐标/颜色/源位图）；
  2) 用已移植的 MIT `gpe-raster.c` / `gpe-sprite.c` 实现命令 → VDF(0x20312000) 光栅；
  3) 判据：截图出现非测试图案的内容（App 的 UI）。

## c285/c286（2026-10-23 04:48）：GPE 缓冲区扫描，命令区在偏移 0x18000 附近
- 用 `/dev/mem` mmap fake_gles 的 DMA 缓冲（phys `0xa7c0000`，256KB）并扫描非零 word：
```
[0]=0x00000001 [1..3]=0xFFFFFFFF [9..10]=0xFFFFFFFF        ← 头部状态/寄存器
[24577]=0x00000001 [24578]=0x5F000000 [24579]=0x609A9C00 [24580]=0x604A85D0 ...
```
- 24577*4 = **0x18004** ⇒ 命令/描述符区从 **0x18000** 开始（与"256KB 缓冲、前半寄存器/后半命令环"的结构吻合）。
- 下轮：dump 0x18000–0x18400 这一段前后的完整窗口（含 i=44/48/52 三次对比），
  逆出命令格式（opcode/坐标/颜色/源地址/链指针），再用 MIT `gpe-raster.c` 实现执行器 → 写 VDF。
- 说明：fake_gles 内核模块因交叉编译器（arm-sony-linux-gnueabi-dev-gcc）缺失，暂时无法重编；
  因此执行器先在**用户态**实现（探针读 /dev/mem 里的命令区 → 光栅到 VDF）。

## ★ c289（2026-10-23 05:00）：fake_gles 内核模块**成功重编**（找到可用工具链）
- 关键：内核模块用 buildroot 工具链即可编译：
```
make -C /mnt/build/src/a7c-kernel/linux M=/root/fw/fake_gles ARCH=arm \
     CROSS_COMPILE=arm-buildroot-linux-gnueabihf- modules
```
- 给 `fake_ioctl` 加了 ioctl 参数结构 dump（前 60 次，cmd=0xc0048211/12/0d）：
```
GLESDBG cmd=c0048211 arg=08b50ccc w=00000001 059e4a64 00c621d0 06274409 04036018 08b50ce8 06147a51 0001c460
```
- 产物：`/root/fw/fake_gles/fake_gles.ko`（43060B，含 GLESDBG），已写入 n15 `/kmod/grm_gles.ko`；
  镜像 `/mnt/build/ab_v289.img`；`FAKEGLES init ... bytes=262144` 确认新模块生效。
- 下一步（下轮）：
  1) 把 dump 扩到 16 个 word + 再 dump `arg` 指向的更深处（部分字段像栈地址，说明要跟指针）；
  2) 多种 ioctl 各取若干样本，逆出 GPE 命令/描述符结构；
  3) 用 `gpe-raster` 在**内核模块内**或**用户态**实现执行器，把命令光栅到 VDF 0x20312000；
  4) 判据：截图出现非测试图案的 App UI。

## ★★★ c289–c293（2026-10-23 05:12）：GPE 命令结构浮现——App 通过 /dev/mem **直接写 GPE 寄存器**
- fake_gles 现在能 dump ioctl 参数并**跟踪用户指针**（两级），实测样例：
```
GLESDBG cmd=c0048212 arg=d9caa34 ok=1
  A = 00000001 06db4c28 06db4c28 0d9caa48 06152bff 00000036 1007200c 100720e8
  B = 100720e4 100720c4 100720c0 00000001 06e3ad10 10072030 1007201c 10072018
  P1[06db4c28] = 013d7aa0 0003a788 0400f5c4 05ae4fd4 ...
  P3[0d9caa48] = 00000036 1007200c 100720e8 100720e4 100720c4 100720c0 ...
  P6[1007200c] = ffffffff 00000000 ... ffffffff ffffffff
```
- 关键：句柄结构里出现 **`0x1007xxxx` 一串地址**（0x1007200c/0x100720e8/0x100720e4/0x100720c4/0x100720c0/0x10072030 …）
  —— 这是 **GPE 寄存器块（MMIO）**；App 通过 `/dev/mem` 把它映射进用户态，
  **直接写寄存器来下发绘制命令**（0x013d7aa0/0x0003a788 之类是命令字与长度）。
- 也就是说：fake_gles 的 ioctl 只是"同步/kick"，真正的命令走的是 **MMIO 写**，
  而我们的 QEMU 机器**没有在 0x10070000 附近实现 GPE 寄存器块** → 写入被丢弃/无效。
- 下轮计划（QEMU 侧，已有源码可编）：
  1) 在 `hw/arm/bionz.c` 增加 **GPE 寄存器区（0x10070000 起，至少 0x1000）**：
     读返回合理默认值，写时 `qemu_log`/printk 记录（地址+值+PC）；
  2) 跑一次，收集寄存器写序列 → 逆出命令协议（opcode/坐标/颜色/源位图/链）；
  3) 在寄存器写回调里实现 **软件光栅 → VDF 0x20312000**（可先用 gpe-raster 的算法）；
  4) 判据：截图出现非测试图案的 App UI。

## ★ c294（2026-10-23 05:15）：GPE 命令缓冲在 **DDR RAM 0x1007xxxx**（不是 MMIO），QEMU 侧已加寄存器块备用
- 推理修正：句柄里的 `0x1007200C/0x100720E8/0x100720C4/0x100720C0/0x10072030` 落在 **DDR0（0x00000000–0x40000000）**
  范围内 ⇒ 它们是 **物理内存里的命令缓冲地址**（App 在 RAM 里构造 GPE 命令链，再把地址通过 kick ioctl 交给"硬件"）。
- 因此正确路线是：**读 DDR 里的命令链 → 光栅到 VDF**，而不是等 MMIO 写。
- 本轮同时完成（工具链修复）：
  - QEMU 源码 `hw/arm/bionz.c` 里给现有 `zz_gpe` 模型**增加 0x10070000 映射**（备用，当前 App 未写该区，ZZGPEREG=0）；
  - 补装 GTK/SDL/epoxy/pango/cairo 等依赖后 **qemu-system-arm 重新链接成功**（69,537,784B，05:14）；
  - ⚠️ 期间因缺依赖导致旧二进制一度被删，现已恢复并可用（后续务必先 `cp qemu-system-arm qemu-system-arm.bak` 再改）。
- 下轮：探针用 `/dev/mem` 读 **0x10070000 起 1MB**（重点 0x10072000 附近），dump 非零 word 与命令链结构；
  然后按格式实现用户态光栅（写 VDF 0x20312000）。

## ★ c295（2026-10-23 05:20）：DDR 里找到 **屏幕/帧描述符**（640×480）
- 探针用 `/dev/mem` 扫描 **0x10070000 + 0x10000**，发现两块非零结构：
```
[0xC00]=0x080C4880  [0xC04]=0x08AEC800  [0xC08]=0x01E00280  [0xC0C]=0x01E00280
[0xC10]=0x00004401  [0xC18]=0x00000001
[0x1800]=0x00000004 [0x1804]=0x2056E3B0
```
- 解出的关键字段：
  - `0x01E00280` → **宽 0x280=640、高 0x1E0=480**（出现两次，源/目标）
  - `0x44 01` → 格式/标志
  - `0x080C4880` / `0x08AEC800` → 源缓冲指针（DDR）
  - `0x2056E3B0` → 指向 **0x205xxxxx 图形保留区**（与我们 streamfb 的 0x20312000 同一段）
- ⇒ 这是 **显示/帧描述符**（屏幕 640×480、格式、源/目标指针），App 已按真机显存布局准备好；
  我们的 streamfb 只呈现 `0x20312000`，而 App 期望的目标在 `0x2056E3B0` 附近。
- 下轮两条可选路径：
  1) **改 streamfb 呈现地址**：把 `-global driver=bionz-streamfb,property=addr` 指到 App 实际使用的地址
     （0x2056E3B0 所在帧基址，通常对齐到 0x20568000/0x20560000 之类），即可直接看到 App 画面；
  2) 或探针周期性把这 640×480 区域 memcpy 到 0x20312000（软件搬运）。
- 优先做 (1)：只需改 QEMU 启动参数，零风险、可逆。

## c296–c298（2026-10-23 05:25）：试改 streamfb 呈现地址 → 都是噪声，说明"像素还没被画出来"
- c296：`-global bionz-streamfb,addr=0x20560000`（由描述符 0x2056E3B0 推断）→ **全黑**；
- c297：扫描 0x20000000–0x22000000，找"高密度非零"页 → 热点在 **0x20030000**、**0x20070000** 等；
- c298：`addr=0x20030000` → 截图是**随机噪声**（见附图），不是 UI。
- ⇒ 结论：**App 目前只在 RAM 里准备了描述符/句柄（640×480、格式、指针），
  真正的光栅化依赖 GPE 硬件，而 fake_gles 从不执行 → 显存里根本没有画面**。
  这也解释了所有"换个地址看看"的尝试都只能是噪声/黑色。
- ⇒ 剩下唯一正确路线：**实现 GPE 命令执行器**。
  命令流最可能在描述符指向的 **0x2056xxxx 区**（0x2056E3B0 附近），下轮直接 dump 该区找命令链；
  找到后按格式在用户态光栅到 VDF（或让 fake_gles 执行）。

## c299（2026-10-23 05:28）：0x2056xxxx 区里找到疑似命令/上下文结构
- `scan(0x20560000, 0x10000)` 非零项（绝对地址 = 0x20560000 + offset）：
```
[41476]=0x2056A204: 0x00000019 0x803121E0 0x8056D230 ...
[41496]=0x2056A218: 0x00000018 ...
```
- `0x19 = 25` 像"条目数/命令数"；`0x803121E0`、`0x8056D230` 像 **Thumb 函数指针/缓冲指针**
  ⇒ 这更像是 **GPE 上下文/命令描述表**（含回调或缓冲引用），而不是纯像素。
- 下轮：dump `0x2056A1C0–0x2056A300` 完整窗口（两次时间点对比），并按指针继续跟一层，
  确认命令格式后实现执行器（光栅到 VDF 0x20312000）。

## ★ c300–c305（2026-10-23 05:45）：GPE 命令链本体拿到 + 地址实验收尾
- c301/c302/c303 用逐级指针 dump（内核 ioremap 读物理）：命中 **命令链基址 0x013D7AA0（len=0x3A788≈239KB）**，
  内容为 **9 word 一组的链式描述符**：
```
00000400 00000001 FFFFFFFF 00000000 00000000 00000000 613D7AB8 613D7AB8 00000223
00000400 00000001 FFFFFFFF 00000000 00000000 00000000 613D7ADC 613D7ADC 00000223
00000400 ...
```
  - [0]=0x400（计数/槽数） [1]=1 [2]=-1 [6]=[7]=下一节点指针（=自身+0x24） [8]=0x223（opcode/类型）
  - 全部条目同构 ⇒ 这是 **GPE 描述符环（free/pending list）**，真正的绘制命令在 kick 时由硬件消费。
- 地址实验收尾（streamfb `addr` 覆盖）：
```
0x20560000 → 全黑
0x20030000 → 随机噪声
0x08AEC800 → 有横条纹的噪声（非 UI）
```
  ⇒ 确认**没有任何"已画好的帧缓冲"**，因为没有人执行 GPE 命令。
- 已具备、只待最后一步的能力：
  1) 能读任意物理内存（/dev/mem + 内核 ioremap）；
  2) 能改内核模块（fake_gles 重编 OK）；
  3) 能改并重编 QEMU（已备份二进制）；
  4) 知道命令环在 0x013D7AA0、描述符格式为 9-word、opcode 字段在 +0x20。
- 下轮计划：在 `kick` 时**遍历该环并解析描述符**（把 opcode=0x223 等枚举出来，dump 每条目的完整 9 word 与
  可能的源/目标位图指针），建立 opcode→操作 的映射；随后实现光栅到 VDF 0x20312000。

## c306–c309（2026-10-23 05:55）：绘制入口排查收敛
- c306：命令环 head/tail 对比 —— 条目同构，仅 [8] 从 0x223 递增到 0x23D（像 id/序号），命令体不在环里；
- c308：kick 描述符里的 `0x0CE2D000` 区实测**全零** ⇒ App 分配了结构等硬件填写（进一步印证"没人执行"）；
- c309：interpose `GRM_bitmapCreate` → **零调用**；`GRM_gpermFillrect/Rectblit` 亦零调用（c307）。
- ⇒ 结论：本机 UI 的绘制**完全走 GPE 硬件通道**（不经过 libObj 导出的 GRM 包装函数），
  所以"用户态拦 GRM 调用"这条路对本机型无效；唯一出路是 **实现 GPE 硬件模型**。
- 现有可复用资产（下一步直接用）：
  - `fake_gles` 内核模块（可重编，含 ioctl/kick/指针跟随/ioremap dump）；
  - QEMU `hw/arm/bionz.c`（可在 dmpgles2/DMM 层面加模型）+ 已备份二进制；
  - VDF 呈现通道（0x20312000）与软件光栅算法（MIT gpe-raster/gpe-sprite）；
  - 命令环地址 0x013D7AA0 / 描述符 9-word 布局 / opcode 字段 +0x20。

## c310/c311（2026-10-23 06:05）：/usr 分区定位尝试（未完成，供接手）
- 在镜像里搜 `libInfraIPsec.so` 字符串命中 **0x3048A2CE** 附近；在同一区域扫 ext2 超级块（`0xEF53`@+0x438）：
```
候选: 0x2F400000 / 0x30400000 / 0x31400000
取 0x30400000 抽出 300MB：dumpe2fs 正常（block=1024, 307200 blocks, UUID 3d7a5506…）
但 debugfs 报 "/: Ext2 inode is not a directory"
```
⇒ 说明该分区内容在镜像里存在，但**我们抽出的起始偏移还有偏差**（约 ±0x400 级别），
或该分区是 **dnflasha15（双份副本）**、真实 /usr 在另一副本。
- 价值：若能把真实 /usr 抽出来，就能静态分析 **发起 dmpgles2 ioctl 的图形库**（libObj 里没有这些 ioctl 常量），
  从而直接得到 GPE 命令格式，省掉盲猜。
- 下一步建议：
  1) 用 `partinf.conf`/分区表精确定位 nflasha15 与 dnflasha15 的 **用户区地址**；
  2) 或直接在 guest 里读 `/proc/mtd` + `dd if=/dev/mtdblock? `（我们已有 root）；
  3) 拿到 /usr 后 `grep -rl` 常量 `0xc0048212` 找到图形库并反汇编。

## c312（2026-10-23 06:06）：guest 内 /proc/mtd 不可用
- 探针读 `/proc/mtd` → **fail**（该内核/配置未暴露 mtd 分区表；/proc 本身可读，但无此文件）；
- 因此无法从 guest 侧直接拿到 nflasha15 的准确偏移，只能继续在镜像里扫超级块。
- 附注：本轮 run 中 dmpgles2 的 0xc0048211/12/0d **持续高频出现**（说明 App 一直在 kick GPE，
  进一步佐证"App 在画、硬件缺失"）。

## ★★ c313（2026-10-23 06:12）：找到 dmpgles2 ioctl 的发出者 —— **libObj 的 `rm_reg_*` 家族**
- 做法：`fake_gles` 在 ioctl 里打印 `task_pt_regs(current)->ARM_pc/lr`；实测：
```
GLESPC cmd=80048206 pc=042f517c lr=06151973 comm=im.elf.real
GLESPC cmd=c0048202 ... lr=06151991
GLESPC cmd=80048263 ... lr=06151aa7
GLESPC cmd=c0048211 ... lr=06152693
```
- `lr` 全部落在 **libObj+0x7749xx 区间**（本 run libObj base=0x59DD000，0x06151973-0x59DD000=0x774973）；
  该处正是 **`rm_reg_init`** 系列函数：
```
774938: blx open@plt            ← 打开设备（字符串在字面量池）
774960: movw r1,#0x8206 ; movt r1,#0x8004 ; blx ioctl@plt     ← 0x80048206
774980: movt r1,#0xc004 ...                                    ← 0xc00482xx 系列
774bf2 / 774c52 / 775172 同样构造 0xc004xxxx 命令
```
- 结论：**GPE/dmpgles2 的寄存器与命令接口实现在 libObj 内部（0x7744xx–0x7768xx）**，
  不在 libgpe.so / libgrm.so / libdmpNative.so 里（那三个库中都没有这些 ioctl 常量）——
  这解释了为什么在 libObj 里搜 0x c0048212 搜不到（命令字是 `movw/movt` 拼出来的）。
- 产物：`oss/artifacts/libObj_rm_reg.dis`（该区间 3539 行反汇编，含 12 处 ioctl@plt 调用点）。
- 下轮：在该区间内定位 **写入命令数据 + kick(0xc0048212)** 的函数，
  顺着它看命令结构（很可能就是"往映射区写寄存器/命令 + kick"），从而得到 GPE 命令格式。

## ★★ c314（2026-10-23 06:18）：App 把 /dev/stream 直接 mmap 到 **phys 0x20312000（32MB）**
- 内核 QMMAP 跟踪显示（非匿名映射）：
```
QMMAP len=0x2000000 off=0x20312000 prot=0x3 flags=0x01 fd=10 ret=0x0d3c8000 name=stream
QMMAP len=0x1498e7c off=0 fd=6 ret=0x59dd000 name=libObj.so
QMMAP len=0x2000000 ... name=stream (重复两次)
```
- ⇒ **App 的显示映射就是我们 streamfb 呈现的同一块物理内存（0x20312000，32MB）**；
  它把这块内存当"显存 + 命令区"使用，像素部分只占 640×480×2=614KB，**剩余 ~31MB 很可能放 GPE 命令缓冲**。
- 同时 ioctl 统计（85s）：`0xc0048211`(wait) 79 次、`0xc0048212`(kick) 77 次、`0xc004820d`(event) 22 次、
  `0xc0048203`(控制寄存器) 5 次 —— 典型的"等待→kick→等事件"渲染环。
- 下轮：**扫描 0x20312000+0x100000 .. +0x2000000**（即映射区里像素之外的 31MB），
  找命令缓冲/描述符，拿到 GPE 命令格式；这是目前最有可能一击命中的地方。

## c315/c316（2026-10-23 06:22）：多个 stream 缓冲都是空的
- c315：扫描 `0x20312000 + 16MB`（映射区像素之外）→ **无热页（全零）**；
- c314 的 QMMAP 显示 App 还映射了另外三块 stream 缓冲：
```
0x22312000 (4MB, fd=20) / 0x228B2000 (2MB, fd=21) / 0x22712000 (1.625MB, fd=22)
```
- c316：把 streamfb 呈现地址改到 `0x228B2000` → 仍是 **QEMU 测试彩条**（该区为零）。
- ⇒ 所有显示缓冲都**没有被写入过**，再次确认"没有执行者就没有画面"。
- c314 还发现一个可疑提交路径：`ioctl(0xc004820e)`，参数 `{0, 0x042BA2A0, 递增数值(0x6E5F0…), …}`
  （lr 指向 libObj 0x7746xx 区，正好是一段 **memcpy/提交** 代码）—— 下轮从这里继续，可能是真正的"提交帧"入口。

## ★★★ c317/c318（2026-10-23 06:26）：**GPE 提交协议首次完整捕获**
- 关键发现：QEMU 的 `ZZGPEREG` 日志走 **stderr**，之前被 runner 丢到 /dev/null；改用 `run_cap.sh` 保存 `qemu.out` 后立刻看到：
```
ZZGPEREG w off=0x1804 size=4 val=0x2056e270 pc=0x06151ed0 lr=0x06152091
ZZGPEREG w off=0x1800 size=4 val=0x4        pc=0x06151eb0 lr=0x061524d7
ZZGPEREG w off=0x1804 size=4 val=0x2056e290 ...
ZZGPEREG w off=0x1800 size=4 val=0x4        ...
```
  ⇒ **协议：先把"命令描述符的物理地址"写到 GPE 寄存器 `0x1804`，再把 opcode 写到 `0x1800`（本次为 4）**。
  GPE 基址 0x50000000（代码注释也确认 libObj 把 /dev/mem 映射到 0x50000000 直接编程寄存器）。
- 进一步在 QEMU 里加入"kick 时 dump 描述符"（`ZZGPECMD`），拿到真实命令内容：
```
ptr=0x2056e270 val=0x4:
  0116f000 02056db0 0117f000 00000001 0044f000 00000000 0010f000 12345678
  00000000 ... (共 8 个有效 word，之后全 0)
```
  - **word7 = 0x12345678**（魔数，确认是有效命令块）
  - 其余 7 个 word 为参数（含 0x02056DB0 这类 DDR 内地址）；
  - 11 次 kick 内容完全相同（当前只重复提交同一条 opcode=4 的命令，可能是初始化/清屏）。
- 这是**第一次拿到 GPE 命令的真实二进制格式**，为写执行器提供了输入。
- 下轮：
  1) 把 dump 扩到 64 word 并**在 App 触发不同 UI 行为时对比**（如 openView 前后），找出 opcode 变化的命令；
  2) 分析 `0116f000/0117f000/0044f000/0010f000` 这类"低 16 位固定 0xF000"的字段（疑似坐标/标志打包）；
  3) 按格式实现执行器（fill/blit → VDF 0x20312000），先用 opcode=4 试出"清屏/填充"语义。

## ★★★★ c320/c321（2026-10-23 06:30）：**GPE 执行器打通 —— 命令真的变成像素了**
- 在 QEMU `zz_gpe_write` 里给 kick(opcode=4) 加了一个最简执行器：把描述符 word[4] 当颜色，填满 640×480 framebuffer。
- 第一次运行失败（屏幕全黑）——原因找到：**streamfb 的 `blank-pattern` 默认为 on**，
  当缓冲"全同色"时会用测试彩条覆盖。加 `-global driver=bionz-streamfb,property=blank-pattern,value=off` 后：
```
ZZGPEFILL opcode=4 color=f000
截图取样： (247,0,0) ×5  → RGB565 0xF000 渲染成红色，整屏红
```
- ⇒ **端到端链路首次全部打通：GPE 寄存器写入 → 执行器 → VDF(0x20312000) → streamfb → QEMU 窗口**。
  这是"App 画面显示"的**最后一段基础设施**，现在它工作了。
- 产物：`oss/artifacts/c321/gpe_fill_red.png`（整屏红，证明执行器生效）。
- 下轮：把执行器从"opcode 4 = 填充"扩展为真实语义：
  1) dump 描述符 64 word，结合 `0116F000/0117F000/0044F000/0010F000` 的规律猜字段；
  2) 让 App 产生更多命令（例如推动 UI 视图/让 render 跑起来），收集 opcode 种类；
  3) 按 a7c2 的 gpe-raster 实现 fill/blit/sprite，把 App 真正画出来。

## c322/c323（2026-10-23 06:40）：执行器就绪，但 App 只发 1 条命令
- c322（200s 长跑 + blank-pattern=off）：GPE 写入统计
```
0x1804 写入 1 次；0x1800 写入 1 次；opcode 分布 = {0x4: 1}
```
  ⇒ **App 在 200 秒里只提交了 1 条 opcode=4 的 GPE 命令**，之后没有任何绘制提交。
- c323（arg=1 触发应用阶段）：47 个模块加载后崩在 **THREAD_INFRA0**（address 未打印，trap 0x0e），
  GPE kick = 0 —— 说明应用阶段还没走到绘制就挂了。
- 结论（当前唯一阻塞）：
```
执行器/显示链路 ✅（opcode 4 → 整屏红，已截图）
App 侧渲染提交 ❌ —— App 的 UI/render 循环没有真正跑起来
```
- 下一步方向（按可行性排序）：
  1) **让 App 自己创建 THREAD_APP 并给它正确参数**（IMDB idx143 的 appFw/Application_System_Init 路径，
     我们手工 arg=1 会崩在 THREAD_INFRA0，需要定位该崩溃点）；
  2) 或找出"谁在真机上触发渲染"（很可能是 view 打开后的 requestApplicationExecute / 事件循环）；
  3) 用执行器把 App 将来的命令翻译成像素（框架已就绪）。

## ★ c324/c325（2026-10-23 06:47）：GPE 帧描述符寄存器的语义
- 把 opcode=4 改成"把 8 word 写入寄存器"后，App 立刻多写了 6 个寄存器（说明 4 是"应用/提交"类命令）：
```
off=0x0C00 val=0x080C4880   ← 源缓冲物理地址
off=0x0C04 val=0x08AEC800   ← 目标/显示缓冲物理地址
off=0x0C08 val=0x01E00280   ← 高480(0x1E0)/宽640(0x280)
off=0x0C0C val=0x01E00280   ← 同上（第二矩形/裁剪）
off=0x0C10 val=0x00004401   ← 格式/标志
off=0x0C18 val=0x00000001   ← 使能
```
  ⇒ **GPE 帧描述符**：源 0x080C4880 → 目标 0x08AEC800，640×480，格式 0x4401，使能。
- 用 streamfb 直接呈现这些缓冲的结果：
```
addr=0x08AEC800 → 横条纹噪声
addr=0x080C4880 → 彩色噪声（见截图 c325）
```
  ⇒ 缓冲里是**未初始化/垃圾数据**，进一步证明 **App 从未真正渲染**（它只配置了 GPE，没有提交绘制）。
- 现在的理解闭环：
```
App 初始化 GPE（0x0C00.. 帧描述符 + opcode 4 提交）→ 等待"渲染/场景"发生
但 App 的 UI 渲染循环从未启动 → 源缓冲始终是垃圾 → 屏幕上没有画面
```

## c327（2026-10-23 06:55）：当前基线复测（稳定性确认）
```
pVM=1,417,064（稳定）   crash/trap=0
GPE 写 28 次（6 次寄存器初始化 + 11×2 次 kick）
中心像素 (0,0,0) —— blank-pattern=off 时如实反映"显存为空"
```
⇒ 基线可复现、执行器就绪、App 仍不绘制。这就是当前最好、最干净的起点：
任何后续"让 App 画起来"的实验都可以直接在这个基线上做，并用
`ZZGPEREG/ZZGPECMD` 判定 App 是否真的开始提交绘制命令。

## c328（2026-10-23 07:00）：`dmode=1` 无效
- 把 bootin 包装脚本改成 `im.elf target=qemu dmode=1` → **im.elf.real 直接退出（code=61952）**，
  GPE 写入 0 次 ⇒ `dmode=3` 才是有效值，其它值会被 im.elf 拒绝。
- 结论：启动参数层面已无可用变化（target=qemu + dmode=3 是唯一被接受的组合）。

## c329/c330（2026-10-23 07:06）：视图资源与按名开视图
- `/usr/share/app` 有 **308 个 UI 资源**（`view*.uxc`、`string_*.uxc`、`LayoutMaster_Menu.uxc` 等），可见菜单视图确实存在。
- 用 `ux::core::ID::genID(name)` 计算视图 ID 并调用 `viewManagerIf::openView(id,0)`：
```
viewBaseMenu   id=0xb068a1c7   open ret=1417624
viewGlobalMenu id=0xd03c7f6d   open ret=1417624
viewSettingMenu id=0xd7ec001e  open ret=1417624
```
- 结果：**三次都返回同一个非零值**，但 GPE 写入仍是 28 次 / 11 次 kick（无新增），屏幕仍黑。
- ⇒ openView 只是"登记"，真正渲染仍需要 App 的 Model/Widget/Scene 状态（与路线 A 的判断一致）。
- 下轮（最后尝试方向）：
  1) 对 openView 返回的对象调用 `View::requestApplicationExecute`（0x89FFE4）或 `drawDirect`，看是否能触发一帧；
  2) 若仍无命令，则确认结论：**需要实现最小 Model/Widget 状态**才能进入渲染，这超出本轮剩余预算。

## c331（2026-10-23 07:08）：drawDirect 仍然崩
- `openView(viewBaseMenu)` → ret=1440872，随后调用 `View::drawDirect()` → **trap 0x0e**（与之前一致）；
- kick 仍为 11 次，屏幕无变化。
- ⇒ "强行调用绘制"这条路彻底关闭：视图对象在没有 Model/Widget/Scene 状态时是不完整对象。

## 阶段结论（2026-10-23 07:10）
本轮探索已把整条链拆到"最后一公里"：
```
✅ 能启动 App 到 61 模块、管理器图/ViewManager/GPU 引擎全部就绪
✅ 能读出 GPE 协议（0x1804 地址 / 0x1800 opcode）、帧描述符寄存器、命令块格式
✅ 能实现并验证 GPE 执行器（opcode 4 → 整屏红，截图存档）
✅ 能列出 308 个 UI 资源、按名字算视图 ID 并 openView
❌ App 不产生绘制命令；强调 drawDirect 崩；应用阶段入口崩于 THREAD_INFRA0
```
未完成的部分需要**补齐 App 内部的 Model/Widget/Scene 状态**（等价于重写 App 一部分业务逻辑），
不是再找一个开关就能解决的。所有可复用资产、协议数值、工具命令都写在
`docs/ZVE10_QEMU_UI_HANDOFF.md` 与本文档中。

## ★ c332（2026-10-23 07:12）：发现 im.elf 的 **sync API 命令表**（未及验证）
- im.elf 内 `imelf_sync_api_cb`（函数入口 **0x986C**，r0=请求结构指针）分发这些命令：
```
0x9400E1（+ 0x12345678 标记）、0x920143、0x92015A、0x92014B、0x92014D、0x92014F、0x920152、0x92015C
```
  - `0x920143` 分支：写 `[req+4]=0x12345678`，调用 `ade0()`（取某实例）存 `[req+8]`
  - `0x92014B/14D/14F/152/15C` 分支：调用 `933C(req…)`（带 0x94016A 作为参数之一）
  - `0x9400E1/0x94002x` 分支：调用 `b7dc(cmd,arg1,arg2)`（IMDB 条目执行）
- 这些正是"外部进程让 im.elf 干活"的接口（很可能包含 **启动应用/切换到 UI** 的命令）。
- 下轮（若能继续）：在探针里构造 `{cmd, ..., args}` 结构并直接调用 `0x986C`，
  逐个试 `0x9201xx` 系列命令，观察是否出现 THREAD_APP 或 GPE 绘制命令。
- 注意：本轮已到预算边界，此方向尚未验证；已完整记录以便续做。

## c333（2026-10-23 07:15）：直接调用 im.elf sync API —— 无效果
- 探针构造 `{cmd,0,0,0…}` 并直接调用 `imelf_sync_api_cb`（0x986D）：
```
SYNC cmd=0x920143 ret=0  w4=0 w8=0
SYNC cmd=0x92014B ret=0  w4=0 w8=0
SYNC cmd=0x92014D ret=0  w4=0 w8=0
SYNC cmd=0x920152 ret=0  w4=0 w8=0
SYNC cmd=0x92015C ret=0  w4=0 w8=0
```
- 全部返回 0、无副作用（kick 仍 11 次、屏幕仍黑、无 trap）。
- 判读：该回调多半需要**真实的请求队列上下文**（由 UIPC 收包线程构造），
  直接以裸结构调用不会触发实际动作；正确做法是让 App 自己的 sync 队列收到这些命令。
- 至此，本阶段可行手段基本用尽。**剩余工作 = 补齐 App 的 Model/Widget/Scene 状态**
  （或从 App 侧找到它等待的那个真实触发源），这已超出"逆向查找开关"的范畴。

## c334（2026-10-23 07:18）：IMDB 命令分发器直接调用也崩
- 探针直接调用 im.elf 的命令分发器 `0xB6D8`：
```
CMD 0x40000001 tag=1 → 崩溃（trap 0x0e），模块数 0、kick 11（不变）
```
- ⇒ 该分发器同样依赖真实请求上下文（队列/回调环境），裸调用无效且会崩。
- 小结：**所有"直接在探针里调用 App 内部启动入口"的尝试都失败了**（0xA1B0 空转/崩、0x986C 无效果、0xB6D8 崩），
  因为这些入口都需要 App 自己的线程/队列上下文。
- 可信结论：要推进必须二选一——
  (a) 让 App 自己的某条线程收到"开始渲染"的真实 UIPC 命令（需要知道 qid+payload，真机上由 mf/ud_im 发送）；
  (b) 补齐 Model/Widget/Scene 状态（工作量大）。
  两条都超出当前会话可完成范围，已在交接文档中列明。

## c335（2026-10-23 07:22）：枚举 App 注册的 21 个 sync 队列（qid ↔ 回调）
```
0x0094016A→0x5EFFFC4C(栈地址)  0x003F6086→0x04088304  0x00940270→0x0001550C
0x011E6088→0x051A49C0          0x011E02EC→0x051A49B8  0x0097E01F→0x0003933C
0x020F099D→0x08BE2EEC          0x009001B9→0x06E75DA4  0x007E018E→0x0950114C
0x007B018D→0x095019DC          0x012182ED→0x09501E78  0x016C0475→0x0947F418
0x016C8477→0x0947F388          0x00CA0288→0x0863AB48  0x007C0190→0x09502D3C
0x00CA01A4→0x095034EC          0x00CA8192→0x095034BC  0x007D0033→0x09507628
0x011B02A8→0x095037BC          0x012282F6→0x09505EB4  0x012182EA→0x09505EFC
```
- 观察：多数回调在 `0x095xxxxx`（viewUnified2 等），**没有一个是 im.elf 的 `0x98xx` sync API 回调**
  ⇒ im.elf 的 sync API 队列在我们环境里**从未注册**（真机由 `mf`/`ud_im` 建立）。
- 这也解释了 c333/c334 直接调用无效/崩溃：App 内部根本没有对应的队列上下文。
- 结论：要让 App 开始渲染，必须**重建那条外部调用链**（注册队列 + 发送 0x9201xx 命令），
  或补齐 Model/Widget/Scene 状态；两者都是后续阶段的工作。

## c336（2026-10-23 07:26）：向已注册队列发 0x9201xx 命令 —— 该 run 提前崩，结论未取得
- 探针向 `0x0094016A` / `0x00940270` 发送 `0x92014D/152/15C` 同步消息；
- 该 run 在 i=46 之前就崩了（kicks=0、trap=1），未取得有效数据 —— 属 run 间不稳定（此前同类镜像可稳定跑到 100s+）。
- 后续建议：这类注入实验需要先确认基线稳定（先跑 30s 验证 kicks=11），再做注入；否则结果不可解释。

## c337/c338（2026-10-23 07:30）：向已注册队列注入 —— 返回 -1024（无效队列）
- 加"pVM≠0 才注入"的守卫（避免上一轮提前崩导致无结论），i=55 注入：
```
INJ guard pvm=0x00156028     ← App 已初始化
SQ q=0x0094016A cmd=0x92014D ret=-1024
SQ q=0x0094016A cmd=0x920152 ret=-1024
SQ q=0x00940270 cmd=0x92014D ret=-1024
kicks=11（不变）, trap=0
```
- ⇒ 这些注册的队列是**异步队列**，不能作为 sync 服务目标；im.elf 的 sync API 通道在我们的环境里确实不存在。
- 旁证：镜像里搜到 **`ud_im.elf` 位于 0x427xxxx（nflasha1 cramfs 区）**，而 `mf.elf` 不存在于镜像；
  官方链里"驱动 im.elf 渲染"的外部件就在那块 cramfs 里。
- 下轮（若继续）：
  1) 从镜像 0x427xxxx 抽 cramfs（或用 guest `rootfstype=cramfs` 挂载），取出 `ud_im.elf`；
  2) 在 bootin 包装脚本里先起 `ud_im.elf`，再起 `im.elf`，观察是否触发 THREAD_APP/渲染；
  3) 若 ud_im 需要 mf（镜像里没有），则此路也不通。

## ★★★ c339/c340（2026-10-23 07:45）：从 cramfs 抽出官方链组件，**mf 让 THREAD_APP 首次出现**
- 从镜像 `0x424200` 处抽出 cramfs（4.7MB）并解包成功：
```
/usr/bin/mf            (39KB ARM ELF, 官方"媒体/显示框架"进程)
/usr/bin/ud_im.elf     (updater/传感器初始化)
/usr/bin/UdtrMain.sh / execute_prod.sh / common.src / insmod.sh …
```
- `execute_prod.sh` 关键行：
```
LD_PRELOAD="/usr/lib/libSysDef.so" mf &
/usr/bin/sen.elf &
```
  ⇒ **官方链里 mf 是带 `libSysDef.so` preload 启动的**（我们此前从未启动过它）。
- c339（先起 mf，再起 im.elf）：
```
EXECVE /bin/mf
EXECVE /usr/bin/im.elf → im.elf.real
**THREAD_APP pid=133 出现**（此前在正常启动里从未出现过！随后 code=0 退出）
THREAD_INFRA0/1/2 也创建并退出
GPE 写入 = 0（显示初始化没走到）
```
- c340（mf + sen + 探针代劳 ai(0)）：GPE 写入 0、探针未打印 PV、无 trap —— 组合不稳定。
- 结论/下一步：
  1) **mf 是让 App 进入"应用阶段"的关键外部件**（THREAD_APP 因此被创建）；
  2) 但它随后立即退出（可能因为 IMDB 条目过滤 arg=0，或缺少 ud_im 的后续驱动）；
  3) 下轮：把 `ud_im.elf` 也放进镜像并按官方顺序启动（mf → sen → ud_im → im.elf），观察 THREAD_APP 是否真正跑起来并提交 GPE 命令。

## c341（2026-10-23 07:52）：官方顺序（mf→sen→ud_im→im.elf）
- 镜像里同时放入 `mf` / `ud_im.elf`（来自 cramfs）+ `sen.elf`，按官方顺序启动：
```
EXECVE /bin/mf → /bin/sen.elf → /bin/ud_im.elf → /usr/bin/im.elf → im.elf.real
THREAD_APP pid=136 创建后立即退出（code=0）
GPE 写入 = 0（显示未初始化），无 trap
```
- ⇒ mf 确实能让 App 走到"创建 THREAD_APP"，但 THREAD_APP 的执行体仍然空转退出
  （与手工 `ta(0)` 表现一致：IMDB 应用阶段过滤条件不满足）。
- 这条线的下一步很明确：**让 THREAD_APP 的 arg 非 0、或让 IMDB 应用阶段条目匹配**
  （需要知道 mf 传的 arg 与 IMDB globals[40] 的期望位），属于再次反汇编 mf→im.elf 的调用参数。

## c342（2026-10-23 08:00）：mf 的真身 = **updater 侧进程**（不是显示框架）
- 读 cramfs 里的官方脚本：
```
UdtrMain.sh:  ud_im.elf &  → execute_prod.sh（prod） / execute_ufp.sh（user/minor/service）
execute_prod.sh: LD_PRELOAD=/usr/lib/libSysDef.so mf &   ; sen.elf &
execute_ufp.sh:  同 preload 启动 sauu（固件更新）
```
- mf 内含 `UdrAPI` 字符串并调用 `osal_snd_sync_msg` ⇒ **mf 属于 updater/更新流程**，
  它让 THREAD_APP 出现是"UIPC 流量副作用"，并非显示驱动。
- ⇒ 正常 UI 启动链不在这份 cramfs 里（cramfs 是 updater initrd）；我们的 `bootin → im.elf` 才是 UI 逆向链。
- 阶段终态（本会话）：
```
✅ 显示链路/GPE 协议/执行器：全部打通并验证
✅ App 启动到 61 模块、管理器图/ViewManager 就绪
❌ App 的 UI 渲染循环仍未启动（需要外部驱动或补齐 Model/Widget 状态）
```

## ★★★ c343–c345（2026-10-23 08:05）：**GPE 合成步骤（src→dst）打通**
- 观察写入顺序：App 先发 11 次 `0x1800=4`（kick），**之后**才写帧描述符 `0x0C00..0x0C18`；
  因此把执行器挂到 **0x0C18（使能）写**上：
```
ZZGPECOPY2 src=0x080c4880 dst=0x08aec800 640x480 val=0x1
截图：中心像素 (66,138,222) —— 不再是黑/测试彩条，而是"从 src 拷到 dst"的真实内容
```
- ⇒ **GPE 的"合成/搬运"语义被正确实现**：App 给出的帧描述符（源/目标/几何/格式/使能）
  已经能驱动我们的执行器把内容搬到显示缓冲，并由 streamfb 呈现。
- 目前屏上显示的是 **src（0x080C4880）里的内容**，而该区域是 App 的临时缓冲（噪声）；
  一旦 App 真正往 src 里绘制，屏幕就会出现真实 UI。
- 产物：`artifacts/c345/gpe_copy.png`。
- 下轮：把执行器继续扩展（fill / blit / sprite 三种 opcode），并观察 App 是否会在 src 上作图。

## c346（2026-10-23 08:08）：执行器定稿为「compose 模型」，基线复测通过
- 关闭误导性的"红色填充"原型，保留两个正确语义：
  1) kick(opcode 4)：若帧描述符已设置 → 按 src/dst/geom 做 memcpy（合成搬运）；
  2) 写 `0x0C18`（使能）时同样执行一次合成。
- 基线复测（120s）：
```
kicks=11  copy=1  pVM=1,396,440  trap=0
```
⇒ 稳定；执行器不再注入假画面，只忠实执行 App 给出的帧描述符。
- 结论：**图形后端的"最后一公里"已完成**——App 给什么（源/目标/几何/使能），屏幕就显示什么。
  剩余问题唯一定义为：**App 尚未向源缓冲绘图**（业务/模型层缺失）。

## c347（2026-10-23 08:12）：**决定性证据：App 完全不写源缓冲**
- 探针对 App 的帧源缓冲 `0x080C4880`（640×480×2）在 t≈45s 与 t≈57s 各取一次校验和：
```
SUM45 sum=0x66A6CAA9 nz=151651
SUM57 sum=0x66A6CAA9 nz=151651   ← 完全相同
```
- ⇒ 12 秒内该缓冲**一个字节都没变**：App 从未向画布写入任何像素。
  内容 ~49% 非零，符合"未初始化内存"特征。
- 这把"App 不画"从推断变成了**直接测量的结论**。
- 至此本阶段所有方向都已闭环：
```
显示后端 ✅（能忠实执行 App 的帧描述符）
App 渲染 ❌（测量证明：不写画布）
原因：缺外部驱动 / 缺 Model-Widget-Scene 业务状态
```

## c348（2026-10-23 08:14）：源/目标缓冲**双双静止**
```
t=45s:  src(0x080C4880) sum=0x66A6CAA9 nz=151651   dst(0x08AEC800) sum=0xFC7EA03A nz=151629
t=57s:  src 同上                                    dst 同上
```
- 12 秒内两个缓冲**完全没有变化**（App 既不写源也不写目标）。
- 结合 c345（我们的合成把 src 拷到 dst 后可显示）⇒ 链路没问题，App 侧确实静止。
- 本会话可推进的部分到此为止；剩余工作已在 `docs/ZVE10_NEXT_STEPS.md` 中给出精确指引
  （地址、协议、假设、复现命令、坑位）。

## c349（2026-10-23 08:18）：把 GPE 事件返回值改成全 1 饱和 —— 无变化
- `fake_gles` 的 `0xc004820d` 事件返回值由 1 改为 0xFFFFFFFF（试图满足任何 event-bit 等待者）：
```
kicks=11（不变） gpew=28（不变） pVM=1,380,080  trap=0
```
- ⇒ 事件位不是门控点；已把模块回滚为返回 1（与既有结论一致）。
- 至此可在本会话内尝试的手段已全部用尽，最终状态见 `docs/ZVE10_NEXT_STEPS.md`。

## c350（2026-10-23 08:25）：定位到目标文档提到的 `CreateAndAddSequence`
- libObj 导出：`OBJAVBBCBASECLASS::SeqController::CreateAndAddSequence(unsigned, MWF::ObjMsg*)`
  （mangled `_ZN17OBJAVBBCBASECLASS13SeqController20CreateAndAddSequenceEjPN3MWF6ObjMsgE`）
  ⇒ 这是"序列/绘制请求"的入口之一，属于 **成员函数**（需要 SeqController 实例）。
- 同类还有 `GlobalAccessor::SendAppEvent(MWF::ObjMsg*)`、`SequenceBase::SetParameter/ReceiveResponse` 等，
  都需要各自实例或全局访问器；当前我们未能拿到 SeqController 实例（也没有工厂导出）。
- 若后续要继续：从 `OBJAVBBCBASECLASS` 的初始化路径里找 `SeqController` 的创建点（应随 view/model 初始化建立），
  拿到实例后调用 `CreateAndAddSequence` 提交一帧绘制请求，观察 GPE 命令是否出现。

## c351（2026-10-23 08:30）：环境整理
- 清理了 `tmp/` 下可重建的实验镜像（保留 `tmp/ab_tb_v225.img` 作基线母盘、`ab_v291.img`/`ab_v339.img`）：
  磁盘占用 **89% → 66%**（释放约 45GB）。
- 仍保留 `/mnt/build/ab_tb_v114..226.img`（100 个，133GB）——属队友实验产物，未删除；
  如需进一步释放可自行清理（`ab_v291.img` 可随时由 `tmp/ab_tb_v225.img` + 探针重建）。
- QEMU 无残留进程（pgrep=0）。

## c352（2026-10-23 08:35）：SeqController 实现在 **libmpr.so**（不是 libObj）
- 在 libObj 里 `SeqController::*` 全部是 **未定义导入（U）**：
```
U _ZN17OBJAVBBCBASECLASS13SeqController20CreateAndAddSequenceEjPN3MWF6ObjMsgE
U ...GenerateSequence / DelayCreateSequence / OnSeqLifeCycleChanged
U _ZN17OBJAVBBCBASECLASS20DefaultSeqController10CanExecuteEjPN3MWF6ObjMsgE
```
- 而这些符号来自 **`/usr/lib/libmpr.so`**（启动时 QMMAP 已见其被映射，约 21MB）。
- ⇒ "提交绘制序列"的实现体在 libmpr，与 App 业务（BBC/DDS 模型）同层；
  这进一步说明：**要让 App 落笔，需要 libmpr 层的模型/序列状态真正被初始化**，
  单靠调用某个导出函数不可行（实例与状态都在那层内部）。
- 下阶段建议（更新到 NEXT_STEPS）：从 `libmpr.so` 的 `SeqController`/`DefaultSeqController`
  初始化路径入手，找它们在哪创建、依赖哪些服务/配置，再决定是补服务还是补状态。

## ★★ c353/c354（2026-10-23 08:30）：打通"从 guest 抽取任意文件"的通道
- 方法（可复用，非常重要的能力）：
  1) 探针用 `/dev/mem` mmap **phys 0x21312000**（帧缓冲映射区偏移 16MB），
     把目标文件按 64KB 分块读入该物理区，并打印字节数（`CLIB <path> bytes=N`）；
  2) 宿主机用 **QMP `pmemsave`** 从 0x21312000 导出，再按 N 截断 ⇒ 得到原文件。
- 实测：
```
/usr/lib/libmpr.so     → 13,021,036 字节，ELF 头正确 ✅
/usr/lib/libMprSeq.so  → 7,548 字节（stub，只有 _init/_fini + 少量符号）
```
- 结论：`SeqController` 不在 libMprSeq（stub）也不在 libmpr（stripped 无该符号）；
  下一步应抽取 **`/usr/lib/libDDS.so`** 与 **`/usr/lib/libmprctrl.so`**（体积较大，最可能承载 BBC/序列实现）。
- 该通道的用途：**以后任何 guest 文件（库/配置/资源）都可以按需取出做静态分析**，
  不必再受"分区挂不上"限制。


## ★★★ c356（2026-10-23 08:40）：`SeqController` 实现在 **/usr/lib/libBizFw.so**
- 方法：探针 `dlsym(RTLD_DEFAULT, 完整 mangled 名)` 拿到运行时地址，再在 `/proc/self/maps` 里找归属映射：
```
CreateAndAddSequence  = 0x085B0D41  →  08571000-085BD000 r-xp  /usr/lib/libBizFw.so
DefaultSeqController::CanExecute = 0x085B0FB9（同一库）
```
- ⇒ 之前抽的三个库都排除：`libmpr`(13MB, 无该符号) / `libDDS`(7.5KB stub) / `libMprSeq`(7.5KB stub)。
- ⇒ **BBC 业务/序列层 = libBizFw.so**，这正是"让 App 落笔"的最后一层。
- 下阶段入口（已写入 NEXT_STEPS 第八节）：用抽取通道取出 libBizFw.so → 分析 SeqController 初始化与依赖 →
  触发一次 CreateAndAddSequence → App 开始往 0x080C4880 绘图 → 现有执行器自动上屏。


## c357（2026-10-23 08:40）：libBizFw.so 抽取成功（318,780B，29 个 SeqController 符号）
- 用 guest 抽取通道拿到 `/usr/lib/libBizFw.so`，`nm -D -C` 可见：
```
SeqController::CreateAndAddSequence(uint, MWF::ObjMsg*)            @0x3FD40
SeqController::SeqController(uint, ObjActiveBaseAvbbc*, GlobalAccessor*) @0x3FCDC
DefaultSeqController::CanExecute/EnqueueMsg/DequeueMsg
```
- **依赖关系明确**：要构造 SeqController，必须先有 `ObjActiveBaseAvbbc*`（BBC 活动对象）与 `GlobalAccessor*`，
  这正是"view/model 侧"从未建立的部分。
- 产物：`/mnt/build/tmp/libBizFw.so`（下阶段可直接反汇编分析）。


## c358（2026-10-23 08:45）：SeqController 构造/提交的静态分析
- 构造 `SeqController(uint, ObjActiveBaseAvbbc*, GlobalAccessor*)`：
```
[this+0] = vtable(SeqController)   [this+4] = r1(uint)
MWF::ObjLock 构造于 [this+8]
[this+36] = ObjActiveBaseAvbbc*    [this+40] = GlobalAccessor*
[this+44] = 0   [this+48] = 0        （若 GlobalAccessor 非空，还会调其 vtable 注册/回调）
```
- `CreateAndAddSequence(uint, MWF::ObjMsg*)`：
```
r3 = [this->vtable + 0x28] ; blx r3      ; → 是否可创建（返回 0 表示不可创建）
if (r0 != 0) { 记录日志（getPrintLevel()>0 时）… }
```
⇒ 直接用空/伪造对象调用会在 **vtable+0x28 这一步** 失败或崩溃（与之前观察一致）。
- 因此"最小可行路径"必须提供：**真实的 ObjActiveBaseAvbbc 实例**（随 view/BBC 建立）+ 合法 GlobalAccessor，
  这确认了 `ZVE10_NEXT_STEPS.md` 第九节的判断：缺的是 model/BBC 层状态，而不是某个开关。

## 会话结束（2026-10-23 08:50，Round 200/200）
- 本会话（c190→c358）在 ZV-E10 QEMU UI 逆向上的净产出：
  1) 显示后端全通并验证（GPE 协议解出 + 执行器实现 + 帧合成出图）；
  2) App 不绘制被**直接测量**证实（src/dst 缓冲 12s 校验和一致）；
  3) 阻塞点在字段级定位：`libBizFw.so::SeqController(uint, ObjActiveBaseAvbbc*, GlobalAccessor*)`
     缺实例与上游 BBC/model 状态；
  4) 建立两项可复用能力：guest 任意文件抽取通道、GPE 执行器骨架（QEMU 内）。
- 下一步入口：`docs/ZVE10_NEXT_STEPS.md`（第八/九节）。

## ★★ c359–c362（2026-10-23 10:25，补 model 层第 1 步）：**应用阶段入口库是 stub**
- c359（interpose 观察）：正常启动 120s 内
```
ObjActiveBaseAvbbc ctor 调用次数 = 0
SeqController ctor 调用次数     = 0
CreateAndAddSequence 调用次数   = 0
```
  ⇒ **BBC/序列层从未被初始化**（与"帧缓冲零变化"一致）。
- c360：`ai(0)` + `openView(viewBaseMenu)` 后仍是 0/0/0 ⇒ ViewManager 路径不会创建 BBC 对象。
- c361/c362：把 `/usr/lib/libBizInitializer.so` 抽取出来（7,772B），发现它的导出全是 **ARM stub**：
```
BIZ_Init                @0x740 : 00 00 a0 e3 1e ff 2f e1   = mov r0,#0 ; bx lr
Application_System_Init @0x758 : 同上
subapp_init             @0x760 : 同上
vssapp_init             @0x770 : 同上
DPro_App_UI_init        @0x780 : 同上
```
  ⇒ **IMDB 应用阶段（idx143–146）指向的四个入口，全都是"返回 0 的空实现"**。
  这解释了为什么"应用阶段"跑起来也什么都不做——这个机型/镜像里它们是占位 stub。
- 结论（重要）：
  1) UI 真实入口只能是 **libObj 的 `Application_System_Init`**（我们已在调用，pVM/gfx/res/wg 均因此建立）；
  2) BBC/序列层（libBizFw）需要另行初始化——它不是被 IMDB 阶段拉起的。
- 下一步：查 libObj 里 `Application_System_Init` 创建的 **Appframework 线程**是否真的进入其主循环
  （线程 wchan/PC 采样），以及它为什么没有产生 view 渲染。

## ★★ c363（2026-10-23 10:30）：线程活动采样 + 关键怀疑（固件镜像可能不是零售 UI 版）
- 探针用 `/proc/self/task/<tid>/schedstat` 采样 8 秒 CPU 运行时差：
  **没有任何线程有 >1ms 的 CPU 增长**（全是阻塞/空闲）⇒ App 整体静止，与帧缓冲零变化一致。
- 结合 c359–c362 的三个"0 调用"（ObjActiveBaseAvbbc ctor / SeqController ctor / CreateAndAddSequence）
  和 **libBizInitializer.so 的五个入口全是 `mov r0,#0; bx lr` 空实现**，得到重要怀疑：
```
本镜像的 /usr 可能是 **service/updater 变体**，业务层（BIZ/BBC/序列）被 stub 掉了：
  · IMDB 应用阶段期望的 appFw.so / subapp.so / gui.so / libVssApp.so 在 /usr/lib 中不存在；
  · 唯一提供这些符号的 libBizInitializer.so 全是空实现；
  · 因此即使把 IMDB 应用阶段跑通，也不会产生任何 UI。
```
- 如果这个判断成立，那么"零售 UI 版固件"才是下一步需要的输入；
  当前镜像下我们能做的只剩：给 uxengine/ViewManager 造数据（等于重写业务层），不现实。
- 待验证：`BIZ_Init` 是否在任何时刻被调用（interpose 计数）；若从未调用，则确认业务层完全未启用。

## ★★★ c364–c370（2026-10-23 10:55，补 model 层）：可以人工构造 BBC 对象了
- c364：interpose `BIZ_Init/subapp_init/vssapp_init/DPro_App_UI_init` → **全 0 次调用**
  ⇒ 业务层入口在正常启动中从未被调用。
- c365：主动调用 `DDSUserInit / InfraAvbbc_Init / grm_init / dmp_allocator_lib_init / CapIdSync_Init`
  → 均无副作用（BBC ctor 仍 0、GPE 仍 11 kick）。
- c366/c367：`AvbbcLogManager::Initialize()` 返回 1；`InitEnv4PcModeling` 需先 dlopen libBizFw 才可见。
- c368：`ObjAvbbcCmn::InitEnv4PcModeling(0)`（正确符号 `_ZN11ObjAvbbcCmn18InitEnv4PcModelingEj`）
  **返回 0（成功）**，但 BBC ctor 仍 0。
- c369/c370（关键）：**手工构造成功**
```
malloc ObjActiveBaseAvbbc(0x800) → ctor(0,"ScalarApp",0)   ✅ 无崩溃
malloc GlobalAccessor(0x400)   → ctor()                   ✅ 无崩溃
malloc SeqController(0x400)    → ctor(0,oab,ga)           ✅ 无崩溃
CreateAndAddSequence(seq,0,0)  → **进入后阻塞**（打印 "calling CAS" 后无返回，无 trap）
```
  ⇒ 这是全项目第一次把 BBC/序列对象造出来并真正进入提交函数；
  它**阻塞**而不是崩溃，说明：序列提交需要**序列执行者/命令接收者**（真机由 BBC 运行时提供）。
- 结论：
  1) 缺的确实不是"某个开关"，而是 BBC 运行时（序列执行线程 + 命令接收者 + ObjAvbbcCmn 环境）；
  2) `InitEnv4PcModeling` 成功但不会自动建这些；
  3) 若要继续，需要在 libBizFw 里找 **AddSeqFactoryToCmdReceiver / AddCmdFactoryToSequenceManager**
     （ObjActiveBaseAvbbc 的两个 weak 导出）以及命令接收线程的创建点，把运行时补起来。

## ★★★ c364–c374（2026-10-23 11:15）：**重要更正：BBC 不是 UI 层，是相机控制层**
- 已实证（本轮新增）：
```
BIZ_Init / subapp_init / vssapp_init / DPro_App_UI_init  调用次数 = 0（c364 interpose）
libBizInitializer.so 里这五个入口全是 ARM stub: `mov r0,#0 ; bx lr`
DDSUserInit/InfraAvbbc_Init/grm_init/CapIdSync_Init     调用后无任何副作用
ObjAvbbcCmn::InitEnv4PcModeling(0) 返回 0（成功），但仍不产生 BBC 对象
手工构造 ObjActiveBaseAvbbc + GlobalAccessor + SeqController 全部成功（无崩溃）
  → CreateAndAddSequence 进入后**阻塞**（无返回、无 trap）
libObj 只导出 InfraAvbbc_Init/Res/Sus；viewUnified2.so（14.3MB，已抽取）**0** 个 Avbbc 符号
```
- **定性更正**：`libBizFw.so` 的导出是 `SdiCh / HdmiCh / TcubCh / DetectCh / Fsm …`
  ⇒ **BBC 是相机音视频/基带控制**（Sdi/HDMI/Tcub/检测通道），`SeqController::CreateAndAddSequence`
  是**相机操作序列**，**不是 UI 绘制入口**。此前"用 SeqController 让 App 落笔"的判断应作废。
- 因此 UI 侧仍然回到原来的结论：App 的 uxengine/ViewManager 已建立，但**没有屏幕位图**
  （`GRM_screenGetOnBitmap()=0`、`GRM_fbrmSetScreenPreference()=-9999`），
  `View::drawDirect()` 因此崩溃；`BMP_Create` 因 grm_ma 分配失败返回 -9999。
- 真正可尝试的下一步（UI 方向）：
  1) 在 preload 里**完整替换 BMP_Create**（按真实 0x80 字节布局 + 52 字节数据块）返回自建位图，
     并同时替换 `GRM_bitmapGetPhysicalAddress/GetRowPixelCount/gpermFillrect/gpermRectblit`，
     让 uxengine 有一个"指向 VDF 的屏幕位图"可以画；
  2) 再调 `View::drawDirect()`，若成功即出画面（GPE 执行器已就绪）。

## c375/c376（2026-10-23 11:20）：用户态 BMP 后端首次拦截成功，但该 run pVM=0
- c376 探针实现了 **BMP_Create 的用户态替换**（真实失败时用真实 ctor `libObj+0x69B38C` 造 0x80 对象 + 52B 数据块，
  并把位图直接映射到 VDF `0x20312000`），并替换了 `GRM_bitmapGetPhysicalAddress / GetRowPixelCount / gpermFillrect`。
- 实测：
```
FBMP w=640 h=480 obj=0x0005F5A0 vdf=0x0F740000     ← 成功拦截并返回自建位图
DRW pvm=0x00000000                                 ← 但本 run ViewManager 为空
kicks=0                                            ← 且 GPE 完全未初始化
```
  ⇒ 这个 run 的 App 初始化没有走完（pVM=0、kicks=0），无法据此判断 drawDirect 是否可用；
  可能是我们的 BMP 替身影响了早期初始化，也可能是 run 间不稳定。
- 下一步（若继续）：
  1) 把 BMP 替身做成"**惰性**"：只在 `Application_System_Init` 之后（pVM≠0）才启用，
     避免影响早期初始化；
  2) 用稳定基线确认 kicks=11 → 再在 i=50 打开视图并 `drawDirect`，观察是否出现 gperm 绘制调用；
  3) 若 drawDirect 仍需"屏幕位图"，则同时替换 `GRM_screenGetOnBitmap` 返回我们的 VDF 位图。

## c377（2026-10-23 11:25）：BMP/GRM 拦截太靠前 → App 直接退出
- 把 BMP 替身做成"条件启用"（仅 pVM≠0 时接管）并增加 `GRM_screenGetOnBitmap` 替身：
```
Y9 worker / Y9 base=94228480     （探针正常启动）
ZZEXIT im.elf.real pid=143 code=0 （**App 进程退出**）
PV i=52 pVM=0 ；kicks=0
```
- ⇒ 即便条件启用，**替换 BMP_Create / GRM_screenGetOnBitmap 仍会让 App 初始化失败退出**（很可能这两者被早期引导路径使用）。
- 教训：这类替换必须做到**极窄**：
  1) 只在 `i>=50`（App 初始化完成后）由探针显式打开开关，之前完全透传；
  2) 或者不改 BMP_Create，而只替换 `GRM_screenGetOnBitmap` + `gperm*`（这三个只在绘制期调用）。
- 本轮未能取得 drawDirect 的结果；BBC 方向已证伪（相机控制层），UI 侧下一步是"极窄 GRM 替换 + 绘制期开关"。

## ★ c378（2026-10-23 11:32）：窄化 shim 生效，drawDirect 崩点定位到 libObj+0x8A4534
- 探针只在 i>=50 打开开关，仅替换 `GRM_screenGetOnBitmap / bitmapGetPhysicalAddress / GetRowPixelCount / gpermFillrect`：
```
SHIM on vdf=0x145A3000 fake=0x001B6F90
DRW2 pvm=0x00156CD0            ← App 正常初始化（ViewManager 非空）
DRW2 ov=1437168                ← openView(0x75) 返回非零
DRW2 dd                        ← 进入 View::drawDirect()
→ trap 0x0e, address=0x00000013, pc=0x06281534 (libObj+0x8A4534), lr 同址
```
- ⇒ **屏幕位图替身不足以让 drawDirect 工作**：它在更早的地方解引用了一个 `NULL+0x13` 的对象
  （崩溃点落在 `ViewManager::getModelInfo` 附近的函数体内），说明 view/model 侧仍缺对象。
- 结论/下一步（非常具体）：
  1) 反汇编 `libObj+0x8A4500` 区间的函数（`ViewManager::getModelInfo` 邻近），看它取的是哪个成员
     （很可能 `[view+?]` 或 `[modelInfo+0x13]`），据此构造对应的最小模型对象；
  2) 再回到 drawDirect；
  3) 屏幕位图替身（GRM_screenGetOnBitmap→VDF）本身已验证可用且不影响早期初始化——
     这条经验可复用（关键是**只在初始化完成后开启**）。

## ★ c379–c383（2026-10-23 12:55，UI 方向定位）：openView 是"发事件"，view 无 model
- c379/c380：用 `/proc/self/task/<tid>/schedstat` 与 `/proc/self/task/<tid>/stat` 的 utime 做 8 秒差分采样，
  **没有任何线程出现可测的 CPU 增长** ⇒ App 的线程全是阻塞态（不是空转）。
- c381：`View::drawDirect()` 反汇编为：
```
r0 = [this+0x24] ; b 0x8a4528
0x8a4528: r4=r0 ; if (ScalarAppfwIF::IsViewModeDiadem()) { r0=[r4+20]; b 0x8a6bc4 } else return
```
  实测我们拿到的 view 对象 `[view+0x24] = 0xFFFFFFFF`（无效 model 指针）→ 崩溃地址 0x13/0x14 正是 `[r4+20]`。
- c382：调用 `ViewManager::getModelInfo(pVM, id, 0..3, 0,0)` → **全部返回 0**（没有任何 model）。
- c383：用真实 `ux::core::ID::genID("viewBaseMenu")=0xB068A1C7` 再 openView → 返回 1384856，`[view+0x24]` 仍是 0xFFFFFFFF。
- 反汇编 `viewManagerIf::openView(int,int)`（libObj+0x140FB8）：
```
bl 0x140F40 ; 然后 tail-call 一个 PLT 入口（疑似"把事件投递给 App"）
```
  ⇒ **openView 是异步"发请求"**，返回的不是 View*，而是该投递调用的返回值（不同 id 返回同一值即可解释）。
  真正的 view/model 创建发生在 **App 自己的线程**里处理该事件时。
- 结论：UI 要出画面，必须让 **App 的事件处理线程**跑起来并处理 openView 事件；
  而当前所有 App 线程都阻塞（futex / osal_rcv_msg_tmo）。
- 下一步（下一轮）：定位 ai(0) 创建的那个 UI 线程的 tid 与其 wchan/等待对象，
  判断它是在等 UIPC 消息还是等 EventManager 条件变量，再决定注入方式。

## c384–c387（2026-10-23 13:10）：App 线程全景 + PC 采样（App 在"等事件"，不在计算）
- 线程全景（探针 dump `/proc/self/task/*/{comm,wchan}`，两次采样）：
```
134 im.elf.real (futex)   135/136 uipc_proxy_0/1 (__k_4u_wait_cb)  141 uipc_proxy_3
142 mwf_monitor_inp (hrtimer_nanosleep，周期性轮询)
143 im.elf.real (w=0 运行中)  146 THREAD_INFRA1 (w=0)  147 THREAD_INFRA2 (w=0)
149 grm_result(futex) 150 grm_gpe(futex) 191 uxengine_res1 192 uxengine_res2 (futex)
193 grm_dn_vdf_if (osal_rcv_msg_tmo)
148/151/152/153/158/159/160/162/164/165/169/171/178/183 …= osal_rcv_msg_tmo（等 UIPC 消息）
190 Appframework (w=0)
```
  ⇒ App 有完整的框架线程群，但**全部处于"等待"状态**（futex 或 UIPC 收包）。
- PC 采样（QEMU gdbstub `-gdb tcp::1234`，连续 6 次取 `$pc`）：
```
PC = 0x040A2146 ×6（落在 libpthread 的 futex/syscall 路径）
```
  ⇒ 采样期间 CPU 只在"等锁/等事件"的代码上，**不是**在渲染或忙循环。
- 结论：App 的 UI 事件处理线程没有可处理的事件源（没有任何输入/定时器驱动它），
  所以 view/model 永远不会被创建，`drawDirect` 拿不到 model（`[view+0x24]=0xFFFFFFFF`）。

## c388（2026-10-23 13:15）：`View::requestModel()` 也崩（对象不是完整 View）
- libObj 的 View 导出方法一览（有用）：
```
View::requestModel(int)                @0x89FFBC
View::compareModelStatus(int)          @0x89FFAA
View::hasModelControl()                @0x89FF98
View::requestApplicationExecute(Event*)@0x89FFE4
View::pushEvent(Event*)                @0x8A0040
View::drawDirect()                     @0x8A0074
View::getModelInfo(int,int,ParamList*,ParamList const*) @0x89FFD6
```
- 用 `openView(0xB068A1C7)` 的返回值当 View*，依次调用 `requestModel(0..3)`：
  **第一次调用即崩溃（trap 0x0e）**，说明 openView 的返回值并不是一个可用的 View 实例
  （其 `[+0x24]` 恒为 0xFFFFFFFF 也印证）。
- 结论（本目标内的最终结论）：
  1) **BBC/libBizFw/SeqController 与 UI 无关**（相机控制层），原目标前提不成立；
  2) UI 侧缺的是 **App 内部的 view/model 实例**，它们只能由 App 自己的事件线程在处理事件时创建；
  3) 而 App 的所有线程都在等事件（线程全景 + PC 采样证据），**没有任何事件源**驱动它；
  4) 因此在本镜像（libBizInitializer 全 stub、/usr/lib 无 appFw/subapp/gui/libVssApp）下，
     靠探针注入无法把 UI 拉起来。

## ★★★ c389–c392（2026-10-23 13:30）：**找到真机分区与官方启动脚本**（用户提示）
- 工作区里早就有真机分区 dump（我之前没用上，这是本轮最大的收获）：
```
/root/fw/nflasha15_fixed.img            ← 真机 /usr 分区（ext2，300MB，与运行镜像里的 libObj.so 同为 21,305,528 字节）
/root/fw/zve10_delivery/ZVE10_EMMC_FULL.img (1.9GB) + nflasha15/16/17/23/5/3/12/2/18/6/4.img
/mnt/build/fw/ZVE10_V203_unpacked/firmware.tar_unpacked/0800_appli/  （只是 setting/lens 数据）
```
- 真机 /usr/bin 里有关键脚本与二进制：
```
/bin/up.sh              ← 真机启动脚本（挂 /setting、读 mode、调 bootin）
/bin/change_mode.sh     ← 定义了 **dmode 语义**：
   0 内核启动 / 1 +global infra / 2 +LIRO / **3 +appFw boot（默认）** / 4..6 nounified driver 变体 / 7..10 调试变体
/bin/bootin.elf (15336) ← 真机启动器（使用 osal_* UIPC、Backup_read、fork/waitpid/execve）
/bin/im.elf     (23240) ← 真机 App（ELF）
```
- 实测（用真机 bootin + 真机 im.elf）：
```
c390: 脚本写 /bin/bootin.real → "not found"（运行时 /bin 是 initrd，真机分区挂在 /usr！）
c391: 改 /usr/bin/bootin.real target=qemu dmode=3 → 真机链跑起来了：
      bootin.real → sen.elf / launch_shell.elf / net_cmdd.elf / ash / im.elf
      但真机 im.elf **崩溃**（pc=0x056F3372, address=0），报 "Failed to mmap!"
c392: 保留我们的 LD_PRELOAD 包壳 + 真机 im.elf 二进制：
      kicks=11、gpew=28、trap=0、probe 已加载 —— **行为与自定义链完全一致**
```
- 结论：**启动器不是缺口**（真机链路跑通后 App 表现相同：只发 1 条 opcode=4，不绘制）。
- 下一步（有明确线索）：
  1) 真机 `up.sh` 会 `mount /setting` 并从 `/setting/mode/dmode` 取 dmode —— 我们的 initrd 可能没挂 /setting，
     App 要读的配置（语言/设置/CautionConfig）可能在那个 vfat 分区里；
  2) 用真机 `up.sh` 作为启动脚本（而不是我们的 bootin 包壳），把 /setting 挂载补齐；
  3) 再观察 kicks/GPE 是否出现绘制命令。

## ★★ 与"救砖"对话联调（2026-10-23 19:40）：定位到"缺对端服务"而非缺参数
- 对方（有真机）结论：C2 参数表不存在（运行时是数字 ID；`PRM_*` 字符串 596/599 只出现在日志格式串）；
  C4 视图机制 = `global.xdb` 给出资源名 → 派生 `ViewXxxToInstance` → **dlsym** 解析调用（无静态表）；
  `BACKUP.BIN.fixed` 是伪造（11B 全落在零区），真机原件是 `BACKUP.BIN.real`。
- 我方（QEMU）本轮实测：
  1) App 已加载的 UI 脉络（`/proc/self/maps`）：
     `global.xdb`(12KB) → `lang_cmn.uxc`(4KB) → `color_cmn.uxc`(4KB) → `viewUnified2.so`(15MB) → `image_cmn_43.uxc`(24MB)
     **但没有任何 `view*.uxc` 被映射** ⇒ view 实例从未创建（与对方 dlsym 假设一致，卡在更早处）。
  2) `/proc/osal/uipc` 全文（19,689 B）导出成功：
     ```
     Global: msgqs 430, sems 99, msgs 627, cbs 222
     Process 134 (im.elf): msgqs 25, msgs 0      ← 本地队列全空
     Process 128 / 114: msgqs 0
     GMSGQ 积压: 0x13A=609, 0x20=8, 0x139=3, 0xF9=2, 0x48=1
     ```
     ⇒ **整个 OSAL 世界只有 3 个进程**；发给外部子系统的消息堆在全局队列无人取。
  3) UIPC 风暴：`0x3E014D` 17,493 次、`0x8F013A` 1,350 次（`0x8F`=子系统，`0x013A`=本地队列 = 上面积压 609 的那个）。
- **根因判断（比"缺参数"更具体）**：QEMU 的自定义启动链只起了 `im.elf`(+`sen.elf`)，
  真机 init 会拉起 `launch_shell.elf` / `net_cmdd.elf` / `sen.elf` / `bootin.elf` 等一整套服务；
  缺了这些对端，App 的 UIPC 请求无人回应 → 重试 → 永远走不到创建 view。
- 已找到**零售 initrd**：`nflasha3:/initrd.img`（2,183,168 B, ext2），其 `/sbin/init` 字符串含
  `<<insmod start>> /usr/bin/launch_shell.elf <<init start>> /rootfs /usr/bin/bootin.elf /usr/bin/sen.elf /usr/bin/net_cmdd.elf /bin/ash --login`。
  用 `-initrd initrd_n3.img` 启动成功（init→bootin/net_cmdd/launch_shell），但 init 会 exit(1) 反复重启，
  我们的 bootin 包壳未把 im.elf 拉起来（probe=0）。
- 下一步：
  1) 修 `bootin.elf` 包壳（直接 exec `/usr/bin/im.elf`，不再依赖缺失的 bootin.real），
     并确保 `net_cmdd.elf`/`launch_shell.elf`/`sen.elf` 存活，作为 UIPC 对端；
  2) 复测 `/proc/osal/uipc` 的进程数与 0x13A/0x3E 积压是否消失；
  3) 基线改用 `BACKUP.BIN.real` 作为 ground truth（`.fixed` 标注为伪造）。

## c402–c404（2026-10-23 20:00）：bootin/dmode 路径彻底查清 —— 不是卡点
- c402（真机 bootin + `/setting/mode/dmode=3` + `preload=/lib/libgrmlog.so`）：
  `kicks=11 gpew=28 pVM≈1.40M trap=0` ⇒ **与直接跑 im.elf 完全一致**。
- c403：给 `im.elf` 包壳加 argv 记录，抓到 im.elf 的真实命令行：
```
CMDLINE[/usr/bin/im.elf.real debug target=qemu s g r u l i a lazy-global ]
```
  ⇒ **不是"选一个模式字母"，而是把 7 个字母（s g r u l i a）当成标志一起传**，
  外加 `debug`、`target=qemu`、`lazy-global`。
- c404（A/B：dmode=0）：
```
kicks=0 gpew=0 无 CMDLINE、无 PV  ⇒ App 根本没被拉起
```
  ⇒ dmode=3 才是"启动 App"的档位；dmode=0 连进程都不起。
- 结论：**bootin / dmode / LD_PRELOAD 这条路径已经完全排除**（两次独立验证 + A/B 对照）。
  下一步按与"救砖"对话的约定：hook `dlsym`（记录 `View*ToInstance` 请求与返回值）
  + 统计 `osal_valloc_msg_wait` 的 qid；并在之前先做一次便宜的 `IMDB_find_entry` 检查。

## c405–c406（2026-10-23 20:10）：`target=` profile 也不改变行为（第三个被排除的变量）
- c405：直接在 guest 里调 `IMDB_find_target_bit(name)`：
```
qemu=2   default=1   nfs=8   set=16   USBJ=-1   RE-SUB=-1
```
  ⇒ **qemu 返回 2 而不是 -1** ⇒ im.elf 不会走"静默降级"分支（对方担心的 `bit==-1 → 默认那套` 在 qemu 下不成立）。
  注意：`f=0x9144` 说明该符号解析到 **im.elf 自身**（0x8000-0xd000 区间），不是 libIMDB 里的实现。
- c406：A/B 把 `target=qemu` 换成 `target=default`（包壳替换 argv 后再 exec）：
```
MAPS libBizInitializer=0   libEMG=3   libAdjustUIProxy=0   libInfraMediaPwrMgr=0
MAPS viewUnified2=3        libObj.so=3
kicks=11  gpew=28  pVM≈1.41M  trap=0
```
  ⇒ **profile 切换确实生效**（qemu-only 的 `libAdjustUIProxy`/`libInfraMediaPwrMgr` 不再加载，
  default-only 的 `libEMG` 加载进来了），**但 App 行为完全不变**（kicks/pVM/GPE 与 target=qemu 一致）。
  `libBizInitializer` 仍未加载（enable=0x01 却没进，说明它还有额外加载条件）。
- 三个变量已被排除：**bootin 启动方式 / dmode 档位 / target profile**。
- 下一步（与对方约定的最后一项）：**dlsym hook**（记录 `View*ToInstance` 请求与返回值）
  + 统计 `osal_valloc_msg_wait` 的 qid —— 回答"卡在建 view 前还是 UIPC 前"。

## c407（2026-10-23 20:20）：**view 工厂 0 次调用** —— 卡点在"请求 view"之前
- 探针定义并拦截了 8 个 `View*ToInstance` 工厂（ViewRoot / ViewBaseMenu / ViewGlobalMenu /
  ViewSettingMenu / ViewCaution / ViewSnapShotBoot / ViewRootUtility / ViewRootSelector），
  命中即打印 `FAC <name>` 并按 `viewUnified2` 基址 + 偏移转发真实实现。
- 实测一次完整 run：
```
FAC 计数 = 0        （没有任何一次 view 工厂请求）
kicks=11  pVM≈1.41M  trap=0
```
- 与既有证据合并，停滞位置被夹在中间：
```
✅ 已发生：libObj 映射、viewUnified2 映射、global.xdb/lang_cmn/color_cmn/image_cmn_43 全部映射
❌ 未发生：任何 view*.uxc 的 mmap；任何 View*ToInstance 的请求
← 说明 App 在"资源就绪"与"选哪个 view"之间被卡住（不是工厂失败，是根本没走到请求）
```
- 结论：卡点应在**更早的初始化/等待**，而非 view 层本身；与 `/proc/osal/uipc` 里
  `0x13A` 积压 609 条、`0x3E014D` 17,493 次重发相吻合。
- 下一步两个方向：
  1) **UIPC 等待分析**：hook `osal_valloc_msg_wait`/`osal_snd_sync_msg`，拿 qid + 调用者 LR，
     定位"在等谁的回答"；
  2) **真机基准**（对方 B2）：真机 UI 正常时 im.elf 的 maps + cmdline，看真机在同一时刻
     是否已经 mmap 了某个 view*.uxc —— 用来确认我们缺的到底是"某条消息"还是"某个初始化调用"。

## ★★★ c408–c410（2026-10-23 20:45）：换成**纯净真机 /usr** 后，暴露了更底层的问题
背景：对方核对真机 `/usr/lib` = **209 项**，而我们镜像里是 **319 项 + 一个手写 `libBizInitializer.so`(7,772B, 全 `mov r0,#0; bx lr`)**
⇒ **我们的 n15 不是纯净真机 /usr**（早期实验加过 stub）。改用 `zve10_delivery/nflasha15.img`
（sha256 `d9ad9954…`，207 项）重跑：

| run | 组合 | 结果 |
|---|---|---|
| c408 | 纯净 /usr 直接换入（含其自带 kmod） | 开机即崩：`udm_udate`(pid 82) trap ⇒ **kmod 与本内核不匹配** |
| c409 | 纯净 /usr + **我们用惯的 /kmod**(103 个) | 内核正常，**im.elf 起来了但崩在 `InfraOnDmdLdr`**：`address=0x029C19E0 trap=0x0e`；崩溃前连续 `IMOPEN FAIL` 了 `libNVM.so` / `libSaveLoadSettings.so` / `libbkdmn.so` / `libInfraInterchipDatatrans.so` |
| c410 | 纯净 /usr + 纯净 kmod + **零售 initrd**(nflasha3) | 不崩，但只反复 `EXECVE /bin/ash`，bootin/im.elf 未被拉起 |

**关键推断（这是本轮最重要的结论）**
- 我们旧镜像里那 **110 个额外文件**（含 `libNVM.so` / `libSaveLoadSettings.so` / `libbkdmn.so` 等的 stub）
  一直在**掩盖"缺库"失败**，让 im.elf 一路跑到 `kicks=11`；
- 换成纯净 /usr 后，同样缺这些库，**im.elf 却在 `InfraOnDmdLdr`(按需加载器) 里段错误**；
  而真机同样缺这些库却不崩 ⇒ **差异不在 /usr，而在我们环境的其它部分**（内核模块集 / /system(nflasha3) / /setting / initrd 版本）。
- 现存环境是"混搭"：我们的内核 + 部分自制模块 + 纯净 /usr；**版本与模块集不自洽**。

**下一步（下一步必须做的一致性重建）**
把所有启动件统一到**同一份固件版本**上再谈 App：
1. 内核 + initrd 取 `nflasha3/boot`（`vmlinux.bin` / `initrd.img`，v2.03）而不是我们改过的 zImage；
2. `/kmod` 取同一版本的 `nflasha15/kmod`（不要混用我们的 `grm_gles.ko`）；
3. `/system`(nflasha3) 与 `/setting`(nflasha2) 用 dump 原样；
4. 之后再决定探针怎么注入（用 `/setting/mode/preload` 即可，无需替换 im.elf）。

## ★★ c411（2026-10-23 21:00）：内核 vermagic 已对齐真机，但原厂模块仍崩 —— 闸门不止 vermagic
- 按对方给的结论重建内核：`CONFIG_LOCALVERSION="+"` + `LOCALVERSION_AUTO=n`
（`localversion-rt` 里已有 `-rt106`，所以 release 拼成 **`3.0.27_nl-rt106+`**，与 Sony 完全一致），
  用 `LD_LIBRARY_PATH` 指向工具链 compat 目录后编译成功：
```
include/config/kernel.release = 3.0.27_nl-rt106+
arch/arm/boot/zImage          1,769,480 B
fake_gles.ko vermagic        = 3.0.27_nl-rt106+ SMP preempt mod_unload ARMv7   ← 也已对齐
```
- 但用 **纯净 /usr + 原厂 kmod + 对齐后的内核** 启动（ab_v411）：
```
内核线程 udm_udate(pid 82) 仍然 trap（address=0, trap_no=0）
probe=0  kicks=0  gpew=0
```
⇒ **vermagic 只是第一道门，过了它原厂模块仍然崩**：我们的内核树是 `a7c-kernel` 的 **QEMU 变体**
（机型和外设是仿真出来的），而原厂 `udate/dmm/grm_ma/...` 期待**真实外设**，访问即 trap。
- 结论（重要）：**"纯净 /usr + 原厂模块"这条路在 QEMU 里走不通**，除非我们的 QEMU 机型把这些外设也仿真出来；
  现实可走的是"混合"：**纯净 /usr + 少数必需模块用我们的仿真版**（如 `grm_gles.ko`）——但要逐个甄别哪些模块必须原厂、哪些必须仿真。
- 当前仍成立的事实：vermagic 对齐这一步**永久有价值**（以后要加载任何原厂模块都必须如此），已写进配置；
  fake_gles 已同步重编为同一 vermagic。

## c412（2026-10-23 21:10）：混合集第一版（udate→stub + fake_gles）—— 崩溃消除，但启动停在模块装载阶段
- 对方从零售 initrd 的 `/sbin/init` 内挖出**35 条 insmod 表**，并给出分层：
  T1 引导必经(6) / T2 UI 前必需(21) / T3 硬件强绑(udate/liro/hdmi/cec/accel/mag/compass/usbg_*/bcmdhd) / T4 缺失容忍(nfc/sata_drv/msen_drv)
- 我方建了 `udate_stub.ko`（`module_init` 直接返回 0，无线程），vermagic 已对齐 `3.0.27_nl-rt106+`；
  并替换 `grm_gles.ko` → 我们的 `fake_gles.ko`（提供 `/dev/dmpgles2`）。组合成 ab_v412。
- 结果对比：
```
ab_v411（纯净 /usr + 原厂 kmod）      : udm_udate trap（address=0）          ← 崩
ab_v412（udate→stub + fake_gles）     : trap=0  无崩溃，但停在模块装载/挂载阶段
                                        probe=0  kicks=0  gpew=0
                                        日志尾部：MODLOAD attempt ×22 + AL_EMMC_SEQ tuning 警告
```
- 注意：`mounting1 /dev/dnflasha* failed errno(6)` 这 3 条**在历史成功 run 里也一直存在**（init 会走回退分支并成功），
  所以它们不是本次停滞的原因，不要误判。
- 结论：udate 这个 T3 模块确实是崩溃点，替换有效；但要走到 chroot/im.elf 还需要**逐个解决后续模块**
  （当前卡在模块装载/挂载阶段，需要更细的模块级 trace 才能定位是哪一个）。
- 下一步：
  1) 给内核加简单的 `MODLOAD` 名字打印（或改用 initrd 的 insmod 脚本显式逐条加载并打日志）；
  2) 按 T1→T2 顺序，只加载必需模块，逐个排除；
  3) `grm_gles` 用 fake_gles 已证明可行（对方：真机 UI 不需要真实 GPU 命令流）。

## c413（2026-10-23 21:18）：eMMC 是 SDR 而非 HS200（对方真机 klog 对照证实）；初始化脚本不可插桩
- 真机 vs 我们（klog 对照）：
```
真机：eMMC: HS200 mode ×4（0.065s 就辨识出 nflasha/nflashaB0/nflashaB1）
我方：eMMC: SDR mode；AL_EMMC_SEQ ... Software Tuning is forbiden for non-HS200（反复）
      + BKOPS/HPI/Sanitize not supported + OCR/EXT_CSD discrepancy
```
⇒ 我们的 QEMU eMMC 模型是 **SDR 卡**，驱动退化为"软件 tuning"而软件 tuning 对 non-HS200 被禁止 → 反复重试，
   极可能拖死后续 insmod/挂载（与 "停在模块装载阶段" 吻合）。
- 想在 guest 里做 `dd` 计时验证（对方建议 #1），但 **探针根本没加载**（App 没起来）→ 无法从 App 侧测。
- 重要发现：**我们的 initrd (`handoff_p0/initrd_q4.img`) 与零售 `nflasha3/INITRD.IMG` 是同一份**（同为 2,183,168 B，
  内部 `/sbin/init` 就是那个 28,580 B 的二进制），**且 initrd 里没有 `/bin`、没有任何 shell**。
  ⇒ 想插桩 init（逐模块打日志 / 早期 dd 计时），必须先把 busybox 塞进 initrd 并用自己的 init 脚本替换。
- 下一步（两条并行）：
  1) **给 QEMU eMMC 模型补 HS200**（或让驱动认为支持），目标：klog 出现 `eMMC: HS200 mode`、tuning 告警消失；
     这是"能不能继续往下走"的前置条件；
  2) 同时准备**可插桩 initrd**：塞 busybox + 自定义 init（挂载/逐条 insmod + 日志 + 早期 dd 计时）。

## ★★ c414–c416（2026-10-23 21:40）：**模块级日志打通，定位到 upm.ko**
方案 A（用户选的"可插桩 initrd"）最终用**更省的做法**实现：不改 initrd，而是**给内核 `load_module()` 打日志**：
```c
kernel/module.c: load_module()
  + printk("MODLOAD read ok len=%lu\n", len);
  + 末尾 printk("MODLOAD OK name=%s\n", mod->name);
```
（一次编译错误 `Elf32_Ehdr has no member name` 后改用 `mod->name` 打印成功。）
重建内核（`CROSS_COMPILE=arm-buildroot-linux-gnueabihf-` + `LD_LIBRARY_PATH=.../compat`）后跑 ab_v412：

- 22 次模块装载，按尺寸对照 `nflasha15/kmod` 得名字（**顺序即装载顺序**）：
```
4896 osal_utm → 38960 osal_uipc → 20156 utimer → 73764 backup → 38992 osal_ulogio
→ 33768 input → 22392 udate_stub → 12948 indctr → 14212 i2c → 21912 sircs
→ 3384 stream → 4144 stream2 → 17476 usb_portMonitor → 31204 usbg_sen
→ 14796 usb_extcmd → 35152 kikilog → 85588 dmm → **137260 upm ← 最后一个**
```
- 日志到此为止：**upm.ko 之后没有任何 MODLOAD attempt**，随后只有反复的
  `AL_EMMC_SEQ ... Software Tuning is forbiden for non-HS200`。
⇒ **卡点收敛到 `upm.ko` 的 init（或紧随其后的模块装载被阻塞）**，且 eMMC tuning 告警与它同时间段出现。
- 下一步（明确）：
  1) 把 `upm.ko` 也换成 stub（照 udate 的做法），看是否继续往下走（下一个应是 grm_ma/grm_gles）；
  2) 若换上 stub 后 eMMC tuning 告警消失 ⇒ 说明 tuning 是 upm 触发的，可一并定性；
  3) 顺带保留 `MODLOAD read ok` 日志（对后续每个模块的定位都有效）。
- 注：之前尝试"塞 busybox 进 initrd 替换 /sbin/init"失败（`/bin/busybox(init)` 崩在 address=4），
  已放弃该路线，改用内核日志方案（更干净、零副作用）。

## ★★★ c417–c419（2026-10-23 22:00）：逐模块替换 stub —— **从"卡在模块装载"推进到"im.elf 已启动"**
沿用 udate 的 stub 模板，按崩溃顺序逐个替换（每个 stub 都 `module_init` 直接 return 0）：

| run | 新增 stub | 模块装载数 | 结果 |
|---|---|---:|---|
| c412 | udate | 22 | 停在 upm |
| c417 | + upm | 26 | 继续装载 grm_ma / fake_gles / accel / mag，随后 **`udm_mag` 内核线程崩** |
| c418 | + accel / mag / compass | **32** | **`/usr/bin/im.elf` 启动成功**（EXECVE 出现），随后用户态崩（trap 0x0e, address=0） |
| c419 | + backup | 32 | im.elf 仍启动，用户态崩（trap 0x0e, address=0）；**内核 oops 消失**（此前 `backup_suspend+0x828` NULL 解引用） |

关键进展（历史性）：**这是第一次让"零售 init → chroot → bootin → im.elf"整条链在纯净 /usr 上跑起来。**
- c418 之前：卡在内核模块装载（upm 之后无任何 MODLOAD）
- c419：模块全部装完（32 个），im.elf 进入用户态、映射了 `libSysDef.so` / `libMWF.so` 等
- 新问题：im.elf 用户态 `address=00000000, trap=0x0e`（NULL 解引用）——很可能因为我们 stub 掉了 `backup.ko`，
  导致 `libbackup.so` 的备份读写失败（真机上 `/usr/bin/im.elf` 依赖 Backup_read 读射频校准）。
- 另：探针（probe=0）没有加载 —— 纯净 /usr 下 `/usr/bin/im.elf` 是原版（没有我们的 LD_PRELOAD 包壳），
  而 `bootin` 是否真的读了 `/setting/mode/preload` 尚未验证；下一步需要把探针装进去才能继续观测。

### 下一步（顺序）
1) 让探针能被装入：确认 `/setting` 在 im.elf 启动前已挂载 + `bootin` 对 `preload` 的处理（或退回"包壳替换 im.elf"的注入方式）
2) 定位 im.elf 用户态 NULL 解引用：优先怀疑 `backup` 被 stub 后的连锁反应；
   备选方案是**不 stub backup**，而是给它提供"最小的 /dev/backup 语义"（返回失败但结构合法）
3) 用带 `MODLOAD` 日志的内核继续跑，保持每次只改一个变量。

## ★★ c420–c421（2026-10-23 22:40）：发现 upm/backup 的相互依赖（重要，纠正之前的 stub 策略）
单变量对照（都基于 accel/mag/compass 已 stub 的 ab_v418）：

| run | upm | backup | 结果 |
|---|---|---|---|
| c418/419 | **stub** | stub | 32 模块 + im.elf 启动，用户态 NULL 崩 |
| **c420** | **原厂** | stub | **32 模块 + im.elf 启动**（upm 不再卡）→ 用户态崩 |
| **c421** | **原厂** | **原厂** | **只有 22 模块就停住**（停在 upm），无崩溃 |

⇒ 结论 1：**upm 原本"卡死"并不是 upm 自己的问题**——它需要 accel/mag/compass 正常；
   把传感器 stub 掉之后，**原厂 upm 可以正常装载**。所以 **upm 不该被 stub**（我之前 stub 它是误判，已纠正）。
⇒ 结论 2：**原厂 backup 会让流程停在 upm**（backup 持有/等待某个与 upm 相关的同步），
   而 **backup 被 stub 后 upm 才能继续** ⇒ backup 与 upm 之间存在启动期同步依赖。
⇒ 结论 3：im.elf 的用户态崩溃点在 **`libMWF.so` 内**（pc=0x058AB372 落在 libMWF 映射区，address=0），
   崩前最后几条是 `UIPC ... f0=0x3E014D`（**backup 服务端点**）⇒ 因为 backup 被 stub，服务无人应答，libMWF 拿到 NULL 后解引用。

### 因此 ① 的正确目标（下一轮）
不是"随便写个 backup stub"，而是要**提供可用但不会阻塞 upm 的 backup 服务**。两条路：
1) 写一个最小 backup 内核模块：导出 `Backup_read/Backup_check/backupfile_*` 等符号，并实现
   `/dev/backup`（或 libbackup.so 实际使用的接口）从 RAM 里返回一份备份数据（可用真机 `Backup.bin` 1,235,836 B 作为内容）；
2) 或者给原厂 `backup.ko` 打补丁，跳过它会阻塞 upm 的那段同步（需定位 backup_suspend / async_helper）。
下一步先查清 **libbackup.so 到底怎么和内核通信**（ioctl 号 / 设备名 / 符号），再决定 1 还是 2。

## ★★★ c422–c423（2026-10-23 22:50，新 goal 第 1 轮）：探针在纯净 /usr 上注入成功，35 模块、不崩
- c422（长跑 330s 对照，检验"eMMC 慢导致卡住"假设）：
```
c421/c422（原厂 backup）: 22 模块后停死，330s 仍无进展；tuning 告警 374/390 次
c418/c419/c420（stub backup）: 32 模块，im.elf 启动；tuning 告警 582–623 次
```
  ⇒ **tuning 次数与"卡不卡"无相关性**；卡死只在"原厂 backup"时出现 ⇒ **不是 eMMC 慢，是 backup 自身阻塞 upm 初始化**。
- c423（把探针装回纯净 /usr 链）：用"替换 `/usr/bin/im.elf` 为 LD_PRELOAD 包壳 + 真身 im.elf.real"的方式注入：
```
probe=1        ← 探针成功加载（纯净 /usr 下第一次）
modloads=35    ← 模块装载数从 32 → 35（比之前更多）
trap=0         ← 无崩溃
PV i=56 pVM=0  ← 但 ViewManager 仍是 0
Y9 rm_init=1   ← ResourceManager 初始化返回 1（失败）
Y9 i=0 gfx=0 res=0 wg=0 w0=1 w2=120   ← GraphicsEngine / ResourceManager / WidgetSystem 全为 0
```
  ⇒ 结论：**注入链路已打通、启动链已稳定到 im.elf 内部循环**，但 App 的管理器图没建起来
  （`gfx=0 res=0 wg=0`），卡在更早的初始化（可能仍是 backup 服务缺失导致的连锁，或时间/顺序问题）。
- 下一步（明确）：
  1) 查 `rm_init=1` 的含义（ResourceManager::init 返回非 0）；
  2) 在探针里把 backup 的 UIPC 端点（0x3E014D）**注册成应答方**，看是否能让管理器图建立（这是"最小 backup 服务"最省的做法，先纯用户态）；
  3) 保持每次只改一个变量，继续用 `MODLOAD` 日志 + `Y9` 日志判定。

## c424–c426（2026-10-23 23:05，新 goal 第 2 轮）：纯净 /usr 上"管理器图不建立"的原因排查
- c424（线程表）：纯净 /usr 下 App 的线程都在（`THREAD_INFRA0`×3、`InfraOnDmdLdr`、`uipc_proxy_0..3`、
  `ACCESSLOG_MAIN`、`mwf_monitor_inp`、`InfraDecoder/Encoder/...`），**但没有 `Appframework` / `uxengine_res*` 线程**。
- c425（**不调用** `Application_System_Init`）：`pVM=0` 一直到 i=56 都没变
  ⇒ **im.elf 自己不会初始化 App 框架**（它只 `dlopen("/usr/lib/libObj.so")`，是否调用取决于 IMDB 判定）。
  也就是说：**在纯净 /usr 上，必须由我们主动调用 `Application_System_Init`**。
- c426（把脏镜像里那 9 个自制 stub 库——`libNVM/libSaveLoadSettings/libbkdmn/libAtn/libI2CRight/libi2cuipc/libInfraError/libInfraInterchipDatatrans/libread_refresh`——补回纯净 /usr）：
  `gfx=0 res=0 wg=0` 仍然不变，而 `IMOPEN FAIL` 从 163 涨到 422 ⇒ **这 9 个不是关键依赖**。
- 现状小结：
```
纯净 /usr：35 模块装载成功、im.elf 常驻、线程齐全、无崩溃  ✅
但：Application_System_Init 被调用后 ViewManager/GraphicsEngine/WidgetSystem 仍是 0
    （脏镜像里它们是 1.4M/526k/528k，说明脏镜像里另有一些东西让初始化成功）
资源侧已排除：/usr/share/app 在纯净镜像里同样是 310 项（资源不缺）
```
- 下一步候选：
  1) 用二分法在脏镜像那 ~110 个自制库里定位"哪一个让管理器图能建立"（每次加一半，跑一次）；
  2) 或者直接看 `Application_System_Init` 反汇编，找出它在什么条件下才创建管理器/线程，
     把我们缺的前置条件补齐（比盲试快）。

## c427–c429（2026-10-23 23:15，新 goal 第 3 轮）：Appframework 线程存在但阻塞在 futex
- c427（interpose `osal_create_thread` + 线程表）：抓到 5 次线程创建，最后一次正是 `Application_System_Init` 发起的：
```
OCT a=0x072D7F24 b=0 c=1 d=0x6CF8        ← d=0x6CF8 与 ai 里 GOT 槽偏移一致
OCT ret=0x000000AA (tid 170)
线程表：tid=170 c=Appframework w=futex_wait_queue_me   ← 线程建起来了，但阻塞在 futex
```
  ⇒ **根因不是"线程没建"，而是"线程建了但卡在 futex"**，所以管理器图（gfx/res/wg）永远建不出来。
- 内核 ZZFUTEX 日志给出它等待的地址：
```
ZZFUTEX Appframework pid=170 op=0x81 uaddr=0x08FF6CCC lr=0x0409CD8D
ZZFUTEX Appframework pid=170 op=0x80 uaddr=0x072DFF44 lr=0x0409FD5F
```
  （0x0409xxxx 是 libc/pthread 的 futex 封装；uaddr 均落在进程堆/静态区，正好对应 libObj .bss 里的线程对象附近）
- c428（扫描该线程栈找 libObj 返回地址）：拿到 `tid=170 ss=0x93834944`，但扫描区间内**没有** libObj 代码指针
  （说明它当时停在 libc 的等待里，libObj 帧不在扫描窗口内或被优化掉了）。
- c429（尝试在用户态注册 backup 端点 0x3E014D 作应答方）：
```
osal_reg_msg_queue_cb(0x3E014D, cb) → rc=-1024（无效）
```
  ⇒ 该 API/该 id 组合不对（0x3E014D 是"子系统 0x3E + 本地 0x014D"的全局形态，
  注册本地队列要用本地 id，或要用另一个注册 API）。需要先弄清 im.elf/libMWF 用的注册方式。
- 下一步：
  1) 用 im.elf/libMWF 实际用的注册 API（对照 libMWF 的导入：osal_reg_msg_queue_cb / osal_reg_sync_msg_queue_cb）
     并试本地 id（0x014D）注册；
  2) 或直接反汇编 libObj 的 Application_System_Init 线程入口，看它第一步等的是什么（futex 的 owner 是谁）。

## ★★★ c430（2026-10-23 23:20，新 goal 第 3 轮续）：找到 Appframework 卡住的**具体函数**
对方建议"别急着通读 `Application_System_Init`，去看它内部每一步"——按这个思路反汇编 `createRequestModelExecuteEvent`
（`libObj+0x8A2314`，`Application_System_Init` 在起线程前调用它）发现：

```asm
8a2314  push {r4,r5,r6,r7,lr}
8a231a  mov  r6, r1                 ; r1 = name 指针
8a2328  movs r2, #52                ; ★ 请求 52 字节消息
8a232a  ldr  r0, [pc]               ; 消息池句柄/参数
8a232e  blx  osal_valloc_msg_wait@plt   ; ★★ 这里可能阻塞
8a2332  cbnz r0, 8a237a             ; 失败则走错误分支
```
⇒ **`createRequestModelExecuteEvent` 第一件事就是 `osal_valloc_msg_wait(…, 52)`**，
而 Appframework 线程实测阻塞在 futex —— **极可能就是卡在这次消息分配/等待上**。

同轮从纯净环境导出的 `/proc/osal/uipc`（15,026 B）：
```
Global: msgqs 273, sems 71, msgs 441, cbs 63
fblock: (19456) free=266752          ← 空闲块充足
realtime vblock: 13A(28768) / 3C(128) / 48(64) / F9(128) …
有积压的队列(M>0)：13A=428, 20=4, 3C=2, F9=2, 48=1
```
⇒ **441 条消息里 428 条堆在队列 `0x13A`**（占 97%），该队列没有消费者。
（注意：这次 pristine 环境是 428 条，之前脏环境是 609 条，形态一致。）

下一步（下一轮直接做）：
1) interpose `osal_valloc_msg_wait`：记录参数、返回值、阻塞时长 —— 直接确认 Appframework 是否卡在这里；
2) 查 `0x13A` 是谁的队列、谁应该消费（对照 im.elf/libMWF 的注册与收包线程）。

## c431–c436（2026-10-23 23:58，第 4–5 轮）：**因果链补全 —— stub backup 导致 ulogio 等模块装载失败 → 消息池耗尽**
- c431（interpose `osal_valloc_msg_wait`）：30 次调用**全部立即返回 NULL**（dt=0–1cs，不阻塞），
  其中 28 次目标是 **qid `0x8F013A`**、调用者 lr=`0x0000B0DF`（im.elf 自身）⇒ **im.elf 在疯狂重试申请消息**。
- c432（先启动 `/usr/bin/ulogio.elf` 再跑 im.elf）：日志暴露了真正的错误：
```
osal_ulogio: Unknown symbol Backup_read (err 0)
osal_ulogio: Unknown symbol Backup_get_datasize (err 0)
init ko_insmod: error(2): insmod /usr/kmod/osal_ulogio.ko ...
```
  ⇒ **我们把 `backup.ko` 换成纯 stub 后，`osal_ulogio.ko` 因符号缺失装载失败**
  ⇒ 日志子系统缺失 ⇒ 日志消息堆在 `0x8F013A` 无人消费（428 条）⇒ 消息池耗尽 ⇒ 后续 `valloc` 全 NULL ⇒ App 初始化停摆。
  **这是一条完整因果链**（对方关于"池耗尽"的判断方向正确）。
- c433/c434：给 backup stub 补导出符号。第一次失败原因很具体：
```
init ko_insmod: error(2): insmod /usr/kmod/backup.ko Backup_load=1
backup_stub2: Unknown parameter `Backup_load'
```
  ⇒ **init 会给 backup.ko 传模块参数**（`Backup_load=1` 等），stub 必须声明同名 `module_param`。
- c435（按原始 backup.ko 的 145 个导出符号**全量生成 stub**，并接受参数）：
```
Unknown symbol 归零（所有依赖模块都能解析 backup 符号）
但下一层崩溃：PC is at mspace_malloc+0x530/0xc64 [grm_ma]
             LR is at mspace_memalign+0xa0/0x1b8 [grm_ma]
             r6=0x5A5A5A5A（毒值）, r8=0x54000000
```
  ⇒ grm_ma（GRM 内存分配器）在初始化时崩，疑似访问/分配 `0x54000000` 附近的保留内存（我们 QEMU 机型可能没有这块）。
- c436（把 backup stub 改"成功返回+零填充"）：**退回 22 模块**（upm 又卡住）⇒
  **upm 的阻塞与 "Backup_* 返回成功" 相关**，所以"全 -1"的 stub3 才是能推进的版本。

### 结论与下一步
```
✅ 已确认完整因果链：backup stub 缺符号 → osal_ulogio 装载失败 → 日志队列 0x8F013A 堆 428 条
   → 消息池耗尽 → valloc 全 NULL → Appframework 无法建管理器（无 UI）
✅ 已做出"能装载 145 个 backup 符号 + 接受模块参数"的替代模块（backup_stub3.ko）
❌ 新卡点：grm_ma 初始化崩（mspace_memalign，毒值 5A5A5A5A，疑似 0x54000000 保留内存）
下一步：确认 grm_ma 的保留内存窗口（是否需要在 QEMU 机型里补一块 RAM/region），
       或临时 stub 掉 grm_ma 看链路能否继续（代价：GRM 位图分配不可用）。
```

## c437–c440（2026-10-24 00:15，第 6 轮）：grm_ma 替换后**完全不崩了**，但管理器仍不建；并修正一个误读
- c437 失败于**磁盘介质错误**（`sd 2:0:0:0 [sdc] Sense Key : Medium Error`，sector 295365552），
  `ab_v435.img` 在 453MB 处不可读 ⇒ 已用 `ab_v424` + stub 重建（新镜像 ab_v438）。**磁盘有坏道，后续注意校验。**
- c438（`backup_stub3`(145 符号+参数) + `grm_ma_stub`）：
```
probe=1  modloads=35  trap=0   ← grm_ma 的 mspace_malloc oops 消失，全程稳定
但 gfx=0 res=0 wg=0 pVM=0
```
- c440（同一镜像跑 **330s**）：仍然是 `gfx=0 res=0 wg=0` ⇒ **不是"时间不够"**，是真的一直没建。
- ★ **修正一个误读**：c431 里 `osal_valloc_msg_wait` 返回 0，我之前当成"返回 NULL（失败）"，
  但 OSAL 的约定是 **0 = 成功**（消息指针通过第二个参数 out 返回）。所以
  **"消息池耗尽"这个结论不成立**，那条因果链要撤掉一半（ulogio 装载失败→日志堆积仍成立，但"池耗尽导致 valloc 失败"不成立）。
- 现在的真实状态：
```
✅ 启动链稳定：35 模块、无内核 oops、探针常驻、可以跑 330s 不动
❌ 管理器图（gfx/res/wg）依然为 0 —— 调用 Application_System_Init 后线程存在（c427: tid170 Appframework），
   但它不产出管理器；且 330s 也不产出 ⇒ 不是等待，是逻辑上就没走到
```
- 下一步（下一轮）：
  1) 反汇编 `ux::gfxeng::GraphicsEngine::sys_getInstance` / `ResourceManager::sys_getInstance`，
     确认单例是"首次调用即创建"还是"必须由某个 init 创建"；
  2) 若是后者，找出那个 init（很可能在 `DPro_App_UI_init`(gui.so，本机缺失) 或 libObj 内部某 init 里），
     评估能否由我们直接调用。

## ★★★ c441–c446（2026-10-24 00:40，第 7 轮）：**重大澄清 —— 脏镜像里的"管理器"是我们探针自己造的**
- 对照实验（同一颗内核、同一 initrd、同一探针逻辑）：
```
c443（脏 /usr ab_v291）: viewUnified2 映射 7 处 → W5 "call init" 执行 → IA 循环调 1768 个函数指针
                        → gfx=533272 res=115579488 wg=535392 pVM=1.39M   ← 管理器出现
c438/c441/c442/c444/c445（纯净 /usr）: viewUnified2 映射 0 处 → "W3 no rw line" → W5/IA 不执行
                        → gfx=0 res=0 wg=0 pVM=0
```
⇒ **结论（重要）**：脏镜像里那些"管理器"并不是 App 建起来的，而是**我们探针找到 viewUnified2 的 rw 段后，
主动调用了它的 init 和 1768 个函数指针**（menuc251 里的 W5/IA 逻辑）才出现的。
  也就是说：**至今为止，"App 自己建管理器"这件事从未发生过**——此前把它当成进展是误读。
- 因此真正的卡点变成：**为什么纯净 /usr 下 `viewUnified2.so` 从来没被加载**（脏镜像下它会被加载）。
- 本轮排除了三种解释（都是单变量）：
```
+61 个 libInfra* stub        (c444) → viewUnified2 仍 0 映射，管理器仍 0
+6 个图形 stub(libgpx/grm/uxengine/dmpNative/diablo/DMPGlslc) (c441) → 同上
target=default（替换 argv）   (c446) → 同上
```
- 下一步（第 8 轮）：
  1) 让内核 `IMOPEN` 日志带上**调用者 LR**（一行内核补丁），直接看"谁在 dlopen viewUnified2"、
     以及纯净 /usr 下这次 dlopen 为什么没发生/失败；
  2) 或者按 libIMDB 表查 viewUnified2 的 enable 位与 profile 关系，确认它属于哪一组、什么条件下才装载。

## ★★★ c447–c451（2026-10-24 01:05，第 8 轮）：找到一个**被早期会话打过补丁的 im.elf**
- 用 dlopen 拦截对比两个镜像（同内核/同 initrd/同探针）：
```
纯净 /usr（c447/c448）: 33 次 dlopen 失败，其中 **DLFAIL libJiritsuUIView.so**（本机不存在）
                        **从未尝试 dlopen 任何 viewUnified*.so** → 视图层永不加载
脏 /usr（c449）       : dlopen 失败 0 次；且 **App 自己在 i=2 就把 gfx/res/wg 建起来了**
                        （早于我们探针的 W5/IA 逻辑，说明脏镜像里是 App 自己建的管理器）
```
- 于是逐项比对两边 `/usr`：`/usr/bin` 只差两个文件 `bootin.real.elf`、`im.elf.real`；
  进一步比对二进制：
```
bootin.elf   : 两边 md5 相同 (d8bfb4f7…)
im.elf 本体  : 脏镜像 /bin/im.elf.real md5 = 9c24e4ce…  ≠  真机原版 f1c746e3…
差异仅 3 字节：
  0x2F46/0x2F47 : 10 B9 (cbnz r0)  →  00 BF (nop)
  0x54F5        : 00 → 30
```
  反汇编显示该处位于"target 选择"逻辑里：
```
af42: blx strncmp(target, "...", 4)
af46: cbnz r0, af4e          ← 被 NOP 掉
af48: orr.w r6, r6, #0x1000   ← 于是【无条件】置上 0x1000 标志
af4c: str r6, [r5, #28]
af50: cmp r4, #1 / bne …
```
  ⇒ **早期会话为了让 App 走"应用/目标"分支，把 im.elf 里这个条件跳转改成了 NOP，强制设置 0x1000 标志**。
- 但对**纯净 /usr** 应用这 3 字节补丁（c451）**仍然不建管理器**（gfx/res/wg 还是 0、viewUnified2 仍 0 次加载）
  ⇒ **补丁不是充分条件**，脏镜像里还有别的东西在起作用。
- 本轮可复用的新工具：`/mnt/build/tmp/im_patched.elf`（stock im.elf + 3 字节补丁，md5 9c24e4ce…，与脏镜像一致）
- 下一步（第 9 轮）：
  1) 用 dlopen 拦截 + 失败清单做**精确二分**：脏镜像下哪些库是"必须成功 dlopen 才能进 UI"的（c448 已给出纯净侧 33 个失败清单）；
  2) 重点检查 /usr/lib 的**符号链接与版本化 .so（libX.so.N）**——上一轮只复制了 112 个 `.so` 实体，可能漏了链接名。

## c452–c454（2026-10-24 01:15，第 8 轮续）：差异仍未锁定，得到两条硬线索
- c452（纯净 /usr + **全部 112 个脏镜像库** + 3 字节补丁 im.elf）：
```
/lib 条目 321 = 与脏镜像完全一致（逐名核对，无缺）
但 gfx=0 res=0 wg=0，viewUnified2 仍 0 次加载  ⇒ 库/补丁都不是充分条件
```
- c453（再把 udate/upm/accel/mag/compass/grm_ma/backup 换回**原厂**）：内核 oops（`udate_init_clock`）
  ⇒ 原厂模块在 -rt106+ 内核 + 纯净 /usr 下会崩（脏镜像当年能用，是因为**那批模块在旧内核上 vermagic 不匹配、根本没加载**）。
- c454（把 /usr/kmod 下 102 个模块全换成"拒绝参数"的通用 stub，只留 fake_gles）：
  `modloads=32`（stub 无参数时仍能装），但 **probe=0、im.elf 没起来** ⇒ 这条路现在也不通，需要单独收敛。
- 本轮新增两个精确结论：
  1. **脏镜像当年"模块没加载"是 vermagic 不匹配的副作用**，不是设计；
  2. **纯净 /usr 下 dlopen 失败清单里 `libJiritsuUIView.so` 是真机也不存在的库**，
     而它失败后 **viewUnified*.so 从未被尝试加载** ⇒ 这是"view 不加载"最直接的上游。
- 下一步（第 9 轮）：
  1) 找出**谁** dlopen 了 `libJiritsuUIView.so`（在 libObj/libMWF/libJiritsu 里搜字符串不行，改用 probe 记录 dlopen 的 caller LR）；
  2) 若它是"可选增强库"，可考虑提供**最小 stub**（导出对方 dlsym 的符号）让流程继续，
     而不是让 UI 初始化在那一步静默终止。

## c455（2026-10-24 01:20，第 9 轮）：**证伪"消息池耗尽"** —— valloc 实际是成功的
- 方法：interpose `osal_valloc_msg_wait`，除了返回值，还读取第二个参数（out 指针）指向的内容：
```
VW out ret=0x00000000 dt_cs=1 out0=0x045FDA8C out1=0x5EFFFDC4
VW out ret=0x00000000 dt_cs=0 out0=0x046ECFAC out1=0x5EFFFBE0
... 全部 out0 都是有效指针（0x045xxxxx/0x046xxxxx）
```
  ⇒ **ret=0 且 out 里拿到有效消息指针 ⇒ 分配成功、没有阻塞**。
  ⇒ 对方基于 `fblock: (19456) free=266752` 推的"只剩 13 块 / 池耗尽"**不成立**（该格式是 `(块数) free=空闲字节`，
    实际上空闲充足）。这条推断可以从待办里划掉。
- 另外从对方给的反汇编线索（线程描述符 `libObj+0x1455F24`，`+0x08 = 0x008A2399`）确认了
  **Appframework 线程的真实入口 = `libObj+0x8A2398`**，其首条实质调用是：
```
8a239e: bl 0x6271D4        ; ← uxengine 相关（引擎初始化路径！）
8a23a2..8a23d4: snprintf(buf,32,...) → bl 0x8A2314 (createRequestModelExecuteEvent(1, buf))
8a23d8: mov r0, r7         ; 之后进入事件循环
```
  ⇒ **引擎初始化就发生在这个线程里**，所以"线程阻塞"与"管理器不建"是同一件事的两面。
- 下一步：继续沿 `0x6271D4` 这条调用链走（它才是 engine/管理器构造的真正入口），
  看它在纯净 /usr 下是否提前返回/异常。

## c456–c459（2026-10-24 01:40，第 10 轮）：引擎单例始终 0；`/tmp/area_check_data.dat` 已就位也无效
- c456（单例监视探针）：GraphicsEngine 单例变量 = `libObj+0x145CAB8`（运行时 `0x072DEAB8`），
  **全启动过程 60 次采样恒为 0**；访问路径 = `sys_getInstance` 读 GOT 槽 `libObj+0x13DCB78` → 变量地址。
- c457（直接调用 `uxengine_init`(`libObj+0x6271D4`)）：**立刻 trap 0x0e** ⇒ 它假定引擎已存在，不是创建者。
- c458/c459（按对方线索：`uxengine_init` 会从 **/tmp/area_check_data.dat** 读区域检查数据）：
  在 im.elf 包壳里先把 `/usr/share/app/area_check_data.dat` 复制到 `/tmp/area_check_data.dat`，
  探针实测 **文件已在（可读 16 字节）**，但：
```
ENG inst 仍恒为 0    gfx=0 res=0 wg=0    pVM=0
```
  ⇒ **area_check 数据不是引擎创建的充分条件**（或引擎创建失败在它之前）。
- 现状（第 10 轮小结）：
```
✅ 启动链稳定（35 模块、无 oops、330s）
✅ 已可观测：engine 单例地址、线程入口、dlopen 序列、valloc 行为、/tmp 资源
❌ GraphicsEngine 从未被创建 ⇒ ViewManager/WidgetSystem 为空 ⇒ 无 view、无画面
下一步：找"谁 new GraphicsEngine 并写 0x145CAB8"（.init_array 或更早的 ux::core 初始化），
       必要时手工构造引擎并写单例槽后，再触发 uxengine_init 走正常路径。
```

## c460（2026-10-24 01:47，第 11 轮）：`sys_getInstance` 的调用全部来自我们自己的探针
- 按对方"路径 A"的建议，interpose `GraphicsEngine::sys_getInstance` 并记录调用者 LR：
```
SGI lr=0x04021BBD ret=0x00000000   × 40（探针自己采样即打满 40 条）
```
  `0x04021BBD` 落在 **libgrmlog.so 自身映射区间** ⇒ **这 40 次都是我们探针自己的调用**，
  应用侧从未调用过它。
- 与既有结论互相印证：
```
引擎 getter 无人调用 + uxengine_init 假定引擎已存在 + 单例变量恒 0
⇒ 整条引擎初始化路径从未被进入（不是中途失败，是压根没开始）
```
- 对方给出的两个结构性事实也确认：
  `uxengine_init`@0x6271CC 是空桩、`uxengine_app_fw_init`@0x627335 也是空桩；
  真正的逻辑在同址的未命名函数里，且它第一步就取引擎 → 引擎为 0 必然 trap（与我实测一致）。
- 现状判断：**在 libObj 内部找不到"创建 GraphicsEngine"的代码**（对方扫 5 处引用：读 4 处 + 销毁写 0 一处）。
  下一步两条：
  1) 扫 `.init_array`（272 个）找"分配后 store 到 .bss 全局"的构造；
  2) 或者**手工创建**：该类无 vtable/无 typeinfo/无导出 ctor ⇒ 可以自己 malloc 一块、
     填好成员（含 [inst+0x220] 等被 getter 引用的字段），写进单例槽，再调 uxengine_init 走正常路径。
     这条路风险中等但可控，且能立刻验证"引擎存在后 UI 能否继续"。

## ★★★ c461–c464（2026-10-24 02:00，第 12 轮）：**手工构造引擎成功，`uxengine_init` 首次跑通**
- 背景：对方穷尽静态搜索确认 **libObj 内不存在 GraphicsEngine 的创建点**（GOT 槽 4 读 1 销毁写 0、
  绝对地址仅 1 处 relocation、/usr/lib 202 个库只有 libObj 定义、其它分区 0 处）。
- 于是我改用"手工引导"（对方建议）：
```
calloc(0x400) 全零 → 写入单例槽 libObj+0x145CAB8
  → 再调 uxengine_init 真身 (libObj+0x6271D4)
```
  迭代过程（每次崩都定位到具体成员）：
```
第 1 次：address=0x4C 崩溃（[NULL+0x4C]）
   → 静态定位：崩在 libObj+0x63D506，取 .bss 全局 libObj+0x145CBC4 的 [obj+0x4C] 回调，而该全局为 NULL
第 2 次：填 +0x220 / +0x234（对方给的偏移）→ 仍崩 0x4C
第 3 次：再填 libObj+0x145CBC4 = calloc(0x400) → **`ENGINIT returned`（不再崩）**
```
- 结果（ab_v463 / ab_v464）：
```
trap=0
gfx = 557752 / 536448   ← GraphicsEngine 单例非 0 且由 uxengine_init 填好
res = 0, wg = 0, pVM = 0, viewUnified2 = 0 次映射, GPE kicks = 0
Appframework 线程(tid=185) 仍 futex_wait_queue_me
```
⇒ **这是本目标第一次让"引擎存在 + 引擎初始化跑完"**；但管理器图仍未建立，
   说明 Appframework 线程还卡在另一个前置条件上（不是引擎）。
- 下一步：
  1) 沿"引擎已就绪"的新状态，看 Appframework 线程到底在等什么（futex uaddr / 栈扫描）；
  2) 若需要，继续按"崩哪填哪"的方式引导后续初始化（resource manager / widget system / view manager）。

## c465（2026-10-24 02:05，第 13 轮）：引擎建成后 Appframework 线程栈扫描（无代码帧）
- 对象/全局的正确用法（对方补充）：
  `libObj+0x145CBC4` 是**可选回调表指针**，用 PC 相对寻址（文件里是增量，搜绝对地址 0 命中）；
  零块 ⇒ 所有钩子视为"未安装" ⇒ 全部跳过（这正是设计意图）。填 `calloc(0x400)` 的做法正确。
  selector 函数 `0x640220` 是**虚拟分发**：后端为空时直接返回，**不会阻塞**。
- 引擎建成后对 Appframework 线程（tid=185）做用户栈扫描：
```
STK tid=185 ss=0x93834944        （能定位到线程）
STKV 0 条                        （ss-32KB 窗口内没有任何 0x04000000..0x10000000 的代码指针）
wchan = futex_wait_queue_me
```
  ⇒ 它的栈帧极浅（可能正停在 futex 系统调用里，pthread 帧已出栈）。
  对方提示：**事件循环线程空闲时就该坐在 futex 上**，所以 wchan 本身不能证明是 bug；
  真正的判别是"它是不是在建完管理器之后才空闲" —— 而我们已知管理器从未建过。
- 结合 ResourceManager 的观察：`sys_getInstance()` 实测返回 0（对方说它有静态默认实例，待再核）。
- 下一步（第 14 轮）：
  1) 按"人造对象 + 写单例槽"的做法处理 ViewManager（`libObj+0x149221C`）与 ResourceManager，
     然后再看 `viewManagerIf::openView` / `uxengine_init` 是否走得更远；
  2) 同时观察 Appframework 线程是否因这些对象出现而变化（若仍 futex，则它确实在等"外部事件"）。

## ★ c466（2026-10-24 02:10，第 14 轮）：ViewManager 构造函数**不是单参数**
- 按对方给的写入点（`8a4f5a..8a4f62: str r5,[&pViewManager]`）与构造入口 `libObj+0x8A4E28`，
  用 `calloc(0x400)` 当 this 调用它 → **崩在 address=0**，pc 折合 `libObj+0x8A4E42`。
- 反汇编开头揭示了原因：**它是多参数构造**，我们只传了 this：
```
8a4e28: stmdb sp!,{r4-r11,lr} ; sub sp,#12
8a4e30: mov  r5, r0           ; this
8a4e32: mov  fp, r1
8a4e34: ldr  r4, [r7, #0x30]  ; ★ 取第 5 个参数（栈上）当某配置对象
8a4e3c: mov  r0, r4
8a4e42: ldr  r3, [r4, #0]     ; ← 我们传的 r4 是垃圾/NULL ⇒ 这里崩
8a4e48: ldr  r3, [r3, #8]
8a4e4c: blx  r3               ; 调配置对象 vtable[+8] 取工厂指针
8a4e4e: str.w r0,[r6],#44
```
  ⇒ ViewManager 构造需要 **(this, ?, ?, ?, 配置对象*, …)**，其中那个配置对象带 vtable（工厂）。
- 结论：**"人造对象 + 写单例槽"这条路走到 ViewManager 时不再便宜**——它不是零初始化就能用的对象，
  而是依赖一层带 vtable 的"配置/工厂"对象，这些对象本身又只能由缺失的宿主创建。
- 阶段性判断（如实记录）：
```
✅ 启动链稳定（35 模块、无 oops）
✅ GraphicsEngine 人造成功、uxengine_init 真身跑通
❌ ViewManager 构造需要 (this + 4 个以上参数 + 带 vtable 的配置对象) ⇒ 手工引导成本陡增
⇒ 继续往下等于重建 App 的对象图（配置对象/工厂/事件生产者），已超出"填坑"能覆盖的范围
```
- 后续可选方向（供下一阶段决策）：
  1) 逆 ViewManager 构造的完整签名与配置对象类型，照做一整套桩（工作量大但路径明确）；
  2) 转而验证**"新菜单 appFw.so 路线"**（如果将来拿到该库）——它不依赖 pViewManager；
  3) 或者接受"QEMU 里只做到底座 + 引擎层"的阶段目标。

## c467–c468（2026-10-24 02:20，第 11 轮）：两条新尝试均为负
- c467（给进程内全部 OSAL 信号量发 signal）：
```
SEMS sig 0x881 / 0x6000..0x601F 等，全部 rc=0
结果：gfx=536472（引擎在）、res=0 wg=0 pVM=0、viewUnified2 0 次、kicks=0
```
  ⇒ **不是被这些信号量卡住的**（至少不是阻塞在这些上）。
- c468（把 /usr/kmod 下 102 个模块换成无效文件，只留 fake_gles，想复刻脏镜像"模块没加载"的状态）：
```
modloads=5（只剩 initrd 自带 5 个）、probe=0、im.elf 根本没起来
```
  ⇒ **App 起不来**，说明这些模块里有启动必需的（dmm/grm_ma/codec 之类），
    "模块全不加载"并不等于当年脏镜像的状态（当年脏镜像只是 vermagic 不匹配，实际仍有若干模块生效）。
- 第 11 轮结论：
```
能做的便宜实验基本做尽：
  ✔ 启动链稳定、引擎人造成功、uxengine_init 跑通
  ✘ ViewManager 需要 5+ 参数 + 带 vtable 的配置对象（调用者是应用引导函数 0x8A2FA6）
  ✘ 信号量全打、模块全禁 —— 都不改变"管理器不建"
下一步只剩：
  A) 逆 0x8A2EE0（应用引导函数，签名 (this)）→ 造出它需要的 this 与依赖对象；
  B) 或收窄目标为"底座 + 引擎层"。
```

## c469（2026-10-24 02:25，第 12 轮）：直接调用应用装配函数 `0x8A2EE0` —— 崩，不是阻塞
- 按对方第八轮结论（`0x8A2EE0` 是线程体里装配整个对象图的函数，`ViewManager ctor` 唯一调用点在其内部
  `0x8A2FA6`），用 `calloc(0x400)` 当 app 调它：
```
APPCALL 0x8A2EE0
（没有 "APPCALL returned"）
trap=1   address=0x498   pc 折合 libObj+0x618C78   lr 折合 libObj+0x627519
```
  ⇒ **它是崩掉而不是卡在 `osal_wai_sem_tmo`**，且崩点在与 engine 相关的一段（0x627xxx 返回地址），
    `address=0x498` 表示某处 `[NULL+0x498]` 解引用。
- 结合前面的结论，第 12 轮定性：
```
1) ViewManager 确实由 0x8A2F A6 创建，而它在线程体调用链上（8A239E → 8A2EE0 → … → 8A2FA6）
2) 但 0x8A2EE0 依赖的不只是"一个 app 结构体"，它内部还会调用 engine/manager 相关代码，
   在"只有人造引擎"的环境里依然会崩（0x498 偏移解引用）
3) 也就是说：装配函数本身依赖一整套我们先前的对象（配置对象 vtable、若干 manager 的默认实例…）
```
- 按对方建议，**此处设为止损点**：不再继续"逐层造桩"，转入结论固化。
### 最终状态（本目标）
```
① 启动链稳定：达成（35 模块、无内核 oops、330s）
② view*.uxc 被 mmap：未达成（0 次）
③ 屏幕出现 App 内容：未达成（GPE kicks=0、屏幕黑）
根因：App 侧对象图（app 装配函数 0x8A2EE0 及其依赖的配置对象/管理器默认实例）
      在我们这套"纯净 /usr + 最小 stub"环境里无法自洽建立；
      真机上它由完整固件环境提供，QEMU 里等价于重建 App 的一层初始化。
已完成的可复用资产：稳定基线镜像、引擎人造方案、全套观测手段、以及逐轮记录的所有地址与实验。
```

## ★★★ 第 13 轮（2026-10-24 02:30）：**根因改写** —— 不是"固件缺组件"，是"我们没给它显示硬件"
对方查到同类项目（chocotan/a7c2-re，CXD90057）为什么能出菜单：他们的 QEMU 里加了 **1,303 行显示设备模型**：
```
gpe-raster.c 90 / gpe-sprite.c 113 / dmm-service.c 113 / headless-osd.c 326
ramfb-viewer.c 305 / sony-lz77.c 72 / hdmi-format-service.c 284
```
而**我们的 qemu-sony 只有两处显示相关**（本轮实测确认）：
```
hw/arm/bionz.c            （机器模型 + 我们加的 zz_gpe 寄存器记录）
hw/display/bionz_streamfb.c（把物理 0x20312000 呈现为 QEMU 画面）
其它：bionz_adc/audio/nand/bootcon/vip
缺：dmm / osd / gpe-raster / gpe-sprite / lz77 / hdmi-format
```
⇒ **正确根因**（替换第 12 轮那条）：
```
宿主 QEMU 未提供 GPE/DMM/OSD 等显示设备模型
  ⇒ 固件显示栈从未初始化
  ⇒ GraphicsEngine / ViewManager / WidgetSystem 的创建路径【不被执行】
  ⇒ 在软件层补对象是逆流（已由 ab_v469 证明：调装配函数直接崩在 ux::wgtsys）
```
  我们追了十几轮的"libObj 里没有 GraphicsEngine 创建点"之谜，答案是：**创建是有条件的，条件正是显示服务就绪**。
- 本轮另做的一次观测（`/proc/udm` 枚举）：探针 getdents 未取到条目（解析待修），
  但真机快照 `a_p0/proc_udm.txt` 明确列出 `dmm_info/dmm_extents/dmm_units/hdmi/cec/ldec` 等 —— 可作为"显示服务就绪"的真机对照清单。
- 下一步方向（对方建议，按性价比排序）：
  1) **先做最小验证**：确认固件帧缓冲能否被我们导出到 QEMU 画面（我们已有 `bionz_streamfb`，可在此基础上加"framebuffer 跟随"）；
  2) 逐个补 **DMM → OSD → GPE raster/sprite**，每补一个看固件初始化是否前进一步；
  3) **判据**：固件是否开始自己调用 `ViewManager::ctor`（`libObj+0x8A4E28`）—— 探针已具备。
  注意：a7c2 是 CXD90057，我们是 CXD90045，GPE/DMM 寄存器与协议可能不同，不能直接搬。

## 第 14 轮（2026-10-24 02:35）：按对方"先拦 GRM 返回成功"实验 —— 无效，崩点在更前面
- 背景更正（对方第八轮）：a7c2 出菜单**不是**写 QEMU 设备模型，而是 **guest 内 LD_PRELOAD shim**：
  拦 `dlsym`、`osal_*` IPC、**GPE 绘图 API（`mpgpe::GpermFillRect/BitBlit` → 软件光栅）**、DMM 应答、取帧导出 ramfb。
  对应到 ZV-E10：libObj 导出 17 个 `GRM_*`（`GRM_gpermFillrect`@0x683F4D、`GRM_gpermRectblit`@0x684087、
  `GRM_screenGetOnBitmap`@0x685A21、`GRM_bitmap*` 家族）。
- 本轮实验（ab_v471）：按建议第 1 步，interpose `GRM_gpermFillrect` / `GRM_gpermRectblit` **只记参数并返回 0**，
  然后调应用装配函数 `0x8A2EE0`：
```
APPCALL 0x8A2EE0
（无 GRMFILL/GRMBLIT 调用记录 —— 说明崩溃发生在任何 GRM 调用之前）
trap=1  address=0x498  pc_off=libObj+0x618C78  lr_off=libObj+0x627518
```
- 静态定位崩点：
```
0x627512: mov r0, r5          ; r5 = callee 指针
0x627514: bl  0x618C78        ; WidgetSystem 某方法
0x618C78: ldr r0,[r0,#4]      ; ← r0 = 0x494（垃圾小值）⇒ 访问 0x498 崩
```
  ⇒ **传进来的 callee 指针是 0x494 这种垃圾值**，来源在上游某次查表/取对象，返回了非法值。
  ⇒ 结论：**"拦 GRM 返回成功"这条最小实验无效** —— 崩溃早于任何绘图调用，
    印证了对方"显示服务未就绪"的定性，但**具体的未就绪点**在 `ux::wgtsys` 的 callee 解析处，而不是绘图 API。
- 下一步（若继续）：
  1) 沿 `0x6274E4`（dispatch）看 r5 从哪来（其调用者如何生成 callee 指针）；
  2) 或直接实现 `GRM_bitmap*` + `GRM_screenGetOnBitmap` 让"对象能造出来"，再看是否走到绘图调用；
  3) 判据仍为：固件是否自己调 `libObj+0x8A4E28`。

## 新 goal 第 1 轮（2026-10-24 03:00）：WidgetSystem 人造成功（0x498 崩溃消失），但卡点前移到 ResourceManager 创建
按对方 `SINGLETON_MAP.md` 的单例地图逐项验证：
- **WidgetSystem 单例（`libObj+0x145CB1C`）手工构造**：`calloc(0x2000)`，
  在 `obj+0x484` 建**自指链表头**（+4/+8 指向自身、+32=0），写入单例变量。
  结果：调 `0x8A2EE0` **不再崩在 0x498**（trap=0）—— 证明"崩点=WidgetSystem 为 NULL"完全正确。
- **ResourceManager 计数毒化假设被否**：实测 `*(u32*)0x145CA64 = 0` 且 `*(u16*)0x145CA6A = 0`
  ⇒ **不是引用计数被毒化**，就是从未创建成功。
- 直接调用 RM 创建器 `0x610184`：**调用后不返回（阻塞）**，`res` 仍为 0。
  ⇒ 卡点从"单例为 NULL"前移到 **"ResourceManager 的创建过程本身会阻塞"**（很可能在等某个服务/信号量/资源）。
- 本轮小结：
```
✅ WidgetSystem 不再为 NULL，0x498 崩溃消失（ab_v472 / ab_v476）
✅ 确认 RM 不是计数问题（ptr=0 且 cnt=0）
❌ 0x610184（RM 创建器）会阻塞 ⇒ 下一步查它阻塞在哪个调用（单步 hook：osal_wai_sem_tmo / open / osal_rcv_msg）
```
- 下一步（第 2 轮）：
  1) 给 `open/openat`、`osal_wai_sem_tmo`、`osal_rcv_msg_tmo` 加日志，定位 `0x610184` 卡在哪一步；
  2) 若是等信号量/消息，判断是谁该发（可能与显示服务就绪相关，回到 a7c2 的 shim 路线）。

## 新 goal 第 2 轮（2026-10-24 03:25）：抓到应用装配函数内部真实的等待序列
用"门控探针"（只在进入 `0x8A2EE0` 后记录 open / osal_wai_sem_tmo / osal_rcv_msg_tmo / osal_valloc_msg_wait）：
```
APPCALL 0x8A2EE0
AVAL  q=0x008F013A          ← 为日志子系统申请消息
AWAI  id=0x0083037A         ← 等信号量（就是 0x8A2F0E 那处）
AWAIRET 0
AVAL  q=0x008F013A
ARCVRET -1022               ← 收消息超时（-1022 = ETIMEDOUT）
AWAI  id=0x0037E019
AWAIRET 0
ARCV  q=0x0037807D          ← ★ 反复在等子系统的消息
ARCVRET -1022
…（该组合循环，永不返回）
```
⇒ **`0x8A2EE0` 卡在"反复向队列 `0x37807D`（子系统 0x37）收消息并超时"的循环上**，
   信号量 `0x37E019` 只是配套的互斥/通知，不是阻塞源。
⇒ 这与 a7c2 的结论一致：应用在**等某个服务就绪**（很可能是显示/合成相关子系统 0x37），
   等不到就一直重试，自然不会走到 ViewManager 创建。
- 本轮另做的对照：`open` 门控计数为 0 ⇒ **这一阶段不做任何文件 I/O**，纯粹是 IPC 等待；
  `osal_wai_sem_tmo` 直接返回 0（c482）也不改变结果 ⇒ 不是信号量。
- 尝试伪造 `0x37807D` 的应答（返回 0 + 零填充）：应用继续走了一步但 **崩在 address=0xFFFFFFFC**
  ⇒ 说明它**解析应答内容**，零填充不是合法消息；需要真实消息格式。
- 下一步（第 3 轮）：
  1) 找子系统 `0x37` 是谁（对照 a7c2 的队列 ID 表：CONTROL/UPDATE/AP/GUI 等），
     大概率是 OSD/GUI 服务队列；
  2) 查真机 /proc/osal/uipc 中 0x37 子系统的队列与客户端（需要对方真机数据），
  3) 若确认是 OSD/GUI 服务，就按 a7c2 契约在 shim 里**实现该队列的应答**（而非直接回 0）。

## 新 goal 第 3 轮（2026-10-24 03:40）：RM 进度阶梯用不上 —— 应用根本没走到 RM 创建
- 按对方给的"进度阶梯"（读 `*(0x145CA64)` 的 +0x30/+0x34/+0x38/+0x3C/+0x44 五个槽）加了 watchdog（12s 后读）：
```
WD3 rm=0x00000000   ← ★ ResourceManager 指针仍然是 NULL
WD3 ws=0x00089728 pvm=0x00000000
```
  ⇒ **应用在创建 RM 之前就卡住了**，所以那 5 个进度槽无从谈起。
  这与第 2 轮的门控追踪一致：`0x8A2EE0` 卡在对队列 `0x37807D` 的循环 `osal_rcv_msg_tmo` 超时上。
- 又做了一次"卡住时刻"的 `/proc/osal/uipc` 抓取（watchdog 在阻塞后把文件写 `/dev/mem`→物理 0x30000000，宿主 QMP pmemsave）：
```
捕获 16000 字节成功；其中 37 段相关条目：
  375(0) 60b34820 512   /  375 0 0 0 0 1 0 0 0   /  37B 0 0 1 0 1 0 0 0   /  37A 1 0 134 0000 0
```
  ⇒ 能看到 0x37 段下的若干队列（375/37A/37B），但**看不出 0x37807D 属于哪个进程**；
  0x37A 有 1 条积压，0x37B 有客户端计数，说明 0x37 子系统在 OSAL 里是"有人注册过"的。
- 结论：**必须先弄清“应用在等 0x37 子系统的什么消息”**，否则 RM/ViewManager 都无从推进。
  下一步：
  1) 找 OSAL 里 0x37 子系统的**注册者**（谁调了 osal_reg_msg_queue_cb 注册该子系统）；
     可在探针里 hook `osal_reg_msg_queue_cb` 记录 (qid, cb) 全量，看谁注册了 0x37xxxx；
  2) 或对照对方真机 dump 判定 0x37 = 哪个服务（a7c2 的 CONTROL/UPDATE/AP/GUI 哪个）。

## 新 goal 第 4 轮（2026-10-24 03:50）：0x37 = DMM；DMM 拦截未命中，ReceiveASync 拦截只能"跳过一次"
对方判定 **子系统 0x37 = DMM（显存管理）**，并指出"收消息路径里又要申请显存"，形成循环。据此做三组实验：
- **实验 A：拦 5 个 `DataflowInfraUtility::*MemFromDmmSync` 导出函数（bump 分配器）**：
```
完全没有命中（无任何 DMM 日志）
```
  ⇒ 说明应用这阶段**还没发出 DMM 请求**（内核 UIPCARG 里也只有 0x8F013A 日志队列的流量），
    所以"收包路径调 DMM"发生在**收到消息之后**，我们还没走到。
- **实验 B：拦 `DataflowInfraReceiver::ReceiveASync`（三个重载，导出符号）返回 0**：
```
RCVASYNC a=0x09AEED48 msg=0x0055011E    ← 被调用，且立刻被重复调用（循环继续）
```
  ⇒ 返回 0 语义上**等于失败/无数据**，调用方继续重试。
- **实验 C：同样拦截但返回 1 且把 msg 前 64 字节清零**：
```
RCVASYNC 只出现 1 次 → 立刻 trap=1，address=0x0055011E（就是那个 msg 指针本身）
```
  ⇒ 返回 1 被当作"有数据"，随后代码**按 msg 结构解引用**，而 0x55011E 并不是可写消息缓冲。
- 本轮结论：
```
1) 0x37=DMM 的判定接受；但 DMM 请求发生在"收到消息之后"，所以先要解决"收不到消息"。
2) ReceiveASync 的返回值语义：0=无数据（调用方重试），非 0=有数据（随后按 msg 布局解析）。
3) 想真正突破，需要知道 msg（DataflowInfraMsg）的内存布局 + 该队列期望的消息类型；
   否则只能盲试。下一步请对方给出 DataflowInfraMsg 头部布局（GetMsgType/GetReplyId 的偏移），
   我可以直接构造一条合法消息喂给它。
```

## 新 goal 第 5 轮（2026-10-24 04:00）：定位到"谁在等"—— libObj 内部 `0x5304A3` 的接收者
按对方更正（超时循环里不涉及 DMM；`0x37` 未定），改为**同时观测接收者的身份与调用者**：
- interpose `DataflowInfraReceiver::ReceiveASync`（4 参数版：this, a1, msg, timeout）：
```
RCV self=0x09AE2D48 a1=0x0055011E msg=0x09AE2B34 lr=0x063B24A3  +4=0x058E0000  +8=0x09658BF4
（稳定重复，说明就是这里在循环）
```
  折合偏移（libObj base 0x5E82000）：**lr = libObj+0x5304A3** ⇒ 等待者就在 **libObj 内部**，
  （该地址夹在导出符号 `ModelCecToInstance`(0x5276F0) 与 `ModelCleaningToInstance`(0x53AC8C) 之间，
   属于未导出内部函数）
- 接收者对象字段：`self+4 = 0x058E0000`（不像 qid，更像通道/句柄/标志），`self+8 = 0x09658BF4`（指针）
  ⇒ 与对方说的"通道对象 +4 字段 = qid"不完全吻合，需要再核对。
- `osal_rcv_msg_tmo` 观测到的 qid 是 `0x37807D`，但 `ReceiveASync` 收到的第一参数是 `0x0055011E`
  ⇒ **两者不是同一个值**，说明 qid 在 ReceiveASync 内部由通道对象换算得出（或第一参数是通道/缓冲）。
- 结论与下一步：
```
· 等待者身份已确定：libObj+0x5304A3（内部函数），它通过 DataflowInfraReceiver 等 0x37807D
· 缺的仍是"消息内容/类型"：需要 DataflowInfraMsg 的布局或该通道期望的消息类型
· 下一步：hook osal_dynamic_id（注意上次加它会让 rm_init 卡住，需谨慎/改签名）或在
  ReceiveASync 内部读取 this+4 的换算逻辑（静态看 0x228D8 开头几行即可）
```

## 新 goal 第 6 轮（2026-10-24 04:10）：按布局填 msg 返回 0 —— 仍无变化；并发现"这些调用不是我们那条线程"
- 按对方给出的 `DataflowInfraMsg` 布局（+0x00 vptr / +0x04 msgType(2=全局) / +0x08 msgSize(u16) /
  +0x0C srcId / +0x10 msgBuf=this+0x14 / +0x14 内嵌缓冲 ≤0x140）实现"合法应答"：
```c
*(u32*)(msg+0x04)=2; *(u16*)(msg+0x08)=24; *(u32*)(msg+0x0C)=0xB3; memset(msg+0x14,0,24); return 0;
```
  结果：
```
RCVA self=0x09CFBD48 qid=0x0055011E msg=0x09CFBB34 al=1 t=0x00000000 sz=0   ← 每次进入时都是"未初始化"
（调用仍在重复；trap=0；pVM 仍 0；viewUnified2 0）
```
  ⇒ 两种可能：(a) 我们填的不是调用方真正使用的那份对象；(b) **这些调用并不来自我们那条被阻塞的线程**。
- **关键观察**：本轮日志里 `RCVA` 出现在 **`APPCALL` 之前**（`APPCALL`/`WS made` 根本没出现），
  说明这些 `ReceiveASync` 调用来自**其它 App 线程**（mwf/Infra 等），而不是我们调 `0x8A2EE0` 的那条。
  ⇒ 之前"RCV 循环 = 我们的卡点"这个因果链需要重新验证。
- 下一步：
  1) 给 interpose 加 **线程门控**（只在我们进入 `0x8A2EE0` 之后生效，并记录调用线程 tid），
     确认"卡住的那条线程"到底在调哪个原语；
  2) 若确认它并不调 ReceiveASync，则回到 `osal_rcv_msg_tmo` 层面，用 tid 门控抓它自己的 qid 序列。

## 新 goal 第 7 轮（2026-10-24 04:10）：线程门控揭穿因果链 —— 我们的线程**不走 ReceiveASync**
按对方"qid 就是 r1、无换算"的实证，给 interpose 加**线程门控**（只在我们调用 `0x8A2EE0` 的那条线程上生效）：
```
RCV OTHER tid=145 qid=0x0055011E msg=0x09CFBB34
RCV OTHER tid=146 qid=0x0055011F msg=0x09EFBB34
RCV OTHER tid=147 qid=0x00550120 msg=0x0A2FBB34
RCV OTHER tid=150 qid=0x00550121 ...
RCV OTHER tid=151 qid=0x00550122 ...
RCV OTHER tid=152 qid=0x005502F2 ...
RCV OTHER tid=155 qid=0x0055001F ...
RCV OTHER tid=164 qid=0x009A015A ...
APPCALL 0x8A2EE0      ← 我们的线程在这一切之后才进入
```
⇒ **三条硬结论**：
```
1) 所有 ReceiveASync 调用都来自【其它线程】，且 qid 是"子系统 0x55 + 每条线程递增的局部 id"
   ⇒ 0x55 = 数据流框架的【每线程通道】，不是我们卡住的那条队列。
2) 我们那条线程（调用 0x8A2EE0 的）从未调用 ReceiveASync。
3) 因此第 4~6 轮"用 ReceiveASync 投消息"方向对本卡点无效（我之前的因果链错了，现予纠正）。
```
- 那么第 2 轮里 gated 探针看到的 `ARCV q=0x37807D` 是**我们的线程直接调 `osal_rcv_msg_tmo`**（不走 DataflowInfraReceiver）
  ⇒ 下一步必须在 `osal_rcv_msg_tmo` 里加 **tid 门控 + 记录 timeout/返回/缓冲指针**，
    弄清它在等哪条队列的什么消息（子系统 0x37，局部 0x807D —— 高位 0x8000 可能是"同步/全局"形态）。

## 新 goal 第 8 轮（2026-10-24 04:25）：卡点最终定位 —— **`osal_wai_sem_tmo(0x83037A, -1)`（无限等）**
给 `osal_wai_sem_tmo` / `osal_valloc_msg_wait` 加**线程门控**（只记录我们调 `0x8A2EE0` 那条线程）：
```
APPCALL 0x8A2EE0
HVAL q=0x008F013A                         ← 申请日志消息
HSEM id=0x0083037A lr=0x06724F15          ← ★ 无限等这个信号量
HVAL q=0x008F013A
```
- `lr=0x06724F15` 折合 **libObj+0x8A2F15** ⇒ 正是 `0x8A2EE0` 内部 `8A2F0E: blx osal_wai_sem_tmo` 那条指令之后。
- 用 watchdog 读该线程 wchan：**`hrtimer_nanosleep`** ⇒ 它在 `osal_wai_sem_tmo` 的超时/轮询实现里睡眠。
- 把 id==0x83037A 的等待**直接返回 0 跳过**后：不再出现新的 HSEM，但线程又进入 `hrtimer_nanosleep`
  ⇒ 跳过第一个等待后，它进入另一个**基于 sleep 的轮询**（不是信号量、不是 IPC、不是文件）。
- 结论（本轮）：
```
1) 应用装配函数 0x8A2EE0 在开头就【无限等待信号量 0x83037A】—— 这是当前唯一确定的卡点。
2) 该信号量应由某个"服务就绪"方 signal；在我们环境里没有任何人 signal 它。
3) 跳过它之后仍有基于 nanosleep 的轮询 ⇒ 后续还有"等状态"的逻辑（大概率还是在等同一批服务）。
```
- 下一步：
  1) 查 0x83037A 是谁的信号量（OSAL 里 id 与所有者映射：可在内核侧 osal 模块或 /proc/osal 中查；
     也可在探针里 hook `osal_create_sem` 记录 id↔创建者 LR）；
  2) 若它是"显示服务就绪"，则回到 a7c2 的路线：由 shim **主动 signal 它**（而非 skip），
     并同时把该服务的应答补上，让后续轮询条件也满足。

## 新 goal 第 9 轮（2026-10-24 04:40）：主动 signal 0x83037A —— 返回 rc=0 但**仍不放行**
- 对方补充的 OSAL 编码规则（已记录，供后续复用）：
```
qid = (port_id << 16) | (0x8000 | local_index)
bit15=1 ⇒ 全局/跨子系统；bit15=0 ⇒ 本地
样本：0x8F013A(本地) / 0x8280074D(本地) / 0x9088076B(本地) / 0x37807D(全局) / 0x0055011E(本地)
osal_snd_msg 只有 2 个参数 (qid, msg)；4 参数的是 osal_snd_sync_direct
冒充服务端正确姿势：osal_valloc_msg_wait(qid,&msg,size,0) → memcpy → osal_snd_msg(qid,msg)
```
- 本轮实验：watchdog 每 6 秒调一次 `osal_sig_sem(0x83037A)`，共 6 次：
```
SIGSEM try=0..5  rc=0（全部成功）
但 APPCALL 仍不返回、gfx/res/wg 不变、viewUnified2 0
```
  ⇒ **signal 被接受但没放行**：可能 (a) 等待方在 signal 之前已超时重试/重入；(b) 该 sem 是"计数/事件"语义，
     需要与其它状态配合；(c) 我 signal 的 id 形态（全局/本地）与等待方不一致。
- 下一步：
  1) hook `osal_create_sem`/`osal_reg_*` 拿 **id ↔ 创建者 LR** 映射，确认 0x83037A 的语义与所有者；
  2) 在 signal 之后**立刻**用 tid 门控观察 `osal_wai_sem_tmo` 是否返回、返回后代码走到哪里（这是判据）。

## 新 goal 第 10 轮（2026-10-24 04:45）：**释放信号量成功**，卡点前移到 `[r4+0x14]`（r4=0）
按对方建议先 `osal_stat_sem` 再"只 signal 一次"：
```
WSEM id=0x0083037A lr=0x06724F15      ← 我们的线程在 0x8A2EE0 里等它
STAT  rc=0 state=0x00000000           ← 信号量存在、计数为 0（=没人 post 过）
SIG   rc=0                            ← 我们 post 一次，成功
STAT2 rc=0 state=0x00000000
WSEM ret=0                            ← ★ 等待被释放！0x8A2EE0 继续往下执行
trap=1  address=0x00000014            ← 新崩点
```
- 崩点指令（反汇编确认）：
```
8a2f0e: ldr r0,[pc]              ; 信号量 id
8a2f10: blx osal_wai_sem_tmo
8a2f14: mov r1,r6 / 8a2f16: movs r0,#1
8a2f18: bl 0x8A2A80
8a2f1c: ldr r0,[r4,#20]          ; ★ 崩在这：r4 == 0 ⇒ 访问 0x14
```
- 我方传入的 app 指针非 0（`APPPTR app=0x082870`），并且已把 `app+0x14` 填了哑配置对象，
  **仍然崩在 0x14** ⇒ 说明**崩的是另一个调用 0x8A2EE0 的入口**（其 r4/this 为 0），
  即"我们放行信号量后，固件自己的线程也走到了这里，而它的 this 是 0"。
- 这也解释了为什么"填 msg / 造对象"都无效：**真正的路径是固件自己的 Appframework 线程**，
  它调用 `0x8A2EE0` 时传入的 this = 0（或等价无效），需要在那条路径上补对象，而不是我们这条。
- 下一步：
  1) 静态确认 `0x8A2EE0` 是否允许 this=0（看第一条 `str r3,[r0,#40]` 是否被我们漏看/是否有前置判断）；
  2) 在 0x8A2EE0 入口 hook（替换首指令为跳转）记录 r0/调用者 LR，**分清两条调用路径**：
     我们的（this=0x82870）与固件线程的（this=？）。

## 新 goal 第 11 轮（2026-10-24 04:55）：按"逐个中和"策略 —— 中和 sem 之后线程**不再触发任何可观测原语**
- 采纳对方第③条策略（不猜所有者，逐个中和等待点并记录下一个）。本轮实现：
  - `osal_wai_sem_tmo(0x83037A)` → 直接返回 0（中和）
  - `nanosleep` → 立即返回 0（快进，并记录调用者 LR）
- 结果：
```
APPCALL 0x8A2EE0
BSEM id=0x0083037A lr=0x06724F15     ← 只出现一次（被中和）
（没有任何 BNS 日志：中和之后它不再调 nanosleep）
trap=0，gfx=557712，pVM=0，viewUnified2=0
```
  ⇒ **中和 sem 后线程既没有崩溃、也没有新的等待原语**，Y9 循环也停在 i=9（说明我们这条线程没有返回）。
  ⇒ 推断它进入了**不经过内核原语的忙等/自旋**（例如在 libObj 内部轮询某个内存标志），
    因此 sem/rcv/snd/nanosleep 钩子都看不到东西。
- 注意与第 10 轮的差异：**"signal 一次（真的走内核）"会前进并崩在 `[r4+0x14]`，
  而"直接返回 0（不走内核）"则静默自旋** ⇒ 说明该 sem 不只是计数，还承载"事件/状态"，
  返回 0 与真实 post 的副作用不同。这是重要线索。
- 下一步（第 12 轮）：
  1) 用 QEMU gdb 对 guest **多次采样 PC**，看自旋地址是否落在 libObj 某函数（从而定位轮询点）；
  2) 或者用 watchdog 读 `/proc/self/task/<tid>/stat` 的 utime 确认是否真的自旋；
  3) 若确认自旋在某函数，就按"崩哪填哪"的方式补它轮询的条件。

## ★★★ 新 goal 第 12 轮（2026-10-24 05:00）：**突破！** 按对方"名字缓冲区"修正后，固件自己的线程跑起来了，`viewUnified2.so` 被 mmap
- 根因（对方查出）：`0x8A2EE0` 的名字来自 `.bss 0x14920E8`，由 `Application_System_Init` 填
  （arg!=0 → strncpy(arg)；arg==0 → 默认 **"X"**）；`0x8A2E70` 用 `strncmp("X",name,20)` 决定是否
  `initializeConfig()`。我们此前直接调 `0x8A2EE0` 而名字为空 ⇒ 走失败分支 ⇒ 配置对象没生成 ⇒ 崩在 0x14。
- **修正（一行）**：改为调用 `Application_System_Init(0)`（`libObj+0x8A2408`，参数 NULL）：
```
RMSTATE ptr=0 cnt=0
WS made=0x00089718
ASICALL arg=0
ASI returned                        ← 正常返回
trap=1 address=0x18
gfx=557744  wg=562968  viewUnified2 出现 7 次！
```
- **本轮实证（第一次）**：
```
ZZREAD Appframework pid=186 fd=23 count=512 dev=viewUnified2.so
QMMAP  addr=0 len=0xE4FB10 fd=23 ret=0x0EC64000 name=viewUnified2.so
QMMAP  addr=0xF7E4000 len=0x228000 off=0xB78000 prot=0x3 fd=23
映射表：0EC64000-0F7DD000 r-x  viewUnified2.so
        0F7E4000-0FA0C000 rw-  viewUnified2.so
```
  ⇒ **App 自己的线程起来了、真的把 viewUnified2.so 映射进内存**（此前从未发生过）。
- 新崩点（已定位）：
```
0x6137DC: getLayer(id) 返回 0 → add r0,#0x18 → r0=0x18
0x8647EC: ldr r3,[r0]   ⇒ 崩在 0x18
```
  ⇒ 需要 **真正的 WidgetSystem::getLayer 能返回 Layer**；我们手工造的零对象没有 layer 注册表。
- 下一步：
  1) 观察 App 是否**自己**创建 WidgetSystem（如果不，就继续手工造，但为 getLayer 补一个最小 Layer 表）；
  2) 或者拦截 `WidgetSystem::getLayer` 返回一个最小 layer（同类"人造对象"手法）。

## 新 goal 第 2/60 轮（2026-10-24 05:10）：interpose `getLayer` 返回最小 Layer —— 崩点从 0x18 前移到 pc=0
- `WidgetSystem::getLayer(const core::ID&)` 是导出符号（`0x618D08`，mangled
  `_ZNK2ux6wgtsys12WidgetSystem8getLayerERKNS_4core2IDE`），据此 interpose 返回一个最小 layer：
```c
static unsigned dmyfn(void){ return 0; }
static void* sub_vtbl[12];      // 全部槽 = dmyfn
static char sub_obj[0x40]; *(void**)sub_obj = sub_vtbl;
static char dmy_layer[0x100]; *(void**)(dmy_layer+0x18) = sub_obj;
getLayer(self, id) → dmy_layer
```
- 结果：
```
ASICALL arg=0 / ASI returned
DUMLAYER ready
GETLAYER self=0x000896E8 id=0x072DEAE0 ret=0x04042B20
trap=1  pc=0x00000000  lr=0x8647F3  (address=0)
gfx=557696 viewUnified2 7 次
```
- 静态对照：崩点在 `0x8647F0 blx r3`，其中
```
8647ec: ldr r3,[r0,#0]    ; r0 = layer+0x18 → 取对象
8647ee: ldr r3,[r3,#12]   ; 再取 vtbl[3]
8647f0: blx r3            ; ★ r3==0 → pc=0
```
  ⇒ 说明**这次调用的对象不是我们造的那个 layer**（我们 vtbl 全槽都填了非零），
    或调用发生在 getLayer 之外的另一个对象路径上。
- 下一步：
  1) 在 `0x8647DC` 这个函数入口加 hook（导出吗？否则用 `getLayer` 返回对象的分发点判断）
     记录 r0/r1，确认到底是谁传了 vtbl[3]=0 的对象；
  2) 或者把 `WidgetSystem` 的 layer 表补全（getLayer 真正读的成员），让固件自己拿到合法 layer。

## 新 goal 第 3/60 轮（2026-10-24 05:20）：`getLayer` + `createLayer` fallback —— **不再崩溃，线程继续跑到 i=39**
按对方建议：`getLayer` 未命中时回退调用导出的 `WidgetSystem::createLayer`（`0x6171FC`，构造时自动注册进 +0x484 环形链表）。
```
ASICALL arg=0 / ASI returned
GL2 self=0x00089720 id=0x072DEAE0 real=0x00000000   ×2（真实 getLayer 未命中 → 走 fallback）
trap=0                     ← ★ 不再崩（此前 pc=0）
Y9 循环推进到 i=39（此前只到 25）
gfx=557752  wg=562976  res=0  pVM=0  viewUnified2 仍 7 次相关日志
日志尾部：IMNANOSLEEP（线程在睡，未崩）
```
- 观察与注意：
```
· 本次 QMMAP viewUnified2 返回 ret=0（上一次是 0x0EC64000 成功）⇒ 时序不同，映射可能失败，
  需要下一轮专门看 "viewUnified2 是否成功映射"（对光出画面很关键）。
· res(RM)=0 与 pVM=0 仍未解决；但线程已能长时间存活（i=39）而不是立刻崩。
```
- 下一步：
  1) 稳定 `viewUnified2` 的映射（检查 mmap 返回 0 的原因：地址冲突/长度/权限）；
  2) 继续沿"崩点前移"的方式，把 App 线程推到 ViewManager 创建（pVM != 0）。

## 新 goal 第 4/60 轮（2026-10-24 05:15）：App 线程已存活但**空闲在 futex**；InputService 未初始化
- 55 秒周期采样（ab_v508）：
```
pvm=0  res=0  wg=0x87F38(人造)  gfx=0x86AD0(人造)  —— 全部不变
期间 getLayer 被调用 2 次（id 固定 0x072DEAE0），都走了 createLayer fallback
```
- 线程状态与调用者（ab_v509）：
```
Appframework tid=185 wchan = futex_wait_queue_me（事件循环空闲）
getLayer 的调用者 lr = 0x064957D9 ⇒ libObj+0x6137D9（就在 0x6137C0 那段 Widget/Layer 代码里）
```
- 事件注入尝试（ab_v510，按对方早前给的触摸注入思路）：
```
InputService::m_pMyInstance(libObj+0x1459548) = 0x00000000   ← ★ 输入服务根本没初始化
⇒ 无法走 createEvent + pushFilterEvent 注入
```
- 结论：
```
App 侧现在处于"线程活着、事件循环空闲、但各子系统（ResourceManager / InputService / ViewManager）都没起来"的状态。
它不是崩了，而是没有"继续初始化"的触发源。
```
- 下一步：
  1) 系统盘点：一次性打印 App 侧各单例（ViewManager/ResourceManager/InputService/InfraInput/ViewIdSoTable…）的当前值，
     找出"哪些是 0"，据此判断还缺哪一层；
  2) 与对方确认 ASI 之后**正常的下一步是谁触发**（是否有"服务就绪"消息/定时期待处理）。

## 新 goal 第 5/60 轮（2026-10-24 05:25）：单例盘点 + 补齐假对象（RM/InputService）—— pVM 仍为 0
- 盘点（直接读变量，避免 dlsym 死锁；ab_v512）：
```
gfx=0x84ED8(人造)  rm=0x00000000  ws=0x86340(人造)  pvm=0x00000000  inp=0x00000000  cb=0x85AF0(人造)
```
  ⇒ 缺的是 **ResourceManager、ViewManager、InputService** 三样。
- 补齐实验（ab_v513/514）：
  - interpose `ResourceManager::sys_getInstance` 返回假对象 `rm_fake`（0x400 零块），并把 `0x145CA64` 也写成它
  - 给 `InputService::m_pMyInstance`(0x1459548) 填一个 0x200 零对象
```
INPFAKE 0x0008BB00
INV gfx=0x88288 rm=0x04042A78 ws=0x896F0 pvm=0 pvm 仍 0 inp=0x8BB00 cb=0x88EA0
trap=0，Y9 跑到 i=39，无崩溃
```
  ⇒ **假对象没有再引发崩溃，但 pVM 仍是 0**：App 线程依旧空闲，没有任何"继续初始化"的动作。
- 结论：**现在的障碍不是"缺对象"而是"缺触发"** —— 需要知道 Appframework 线程在等哪条队列/什么事件。
- 下一步：
  1) hook `osal_rcv_msg_tmo`，**按 tid 记录**（找出 Appframework 线程在等哪个 qid）；
  2) 找到后按对方给的"valloc→memcpy→snd_msg"三步法投一条最小消息，看能否把线程唤醒推进。

## 新 goal 第 6/60 轮（2026-10-24 05:30）：进度探针（operator new / pipe）—— 只看到 viewUnified2 的 new(24)，**没看到 libObj 侧的 new(0x28/0x58/0xAC/0x70)**
按对方建议给 `0x8A2EE0` 的进度点做二分，用**导出符号**替代改二进制：
- interpose `operator new(unsigned)`（`_Znwj`）与 `pipe()`，按"我们的 tid"门控
- 结果：
```
ASI returned
NEW 24 lr=0x0F090AC1   ×N（连续）
（没有任何 new(0x28)=40 / new(0x58)=88 / new(0xAC)=172 / new(0x70)=112 的记录；
  也没有 PIPE 记录）
```
- 分析：
  - `lr=0x0F090AC1` 落在 **viewUnified2.so 的代码段**（0x0EC64000-0x0F7DD000）
    ⇒ 这批 `new(24)` 是**视图引擎侧**的分配；
  - **libObj 侧那四个管理器的分配一次都没出现** ⇒
    要么 `0x8A2EE0` 没走到 `0x8A2F4A` 之后的分配段，要么这些分配发生在**别的线程**（我们的 tid 门控把它们滤掉了）。
  - 与对方指出的矛盾一致：**若 `0x8A2EE0` 完整跑完，pVM 不可能为 0**。
- 下一步（第 7/60 轮）：
  1) 把 NEW 日志加上 **tid**（不加门控、只统计），确认那四个 `new(0x28/0x58/0xAC/0x70)` 是否在**别的线程**发生；
  2) 若确实没有发生过 ⇒ `0x8A2EE0` 在 `0x8A2F26`（pipe）之前就返回/跳走 ⇒ 用 `pipe()` 是否被调用来判定；
  3) 复核 `pVM` 读法（`0x149221C`）—— 对方提出可能是地址/读法偏差，需要交叉验证。

## ★ 新 goal 第 7/60 轮（2026-10-24 05:40）：证实 `0x8A2EE0` **确实在 Appframework 线程（tid 186）里执行**，并走到 `initializeConfig`
- 关键实验：interpose 导出的 `initializeConfig`（`libObj+0x14BE84`）记录调用者：
```
INITCFG tid=186 lr=0x06724EAD
```
  折合 lr = **libObj+0x8A2EAD** —— 正是 `0x8A2E70` 里 `strncmp("X",name,20)==0 → blx initializeConfig@plt` 的调用点
  ⇒ **名字匹配成功、配置子系统初始化被调用、且发生在 Appframework 线程（tid=186）里**
  ⇒ 说明 **`0x8A2EE0` 的入口段（0x8A2EEE..0x8A2F06）在固件线程里是正常执行了的**。
- 但同一次运行里：
```
NEW2 sz=112 (ViewManager 0x70) = 0 次
NEW2 sz=172 (ModelManager 0xAC)= 0 次
pVM=0（两条路径都读：直接读 0x149221C 与经 GOT 槽 0x13DC7B4 解出的 0x0731421C，均为 0）
trap=0
```
- 另本轮复核了对方提的地址疑问：**`pViewManager` 地址 0x149221C 是对的**
  （`*(u32*)(base+0x13DC7B4)` = 0x0731421C = base+0x149221C ✓）
- 于是问题被压缩成：
```
0x8A2EE0 在 tid 186 里进入 → 名字匹配 → initializeConfig 执行
  → 但没有走到 0x8A2F4A 之后的四个 new / ViewManager new(0x70)
⇒ 卡在 initializeConfig 之后、0x8A2F4A 之前的那几步：
   0x8A2EFE (bl 0x8A2A80) / 0x8A2F0E (osal_wai_sem_tmo 0x83037A) / 0x8A2F26 (pipe)
```
- 下一步：把所有 `osal_wai_sem_tmo` / `pipe` 调用**带 tid** 全量记录，看 tid 186 在这三步里停在哪一个。

## 新 goal 第 8/60 轮（2026-10-24 05:55）：带 tid 全量抓取 sem/pipe —— **tid 186 既不等 0x83037A、也没调 pipe**
- 给 `osal_wai_sem_tmo` 加"只记录 id==0x83037A 或 tid==186"的过滤（并仍然对 0x83037A 直接返回 0）：
```
（没有任何 SEMT 记录：tid 186 从未等待 0x83037A；其它线程的 0x9401C3/0x946xxx 等待被过滤掉）
PIPE2 tid=165/171 r=0（另一次运行里），没有任何一条来自 tid 186
sz112(ViewManager)=0，sz172(ModelManager)=0
trap=0，pVM=0
```
- 结合上一轮 `INITCFG tid=186 lr=libObj+0x8A2EAD` 的事实，得到**新的关键判断**：
```
0x8A2E70（名字匹配 + initializeConfig）确实在 tid 186 里被调用，
但它【不是】从 0x8A2EE0 调进来的 —— 因为若从 0x8A2EE0 进来，
随后必然经过 0x8A2F0E(osal_wai_sem_tmo) 与 0x8A2F26(pipe)，而这两者 tid 186 都没有记录。
⇒ 0x8A2EE0 在 Appframework 线程里【可能根本没被调用】（或调用点不是 0x8A23AA）。
```
- 这解释了为什么"名字修好了、viewUnified2 也加载了，但管理器一个都没建、pVM 恒 0"。
- 下一步（第 9/60 轮）：
  1) 确认线程体 `0x8A2398` 是否真的执行到 `0x8A23AA`（`bl 0x8A2EE0`）：
     可 interpose `uxengine_init` 前一步的导出调用，或在 `0x8A2A80`（"P wait" 日志函数）上想办法；
  2) 若线程体提前返回，查它在 `bl 0x6271D4`（uxengine_init 真身）里是否卡住/提前返回 ——
     那正好对应我们"人造引擎"可能没满足它的某个前置。

## 新 goal 第 9/60 轮（2026-10-24 06:05）：锚点验证 —— `uxengine_init(0x6271D4)` 只有我们的探针在调；并发现 **guest 只有 1 个 CPU**
- 用导出锚点验证线程体是否执行到 `0x8A23AA`：
```
interpose UXEngine::gfxeng()(0x610FEC) 与 GraphicsEngine::getAreaCheckExtensionManager()(0x612772)：
  UXG tid=134 lr=0x064A9221     ← tid 134 = 我们探针自己（调 uxengine_init）
  ACE tid=134 lr=0x064A9225
  （没有任何来自 tid 186 的记录）
```
  ⇒ **`0x6271D4`（uxengine_init 真身）只被我们自己的探针调用过**；
    Appframework 线程（tid 186）从未执行到线程体 `0x8A2398` 的第一个调用点
  ⇒ **线程体确实没有真正跑起来**（这与"没有 pipe / 没有管理器分配 / pVM=0"完全一致）。
- 同轮 c523 的 trap 是我们探针自身的问题（interposer 里 dlopen 递归导致 `0x627231` 崩），**不是 App 的崩溃**。
- ★ 新线索：内核日志显示
```
Brought up 1 CPUs / SMP: Total of 1 processors activated
```
  而 `Application_System_Init` 末尾调用 `osal_set_cpu(thread, 15)` ⇒ **把线程绑到 CPU 15**
  ⇒ 在 1 核（机器 max_cpus 甚至被限制为 1，`-smp 2` 直接报 "max CPUs supported is 1"）环境下，
    这个亲和性设置必然无效。**"线程建了但没跑起来"很可能与此有关。**
- 下一步（第 10/60 轮）：
  1) 读 Appframework 线程的 `/proc/self/task/<tid>/stat` **utime 两次**，确认它是否在跑（utime 是否增长）；
  2) 若 utime 恒为 0（从未被调度）⇒ 与 CPU 亲和性/osal 线程调度有关，需从 `osal_set_cpu`/osal 线程创建侧解决；
  3) 若 utime 有增长 ⇒ 线程在跑但卡在某个我们没覆盖的地方，继续锚点二分。

## ★ 新 goal 第 10/60 轮（2026-10-24 06:15）：**确认阻塞点 = `initializeConfig` 内部**（对方推断完全正确）
- 给 `initializeConfig` interpose 加 ENTER/EXIT 日志（只记录 tid 186 的关键分配）：
```
INITCFG_ENTER tid=186 lr=0x06724EAD          ← 进入
（没有 INITCFG_EXIT）                          ← 从未返回
NEW2 sz=24 tid=186 lr=0x05FCDE8F             ← initializeConfig 里的 new(24) 成功了
NEW2 sz=24 tid=186 lr=0x0F074AC1  ×N         ← 之后进入 viewUnified2 的代码（0x0EC64000+0x3E0AC1）
                                             反复分配 24 字节（同一返回地址）⇒ 循环
```
- 折合地址：
  - `lr=0x05FCDE8F` ⇒ libObj+0x14BE8F ⇒ 正是 `initializeConfig` 里 `blx operator new` 之后的返回地址 ✓
  - `lr=0x0F074AC1` ⇒ **viewUnified2.so + 0x3E0AC1**（该地址最近的导出符号是 `ViewRootRemoteToInstance`(0x3326D4)，
    之后属于未导出内部函数）
- 结论：
```
· 0x8A2EE0 → 0x8A2E70 → initializeConfig 进入，new(24) 成功
· 随后 ctor（0x14BD58）把控制流带进 viewUnified2，并在那里【反复 new(24) 循环】
· initializeConfig 永不返回 ⇒ 0x8A2E70 不返回 ⇒ 信号量/pipe/管理器分配/ViewManager 全都不发生
```
⇒ **"1 CPU / CPU15 亲和性"那条线索不再是首要嫌疑**（线程明明在跑）。
   真正的问题是 **initializeConfig 内部那条进入 viewUnified2 的路径陷入循环**（很可能是"创建资源失败→重试"）。
- 下一步（第 11/60 轮）：
  1) 定位 viewUnified2 +0x3E0AC1 处的函数（反汇编该区域，看它的循环条件与它调用的外部 API）；
  2) 若它调用 GRM_*/DMM（我们已知这些在 QEMU 里没人应答），就正好接上"实现 GRM/DMM shim"这条既定路线⑤。

## 新 goal 第 11/60 轮（2026-10-24 06:25）：循环是**无界分配**（1 → 938 次 24 字节），且与我们的 WS 假对象无关
- 在 `operator new` 里按 tid 186 计数：
```
INV 采样：c24=1 → c24=1 → c24=938   （约 20 秒，仍在增长）
c40（0x28）只有 2 次
```
  ⇒ `initializeConfig` → `new(24)` 成功 → 进入 viewUnified2 的代码里**反复 new(24)，无界增长**
  （每轮多 24 字节 ⇒ 既是死循环也是内存泄漏）。
- 排除实验：把 WidgetSystem 假对象的 `+0x484` 环形链表头从"自指"改成"NULL"，
  **c24 仍增长到 938（完全一致）** ⇒ 该循环**不是**我们造的那个链表导致的。
- 调用点定位（按 lr 与 Thumb 位还原）：
```
lr = 0x0F074AC1 ⇒ 实际返回地址 0x0F074AC0（Thumb 位 1）
⇒ viewUnified2.so + 0x3E0AC0
反汇编 0x3E0AB6..0x3E0AC8 显示：0x3E0ABA ldr / 0x3E0ABE 处应有一条 2 字节的 blx（进 operator new）
⇒ 循环体在 viewUnified2+0x3E0A9x..0x3E0AC0 一段
```
- 下一步（第 12/60 轮）：
  1) 从已知导出符号（`ViewRootRemoteToInstance` 0x3326D4）向后定位该内部函数的真实入口，
     完整反汇编 0x3E0A00..0x3E0B20，找出循环条件与它调用的外部 API；
  2) 判断它是"遍历某个列表"还是"重试创建资源" —— 前者看它遍历的结构，后者看它等的资源（可能又回到 GRM/DMM）。

## 新 goal 第 12/60 轮（2026-10-24 06:30）：装 GRM shim（12 个函数）—— 一个都没被调用；PC 采样显示 CPU 大部分耗在内核态
- 本轮实现并在探针里 interpose 了 12 个 GRM 导出函数（bitmapCreate/Composite/GetPhysicalAddress/GetRowPixelCount/
  GetSubRectangle/WriteLockBits/WriteUnlockBits/gpermFillrect/gpermRectblit/screenGetOnBitmap/fbrmInitializeYUV/fbrmSetScreenPreference）：
```
（无论是否按 tid 门控，GRM 日志一条都没有）
⇒ 这些函数在当前阶段【没有被任何线程调用】（或被 libObj 内部直接绑定，绕过 PLT）
```
- c24（tid186 的 24 字节分配计数）在不同运行间波动很大（938 / 9），**与是否装 GRM shim 无关**（是同一次实验的随机性）。
- `INITCFG_ENTER=1, EXIT=0` 仍然成立 ⇒ 初始化仍在 `initializeConfig` 内部不返回。
- 用 QEMU gdb 对 guest **采样 PC**（8 次）：
```
PC=60149D3C ×5 / PC=602CF7A0 / PC=6014F37C
```
  这些地址都在内核态范围 ⇒ CPU 主要耗在**内核**（大量 24 字节分配引发的缺页/memset 等），
  而不是明显停在某个用户态函数里（因为循环太快，采样容易落进内核）。
- 下一步（第 13/60 轮）：
  1) 换策略：**只用 `Application_System_Init(0)`（名字修复）而不预填 engine/WS 假对象**，
     看固件是否能自己走完（此前的 wgtsys 崩溃可能只是"我们直接调 0x8A2EE0"造成的）；
  2) 若不行，再回到"循环条件"分析：用 gdb 连续采样并统计用户态 PC 的分布（过滤 <0x10000000）。

## 新 goal 第 13/60 轮（2026-10-24 06:40）：对照实验 —— **去掉假对象就退回崩溃**；gdb 采样 24 次全落在同一内核/低地址
- 对照（只保留 `Application_System_Init(0)`，不预填 engine/WS/RM/Input）：
```
NOFAKE
ASICALL arg=0 / ASI returned
trap=1  address=0x4C
viewUnified2 出现 0 次（连加载都没到）
```
  ⇒ **我们的 engine/WS 假对象是必需的**（否则立刻崩在 0x4C），
    也说明"固件自己不会创建 WidgetSystem"这条判断依然成立。
- gdb 采样（24 次，间隔 0.5s，在进入循环约 50s 后）：
```
24/24 全部 = 0x60149D3C
```
  ⇒ 该时刻 CPU 停在**同一个地址**（低地址/内核映射区），说明：
    要么处在内核 idle/自旋，要么那个"循环"在约 20 秒后已经结束，
    此时 App 线程又回到等待状态（此前 c24 采样 1→1→938 后不再增长，与此吻合）。
- 结论（本轮）：
```
1) 假对象（engine + WS）是当前唯一能让 App 线程推进的组合；
2) initializeConfig 的问题更像是"跑了约 20 秒（约 938 次 24 字节分配）后停在某处"，
   而不是永久死循环；
3) 下一步应确认它那 20 秒是在做什么（是遍历 310 个资源？还是超时重试？）
```
- 下一步（第 14/60 轮）：
  1) 在 c24 增长期间（前 20 秒）就是否有成对的其它分配/调用做统计（例如 40/88/172 的增长曲线）；
  2) 或在 20 秒窗口内每 2 秒读一次 tid186 的 `/proc/self/task/<tid>/stat` utime 与 wchan，
     判断它是在"自旋计算"还是"反复进入内核"。

## 新 goal 第 14/60 轮（2026-10-24 06:45）：循环是**约 40ms 一轮的慢速重试**（≈26 次/秒的 24 字节分配），不是狂奔
- 把采样改成每秒一次（INV 每次循环都打印），统计 tid186 的分配：
```
c24 序列：0 → 1 → 1 → 1 → 1 → 1 → 1 → 1 → 1 → 1 → 1 → 1 → 1 → 26 → 53 …
⇒ 稳定以约 +26/秒 的速度增长（每 ~38ms 一次）
其他尺寸 c40/c88/c112/c172 全为 0
```
  ⇒ 这是**慢速重试循环**（每轮 new(24) 一次 + 约 40ms 间隔），与日志里大量 `IMNANOSLEEP` 吻合。
  之前看到"938"是因为采样间隔较长、累计了约 36 秒。
- 重要排除：**`viewUnified2.so` 并不导入任何 `GRM_*` 符号**（1793 个未定义符号里没有 GRM_/dmm/gpe 匹配）
  ⇒ 循环里的 GRM 调用若是通过 libObj 内部完成，则我们的 interpose 天然抓不到
  ⇒ 这也解释了"装了 GRM shim 却一次都没触发"。
- 下一步（第 15/60 轮）：
  1) 在 40ms 循环里确定它每轮**检查什么条件**：可以在 24 字节对象内容上做文章
     （new(24) 返回后立刻填标记，看它是否被当作"失败对象"累积）；
  2) 或直接对 `initializeConfig` 的 40ms 等待下手：interpose `nanosleep/usleep` 记录 tid186 的调用者 LR
     （LR 应落在 viewUnified2 的那个重试循环里），从而拿到循环体的确切地址。

## 新 goal 第 15/60 轮（2026-10-24 06:50）：tid186 **不调用任何被追踪的同步原语**（sem/rcv/sleep 全无）
- 分别 interpose 并在 **tid 186** 上记录：
```
nanosleep / usleep / clock_nanosleep  → 0 条
osal_rcv_msg_tmo（任意 qid）          → 0 条
osal_wai_sem_tmo（任意 id）           → 0 条
```
  而同一次运行里 c24 仍以约 26/秒增长（最终 938）⇒ **这个"40ms 节奏"不是来自 sleep 或 IPC 等待**。
- 推论（重要）：
```
循环是【纯用户态 + 每轮都很重】的那种：没有睡眠、没有等信号量、没有等消息，
但因为每轮做了足够多的重活（内存分配/遍历/失败重试），自然形成 ~40ms 的节奏。
⇒ 所以之前"重试等待"的猜测要修正为"每轮都在做一次【失败的操作】"。
```
- 下一步（第 16/60 轮）：
  1) 记录 **tid 186 的 open/mmap**（尤其是否反复打开/映射 `*.uxc` 资源）——这最能说明"每轮在重试什么"；
  2) 若确实是反复 mmap 资源文件失败，则问题落回"资源加载/GRM 内存"这条线（与路线⑤衔接）。

## 新 goal 第 16/60 轮（2026-10-24 07:00）：tid186 也不做**任何 open/openat**；PC 采样显示 CPU 大量时间在内核/空闲态
- 过滤 tid 186 的 `open` / `openat`：**0 条**（同一次运行 c24 仍增长到 938）
  ⇒ 这个循环**不涉及文件*。
- 在循环窗口内（~12s 与 ~18s 各采样 20 次）统计 guest PC：
```
60149D3C ×19   6014F37C ×5   FFFF0018 ×4   602CF7A0 ×2   602CF680 ×2
0402472C ×2    04024722 ×2   602CFE50 ×1   602CF5C0 ×1   042F517C ×1
```
  - `0x60xxxxxx` 段与 `0xFFFF0018` 都在内核/idle 相关区域 ⇒ **CPU 大部分时间不在用户态跑**
  - 用户态样本落在 `0x0402xxxx / 0x042Fxxxx`（libc/libMWF 区间），没有落在 libObj/viewUnified2
- 结合此前 `W7 app wchan = futex_wait_queue_me`：
```
⇒ Appframework 线程实际是【阻塞在 futex（pthread 条件变量/互斥量）】，
  而且我们追踪的 sem/rcv/sleep/open 全部不命中 —— 说明它用的是 pthread 原语，
  并且是"带超时等待"（约 40ms 醒一次），每轮分配一个 24 字节对象。
```
- 下一步（第 17/60 轮）：
  1) interpose `pthread_cond_timedwait` / `pthread_mutex_lock`，记录 **tid 186 + 返回地址**；
     返回地址会直接落在 viewUnified2/libObj 的那个重试循环里 ⇒ 一次拿到循环地址与"等的是哪个 condvar"。

## 新 goal 第 17/60 轮（2026-10-24 07:05）：tid186 也不调 pthread condvar/mutex（连 PLT 层都没有）
- interpose 并在 tid 186 上记录：
```
pthread_cond_timedwait / pthread_cond_wait / pthread_mutex_lock → 0 条
```
  而同一次运行 c24 仍增长到 938。
- 累计排除（全部在 tid 186 上）：`sem`、`rcv_msg`、`nanosleep/usleep/clock_nanosleep`、
  `open/openat`、`pthread_cond_*`、`pthread_mutex_lock` —— **全部 0 条**。
- 这意味着：**节奏来源只剩"我们还没 hook 的调用"**，最可能是：
```
read / write / ioctl（尤其 /dev/dmpgles2、/dev/grm_ma 等设备节点）
或 mmap/brk/clock_gettime/poll/select 一类
```
- 下一步（第 18/60 轮）：interpose `ioctl` / `read` / `write`，仍只在 tid 186 上记录
  （这类调用若高频出现，就说明循环在"反复给某个设备发请求并被拒绝"）。

## ★★★ 新 goal 第 18/60 轮（2026-10-24 07:15）：抓到了！tid186 在循环 `ioctl(fd=3, 0x80000005) = -1`
- interpose `ioctl/read/write`（tid 186 过滤）后，模式非常清楚：
```
IO186 fd=3 req=0x8000000A r=0
IO186 fd=3 req=0x80000005 r=-1     ← ★ 一直失败
IO186 fd=3 req=0x8000000B r=0
（以上三个一组，反复循环）
```
- 对照此前内核 `UIPCARG im.elf.real cmd=0x8000000a/0x80000005/0x8000000b` 的日志：
  **fd=3 就是 OSAL UIPC 的字符设备**，三种 req 分别是
  `0x8000000A`=发送准备 / `0x80000005`=**接收** / `0x8000000B`=发送提交。
  ⇒ **App 线程一直在做 `osal_rcv_msg_tmo`，但接收请求返回 -1（错误，不是超时）**
  ⇒ 它每轮 new(24) 一个缓冲/错误对象，然后重试 —— 这正是那 ~26 次/秒的来源。
- 这也解释了为什么我们 interpose `osal_rcv_msg_tmo` 抓不到：**App 是直接走 ioctl 到内核 osal 模块**
  （`libosal_uipc` 内部用 syscall/ioctl），不经过我们 hook 的那层 C 符号。
- 下一步（第 19/60 轮）：
  1) 在 `ioctl(0x80000005)` 时 dump **第三个参数（struct 指针）的前 16 字节**，
     取出它请求的 **qid 与 timeout**；
  2) 用该 qid 判断"队列是否存在/是否注册"，再决定是"注册队列"还是"投一条消息"（对方已给 valloc→memcpy→snd 三步法）。

## ★★★ 新 goal 第 19/60 轮（2026-10-24 07:25）：拿到确切 qid —— **`0x003E014D`**（子系统 0x3E），接收恒返回 -1
- 在 `ioctl` 里 dump 参数结构（tid 186）：
```
IO186 fd=3 req=0x8000000A r=0   a0=0x003E014D a1=0x0ED63674 a2=0x003E014D a3=0x1
IO186 fd=3 req=0x80000005 r=-1  a0=0x003E014D a1=0x0ED63624 a2=0x00000001 a3=0x046ECF6C
IO186 fd=3 req=0x8000000B r=0   a0=0x003E014D a1=0x046ECF6C a2=0x003E014D a3=0x1
（循环重复，a2/a3 有不同取值）
```
  ⇒ **App 在向 qid `0x3E014D` 发送请求（成功），然后从同一 qid 接收应答（恒 -1）**
- 该 qid 我们历史上多次见过（早期就判断是 **backup 服务端点**）。而我们的 `backup.ko` 现在是**纯 stub**
  （只导出符号、不注册队列）⇒ 很可能就是"没有人应答 → 接收报错 → 无限重试"。
- 另确认：内核模块侧确实提供注册接口 —— `backup.ko` 导入了
  `osal_reg_msg_queue_cb / osal_reg_sync_msg_queue_cb / osal_snd_msg / osal_valloc_msg_wait / osal_snd_sync_direct` 等。
  ⇒ **我们的 backup 替代模块完全可以自己注册一个队列并作最小应答。**
- 下一步（第 20/60 轮）：
  1) 先用"全线程"记录 `ioctl(0x80000005)` 的参数与返回，找一条 **r>=0 成功** 或 **-1022 超时** 的样本，
     用来校准该结构体字段含义（a2 到底是超时还是别的）；
  2) 然后按对方给的"valloc → memcpy → snd"或用内核 `osal_reg_msg_queue_cb` 注册 `0x3E014D` 队列并作最小应答。

## 新 goal 第 20/60 轮（2026-10-24 07:35）：确认 0x3E014D 已注册（是"服务端队列"），失败原因是**没人回复同步请求**
- 在 OSAL dump 里查到该队列确实存在：
```
14D	0 0 0 0 1 0 0 0        ← 子系统 0x3E 下的本地队列 0x14D 已注册（某状态位=1）
```
- 结合 ioctl 调用模式：
```
0x8000000A / 0x8000000B  (发送，成功)  →  a0=0x3E014D
0x80000005               (接收，-1)   →  a0=0x3E014D
```
  ⇒ 这是**同步请求/应答**的典型形态：App 把请求发给 `0x3E014D`（backup 服务队列），
    然后等该服务把应答送回来；**我们的 backup 只是符号 stub，从不回包** ⇒ 接收恒 -1 ⇒ 无限重试。
- 结论：**修复点 = 让 backup 替代模块真正注册队列并对请求作出应答**（这正是路线③要的"服务就绪"）。
- 下一步（第 21/60 轮）：
  1) 扩展 `backup_stub3.ko`：init 里调用 `osal_reg_msg_queue_cb(0x3E014D, cb)`，
     cb 里打印收到的消息头（qid/长度/前 32 字节）并**回一条最小应答**；
  2) 若注册失败（-1024 等），改用 `osal_reg_sync_msg_queue_cb` 或改用"用户态注册"（对方给过的 API 组合）。

## ★ 新 goal 第 21/60 轮（2026-10-24 07:45）：`osal_reg_msg_queue_cb` **必须三参数**（qid, cb, arg），注册成功 rc=0；但**回调一次都没被调用**
- 从 `osal_uipc.ko` 反汇编确认签名（`0x3B4C`）：
```
tst r0,#0x8000   ; qid 不能带 bit15（0x3E014D 满足）
cmp r2,#0        ; ★ 第三个参数必须非 0，否则 -1024
⇒ int osal_reg_msg_queue_cb(unsigned qid, void *cb, void *arg)
```
  按三参数改写后：`BKREG rc=0`（注册成功）。此前 2 参数版本的 `-1024` 就是这个原因。
- 但把该模块与"引擎+WS 假对象 + ASI(0)"探针组合跑（ab_v545）：
```
BKREG rc=0
INITCFG_ENTER tid=186          ← App 线程照旧进入 initializeConfig
c24 仍增长到 938               ← 循环没有被打破
（BKCBM：一次都没有 —— 我们的回调从未被调用）
```
  ⇒ **App 的请求并没有投递到我们注册的这个队列 cb 上**。
- 推断：`0x3E014D` 更可能是 **App 自己的应答队列**，真正被请求的服务端是"另一个端口/队列"，
  或 OSAL 的投递依赖"端口(port)注册"而不只是"队列 cb 注册"。
- 下一步（第 22/60 轮）：
  1) 在当前镜像里**全量记录发送侧**（req=0x8000000A/0x8000000B）的参数，特别是 a1/a2 里的目标与长度；
  2) 用 `osal_stat_msgq(0x3E014D, &st)` 在 App 发送前后各读一次，看"发送是否真的进了这个队列"；
  3) 若确认 App 发的是"另一个 qid"，就按那个 qid 注册并应答。

## 新 goal 第 22/60 轮（2026-10-24 07:55）：队列状态始终 `[0,1,0,0]`（无积压），消息头 dump 出 **dest 也是 0x3E014D**
- 在 backup 模块里加内核线程，每 2s 调 `osal_stat_msgq(0x3E014D, st)`：
```
BKSTAT r=0 [0,1,0,0]   （全程不变：0 条积压、某计数=1）
```
  ⇒ **App 的发送并没有在该队列里堆积**，我们的注册也从未收到回调（BKCBM 仍 0）。
- 发送侧消息头 dump（`ioctl(0x8000000A)` 的 msg 指针前 6 字）：
```
MSG = [0x0EC636A0, 0x00000040, 0x00000001, 0x003E014D, 0x0EC63674, 0x003E014D]
       ↑payload   ↑size=64     ↑type=1     ↑0x3E014D     ↑ptr         ↑0x3E014D
```
  ⇒ 结构与"目的/来源"两个字段都是 `0x3E014D`，说明**这就是发给 0x3E014D 的同步请求**。
- 关键结论（本轮）：
```
· 我们的内核模块注册的是【内核命名空间】的 0x3E014D，而 App 的队列在【进程命名空间】，
  两者不冲突但也不相通 ⇒ 所以既没抢到消息、也帮不上忙。
· App 真正在等的是一个"能回它 0x3E014D 的服务"；在本环境里这个服务缺席
  （原厂 backup.ko 会注册并应答，我们把它换成了纯符号 stub）。
```
- 下一步（第 23/60 轮）：
  1) 尝试在内核模块里**主动向 0x3E014D 发送一条应答**（`osal_snd_msg`），看 App 的接收是否变成 0（=收到）；
  2) 若能收到，再按 App 期望的应答格式逐步补字段（这也顺便验证"服务端到底该回什么"）。

## ★★★ 新 goal 第 23/60 轮（2026-10-24 12:15）：**内核模块主动回包 → App 的 IOCTL 重试循环消失了！**
- 给 backup 替代模块加了一个内核线程：每 3 秒 `osal_valloc_msg_wait(0x3E014D,&m,64,-1)` +
  填 16 个字 + `osal_snd_msg(0x3E014D, m)`：
```
BKREG rc=0
BKALLOC r=0 m=0x60BFFFAC / BKSND rc=0   （循环重复，一次运行里共 166 次）
```
- 结果（ab_v549 / c550，跑 250 秒）：
```
IO186 记录 = 0        ← ★ App 原本那串 ioctl(fd=3,0x80000005) 重试循环【完全消失】
c24 = 0（此前 938）    ← 24 字节分配风暴也消失
ENTER=1 EXIT=0        ← initializeConfig 仍未返回（但形态变了）
viewUnified2 仍出现 7 次；pVM 仍 0；trap=0
```
  ⇒ **这证明"App 等的正是这条队列上的应答"** —— 我们回包后它就不再重试。
- 剩余问题：`initializeConfig` 仍未返回，说明**我们回的包内容不符合它的期望**（收到后既没继续也没崩）。
- 下一步（第 24/60 轮）：
  1) 用不同 `msgType/长度/字段组合`做小步试探（先试 `type=2, size=64`、再试 `type=1`），
     每轮只看 `INITCFG_EXIT` 是否出现；
  2) 同时问对方：ZV-E10 的 `0x3E014D`（backup 服务）应答的**最小字段集**是什么。

## 新 goal 第 24/60 轮（2026-10-24 12:25）：按对方新线索查 `dlopen` —— 只看到重复 `libObj.so` 成功加载，没看到 `viewUnified2.so` 的配置初始化入口被调用
- 对方查明 `initializeConfig`（`libObj+0x14BE85`）实际是 **dlopen 配置模块并调入口**：
```
libObj.so        → initializeModelConfig() / getModelConfig()
viewUnified2.so  → initializeViewConfig()  / getViewConfig()
（失败会 printf "sym( %s ) failed - dlerror() = %s"）
```
- 我方 interpose `dlopen`（带 tid）实测：
```
DL2 tid=121 /usr/lib/libObj.so r=0x39B08
DL2 tid=138/139/140 /usr/lib/libObj.so r=0x39B08   ×N（同一路径反复成功）
（没有观察到 initializeViewConfig 相关的 dlopen 序列，且 INITCFG 本轮也没出现）
```
  ⇒ `dlopen` 本身不是阻塞点（成功且返回句柄）；
  ⇒ 但**日志被早期加载刷满**，配置阶段的 dlopen 可能被 cap 截断，需下一轮用"只记录失败 + 只记录 view/Config 关键字"的方式复核。
- 下一步（第 25/60 轮）：
  1) 只记录 **r==0（失败）** 的 dlopen，以及路径含 `view`/`Config` 的 dlopen；
  2) 同时记录 `initializeViewConfig` / `getViewConfig`（能用 dlsym 从 viewUnified2 句柄取到符号就 hook，取不到就只记录 dlsym 调用）；
  3) 若确认配置模块初始化卡住，则回到"它的构造函数里向哪个队列要数据"（对方已给出 ViewConfig 构造链）。

## 新 goal 第 25/60 轮（2026-10-24 12:45）：还原原厂 backup 的队列注册方式 —— **它注册两条队列：0x3E014D（sync）+ 0x3E0150（async）**
- 反汇编 `backup.ko::backup_osal_init`（0x50C0）得到原厂注册序列：
```asm
50d0: r0 = [0x514c] = 0x3E014D
50d4: r1 = [0x5150] = .bss 内某对象
50d8: r2 = [0x5154] = 0
50dc: r3 = r0
50e0: bl osal_reg_sync_msg_queue_cb      ; ★ 先注册【同步】队列 0x3E014D
...
5108: r0 = [0x515c] = 0x3E0150
510c: r1 = [0x5160] = .bss 内某对象
5114: r2 = [0x5164] = 0
5118: bl osal_reg_msg_queue_cb           ; 再注册【异步】队列 0x3E0150
```
  ⇒ **App 用的 `0x3E014D` 是"同步请求/应答"队列**，由 `osal_reg_sync_msg_queue_cb` 注册。
    这解释了我们之前只注册 async 队列 `0x3E014D` 收不到回调的现象（类型不对）。
- 本轮按原厂序列改写我们的替代模块：先 `osal_reg_sync_msg_queue_cb(0x3E014D, cb, 0, 0x3E014D)`，
  再 `osal_reg_msg_queue_cb(0x3E0150, cb, 0)`：
```
BKREGSYNC rc=-1024     ← 同步注册被拒
BKREGASYNC rc=-1024    ← 异步注册也被拒（此前同参数曾 rc=0，说明 0x3E014D 这次已被占/参数不符）
INITCFG_ENTER=1 EXIT=0
```
  ⇒ 参数形态仍需按 sync API 的真实签名校正（下次直接对照 `osal_reg_sync_msg_queue_cb` 的反汇编）。
- 下一步（第 26/60 轮）：
  1) 反汇编 `osal_reg_sync_msg_queue_cb`（内核模块内），确定其参数与 -1024 条件；
  2) 按正确签名重新注册，并在回调里 dump 请求内容（App 的同步请求会直接送到该回调）。

## 新 goal 第 26/60 轮（2026-10-24 12:55）：**同步队列注册成功了**（rc=0），但没人填应答 → bootin 直接崩
- 反汇编 `osal_reg_sync_msg_queue_cb`（内核 0x3068）得到真实签名：
```asm
tst r0,#0x8000        ; qid 不能带 bit15
cmp r2,#0             ; ★ r2 必须非 0
bl alloc_cb           ; 分配 cb 结构
str r6,[r5,#0x14]     ; r2 → cb+0x14（上下文）
str r8,[r5,#0x18]     ; r3 → cb+0x18
str r5,[r4,#0]        ; ★ r1 是【输出指针】，把 cb 结构地址写回给调用者
```
  ⇒ `osal_reg_sync_msg_queue_cb(qid, void **cb_out, void *ctx, void *ctx2)`
    （与我们之前当成"函数指针"完全不同）
- 按正确签名改写后：
```
BKREGSYNC rc=0 slot=0x60B9C920     ← ★ 注册成功（0x3E014D 同步队列）
BKREGASYNC rc=-1024                ← 另一条异步队列参数仍不对，先不影响
```
- 但本轮出现**新问题**：
```
bootin.elf 用户态崩溃 address=0x0（trap 0），系统落到 "Exception>" 交互提示
INITCFG_ENTER=0  ← 根本没走到 App 初始化
（我们的 bk_sync_cb 一条日志都没有 ⇒ 回调没被调用，但 bootin 已经认为同步请求"成功"并解引用了空应答）
```
  ⇒ **说明这条同步队列在 bootin 阶段就被用到**，只注册不实现应答会让它拿到无效数据而崩。
- 下一步（第 27/60 轮）：
  1) 确认同步回调的 ABI（形参顺序、如何写回应答/长度/返回码），必须由内核侧观察者给出；
  2) 按 ABI 回一个"最小合法应答"（至少让 requester 拿到非空/长度合法的缓冲），
     先让 bootin 不崩，再逐步补内容。

## 2026-10-24 13:10 新菜单线（与「新菜单」会话协作）：混合镜像已能过 bootin，卡在 im.elf 早期崩溃
- 对方修正：`ZVE10_EMMC_FULL.img` = **eMMC boot 分区(4 MiB) + 用户区**，
  用户区内所有分区相对分区表 **+4 MiB**；此前按表偏移写 hybrid ⇒ 除 /usr 外全错位，`/setting` 读到垃圾。
- 我方实测（本地修正）：把原 dump 前 4 MiB（boot area）拼回新 hybrid 镜像后，
  `/usr` 出现在 **732 MiB**（与我们既有镜像约定一致），appFw.so = 53,189,440 B ✓
- 直接跑新镜像（未拼 boot area）：
```
mounting1 /dev/nflasha2 -> /setting as vfat failed. errno(2)
main: mounting /setting failed(-1)   → bootin 走错分支 exec net_cmdd.elf（不存在）→ 退出 255
```
- 拼回 boot area（`/mnt/build/tmp/hb/fixed.img`）后：**卡在 upm**
```
末条 MODLOAD read ok len=137260（upm）
```
  ⇒ 这正是我们老菜单线的老问题：**/usr/kmod/backup.ko 原厂模块会让 upm 卡住**。
    新镜像的 /usr 是 ZV-1F 的，但 kmod 里 backup.ko 仍是原厂 ⇒ 同样中招。
- 换上我们的 stub（backup_stub3 / accel / mag / compass / grm_ma / upm）后（`h4.img`）：
```
modloads=32（越过 upm）
EXECVE /usr/bin/bootin.elf → EXIT code=0   ★ bootin 这次成功
EXECVE /usr/bin/im.elf
IMOPEN FAIL //etc/ld.so.cache, libjemalloc_ds.so.1 …（正常缺失）
崩溃：program=/usr/bin/im.elf(pid 118) address=0x0 trap=0x0e
（没有 IMOPEN OK libObj.so，也没有 appFw ⇒ im.elf 在 dlopen 之前就崩了）
```
- 结论与下一步：
```
① 启动链已推进到"bootin 成功 + im.elf 启动"（新菜单最大进展）
② im.elf 早期 NULL 崩溃 → 需要看它是 ZV-E10 版还是 ZV-1F 版、以及它启动时依赖什么
   （可以注入探针拦 dlopen/main，或直接用 gdb 看崩溃点）
```

## ★★ 新 goal 第 27/60 轮（2026-10-24 13:25）：按 ABI 实现 backup 同步应答 —— **崩没崩、风暴也没了**
- 对方给出同步回调 ABI（内核 `__default_cb` @ 0x2434 实证）：
```asm
2440: r6 = cb
2454: r3 = [r6, #0x14]     ; 回调函数
2458: r0 = r4 + 12         ; ★ arg0 = msg + 0xC（应答缓冲）
245c: r1 = [r6, #0x18]     ; ★ arg1 = ctx
2460: blx r3               ; cb(reply_buf, ctx)
```
  ⇒ 注册签名 `osal_reg_sync_msg_queue_cb(qid, &cb_out, cb_fn, ctx)`；r2==0 返回 -1024（已核实）。
- 按此实现 backup 替代模块：
```c
static void backup_sync_cb(void *reply_buf, void *ctx) {
    memset(reply_buf, 0, 200);        // 应答长度 = 注册时的 size
}
osal_reg_sync_msg_queue_cb(0x3E014D, &g_cb_out, backup_sync_cb, (void*)0x3E014D);
```
- 实测（ab_v602/603）：
```
BKREGSYNC rc=0 cb=0x60B9C920
BKCBN rb=0x60BFFFAC ctx=0x003E014D msg[f7f7f7f7 00018103 a5a5a5a5] rb[00000003 00000000 00000002]   ×N
  → 回调被真正调用！请求内容可见（rb 进入时 rb[0]=3 / rb[2]=2 等，是被请求的操作码与参数）
bootin 不再崩（trap=0），启动链完整走到：bootin → sen.elf → launch_shell.elf → net_cmdd.elf → im.elf
且 c24（24 字节分配风暴）= 9（此前 938）——风暴消失
但 initializeConfig 仍 ENTER=1 / EXIT=0（还没返回）
```
- 现状：
```
✅ bootin 崩溃（address=0）已解决 —— 原因就是"注册了同步队列却不回包"
✅ 24 字节分配风暴消失（此前 938 次）
❌ initializeConfig 仍未返回（现在它在等【另一件事】）
```
- 下一步（第 28/60 轮）：
  1) 用 BKCBN 记录的 `rb[0]/rb[2]` 操作码做应答字段试探（把"成功码"写进应答，看 EXIT 是否出现）；
  2) 同时看 initializeConfig 现在是否又卡在别的队列（用之前的 ioctl/stat 组合观察）。

## 2026-10-24 13:30 新菜单线：v4（ZV-1F 的 im.elf + bootin.elf）—— 仍然崩在 NULL，且崩在库内而非 im.elf 本体
- 用对方 v4 整盘镜像（`zve10_hybrid_full_v4.img`，md5 0f6245dc…）+ 我们的 stub 模块（backup/accel/mag/compass/grm_ma/upm）启动：
```
modloads=32（越过 upm）
EXECVE /usr/bin/bootin.elf → EXECVE /usr/bin/im.elf
IMOPEN FAIL //etc/ld.so.cache (err=-2)        ← 对照：ZV-E10 正常启动时同样会 FAIL 3 次，属正常
崩溃：program=/usr/bin/im.elf(pid 118) address=0x0 trap=0x0e
[ZZEXCSTACK] sp=5EFFFB58 pc=0x05B4F3BE lr=0x05B4F407
栈上代码指针：0x04900DE0 / 0x04074D07 / 0x0406F2C1 / 0x0406F07B（都在 0x04xxxxxx = libc/库区）
```
  ⇒ **换 im.elf 版本没有解决问题**；崩点在库内部（libc 区调用链），im.elf 本体（0x8000 段）没出现在栈上。
  ⇒ 更像"某个前置数据/句柄为空，导致库函数对 NULL 解引用"。
- 已请对方走 A（对比 ZV-E10 vs ZV-1F 的 /etc），我方提议可往 hybrid /usr 注入探针抓 maps + dlopen 序列。

## 2026-10-24 14:00 新菜单线：upm_bootmode 假设被否；探针注入遇上 loader 路径问题
- 按对方建议，把 `upm` stub 升级为**同时创建 `/proc/udm/upm_bootmode`（返回 "0\n"）**：
```
upm_stub2 loaded udm_dir=6a5c6ce0 bootmode=6a5c6c60    ← proc 节点创建成功
bootin → im.elf → 仍然 address=0x0 崩溃（trap 0x0e）
```
  ⇒ **`/proc/udm/upm_bootmode` 缺失不是原因**（该假设排除）。
- 按对方授权尝试注入崩溃探针（SIGSEGV handler 打印 pc + 归属映射）：
```
镜像里：/usr/bin/im.elf → 包壳（LD_PRELOAD=/usr/lib/libcrashprobe.so）
        /usr/bin/im.elf.real ← 原 ZV-1F im.elf
        /usr/lib/libcrashprobe.so ← 探针
结果：im.elf.real: error while loading shared libraries: ld-linux-armhf.so.3: cannot open shared
      （IMOPEN FAIL //usr/lib/ld-linux-armhf.so.3）
⇒ 包壳方式在这个 /usr 布局下触发 loader 的 path 解析问题，探针没跑起来。
```
- 下一步（第 28/60 轮）：
  1) 换注入方式：把探针放到 rootfs 侧并用 `LD_PRELOAD=/lib/...`（与 im.elf 原本的 loader 搜索路径一致），
     或改用**内核异常钩子**（我们已有 ZZEXCSTACK）在崩溃时额外 dump `/proc/<pid>/maps`；
  2) 一旦拿到 pc 对应的映射（0x05B4F3BE 属哪个库），就能定位真正的 NULL 解引用点。

## ★★ 2026-10-24 14:15 新菜单线：**崩溃点精确定位到 `libMWF.so+0x323BE`**（内核 maps dump 方案生效）
- 按对方建议走"方案 3"：给内核异常钩子加 **用户映射 dump**（`ZZMOVMAP`），
  在 `em_show_syndrome()` 里遍历 `current->mm->mmap`（上限 220 段）打印 `start-end 权限 文件名`。重建 zImage 后实测（v5 镜像 + stubs）：
```
[ZZEXCSTACK] sp=5EFFFB58 pc=05B4F3BE lr=05B4F407
pc 0x5B4F3BE -> 0x5B1D000-0x5B58000 rx- libMWF.so
lr 0x5B4F407 -> 同区间 libMWF.so
⇒ 崩溃在 libMWF.so 内：pc = libMWF+0x323BE，lr = libMWF+0x32407
```
- 同时确认两条：
```
① v5（IMDB mask 补丁）没有解决该崩溃（v5 与 v4 行为一致）
② 老菜单镜像（zve10_oldmenu_full.img + stubs）同样崩在同一处（libMWF NULL 解引用）
   ⇒ IMDB 的 10 处 mask 补丁不是这个崩溃的原因
```
- 下一步：把 `libMWF+0x323BE` 交给对方反汇编（ZV-1F 版 libMWF），看它是哪个函数、解引用谁；
  我这边可同时 dump 崩溃前的 dlopen/open 序列与几个关键全局。

## ★★★ 2026-10-24 14:25 新菜单线：崩溃调用链完整解出 —— **Monitor::Init 打印横幅时，MonDrv 的队列还是 NULL**
- 用内核 `ZZMOVMAP` 定位到 `libMWF+0x323BE`，随后用栈上返回地址还原调用链（全部在 libMWF 内）：
```
pc 0x323BE  MonQueue::GetQueueRemain() +0x0     ← ldr r3,[r0] 且 r0 == NULL ⇒ 崩在地址 0
lr 0x32407  MonQueue::IsNearlyFull()  +0xB      ← 调 GetQueueRemain(this)
    0x31419  MonDrv::PutcharSub(int)  +0x15
    0x314AB  MonDrv::PutsNoCR(char const*) +0x13
    0x32657  Monitor::Init()          +0x57     ← 这里在打印初始化横幅
```
- 反汇编 `Monitor::Init()`（0x32600）确认：
```
32606: blx MonLib::Init()
3260a: blx MonFil::Init()
3260e: blx MonFlt::Init()
...
32652: blx MonLib::MsgNewLine()
32656: ldr r0,[pc] / add r0,pc
3265a: blx MonLib::ConPutStr(str)      ← ★ 打印横幅，走 MonDrv → MonQueue
```
  ⇒ **`MonDrv` 没被初始化（其队列指针为 NULL）**，所以一打印就空指针崩。
  `MonDrv::Init()` 本身是导出符号（`libMWF+0x311F0`），但**在这条路径上没有被调用/没有成功**。
- 关键结论：**这个崩溃是"诊断输出炸了"，不是业务逻辑本身**——但它挡住了整个启动。
- 下一步（第 25/60 轮）：
  1) 用 `-nostdlib`（纯裸 syscall）编一个探针，构造期从 `/proc/self/maps` 解析 libMWF 基址，
     主动调用 `libMWF+0x311F0`（`MonDrv::Init()`），看崩溃是否消失；
  2) 若 `MonDrv::Init()` 自身依赖设备（UART/monitor 节点），再补那个设备/节点。

## ★★★ 2026-10-24 14:40：**根因确认 —— `MonDrv::Init()` 里 `mmap("/dev/mem")` 失败**（日志实证）
- 按对方提示，直接在串口日志里找三条诊断串，结果所有镜像/所有轮次都是同一条：
```
open_fail("Failed to open stream driver!") = 0 次
mmap_fail("Failed to mmap!")              = 1 次   ★ 每次都是它
Physical Addr / Mon Buf Size 打印          = 0 次
```
  ⇒ **`open("/dev/mem")` 成功，`mmap` 失败** ⇒ `MonDrv::Init()` 走提前返回分支
  ⇒ `MWF::ObjIf_Mcmd::mp_mon` 保持 NULL（我方 `ZZMWF` 实测：`base=05B1D000 mp_mon=00000000`）
  ⇒ 之后 `Monitor::Init()` 打印横幅 → `MonLib::ConPutStr` → `MonDrv::PutsNoCR` → `PutcharSub` → `MonQueue::IsNearlyFull(NULL)` → **崩在 0x0**
- `MonDrv::Init` 反汇编确认的取数顺序：
```
3121c: Backup_read_4Byte(ID1) → r6   （物理地址）
31222: Backup_read_4Byte(ID2) → r9   （缓冲区大小）
31232: open("/dev/mem", 0x1002)
3127c: mmap(addr=0, len=r9, prot=3, flags=1, fd, offset=r6)
```
  我们的 backup stub 对这些 ID 返回 -1 或 0x1000 ⇒ **mmap 参数非法/越界 ⇒ 失败**。
- 因此这是**平台层缺口**（与菜单新旧无关，两版 libMWF 该段代码完全相同）。
- 下一步（第 30/60 轮）：
  1) 让 `Backup_read_4Byte` 对这两个 ID 返回**真实值**（解析 `/setting/Backup.bin`，magic `BK4`，
     共 1,235,836 B；需要先摸清条目格式/ID 编码 —— 直接在文件里搜 0xFA0028/0xFA005D 没命中，
     说明 ID 是"索引式"而非字面量）；
  2) 或退一步：让 mmap 请求落在 QEMU 已映射的 RAM 区间（`0x00000000-0x40000000` / `0x80000000-0xC0000000`）；
  3) 或最小侵入：把 `Monitor::Init()` 里的打印调用全部 NOP 掉（本次试了 0x32652 一处仍未消除，
     说明还有第二处打印点，需逐个定位）。

## ★★★ 2026-10-24 14:50 新菜单线：**MonQueue interposer 生效，监视器崩溃被绕过**；新崩溃落在 libc
- 按对方给的 a7c2 做法，编了 `libmonconsole.so`（`-nostdlib -shared -fPIC`，无 NEEDED 依赖）：
```c
int _ZN8MonQueue6IsFullEv(void*)        { return 1; }
int _ZN8MonQueue12IsNearlyFullEv(void*) { return 1; }
int _ZN8MonQueue7IsEmptyEv(void*)       { return 1; }
unsigned long _ZN8MonQueue14GetQueueRemainEv(void*) { return 0x40; }
int _ZN8MonQueue7EnQueueEh(void*, unsigned char)    { return -1; }
```
  注入方式：`/usr/bin/im.elf` → `#!/bin/sh` 包壳（LD_PRELOAD=/usr/lib/libmonconsole.so + exec im.elf.real），
  原 im.elf → im.elf.real；库放 `/usr/lib/libmonconsole.so`。
- 结果（ab: h18）：
```
包壳成功（不再有 ld-linux-armhf.so.3 报错 —— 因为 -nostdlib 的 .so 没有 NEEDED）
modloads=33（+1）
EXECVE /usr/bin/im.elf → im.elf.real
★ 监视器崩溃消失（没有 "Failed to mmap"→MonQueue NULL 崩溃路径）
新崩溃：program=im.elf.real(THREAD_INFRA0, pid 134) address=0x0 trap_no=6(非法指令)
[ZZREG] r0=0 r1=0x86 r2=6 r3=0x86 r4=6 ...
[ZZEXCSTACK] pc=0x04265796 lr=0x04274D8F
  pc → libc-2.11.2.so +0x15796 ; lr → libc-2.11.2.so +0x24D8F
```
  ⇒ **启动链第一次越过 MWF 监视器层，进入 App 线程（THREAD_INFRA0）**，崩点转移到 libc。
- 下一步（第 26/60 轮）：
  1) 反汇编 `libc-2.11.2.so +0x15796` 看是哪条指令（trap 6 = 未定义指令，常见于跳转到 0 或执行数据）；
  2) 同步 dump 崩溃前该线程的调用链（栈上 0x04xxxxxx 的返回地址）；
  3) 若仍是"某个函数指针为 0"，继续按"缺哪个初始化"补。

## ★★★ 2026-10-24 14:58 新菜单线：**libc 崩溃的真相 = App 自己 abort**（DMP 分配器初始化失败）
- 在日志里找到崩溃前最后一行应用输出：
```
im.elf.real: dmp_allocator.c:108: dmp_allocator_lib_init: Assertion `e == MA_ErrNone' failed.
ZZTGKILL THREAD_INFRA0 pid=118 tgid=134 sig=6 pc=04265796 lr=04274D8F
```
  ⇒ 崩溃不是"随机非法指令"，而是 **`abort()`（SIGABRT=6）**：
    libc `raise()`→`gsignal()`→`tgkill` 的路径（pc/lr 均落在 `libc-2.11.2.so` 的 raise/gsignal 内），
    随后信号处理/返回把 pc 拉到 0 → 我们内核报成 "trap 6 非法指令"。
- 断言位置归属已查明：
```
在 /usr 分区里搜 "dmp_allocator" → 命中块 → icheck → inode 180 → ncheck = /lib/appFw.so
⇒ dmp_allocator.c 是编进 appFw.so 的（53 MB），不是独立库
appFw.so 的动态符号里没有 dmp/MA 导入（717 个未定义符号里都没有）⇒ 分配器实现是静态内联/内部调用
```
- 结论（本轮）：
```
新菜单 App 已经跑到"初始化 DMP 分配器"这一步，
但它初始化失败 → 断言 → abort ⇒ 这是【真实硬件 GPU/内存分配器】依赖，属 a7c2 那条 shim 路线覆盖范围
（a7c2 的 menu-observe 里正是软件实现 DMP/GRM 分配）
```
- 下一步（第 27/60 轮）：
  1) 在 appFw.so 里定位 `dmp_allocator_lib_init`（用断言字符串 xref / 找 `__assert_fail` 调用点）；
  2) 看它初始化时依赖什么（设备节点 / ioctl / dlsym / 物理内存），决定是 shim 还是补设备；
  3) 若它走 `dlsym` 或某个导出 API，就直接按 a7c2 做法软件实现。

## ★★ 2026-10-24 15:10 新菜单线：又一个断言被突破，新断言指向 **DMP 服务应答（源 id 校验）**
- 在 `libmonconsole.so` 里追加两个 shim（`-nostdlib`，无依赖）：
```c
int dmp_allocator_lib_init(void){ return 0; }   /* 原断言 e==MA_ErrNone */
int dmp_allocator_mmap_init(void){ return 0; }
```
  （两者都是 **libObj.so 导出的符号**：`dmp_allocator_lib_init @0x11BFE4`、`dmp_allocator_mmap_init @0x11C59C`；
   `dmp_allocator_lib_init` 第一步是 `osal_wai_sem_tmo(sem,-1)`，失败即断言 —— 我们直接返回 0 绕开）
- 结果（ab: h19）：
```
✅ 原断言（dmp_allocator.c:108 e==MA_ErrNone）消失
❌ 新断言：
   im.elf.real: int_cmd.cpp:169: void dmpNative::InternalCmd::alloc_sync():
     Assertion `((0x00000000) | (0x0061 << 16) | (0x003C)) == osal_get_msg_src(handle)' failed.
   → SIGABRT（ZZTGKILL sig=6）
```
- 归位：`int_cmd.cpp` / `alloc_sync` 字符串都在 **`/lib/libObj.so`（ZV-1F，inode 274）**里
  （ZV-1F 把 DMP 相关代码并进了 libObj）；`dmpNative::InternalCmd::alloc_sync` **没有导出**，不能 interpose。
- 含义：**DMP 显存请求的应答必须来自源 id `0x61003C`（子系统 0x61、本地 0x3C）**，
  而我们的环境里没有这个服务 ⇒ 客户端收到的消息 src 不匹配 ⇒ 断言。
  ⇒ 这正是 a7c2 里的 **`dmp_service_reply()`**（显存服务应答）要干的事。
- 下一步（第 28/60 轮）：
  1) 在 guest 里查 `/proc/osal/uipc`，确认 0x61 子系统/队列是否存在；
  2) 若不存在 ⇒ 需要在 backup 替代模块里**同时注册一个 0x61xxxx 的同步服务**并回合法应答；
  3) 若存在但无人应答 ⇒ 找出谁该应答（很可能就是被我们 stub 掉的 dmm/grm_ma 一类模块）。

## ★★★ 2026-10-24 15:20 新菜单线：**恢复真 backup.ko + 真 grm_ma/dmm，只 stub upm/gles/传感器 —— 启动首次走通监视器并进入 GLES 初始化**
- 按对方结论做单变量对照：
```
① 全部原厂 kmod（只 stub upm）：
   modloads=35
   Physical Addr : 200EB400 / Mon Buf Size : 00008000   ★ MonDrv::Init 成功（mmap 通了！）
   但内核崩：PC is at sugilite_open+0x28/0x1f8 [grm_gles]（真 grm_gles 访问硬件寄存器）
② 再把 grm_gles 换成我们的 fake_gles（真 backup + 真 grm_ma + 真 dmm + stub upm/accel/mag/compass）：
   modloads=35
   FAKEGLES init major=248 minor=29
   Physical Addr : 200EB400                            ★ 监视器成功
   FAKEGLES open(1) / ioctl 序列正常出现（0x80048206 / 0xc0048202 / 0x80048263 / 0xc0048211 / 0xc0048212 …）
   新内核崩：PC is at fake_ioctl+0x368/0x8ac [fake_gles]，"Illeagle Instruction at 0x5f3db5b0"
```
- 结论：
```
① 我们之前"把 backup/grm_ma 也 stub 掉"是错的 —— MonDrv 的物理地址来自真 backup 数据，
   stub 给假值 ⇒ mmap 失败 ⇒ 监视器 NULL ⇒ 后续崩溃（整条链的起点）
② 真 grm_gles 在 QEMU 里必然崩（访问 GPU 寄存器），必须用 fake_gles
③ fake_ioctl 缺 0xc004820e 等命令的处理，且某条未处理路径导致模块内非法指令
```
- 下一步（第 29/60 轮）：
  1) 给 fake_ioctl 补 0xc004820e（以及日志里出现的其余未处理命令），一律按语义返回 0/写回默认值；
  2) 复现后确认是否还有新的内核崩点；
  3) 若 GLES 序列能走完，预计会进入 dmp_allocator / DMP 服务断言（上一轮那条 0x61003C）。

## 2026-10-24 15:30 新菜单线：修好 fake_gles 后 —— **崩溃线程从 THREAD_INFRA0 前移到 THREAD_APP**
- 对 fake_gles 做了两处改动（`/root/fw/fake_gles/fake_gles.c`）：
```
① 关掉早期加的 K3 调试块（cmd==0xc0048212 时对 GPE 参数做 ioremap 探测 + 大量 printk）
   —— 这段是历史诊断代码，噪声大且会碰非 RAM 地址
② 补处理 cmd 0xc004820e（此前落到 fallback，日志里正是它之后崩的）
```
- 实测（v8 = v5 原镜像 + 真 backup/grm_ma/dmm + 我们 fake_gles + stub upm/accel/mag/compass）：
```
modloads=36            （+1）
Physical Addr : 200EB400   ★ MonDrv 正常
fake_ioctl 不再崩
★ 崩溃线程变成 THREAD_APP（之前是 THREAD_INFRA0）—— 启动链又向前推进一个线程
崩溃：address=0x083360E4, trap_no=0x0e（数据中止/翻译错误）
      [ZZREG] r0=083360E4 r4=083360E4（两者相同 → 很可能是对同一指针的读写）
      pc=0x13674B96 lr=0x1370DBA1（0x13xxxxxx 库区）
```
- 下一步（第 30/60 轮）：
  1) 把 pc/lr 解析到具体库（这次 maps dump 未覆盖到 0x13xxxxxx 段，需要放大 dump 上限或只打 r-xp 段）；
  2) 判断 0x083360E4 是"应用自己分配的缓冲（合法但未映射）"还是"坏指针"；
  3) 若是 GPU/外设地址空间，按 GRM/DMM shim 路线补映射。

## 2026-10-24 15:40 新菜单线：崩溃性质明确 —— **App 写的是 GPU 物理窗口地址，而 fake_gles 只映射了 128 KB**
- 给 fake_gles 加了 mmap 日志后实测（v9 / h24）：
```
Physical Addr : 200EB400                     ★ MonDrv 正常
FAKEGLES open(1) ctx=0 buf=0xFFDC0000 phys=0x980000 bytes=262144   （我们只给 256 KB 缓冲）
FAKEGLES mmap  va=0x09C6C000 size=0x20000   （App 只映射了 128 KB）
崩溃：address=0x085360E4, trap_no=0x0e      ← 地址在 131 MB 处，远在我们的映射之外
```
- 结合上一轮 pc/lr 解析（都落在 **appFw.so**：`off=0x1F26B96` / `0x1FBFBA1`）：
  pc 处指令是 `str.w r3,[r0],#4`（典型的批量拷贝），`r0 == r4 == 0x085360E4`
  ⇒ **App 在往一个 GPU 物理窗口地址拷贝数据，但该地址在进程页表里没有映射**。
- 根因：**fake_gles 的地址映射语义与真驱动不等价** —— 真 `grm_gles.ko` 用
  `vmalloc + vmalloc_to_page/vmalloc_to_pfn + remap_pfn_range` 把一整块 GPU 缓冲映射进调用进程，
  我们的 fake 只映射了 128 KB，而 App 按"大窗口"（≥131 MB 处）去访问。
- 下一步（第 31/60 轮）：
  1) 让 fake_gles 支持**大窗口映射**（按页用 `vmalloc_to_pfn` 逐页 `remap_pfn_range`，
     或在 init 里预留一段 DDR 作为 GPU 窗口并在 mmap 时整段映射）；
  2) 同时搞清楚 App 期望的窗口基址从哪来（下一轮 dump 它在这之前调用的 ioctl 返回值和 appFw 那段的上下文）。

## 2026-10-24 15:45 新菜单线：大窗口映射已实现（逐页 remap），但崩溃地址其实落在 **jemalloc arena 的 guard page**
- fake_gles v10 改进（`/root/fw/fake_gles/fake_gles.c`）：
```
· fake_open 改成 vmalloc(64 MB) 大窗口（非连续也可，因为 mmap 逐页映射）
· fake_mmap 改为逐页 vmalloc_to_pfn + remap_pfn_range（支持大窗口 + vm_pgoff 偏移）
实测：FAKEGLES mmapW va=0x0AB6D000 size=0x20000 off=0x0 map=0x20000  ✓ 映射成功
```
- 但崩溃依旧，且这一次拿到了"地址的邻居"（maps dump）：
```
崩溃写地址 r0=r4=0x085360E4（本次），或 0x083360E4（上次）
map 里：
   08137000-08336000 r-w ?      ← 一块 2 MB 匿名区
   08336000-08337000 --- ?      ← guard page
   08337000-08536000 r-w ?      ← 另一块 2 MB 匿名区
   08536000-08537000 --- ?      ← guard page
⇒ 崩溃地址正好是"某块 2 MB 匿名区末尾 + 0xE4"，即【写进了 guard page】
⇒ 这不是 GPU 物理窗口，而是进程内某块缓冲越界（很像"尺寸拿到的比实际映射的大"）
```
- 判断：App 拿到的"缓冲区大小"很可能来自我们 fake 的某个 ioctl（我们大量返回 0）。
  写 160 字节（r2=0xA0）落到 guard，符合"分配大小 = 0 或偏小"的形态。
- 下一步（第 29/60 轮）：
  1) 给 fake_ioctl 加**返回值日志**（cmd + 返回给用户的值），定位是哪个 cmd 的返回值被当作尺寸/地址；
  2) 用该 cmd 反查 appFw 里对应逻辑（pc 附近即 `0x1F26B96` 的调用者）；
  3) 按真实语义返回合理尺寸（例如 2 MB/4 MB）再验证。

## ★ 2026-10-24 15:50 新菜单线：**这一轮没有崩溃** —— App 存活、进入 futex/nanosleep 等待态（但屏幕仍黑）
- 改动：fake_ioctl 增加"返回给用户的值"日志（cap 200），用于查是否某 cmd 返回 0 被当尺寸。
- 实测（v11 / h26，170s）：
```
FAKEGLES ioctl 序列共 200 条（cap），典型：
  cmd=0xc0048211 w=00000001,00354aac,00000001,00060330 ret=00000000
  cmd=0xc0048212 w=00000001,00060330,00000001,087b49a8 ret=00000000
  cmd=0xc004820d w=00000000,0de41d38,00000000,0674e9a0 ret=00000001
★ 崩溃计数：0（address=/Assertion/Exception> 全部 0 次）
★ 线程状态：ZZFUTEX op=0x80/0x81（futex 等待）+ IMNANOSLEEP，反复读 /proc/self/status、/proc/1/cmdline
⇒ App 没有崩，进入了"等事件/等超时"的稳态
屏幕：640x480，非黑像素仅 75 个 ⇒ 仍无 UI 内容
```
- 判读：
```
① 之前那处 guard-page 越界写很可能是【时序/竞态相关】——本轮改了日志时序就没复现
② 现在的稳态 = App 主循环在等（很可能是等 DMM/OSD/输入事件，或等某个超时轮询）
③ 屏幕黑与"没崩"并存 ⇒ 显示服务（ViewManager）仍未建立，只是没人再踩空指针
```
- 下一步（第 29/60 轮）：
  1) 用 ZZFUTEX 的 uaddr/lr 反查等待对象（appFw 里的地址 → 哪个单例/队列）；
  2) 重跑 2 次确认"不崩"是否稳定（排除偶发）；
  3) 若稳定 ⇒ 转向"给 App 送事件/触发重绘"：OSD 队列 ACK、输入事件注入（InputService::pushFilterEvent）。

## 2026-10-24 16:00 新菜单线：崩溃是**非确定性**的（两轮不同崩溃点）—— 说明缺的是"语义"而不是"某一处"
- 同一镜像（v11）连跑三轮：
```
h26：不崩（futex/nanosleep 稳态，屏幕黑）
h27：崩在 appFw.so+0x1F26B96（写 0x085360E4 = jemalloc extent 末尾的 guard page）
h28：崩在 libc+0x602E8（写 0x6B509114，DDR 之外），lr 在 libObj.so+0x12E13B
```
- 判读：
```
① 崩溃点随机 ⇒ 不是"某一条固定缺项"，而是【内存分配/映射语义不对】
   （dmp_allocator 只被我们"跳过"，实际分配器上下文是空的 → 拿到的指针/尺寸随机不正确）
② 与对方前面的结论一致：替身驱动要"行为等价"，必须先让分配器真的工作
```
- 下一步（第 30/60 轮）：不再"跳过"分配器，而是**按语义实现**：
  1) 保留 `dmp_allocator_lib_init` 真实执行（真 backup + 真 grm_ma 已能提供 /dev/dmpgles2 与 0x61 服务），
     只去掉"返回 0"的 interposer；
  2) fake_gles 的 ioctl 按真驱动语义返回（尤其"分配/取地址/取尺寸"三类），并用 mmap 提供真实可写窗口；
  3) 每修一类，重复跑 3 次看崩溃点是否收敛。

## ★★★ 2026-10-24 16:25 新菜单线：**屏幕第一次出现内容（虽然是噪声）** —— 固定 GPU 窗口 0x08000000 是关键
- 做法：写了一个 5 行的 preload（`libfixmap.so`，`-nostdlib`），在构造期把 GPU 窗口**固定映射**到 App 假设的地址：
```c
int fd = open("/dev/dmpgles2", O_RDWR);
mmap((void*)0x08000000, 16MB, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);
```
  配合 fake_gles 把窗口降到 16 MB（64 MB 会 OOM：guest MemTotal 只有 129 MB）。
- 实测（v14 / h31，170s）：
```
FAKEGLES mmapW va=0x08000000 size=0x01000000 off=0x0 map=0x1000000   ★ 固定窗口映射成功
★ 屏幕：非黑像素 282,732 / 307,200（≈92%）—— 第一次有内容（见 docs/artifacts/2026-10-24_h31_first_nonblack.png）
   内容是【静态噪声/花屏】，说明扫描输出读到的是未初始化的帧缓存数据
崩溃：pc/lr 都在 ld-2.11.2.so（off 0x11D70 / 0x4E9F）—— 动态加载器内部
```
- 判读：
```
① "固定窗口 0x08000000" 这一步是对的：App 确实按 0x08000000 基址使用 GPU 内存
② 花屏 = 帧缓存被"读"但没被"正确写"（或写的是随机数据）
③ 崩溃点从 appFw/libc 转移到动态加载器 ⇒ 说明主流程又前进了一段
```
- 下一步（第 31/60 轮）：
  1) 查动态加载器处的崩溃（很可能是 dlopen 一个不存在的库 + 未判空，或我们的 preload 影响了搜索路径）；
  2) 让帧缓存"先被清成黑色"（在 fake_gles open/mmap 时 memset 窗口），至少把花屏变成黑屏，方便区分是否真有内容；
  3) 继续追 GPE/GRM 的绘制命令是否真的落到了帧缓存。

## ★★ 2026-10-24 16:40 新菜单线：**窗口清黑 → 屏幕变黑（证明这块窗口就是扫描源）**；崩溃地址随窗口大小线性移动
- 实验一（v15）：fake_open 里把窗口 memset 成 0
```
屏幕：非黑像素 75（全黑）—— 而 v14（未清）是 92% 噪声
⇒ 结论：我们映射的这块 GPU 窗口【就是扫描输出的来源】（噪声=未初始化 vmalloc 页面内容）
     换句话说：显示链路已经通了，只是内容还没人画
```
- 实验二（v16）：把窗口从 16 MB 提到 32 MB，崩溃地址随之移动：
```
16 MB 窗口：崩溃地址 0x096000E4 = 0x08000000 + 16MB + 0x6000E4
32 MB 窗口：崩溃地址 0x0A6000E4 = 0x08000000 + 32MB + 0x6000E4
⇒ 崩溃地址 = 窗口末尾 + 0x6000E4（6 MB + 228 字节）——【随窗口大小线性平移】
⇒ App 是"按它认为的池大小"计算地址的，而它认为的池比我们映射的多 6 MB+
```
- 判读：
```
① 现在的问题不是"映射不够大"，而是"App 认为的池尺寸与我们映射的不一致"
② 那个 6 MB 很可能来自某个 ioctl 返回值（我们大量返回 0）或某个尺寸字段
```
- 下一步（第 32/60 轮）：
  1) 逐条比对 fake_ioctl 的 cmd 与返回值，找出"应该是尺寸/偏移"的那个（重点看 0x80048206/0x80048263/0xc004820d）
  2) 若确认某 cmd 应返回 ~6 MB 级别尺寸，按真实语义返回后再看崩溃是否消失
  3) 顺便验证：把 guest 内存从 ~128 MB 提到 256/512 MB（cmdline 里有 mem=1G / mem=256M 多个值，需要确认生效值）

## ★★ 2026-10-24 16:55 新菜单线：**稀疏别名窗口（64 MB VA / 4 KB 实页）→ 不再崩、不再 OOM、屏幕有结构化内容**
- 关键约束确认：guest `MemTotal` = **129 MB**（与真机一致，不能调大——对方从真机快照确认 MemFree 只有 2.8 MB）
  - `mem=1G@0` + `memrsv=680M@0x15800000` + `memrsv=0xAEEB000@0x0A915000` ⇒ 大部分 RAM 被 carve-out
  - `dma_alloc_coherent(32MB)` 直接失败（无 CMA）⇒ 只能走 vmalloc（吃 MemFree）
  - 实测：16/32 MB 窗口能跑但崩溃地址在窗口外；48/64 MB 窗口 ⇒ OOM
- 折中方案（本轮采用）：**别名稀疏映射**
```
fake_init: __get_free_page() 取 1 页（4 KB）
fake_mmap(wid==3): 64 MB VA 区间内，每页都 remap_pfn_range 到【同一个物理页】
⇒ VA 覆盖 64 MB，实际只消耗 4 KB
```
  实测（v20 / h37）：
```
FAKEGLES alias page=6a772000
★ OOM = 0，崩溃 = 0（此前每轮必崩）
★ 屏幕：282,732 非黑像素，且出现结构化颜色 ((1,240,1) 绿 / (240,1,240) 品红) —— 因为整页被重复映射
```
- 判读：
```
① 崩溃根源确认就是"窗口覆盖不到 App 使用的地址"，与"缺服务"无关
② 别名页能让 App 跑起来，但内容会被互相覆盖 ⇒ 只能验证"不崩"，不能验证"画面正确"
③ 下一步要"真实页 + 覆盖足够大"：混合方案（前 N MB 用真实 vmalloc 页，超出部分别名）
```
- 下一步（第 33/60 轮）：
  1) 混合映射：前 16~24 MB 真实页（App 实际会读写的区域），其余别名 ⇒ 既省内存又不崩
  2) 把运行时间拉长到 200~240 s，看 App 是否进入绘制阶段（GPE 命令/帧变化）
  3) 观察屏幕是否出现"非重复"内容

## ★★★ 2026-10-24 17:15 新菜单线：**恒等映射（memmap 预留 + streamfb addr=0x08000000）打通了"显示读到我们窗口"，屏幕出现非重复内容**
- 关键改动（三处配合）：
```
① 内核 cmdline 增加 memmap=16M$0x08000000（后来 32M）⇒ 该物理段从内核可用内存中划出
② fake_gles 增加 wid==4：mmap 时把 0x08000000 起的物理页【恒等】映射进进程
③ QEMU 侧 -global driver=bionz-streamfb,property=addr,value=0x08000000
   ⇒ 显示控制器直接扫描这块窗口
```
- 实测（v23/v24）：
```
FAKEGLES mmapI va=0x08000000 size=0x01000000 off=0x0 map=0x1000000 ident=0x08000000
屏幕：非黑像素 44,617 → 86,111（去掉了之前的"整页重复噪声"，出现 (194,247,1)/(0,189,194) 这类稳定色块）
⇒ 显示路径第一次【真正读到 App 使用的窗口】
```
- 仍存在的关键矛盾：
```
App 只 mmap 了 16 MB（size=0x1000000），但崩溃地址 0x096000E4 / 0x094000E4
= 0x08000000 + 20~22 MB —— 超出它自己映射的范围
⇒ App 认为的"设备可用大小"比它自己 mmap 的更大
⇒ 这个"大小"必然来自某个 ioctl 或配置项（我们 fake 里大量返回 0 / 0x10000）
```
- 下一步（第 33/60 轮）：
  1) 找到决定"设备内存大小"的那个 ioctl（看 mmap 之前最后几个 cmd 的返回值）
  2) 按真实语义返回（例如 32/64 MB），让 App 的地址计算落在自己映射范围内
  3) 一旦地址自洽，再看屏幕内容是否变成真正的界面

## ★★★ 2026-10-24 17:30 新菜单线：**地址空间问题彻底解决** —— 256 MB VA（前 32 MB 恒等 + 其余别名）后，数据中止消失
- fake_gles wid==4 升级：mmap 覆盖 256 MB VA
```
前 IDENT_BYTES(32MB)  ：pfn = (0x08000000 + off) >> PAGE  → 恒等映射（保留区，streamfb 扫这里）
其后                  ：pfn = 同一个别名页，循环映射（几乎不耗内存）
preload 侧 mmap 请求 256 MB（FIXMAP mmap ret=8000000 ✓）
```
- 实测（v26 / h43）：
```
FAKEGLES mmapI va=0x08000000 size=0x10000000 map=0x10000000 ident=0x08000000
★ 数据中止（trap 0x0e）彻底消失 —— 之前 0x094/0x096/0x0C4 那一串地址都在映射内了
● 新崩溃变成 SIGABRT（trap 6，address=0），且日志里【没有 Assertion 文本】
屏幕：54,321 非黑像素，稳定色块（(194,247,1)/(0,189,194)）
```
- 判读：
```
① "窗口覆盖"问题已闭环：现在 App 在任何 0x08000000+offset(≤256MB) 处读写都不会缺页
② 剩下的 abort 没有断言文本 ⇒ 可能来自 abort()/__stack_chk_fail/pthread_create 失败/内部致命路径
③ 下一步就是"抓 abort 的调用者"
```
- 下一步（第 34/60 轮）：
  1) preload 里 interpose `abort`、`__assert_fail`、`__stack_chk_fail`，打印 return_address
  2) 结合 maps 把该地址定位到库/偏移
  3) 若是栈保护/堆破坏 ⇒ 说明我们伪造的显存内容让某个解析越界，需要按语义填窗口

## 2026-10-24 17:35 新菜单线：abort 抓取未命中；另发现 mmap 路径有内核侧告警
- preload 追加 `libabortshim.so`（interpose `abort/__assert_fail/__stack_chk_fail`，打印 return_address）
```
日志确认已加载：IMOPEN OK /usr/lib/libabortshim.so，maps 里有 libabortshim.so r-x
但【没有】任何 ABORT/ASSERT/STACKCHK 输出 ⇒ 致命路径不是这三个符号（可能是直接 raise(SIGABRT) 或内部 longjmp）
```
- 同时内核日志里出现 mmap 路径的告警栈（do_mmap_pgoff / sys_mmap_pgoff）——需要单独看是不是
  我们 256 MB 逐页 remap 导致的（映射页数多、耗时/锁竞争），下一轮要确认并优化（例如只 map 需要的子区间）。
- 下一步（第 34/60 轮）：
  1) interpose `raise`/`gsignal`/`pthread_kill`，直接抓 SIGABRT 的投递点
  2) 检查 mmap 路径告警是否由"256MB 逐页 remap"引起；若是则改成按需映射（惰性/分段）
  3) 若 abort 来自某库内部的致命判定，再按语义补数据

## ★★ 2026-10-24 18:00 新菜单线：加了内核级信号投递追踪（ZZSIG），并改用"一次性 remap"
- 两处改动：
```
① 内核 arch/arm/kernel/signal.c：信号投递时打印
   [ZZSIG] sig= code= pid= uid= cur= comm= pc= lr= sp=
② fake_gles wid==4 改为 **一次 remap_pfn_range**（连续保留区不需要逐页循环）
   preload 侧窗口请求改回 32 MB（与 memmap=32M$0x08000000 对齐）
```
- 实测（v31 / h48）：
```
FAKEGLES mmapI1 va=0x08000000 size=0x02000000 map=0x2000000   ✓ 一次性映射成功
[ZZSIG] sig=4(SIGILL) 出现 10 次，pc 集中在 0x0448 3b6x~7e（libc 区）
[ZZSIG] sig=6(SIGABRT) 出现 0 次（本轮）
最终崩溃：address=0x0A6000E4 trap 0x0e —— 又是 base+38MB（我们只映射 32MB）
```
- 判读：
```
① "SIGABRT 无消息"这一现象在不同配置下并不稳定；本轮出现的是 SIGILL×10 + 数据中止
② 反复出现的 0x0A6000E4 / 0x096000E4 都等于 base + 窗口大小 + ~6MB
   ⇒ App 需要的 GPU 窗口【大于 32 MB】
③ 真机 129MB 内存里要留这么大窗口 ⇒ 必须靠 memmap 预留（不占 MemFree）
```
- 下一步（第 35/60 轮）：
  1) 把 memmap 预留提到 **64M$0x08000000**，窗口同步 64MB，看是否越过 +6MB 那道坎
  2) 若 64MB 仍不够，继续 128MB（memmap 预留不占 MemFree，但要确认内核能正常起来）
  3) 同时保留 ZZSIG 追踪，确认 SIGILL 是否与"窗口内容非法"相关

## 2026-10-24 18:10 新菜单线：窗口大小与崩溃地址的定量关系确认；userspace 惰性映射尝试未生效
- 定量实验（同一镜像只改 memmap/窗口大小）：
```
16MB 窗口 → 崩溃 0x096000E4（= base + 16MB + 0x6000E4）
32MB 窗口 → 崩溃 0x0A6000E4（= base + 32MB + 0x6000E4）
64MB 窗口 → 崩溃 0x0C6000E4（= base + 64MB + 0x6000E4）
⇒ 崩溃地址恒等于【窗口末尾 + 6MB + 0xE4】
⇒ App 是"按它认为的窗口大小"计算下一个缓冲区地址，而它的计算总比映射多 6MB
```
- 尝试 1：preload 里装 SIGSEGV handler，对 GPU 区间内的故障地址按需 mmap 一页
```
fact: 未生效（没有 FIXMAP fault/unfix 日志），最终崩溃 address=0xC0
reason: 我们裸写的 sigaction 结构与 glibc 2.11 实际布局不符（已按 glibc 布局修正仍无效，可能信号被屏蔽或 handler 未装上）
```
- 结论：
```
① 崩溃不是"地址空间不够"，而是"App 认为的池大小 = 映射大小 + 6MB"
② 只要还按"扩大映射"的思路走，崩溃点会一直跟着平移（已用 16/32/64MB 三次验证）
③ 真正要改的是"App 从哪里得到池大小"——大概率是某个 ioctl/参数返回的 size
```
- 下一步（第 36/60 轮）：
  1) 回到 ioctl：把 mmap 前后所有 cmd 的"输入参数 + 返回给用户的值"按顺序打全，
     找哪个值等于/接近"窗口大小 + 6MB"
  2) 若找到，按真机语义修正该返回值，让 App 的池大小与我们映射一致
  3) 放弃"无限加大窗口"和"userspace 惰性映射"这两条路（前者平移崩溃、后者 ABI 风险高）

## 2026-10-24 18:25 新菜单线：确认"没有第二次大 mmap"；接受内存布局更正
- 实测（v35 / h52）：
```
FAKEGLES mmapI1 va=0x08000000 size=0x4000000 off=0x0 map=0x4000000     ← 我们的 preload（64MB）
FAKEGLES mmapI1 va=0x0D84D000 size=0x20000 off=0x0 map=0x20000        ← App 自己的小映射（128KB）
mmapI2 / mmapI3 计数 = 0
⇒ 没有"第二次大映射"，崩点 base+末+6MB 不是"第二块映射缺失"造成的
```
- 同时接受两条更正（来自移植侧 / 反汇编侧核算）：
```
① 真机 MemTotal 就是 129MB（声明 1280MB − 保留 1139.5MB），MemFree 仅 2.8MB
② 崩点是【用户态 VA 未映射】，不是物理地址不存在（DDR0 = 1GB）
③ 真机 bank0 可用区：0x00100000-0x00F00000 / 0x00F08000-0x02E68000 / 0x048C6000-0x0A915000
   ⇒ 0x08000000 一带在真机是【普通可用 RAM】，不是保留区
   ⇒ 我们的 memmap 预留只是"QEMU 侧给 App 专用窗口"的手段，不对应真机布局（这点要写清楚，避免误导）
```
- 下一步（第 37/60 轮）：
  1) 试"物理预留 256MB + 实际映射 256MB，但向 App 报告的 size 仍按请求值"
     （若崩点仍是"末+6MB"，则说明 App 自己按映射大小推算，需要改成"映射 > 它读到的值"）
  2) 若仍不行，考虑在 preload 里 interpose `/proc/self/maps` 读取，
     让 App 看到"比实际映射小"的区间（技术可行但要谨慎）
  3) 让 streamfb 的 scanout 范围 clamp 在实际映射区内，避免把"扫描越界"误判成 App 越界

## 2026-10-24 18:35 新菜单线：raw-syscall 版按需映射（SIGSEGV）尝试失败，回退到 v35 基线
- 尝试：用 raw `rt_sigaction`(174) + 内核 struct sigaction 布局装 SIGSEGV/SIGBUS handler，
  故障时对 GPU 区间内的地址按需 `mmap(MAP_FIXED|MAP_ANON)` 一页
```
结果：FIXMAP2 mmap ret=8000000（窗口映射成功）
      但一次 "FIXMAP fix" 都没有；最终异常 dump 异常（address=0、trap=0、error_code=0x5efffdd0）
      ⇒ handler 多半没装上或执行路径破坏了栈
```
- 决定：**停止这条路线**（ABI/信号语义风险高、收益不确定），回到 v35 基线（32/64MB 恒等窗口 + 单次 remap）。
- 当前可复现基线（新菜单）：
```
镜像 /mnt/build/tmp/hb/v32.img / v35.img
  · 真 backup/grm_ma/dmm + fake_gles(恒等窗口) + stub upm/accel/mag/compass
  · cmdline: memmap=64M$0x08000000（run_resv64.sh）
  · QEMU: -global driver=bionz-streamfb,property=addr,value=0x08000000
状态：屏幕能出内容（稳定色块），崩溃点固定为 "窗口末 + 6MB"，尚未收敛
```
- 待办（下一轮起）：
  1) 判断 App 是否通过 `/proc/self/maps` 读取窗口大小（可在 preload 里 interpose open/read 观察）
  2) 若是，则"映射 > 可见"策略：对 maps 做重写，让 App 看到更小的区间
  3) 若否，则回到 ioctl 返回值排查

## 2026-10-24 18:40 新菜单线：确认 App **不读** /proc/self/maps；崩溃更像"堆/缓冲 OOB"而非窗口
- 检查（多份日志）：
```
/proc/self/maps 打开次数 = 0     ← App 不通过 maps 获取窗口大小
/proc/self/status = 2 次
⇒ "App 按 maps 推算窗口大小" 这个假设【不成立】
```
- 对照 ioctl 序列（崩溃前最后 12 条，全部是 GPE 提交循环的重复）：
```
0xC004820D ret=1  →  0xC0048211 ret=0  →  0xC0048212 ret=0   （约每轮 3 条，反复）
w[] 里 0x0DA6BD38 之类是用户态指针（命令缓冲），不是窗口地址
```
- 修正后的判读：
```
① 崩溃地址 = 窗口末 + 6MB 这个"规律"，可能只是因为【堆紧挨着我们的映射之后增长】，
   而不是 App 在计算窗口大小
② 早期在 0x083360E4 那次（jemalloc guard page）也是同一性质：堆越界
③ 因此根因更可能是"某个 size 得到 0/偏小 ⇒ 分配偏小 ⇒ 越界写"
   —— 与 fake_ioctl 大量返回 0 的行为吻合
```
- 下一步（第 37/60 轮）：
  1) 逐个 cmd 建立"语义表"：把 w[0..3] 与返回值配对，找出应当是 size 的那一个
     （重点：0xC004820D 返回 1、0xC0048211/0xC0048212 返回 0，是否应为长度/地址）
  2) 给这些 cmd 返回合理值后再看崩溃是否消失
  3) 若仍崩，用内核异常钩子把"崩溃前 16 条 ioctl"和"崩溃时 r0/r4"一起关联分析

## ★★★ 2026-10-24 18:50 新菜单线：按真驱动契约重写 fake_gles —— **App 只做了一次 128KB mmap（证实契约模型）**
- 两个协作会话共同确认（基于 `grm_gles.ko` 反汇编）：
```
① 寄存器窗口：ioremap 固定物理 0xF1000000，16 KB
② 环缓冲：单块 vmalloc(128KB)，驱动 init 时只分配一次
③ 128KB 路径要求 vm_pgoff == 0；其它尺寸（0x200/0x2000000）真机直接 BUG()
⇒ 每次 128KB mmap 是【同一块环的别名】，不是相邻切片
⇒ 大窗口（16/32/64MB）是调试构造，不是真机行为
```
- 本轮按契约重写 fake_gles：
```
fake_open  : vmalloc(128 KB) 环缓冲，page 映射用
fake_mmap  : 0x4000  -> remap 固定 pfn 0xF1000（物理 0xF1000000）
             0x20000 -> 要求 pgoff==0，逐页映射同一块环
             其它    -> 返回 -EINVAL（真机是 BUG()）
preload    : 取消大窗口 MAP_FIXED（不再人为制造 0x08000000 窗口）
```
- 实测（v37 / h54）：
```
FAKEGLES open(1) ctx=0 ring=0xB22A6000 bytes=131072
FAKEGLES mmap seq=1 va=0x0C000000 size=0x20000 pgoff=0x0     ★ App 只做了一次 128KB mmap！
⇒ 证实"App 只使用 128KB 命令环"的契约模型
随后：SIGABRT（trap 6, address=0），屏幕黑
```
- 结论与方向（与两个会话一致）：
```
① 大窗口能出画面 ≠ 正确：App 在真机上从不直接写显存
② 正确终点 = 用户态 GPE 层软件绘制（a7c2 的 software_gpe）：
   GRM_gpermFillrect(314B) / GRM_gpermRectblit(824B) 等 21 个 GRM_* 导出
③ 下一步：在 shim 里用 CPU 实现 GRM_gperm*，再由 GRM_screenGetOnBitmap 出帧
```

## ★ 2026-10-24 19:05 新菜单线：SIGILL 之谜解开 —— 是 OpenSSL 的 CPU 特性探测（正常行为）
- 之前日志里反复出现的 `[ZZSIG] sig=4` 一直以为异常，本轮定位到具体指令：
```
从 hybrid /usr/lib 提取 libcrypto.so.3（2,656,568 B），按崩溃 pc 反汇编：
  0x12BB5C  nop.w
  0x12BB60  vorr q0, q0, q0        ← NEON 探测
  0x12BB66  mrrc p15/... cr14      ← 特性探测
  0x12BB6C  aese.8 q0, q0, q0      ← ARMv8 crypto 探测
  0x12BB72  sha1c.32 q0, q0, q0    ← crypto 探测
⇒ 这是 OpenSSL 3 的 ARM capability 探测：故意执行不支持指令、靠 SIGILL 捕获来关特性
⇒ SIGILL ×10 属【正常】，不是 bug
```
- 也验证了 CPU 模型的影响：
```
-cpu cortex-a15：内核起不来（卡在 booting the kernel）
-cpu cortex-a9 ：正常启动（35 模块），SIGILL 仍是 10 次（探测），主崩溃仍在
⇒ 换 CPU 模型不解决主问题
```
- 下一步（第 38/60 轮）：
  1) 主崩溃仍是"先于任何 GRM_gperm*/bitmap* 调用就 SIGABRT"，需要继续定位它的来源
     （已排除 abort/assert/stack_chk/raise/gsignal/pthread_kill 等 interpose，说明是 libc 内部路径）
  2) 可用手段：内核里在 `die_kernel`/`__do_user_fault` 与信号投递处打印更完整的 siginfo（修正字段偏移）
  3) 一旦能到绘制阶段，软件 GPE shim 已按最新规格写好（format→bpp 表 / stride geo+0x18 / 不碰锁）

## ★★★ 2026-10-24 19:15 战略级更正（来自协作会话）：**ZV-1F 新菜单不调用 `GRM_gperm*`**，a7c2 路线不适用于新菜单
- 穷尽验证（五路独立证据）：
```
libObj 内 GRM_gpermFillrect@plt / Rectblit@plt 的调用点 = 0
197 个 /usr/lib/*.so 里当 UND 导入的 = 0
数据段函数指针 = 0
其它库 dlsym 字符串 = 0
im.elf / bootin.elf 引用 = 0
（对照组：GRM_bitmapWriteLockBits@plt 同样方法数出 4 个调用点 ⇒ 方法有效）
```
- 架构差异（关键）：
```
a7c2（A7C II）   ：绘制走 mpgpe::GpermFillRect         ← 我们照抄 gpe-raster.c/gpe-sprite.c 的这条路
ZV-1F 新菜单     ：绘制走 ux::gfxeng / wgtsys（appFw 导入 120 个 ux::gfxeng 符号）
⇒ GRM_gperm* 在 ZV-1F 里是"导出但不被调用"的死接口
⇒ 之前那条"软件 GPE"路线对新菜单【不成立】
```
- appFw 实际导入的 GRM 符号只有 5 个：
```
GRM_bitmapCreateFromPhys / GRM_bitmapRelease / GRM_fbrmCheckUpdateFinished
GRM_fbrmRegisterTriggerFrameworkCb / GRM_fbrmSetScreenPreference
```
- 我方实测印证：
```
h60（v41，含 libswgpe）跑完：SWGPE 日志 = 0
⇒ gperm/bitmap 系列一次都没被调用，App 在更早处就崩了
```
- 下一步（第 38/60 轮）：
  1) 停止 gperm 相关实现（保留已经正确的三项：CheckUpdateFinished→0、CreateFromPhys 的 phys 映射、bpp/stride 结论）
  2) 研究真正的入口：`ux::gfxeng::GraphicsEngine::drawFramebuffer`（libObj 0x0E565A, 250B）
  3) 优先解决"任何图形调用之前就 SIGABRT"的问题 —— 这是当前唯一的主阻塞

## 2026-10-24 19:20 新菜单线：CPU 模型穷举结果（只有 cortex-a5/a9 能起内核）
- 测试（同一镜像 v41，只改 `-cpu`）：
```
cortex-a5（默认） : 内核启动 → 35 模块 → 主崩溃（trap 6）
cortex-a9        : 内核启动 → 35 模块 → 主崩溃（trap 6），SIGILL 探测仍 10 次
cortex-a15       : 内核卡在 "booting the kernel"，起不来
cortex-a7        : 同样卡在 "booting the kernel"
⇒ 能起内核的只有 a5/a9；换 CPU 不能解决主崩溃
⇒ 说明"非法指令/abort"不是缺 NEON/crypto 特性那么简单（a9 有 NEON 也一样崩）
```
- 当前结论（三方一致 + 我方实测）：
```
① 主阻塞 = "App 在任何图形 API（gperm/bitmap/fbrm）调用之前就 trap 6"
② 该 trap 6 不走 signal 投递路径（ZZSIG 里没有 sig=6），是 CPU 直接执行非法指令后被异常钩子接住
③ 与 OpenSSL 的 CPU 探测是两回事（探测的那 10 次 sig=4 都被正常捕获）
```
- 下一步（第 38/60 轮）：
  1) 在异常钩子里把**非法指令的原始 32 位编码**打出来（`*(u32*)pc`），直接看是哪条指令
  2) 同时打印该 pc 所属区间名（ZZMOVMAP 已有）
  3) 依据指令类型判断：是 ARMv8 crypto / NEON / 还是"跳到了数据区"

## ★★★ 2026-10-24 19:45 新菜单线：修掉 fake_gles 内核越界 + 定位到"160 字节结构拷贝写飞"
- **修复 1（内核侧 OOB，这是真 bug）**：
```
崩溃现场：PC is at fake_ioctl+0x2d0/0x520 [fake_gles]
          r8=0xB22AB000（我们的 128KB 环）r6=0x8000 insn=e7982106 (ldr r2,[r8,r6,lsl#2])
根因：cmd 0xc0048212 的调试扫描写成 `for (o=0; o<0x40000/4; o++) b[o]`
      ⇒ 扫描 256KB，而环只有 128KB ⇒ 读到环外 0x20000 处 ⇒ 内核数据中止
修复：上限改为 WIN_BYTES/4，并降低打印条数
```
  ⇒ 修好后内核侧不再崩（`PC is at fake_*` 归零）。
- **修复后的现象（稳定可复现）**：
```
用户态数据中止：address=0x083570E4 / 0x085570E4 / 0x0A6000E4（每次都 0x…E4 结尾）
[ZZINSN] pc=appFw+0x1F26B96  insn=3b04f840 = str.w r3,[r0],#4
        该函数体：push{...} / ldr r3,[r1] / mov r1,r2 / movs r2,#0xA0 / str.w r3,[r0],#4 / blx memcpy / pop
⇒ 是"把 164 字节结构拷到 r0"的例程，r0 指向未映射页
```
- **并行做的静态/运行时探针（QEMU 在后台跑，同时做分析）**：
```
① objdump 复核 gperm 调用点：libObj 内 blx GRM_gpermRectblit@plt ×5 + GRM_gpermFillrect@plt ×1（确凿）
   GRM_bitmapCreateFromPhys@plt 调用点 1 处（0x229DA6）
② preload 探针记录 open/mmap：
   /dev/mem 打开成功（fd=5），mmap off=0x20098000 len=0x7C000 成功 ⇒ libObj 的物理映射通路是好的
   本轮没有打开 /dev/dmpgles2（说明这次没走到 GPU 设备打开）
③ 我们的 GPU shim（lock/unlock 中性化 + gperm CPU 填充）一次都没被调用
   ⇒ 崩溃发生在**任何图形 API 之前**
```
- 下一步（第 39/60 轮）：
  1) 追"那个 164 字节结构写入的目标 r0 从哪来"：
     已定位调用者 lr = appFw+0x1FBFBA1
  2) 用 preload interpose malloc/free，记录分配尺寸，看是否"分配 N 但写 N+0xA4"
  3) 或用 `GRM_bitmapCreateFromPhys` 的 phys 值反推（若该 phys 落在 0x08xxxxxx，说明就是 GPU 缓冲）

## ★★ 2026-10-24 20:35 新菜单线：崩溃写入点 = "大结构基址 + 固定偏移"，malloc 拦截无效（jemalloc 内部绑定）
- 静态分析（appFw.so，.text vaddr == file offset）：
```
崩溃函数 0x1F26B8A：push / mov r4,r0 / ldr r3,[r1] / movs r2,#0xA0 / str.w r3,[r0],#4 / blx memcpy
调用点   0x1FBFB62 / 0x1FBFB70：bl 0x1F26BA2
调用者算目标的方式： r0 = r7 + 常量
  常量取值：0x0E3310 / 0x0E33B4 / 0x02DF464 / 0x0E3644
⇒ 崩溃写入地址 = r7（某个大结构/资源基址）+ 0xE3310 附近
⇒ 反推 r7 ≈ 0x083570E4 - 0x0E3310 ≈ 0x07493DD4
⇒ 即"基址 + 约 930KB 的偏移"落到了未映射页 ⇒ 该基址所指向的分配/映射比预期小
```
- 运行时探针（preload interpose `malloc/calloc/realloc`，记录前 400 次）：
```
MALLOC 日志 = 0 条
⇒ im.elf 用的是 jemalloc（libjemalloc_ds.so.1），内部直接绑定，LD_PRELOAD 拦不到
⇒ 改用「读 /proc/self/maps 找 r7 所在区间的大小」或「hook mmap」来确认分配尺寸
```
- 下一步（第 38/60 轮）：
  1) 在崩溃异常钩子里，用 r7（或反推值）在 ZZMOVMAP 输出里定位所属区间，打印其 **start-end 大小**
     ⇒ 若大小 < 0xE3310+0xA4，即可确认"结构基址分配过小"
  2) 追这个基址从哪来：大概率是"资源文件 mmap"（view*.uxc / .bin），
     需比对 ZV-1F 资源文件的实际大小与 appFw 期望的偏移表
  3) 若确认是资源尺寸不符，优先修正混合镜像里的资源文件版本

## ★★★ 2026-10-24 20:45 新菜单线：**崩溃写入 = 越过一个 0x1FF000 映射的末尾仅 ~0x170 字节**（越界很小、很具体）
- 新增内核探针 `ZZR7`（在异常 dump 里查 r7 属于哪个 vma 及其大小）实测：
```
[ZZREG] r0=085570E4 ... r7=08473A88
[ZZR7]  r7=08473A88 in 08358000-08557000 size=001ff000 ?        ← 匿名区，约 2MB−4KB
崩溃写入地址 = 0x085570E4 = vma 末尾(0x08557000) + 0xE4
可用余量（r7 → vma 末尾）= 0xE3578；本次写入需要的偏移 ≈ 0xE3644 + 0xA4
⇒ 只差 ≈ 0x170（约 368 字节）
```
- 判读：
```
① 这不是"随机野指针"，而是【刚好越过映射末尾一点点】
② 映射大小 0x1FF000 = 2MB − 4KB  ⇒ 典型的 chunk 末尾留了一页
   （jemalloc 的 huge extent 会留 guard page；我们的 mmap 探针也确实抓到大量 len=0x200000 匿名映射）
③ App 在这个对象里写到 ~0x1FF0CC 处 ⇒ 比"可写区"多写约 0x170 字节
⇒ 极可能是【结构尺寸/元素个数不匹配】：分配按 N 个元素算，循环按 N+少许 个写
   （混合镜像 = ZV-1F appFw + 部分 ZV-E10 资源/配置，最可能就是这种"尺寸约定"不一致）
```
- 下一步（第 39/60 轮）：
  1) 对比同一次运行里 mmap len=0x200000 的具体返回地址与 r7 的关系，确认 r7 是否正好在该 chunk 起始+0x11BA88
  2) 追 r7 的分配来源：谁创建了这个 ~2MB 对象（大概率是某种 "resource/model 数组"）
  3) 若是"元素个数来自资源配置" ⇒ 直接看混合镜像里对应配置文件（ZV-1F vs ZV-E10 版本的差异）

## 2026-10-24 20:55 新菜单线：确认该对象**不是** operator new 分配的（jemalloc 内建，拦截看不到尺寸）
- 新增 preload `libmlog2.so`：hook `_Znwj` / `_Znaj`（≥64KB 才记）
```
NEWO ptr=0x5E02008 size=0x5A4700   （5.6MB，另一处大分配）
NEWA ptr=0x10BDF008 size=0x20000
⇒ 崩溃涉及的 ~0xE3578 对象【没有】出现在 operator new 日志里
⇒ 说明它来自 jemalloc 内部（im.elf 静态链入 jemalloc，malloc/new 内部直连，LD_PRELOAD 拦不到）
```
- 结合上一轮的硬指标：
```
vma 0x08358000-0x08557000（0x1FF000 = jemalloc chunk）
r7 = chunk+0x11BA88，可写余量 0xE3578
App 写到 r7+0xE36xx ⇒ 比余量多 ~0x170（368 字节）
⇒ 极可能是【少算一个元素/一个字段】导致的越界，且很可能来自"资源表元素个数"与"分配大小"不一致
```
- 下一步（第 40/60 轮）：
  1) 请离线反汇编侧解 appFw 里 0x1FBFBxx 所在函数：它遍历的是哪张表、元素大小多少、个数从哪读
  2) 我方并行：用内核 `ZZR7` 增强版（同时打印 r6/r8/r9）观察那次写入循环的步长，
     反推元素大小，再对照混合镜像里资源文件的条目数

## ★★★ 2026-10-24 21:00 新菜单线：**绕过 jemalloc guard page 成功 → App 越过那 368 字节越界，进入 DMM 分配路径**（又一个硬件模块崩点）
- 手法（两条 preload，均 `-nostdlib`）：
```
libmmapad.so : 匿名 mmap 长度额外 +0x2000（给越界留余量）
libmprotsh.so: interpose mprotect，忽略 PROT_NONE（jemalloc 的 guard page 不再生效）
```
- 实测（v50 / h73）：
```
MPROT skip PROT_NONE ×30（guard page 全部放开）
崩溃从"appFw 里 164 字节记录拷贝越界"消失
★ 新崩点：PC is at dmm_ctrl_Main+0x140/0x278 [dmm]
   insn=e5943008 (ldr r3,[r4,#8])，r4=0x2FCFD048 ⇒ 内核模块 dmm 拿到的指针非法
```
- 判读：
```
① 我们的"越界 368 字节"确实只是**症状**：放开 guard page 后 App 继续走了
② 现在卡在**真的 dmm.ko**（我们一直保留真模块）—— 它收到非法指针
③ 最可能来源：fake_gles 的 ioctl 对未实现命令返回 0，上层拿 0 当句柄/地址传给 dmm
   ⇒ 应改为"未实现命令返回 -ENOSYS/-EINVAL"，让上层走错误分支而不是带着 0 继续
```
- 下一步（第 41/60 轮）：
  1) fake_ioctl 未实现命令统一返回 `-EINVAL`（而不是 0），重跑看 dmm 是否还崩
  2) 若仍崩，打印 dmm 崩溃前的 ioctl 序列（我们已有 ioctl 日志），定位是哪个 cmd 传了 0x2FCFD048
  3) 同时保留 mmapad+mprotsh（它们让我们越过了非硬件性障碍，且对真机无害——只在 QEMU 测试用）

## 2026-10-24 21:10 新菜单线：fake ioctl 改 -EINVAL + guard 绕过 → 崩溃点再次前移（现在 address=1）
- 改动：
```
fake_ioctl：所有未实现命令统一返回 -EINVAL（此前返回 0，会把 0 当句柄传下去）
镜像 v51 = v50 + 新 fake_gles
```
- 实测（v51 / h74）：
```
crash=1  但【没有内核侧崩溃】（无 PC is at）
用户态：address=0x1, trap=0x0e
[ZZINSN] pc=0x040960B4 (libc/pthread 区)  insn=b9816821
[ZZREG]  r4=1 r5=1 r6=0 r7=0 r8=0x08394D40 r9=0 r10=0
ZZR7: r7=0（与上一轮"大结构基址"完全不同）
⇒ dmm_ctrl_Main 的内核崩点消失了（-EINVAL 生效，上层走了错误分支）
⇒ 现在是新的用户态"把 1 当指针用"的崩溃，位置在 libc/pthread 区
```
- 判读：我们已连续越过三类障碍（堆越界 → 内核 dmm 崩溃 → 现在的 libc 区空指针），
  每一类都更靠后；说明"打通方向"是对的，只是每层各有各的问题。
- 下一步（第 42/60 轮）：
  1) 用 ZZMOVMAP 把 pc=0x040960B4 落到具体库/函数（libc? pthread? 还是别的库的 0x4096 段）
  2) 检查 r4/r5=1 的来源（很可能是某个返回值被当指针，例如我们 fake 返回 -EINVAL 后上层未处理）
  3) 若确认是"错误分支未处理"，则对那个 cmd 返回一个合法值而不是 -EINVAL

## ★★★ 2026-10-24 21:20 新菜单线：**连续 190s 零崩溃**（第一版"能稳定跑"的配置），但 App 停在主循环不绘制
- 本轮唯一改动：fake_ioctl 的 `0xc004820D`（wait-for-GPE-event）返回值 **1 → 0**
```
上一轮：address=1 崩溃（libosal_uipc.so+0x50B4，do_sleep_tmo 里 ldr r1,[r4,#0]，r4=1）
本轮  ：crash = 0（无 data abort、无内核 oops、无 SIGABRT）
```
- 现配置（v52）：
```
kmod：真 backup/grm_ma/dmm + fake_gles（忠实 128KB 环契约，ioctl 未实现→-EINVAL，事件值 0）
      + stub upm/accel/mag/compass
preload：libmmapad.so（匿名 mmap +0x2000）+ libmprotsh.so（忽略 PROT_NONE）
内核：含 ZZREG/ZZMOVMAP/ZZR7/ZZSIG/ZZTGKILL/ZZINSN 的全套调试钩子
```
- 运行状态（190s）：
```
modloads=36
FAKEGLES GLESKICK ×4（App 有在往环里提交命令）
ZZFUTEX ×44 / IMNANOSLEEP ×7  ⇒ App 主循环在跑、在等待
★ UXIMG / GPUSHIM 日志 = 0   ⇒ 没有调用 GraphicsImage/gperm 绘制接口
屏幕：全黑（非黑像素 75）
```
- 判读（下一步方向）：
```
① 现在不是"崩"，而是"App 起来了但没走绘制分支"
② 可能卡在：
   a) 等 /dev/stream 的 vsync/事件（我们机器模型的 streamfb 可能不发中断）
   b) 等 GPU 完成信号（环里没人消费 ⇒ 需要 CheckUpdateFinished / 事件应答）
   c) 还在资源加载/配置阶段
③ 下一轮先看：/dev/stream 的 open/ioctl/read 行为（probe 已有能力），
   以及 streamfb 模型是否有 vblank/中断可配
```

## 2026-10-24 21:25 新菜单线：零崩溃状态下 App 的等待点（futex 在 im.elf 自身数据段）
- 解析 h75 的 ZZFUTEX：
```
uaddr=0x0002A3A0 / 0x0002A374  ← 都落在 im.elf 自己的 data/bss（0x16000-0x37000 区）
lr = 0x040B3D8D / 0x040B4D0D   ← libpthread 区（推断 pthread_cond_wait / mutex）
⇒ App 的线程在等【自己内部的 condvar/mutex】，不是等我们某个设备
```
- 结合 GLESKICK 只有 4 次 ⇒ App 主循环跑完初始化后就进入"事件等待"，
  典型 UI 框架在"无输入/无重绘请求"时的状态。
- 判读与下一步（第 44/60 轮）：
```
① 现在缺的是"触发一次绘制"的事件源（触摸/按键/定时器/VSYNC）
② 可行路径：
   a) 注入输入事件（libObj 的 InputService::pushFilterEvent）—— 目标里就要求"尽量可交互"
   b) 让 /dev/stream 侧产生 VSYNC（streamfb 模型目前是纯被动镜像，不发中断）
   c) 检查 App 是否有定时器线程（nanosleep ×7 说明有，可能只是低频心跳）
③ 优先试 a：注入一次触摸事件，看是否触发一帧绘制（GPUSHIM/UXIMG 日志会出现）
```

## 2026-10-24 21:35 新菜单线：输入注入路径未触达；v53 组合出现新的内核侧故障（时序相关）
- 本轮尝试：interpose `InfraInput::SetCallback`（appFw 确实导入它）→ 捕获回调 → 8 秒后合成触摸事件
```
实测（v53 = v52 + libinputinj + libgpushim）：
  INPUTINJ 日志 = 0 条 ⇒ SetCallback 从未被调用
  GPUSHIM  日志 = 0 条
  modloads=34（比 v52 少 2）
  ★ 出现内核侧故障 2 条：
     "Unable to handle kernel NULL pointer dereference at virtual address 00000630"
     "Unable to handle kernel paging request at virtual address 53C972A8"
```
- 判读：
```
① App 还没走到"注册输入回调"这一步 ⇒ 输入注入现在做也没用，必须先把 App 推进到 UI 事件循环
② v53 与 v52 只差两个 preload，但内核侧出现故障 ⇒ 说明当前"零崩溃"对时序/组合敏感，
   不是完全稳定（v52 的 190 秒零崩溃可能是窗口性的）
③ 那两条 "Unable to handle" 没带完整 oops（em console 吞了），
   下一轮需要把 oops 全文捞出来（PC is at / Modules linked in）
```
- 下一步（第 45/60 轮）：
  1) 复跑 v52 与 v53 各一次，确认"零崩溃"是否可复现（排除偶然）
  2) 捞完整 oops（改内核 em 输出或看 /proc/kmsg）
  3) 在确认稳定的配置上，找出 App 卡在 UI 初始化哪一步（IMOPEN 序列 + 线程状态）

## ★★★ 2026-10-24 21:45 新菜单线：找到"App 为什么空转"的结构性原因 —— **混合镜像里 bootin.elf 失败退出，sen.elf 从未启动**
- 对比两边的启动链（EXECVE 序列）：
```
老菜单基线（ab_v603，E10 /usr）：
  bootin.elf → sen.elf → launch_shell.elf → net_cmdd.elf → ash → im.elf
新菜单混合镜像（v52/v53/v54，ZV-1F /usr）：
  bootin.elf → (无 sen.elf) → launch_shell.elf → net_cmdd.elf → ash → im.elf
  且日志里 bootin 之后紧跟 "EXIT init pid=... code=65280"（=255，失败）
```
- 已验证不是"文件缺失"：
```
/bin/sen.elf 存在（19,208 B）
/lib/libbackup.so 存在（9,384 B，与 E10 原厂一致）
/bin/bootin.elf 换成 E10 原版后【仍然】不启动 sen.elf（排除版本问题）
```
- 判读：
```
① 混合镜像里 bootin 静默失败（退出 255），sen.elf 从未运行
② 于是 im.elf 是由 ash 兜底启动的（缺 sen.elf 提供的环境/参数/服务）
   ⇒ 这很可能正是 App"起来了但不进 UI 初始化、不打开 /dev/stream、不绘制"的根因
③ 之前所有"App 空转"的观察，都可能只是这个兜底启动路径的后果
```
- 下一步（第 47/60 轮）：
  1) 用 ltrace 式探针（preload 拦 open/openat/access/stat）记录 bootin 在失败前最后访问的路径
     —— 直接看它卡在/缺什么（比猜快）
  2) 若确认某个 /setting 或 /system 资源缺失，补进镜像
  3) 一旦 sen.elf 能跑起来，再评估 App 是否进入正常 UI 初始化

## ★★★ 2026-10-24 21:55 重大自查：**最近几轮"零崩溃"其实是 appFw 根本没加载**（用户质疑完全正确）
- 证据（同一条判据：日志里 appFw 出现次数 / maps 里的 appFw vma）：
```
v35(v43 基底) h69 : appFw=14 处、maps 里 5 个 vma、crash=1      ← appFw 真的加载并运行了
v52/v53 h75/h77/h79: appFw=0 处、无 vma、crash=0、kicks=4      ← appFw 从未加载
隔离实验 h80（v55 = v43 + 新 fake_gles，无 mmapad/mprotsh）：appFw=0、crash=0
⇒ 结论：是【fake_gles 改成"未实现 ioctl 返回 -EINVAL"】导致 App 走了错误分支，
        此后根本不再加载 appFw.so
```
- 含义（必须记下的教训）：
```
① 前几轮"零崩溃/稳定 190s"是假象：UI 框架没起来，自然不崩
② 之前追的 appFw 内部崩溃（0x1F26B96 越界、guard page、dmm 崩溃）才是"程序真的在跑"的证据
③ 判断"有没有前进"必须用【appFw 是否加载 + 是否调用绘制接口】为判据，
   不能用"有没有崩"当判据
```
- 下一步（第 48/60 轮）：
  1) 回退 fake_gles 的 ioctl 语义到"未实现返回 0"（让 appFw 能加载），
     改用【mmapad + mprotsh】来吸收越界（h73 已证明能越过那 368 字节）
  2) 在"appFw 已加载"的前提下，逐个处理 dmm 崩溃（h73 那条 dmm_ctrl_Main+0x140）
  3) 建立固定判据：每轮记录 appFw 是否加载、绘制接口是否被调用、屏幕是否有内容

## ★★ 2026-10-24 22:05 切回老菜单（ZV-E10 原 /usr）：基线复现 + 定位到"加载完 CautionConfig 后进入空闲"
- 基线复跑（ab_v603 / o1）：
```
modloads=35、sen.elf 启动 ✅、im.elf 启动 ✅、crash=0、INITCFG_ENTER=1 / EXIT=0
屏幕：仍是 0x20312000 的旧噪声（非黑 282732）——未绘制
```
- `INITCFG_ENTER` 之后实际发生的事（日志时间顺序）：
```
1) NEW2 sz=24 tid=186                       （分配配置对象）
2) MISSSO //lib/viewUnified2.so → ZZREAD dev=viewUnified2.so
   QMMAP viewUnified2.so 0xE4FB10 字节（r-x）
   QMMAP 偏移 0xB78000 处 0x228000（数据段）
3) MISSSO //lib/CautionConfig.so → ZZREAD dev=CautionConfig.so
   QMMAP CautionConfig.so 0x12098D8 字节 + 数据段 + 一块 anon
4) 之后：只剩 IMNANOSLEEP / ZZFUTEX，无新 open/read
⇒ initializeConfig 在加载完这两个模块后【不再有 I/O】，进入 condvar 等待
```
- 尝试与结论：
```
· 试过 interpose libc dlsym 来抓 initializeViewConfig/initializeModelConfig 的调用 —— 无效
  （DLSYM 日志 0 条；libcfgtrace.so 确认已加载）
⇒ 因为 viewUnified2/CautionConfig 是【im.elf 自带 OnDemandLoader 手动加载】的，
  符号解析不走 libc dlsym ⇒ 必须换挂钩点
```
- 下一步（第 47/60 轮）：
  1) 挂钩 `libOnDemandLoader.so` 的导出（`OnDemandLoaderInitialize` / `odl_init` / `odl_res`…），
     在它"加载完一个模块并返回入口"时打点，确认它把哪个入口交给了 initializeConfig
  2) 或者直接在 viewUnified2.so 的 `initializeViewConfig`(0x67D740) 入口做 **inline hook**
     （函数已导出且是尾调，改动风险可控）
  3) 若确认卡在 initializeViewConfig 内部，再逐层看它等哪个队列/锁

## 2026-10-24 22:10 老菜单：dlopen 入口/出口探针结果 —— **App 不走 libc dlopen**（再次证实手动加载器）
- 探针（`libdltrace.so`，interpose `dlopen` 的进入/返回 + `openat`，只匹配 viewUnified/CautionConfig/libObj）：
```
DLOPEN 日志 = 0 条、OPENAT 日志 = 0 条
而同一轮里 INITCFG_ENTER=1 / EXIT=0、viewUnified2 与 CautionConfig 仍被读取并 mmap
⇒ 加载确实发生，但【不经过 libc dlopen】⇒ 是 im.elf 自带 OnDemandLoader 手动加载
⇒ 对 initializeConfig 的阻塞点，dlopen 探针这条路无效
```
- 另一尝试（maps dump 探针 `libgrmlog-c620.so`，在 worker 里读 /proc/self/maps 并打印）：
```
MAPS| 标记打印出来了，但内容为空（读返回 0 或 kmsg 单次写入过大被截断）
⇒ 需要分块/逐行打印，下一轮改进
```
- 现状判定（老菜单）：
```
· 启动链完整、35 模块、无崩溃
· initializeConfig 不返回
· 已确认它加载了 viewUnified2.so + CautionConfig.so 后进入 condvar 等待
· 阻塞点不在 libc dlopen、不在我们已实现的 backup 队列
⇒ 最可能：viewUnified2 的 1768 个构造函数之一里做了同步 IPC（对方给出 DT_INIT_ARRAY=1768 项）
```
- 下一步（第 47/60 轮）：
  1) 改进 maps dump（逐行、限长），拿到 im.elf 的地址布局 → 把 ZZFUTEX 的 uaddr/lr 解析成"哪个库的哪个函数"
  2) 若确认等待点落在 viewUnified2 内部，就针对该处做 shim（例如把它的等待改为直接返回）
  3) 同时验证对方给的下一卡点：initializeConfig 返回后 osal_wai_sem_tmo(0x83037A,-1)

## ★★ 2026-10-24 22:15 新菜单回归定位（协作会话要求的验证）：**只 revert -EINVAL 不够**
- 实验：
```
v43（原始 fake，宽松 mmap + 未实现返回 0）      → appFw=14 处、maps 有 vma、crash=1   ← appFw 会加载
v55（v43 + 新 fake，未实现→-EINVAL）            → appFw=0、crash=0
v56（v43 + 新 fake，未实现→return 0，其余同 v55）→ appFw=0、crash=0                    ← 仍然不加载！
⇒ 说明导致"appFw 不加载"的不只是 -EINVAL，
  新 fake 的其它改动（很可能是"忠实 mmap 契约"：只接受 0x4000/0x20000、其它 -EINVAL）
  同样会让 App 走错误分支
```
- 结论（写清楚，避免以后重复踩）：
```
· 新菜单这条线【没有死】——v43 证明 appFw 能加载、能跑到绘制前
· 但"忠实驱动契约"这一步（限制 mmap 尺寸、ioctl 返回 -EINVAL）会改变 App 控制流
  ⇒ 要么按协作会话给的真驱动常量填合法值（0x20000 / 0x4000 / format 匹配显示格式），
     让上层在"成功分支"上跑；要么暂时保留宽松行为
· 当前用户指定主攻老菜单，新菜单保留 v43 作为"appFw 可加载"的基线
```
- 老菜单线不受影响，继续推进（卡点：initializeConfig 不返回，疑在 viewUnified2 构造函数/内部等待）

## 2026-10-24 22:20 老菜单：maps dump 探针仍读不到内容（两次尝试都失败），改用内核侧方案
- 两次用户态探针结果：
```
c620（整块读 8KB 打印）      → 只打印出 MAPS| 标记，内容空
c621（读后按行过滤 r-xp）    → 输出 "MAPSX empty"（read 返回 <=0）
```
- 判读：在 Appframework 线程里通过我们的 op/gd 包装读 /proc/self/maps 拿不到数据
  （可能是包装函数/缓冲/时序问题），继续在这条路上花时间不划算。
- 改用内核侧方案（下一轮）：
```
在 arch/arm/kernel/exception.c 里已有的 ZZMOVMAP 代码基础上，
加一个可由 /proc/sysrq-trigger 或 proc 文件触发的"dump 指定进程 maps"入口，
或干脆定时（每 30s）对 comm=="im.elf.real" 的进程 dump 一次 r-xp 段。
—— 内核态遍历 mm->mmap 是可靠的，前面已经验证过（ZZMOVMAP 输出 456 段成功）。
```
- 老菜单当前结论汇总（写清楚供下一轮直接用）：
```
· 启动链完整、35 模块、无崩溃；initializeConfig 不返回
· initializeConfig 已加载 viewUnified2.so + CautionConfig.so，随后进入 condvar 等待
· 不走 libc dlopen（探针 0 命中）⇒ 手动加载器
· backup 同步队列已实现且 bootin 阶段正常
· 待验证：等待点是否在 viewUnified2 的构造函数里（对方建议查 1768 个 DT_INIT_ARRAY）
```

## ★★★ 2026-10-24 22:30 老菜单：**找到一个"只等不signaled"的信号量 0x018004B5**（dmp_allocator 用的那个）
- 方法：interpose `osal_wai_sem_tmo` / `osal_sig_sem`，按 sem 去重打印，取"等了但从未被 signal"的集合：
```
去重后 waits 22 个 sem，其中 21 个都有对应 SIGSEM
★ 唯一"WAITED-BUT-NEVER-SIGNALED"：
   sem = 0x018004B5   caller lr = 0x0653EB69
（0x18004B5 正是我们早前在 dmp_allocator_lib_init 反汇编里看到的字面量）
```
- 对照实验：让 `osal_wai_sem_tmo(0x018004B5, -1)` 直接返回 0（BYPASS）后重跑：
```
WAISEM_BYPASS 命中 1 次
但 INITCFG_ENTER=1 / EXIT=0 仍然不变，crash=0
⇒ 这个信号量不是 initializeConfig 的唯一阻塞点（它可能是 DMP 分配器线程自己的等待）
```
- 结论/下一步（第 47/60 轮）：
```
1) 把"BYPASS + 去重信号量表"合并进同一个探针，
   下一轮就能立刻看出：bypass 之后【下一个】从未被 signal 的 sem 是哪个
2) 同时把 lr=0x0653EB69 解析到具体库/函数（需要 im.elf 的 maps —— 内核侧 dump 计划仍待做）
3) 若 signal 侧本该由某个被我们 stub 掉的模块发出（grm_ma/dmm/upm），
   就在对应 stub 里补上 osal_sig_sem 调用
```

## ★★ 2026-10-24 22:45 老菜单：三连实验（信号量集合差 → 全部满足 → 仍需 config 对象）
- 实验链：
```
① Bypass sem 0x018004B5 后重跑（c626）：
   去重后 21 个 sem 的等待【全部】都能找到对应 SIGSEM ⇒ 不再有"只等不发"的信号量
   但 INITCFG_ENTER=1 / EXIT=0 仍不变
② 再查 osal_rcv_msg_tmo（c627）：9 个 qid 有接收，
   其中 0x37807D / 0x12C032F 返回 -1022（超时，正常），其余 r=0
   ⇒ 也没有"卡住不放"的接收
③ 直接跳过 initializeConfig（c628：interpose 里 return 0，不调真函数）：
   ★ INITCFG_EXIT 立刻打印（r=0），pvm=0x00000000
   然后 address=0x0 数据中止（NULL 解引用）
   ⇒ 说明【后续流程确实需要 initializeConfig 建好的配置对象】
```
- 收敛结论：
```
· 信号量/消息接收都不是当前瓶颈（已全部满足）
· initializeConfig 内部卡点在【别的东西】上，且它必须真正完成（不能跳过）
· 下一步必须看清 initializeConfig 内部到底停在哪一步 ⇒ 需要 im.elf 的 maps
  （用户态读 /proc/self/maps 两次都失败，改为内核侧 dump）
```
- 下一步（第 48/60 轮）：
  1) 内核侧实现"定时 dump im.elf.real 的 r-xp 段"（复用已验证的 ZZMOVMAP 遍历代码）
     → 解析 lr=0x0653EB69 / 0x08AC5D8F 等地址所属库与函数
  2) 若确认停在 viewUnified2 的 initializeViewConfig 内部，就在该库入口做 inline hook
  3) 继续保持"每轮记录：INITCFG_EXIT 是否出现 / pvm 是否非 0 / 屏幕是否有内容"

## ★★★ 2026-10-24 22:55 老菜单：**内核侧 maps dump 成功 + 把阻塞点解析到 `dmp_allocator_lib_init`**
- 新增能力：fake_gles 里加了 `maps_thread`（每 8s 对 comm 前缀 "im.elf" 的进程 dump r-xp 段，最多 12 轮）
```
实测（ab_v630 / o13）：MAPD 输出 492 行，拿到完整布局
  im.elf.real   00008000-0000D000
  ld-2.11.2.so  04000000-04018000
  libObj.so     05E82000-071E0000     ← 基址 0x05E82000
  ...（其余库依次）
```
- 用该布局解析此前"只等不被 signal"的信号量 0x018004B5 的等待者 lr=0x0653EB69：
```
0x0653EB69 - 0x05E82000 = 0x6BCB69
反汇编 libObj_cur.so @0x6BCB58 ⇒ 函数名 **dmp_allocator_lib_init**
  （与我们早前在新菜单里 shim 过的是同一个函数；它开头就是 osal_wai_sem_tmo(sem, -1)）
⇒ ★ 老菜单卡住的"没人发信号的 sem" 就在 dmp_allocator_lib_init 里
```
- 对照实验（ab_v631：用 LD_PRELOAD interpose `dmp_allocator_lib_init` 返回 0）：
```
DMPINIT_SHIM 命中 = 0
⇒ interpose 无效：该函数在 libObj 内部被【直接调用】（同 DSO 内 bl，不走 PLT）
⇒ 必须改用 inline hook / 二进制补丁（把开头改成 mov r0,#0; bx lr）
```
- 下一步（第 49/60 轮）：
  1) 对 `libObj.so` 的 `0x6BCB58`（符号 `dmp_allocator_lib_init`）做 inline 补丁：
     把前 8 字节改成 `movs r0,#0 ; bx lr`（Thumb），让调用方直接拿到"成功"
  2) 补丁后重跑，观察 INITCFG_EXIT 是否出现、pvm 是否变非 0
  3) 若 initializeConfig 返回 ⇒ 继续推进 0x83037A 信号量 → 管理器分配 → ViewManager ctor

## ★★★ 2026-10-24 23:05 老菜单：**打补丁后屏幕首次出现"整片纯色填充"（灰色 90,90,90）**
- 改动：把 `libObj.so` 的 `dmp_allocator_lib_init`（vaddr 0x6BCB58）前 4 字节改成 Thumb
  `movs r0,#0 ; bx lr`（即直接返回成功），写入 nflasha15 分区（注意：**不是 n7**，之前一次打错分区）
- 实测（ab_v641 / o16）：
```
INITCFG_ENTER=1 / EXIT=0（initializeConfig 仍未返回）  crash=0
★ 屏幕：主色 (90,90,90) 占 262,144 像素（对照无补丁时主色是黑/噪声）
   ⇒ 第一次出现"整片纯色背景"——说明有线程在往帧缓冲里做 clear/fill
截图：docs/artifacts/2026-10-24_oldmenu_gray_fill.png
```
- 判读：
```
① 屏幕不再是纯噪声/纯黑 ⇒ 显示链路 + 帧缓冲已经"有人写入"
② 但 initializeConfig 仍不返回 ⇒ 灰色背景不是它之后画的，可能是别的线程（VDF/背景层）
③ 注意：这个灰也可能是我们的探针/其他路径清屏，需要对照确认（下一轮先做对照）
```
- 下一步（第 50/60 轮）：
  1) 对照实验：ab_v603（无补丁）在同一时间点截图，确认灰色是否只在补丁版出现
  2) 若确认是补丁带来的：说明 dmp_allocator 初始化成功让某个显示线程跑起来了
     ⇒ 继续沿"让更多初始化返回成功"推进（initializeConfig / ViewManager）
  3) 同时做 L1 里程碑实验：preload 直接 mmap /dev/mem(0x20312000) 写测试图案，
     证明"我们能主动让屏幕显示任意内容"（与 App 无关的硬证据）

## 🎉🎉 2026-10-24 23:10 老菜单 **L1 达成：屏幕出现我们写入的彩条**（显示链路完全打通）
- 做法（最小闭环）：
```
preload 构造函数（libtestpat.so，-nostdlib）：
  fd = open("/dev/mem", O_RDWR)
  p  = mmap(NULL, 0x1E0000, PROT_READ|WRITE, MAP_SHARED, fd, 0x20312000)   ← VDF 帧缓冲物理地址
  以 16bpp 写 8 条彩条（640×480）
实测：TESTPAT bars written to 0x20312000 va=0x474e000 ⇒ 成功
```
- 屏幕效果（见 docs/artifacts/2026-10-24_oldmenu_L1_colorbars.png）：
```
上半部分出现清晰的 8 色彩条（白/品红/绿/黑…），下半部分仍是未初始化噪声
⇒ ★ 我们已经能把任意像素显示到屏幕上（与 App 无关的硬证据）
```
- 说明与遗留：
```
① 彩条只覆盖上半部 ⇒ 我们对 streamfb 的 stride/format/height 假设与实际模型不完全一致
   （模型默认 width/height/stride/format 可用 qdev 属性显式指定，下一轮先对齐）
② 这条路径同时确认：0x20312000 就是显示扫出的缓冲、且在 QEMU 里可写
   ⇒ 为 L2（让 App 的 GRM 绘制落到同一缓冲）建立了基础事实
```
- 下一步（第 50/60 轮）：
  1) 用 `-global driver=bionz-streamfb,property=width/height/stride/format` 对齐模型与我们的写入布局
  2) L2：按协作会话的建议，interpose `GRM_bitmapCreate`，把 `[bmp+0x08]` 指向 0x20312000 的映射
     + 用 swgpe 的 CPU 实现 gpermFillrect/Rectblit
  3) 判据：屏幕出现"由 App 的绘制调用产生"的色块/内容

## 🎉🎉🎉 2026-10-24 23:20 老菜单 **满屏彩条成功：显示链路 100% 打通**（几何也对齐了）
- 关键：给 streamfb 模型显式指定几何与格式
```
-global driver=bionz-streamfb,property=width,value=640
-global driver=bionz-streamfb,property=height,value=480
-global driver=bionz-streamfb,property=stride,value=1280      ← 640×2B（RGB565）
-global driver=bionz-streamfb,property=format,value=1
```
- 实测（ab_v651 / o19）：
```
TESTPAT 写入 0x20312000（/dev/mem mmap，VA 0x474e000）
屏幕统计：8 种纯色各 38400 像素，合计 307200 = 640×480 ⇒ 【满屏彩条，无噪声】
截图：docs/artifacts/2026-10-24_oldmenu_fullscreen_bars.png
```
- 意义：
```
① 显示链路（/dev/mem → VDF 缓冲 0x20312000 → bionz-streamfb → 屏幕）完全验证
② 几何/格式已验证：640×480、stride 1280B、RGB565
③ 我们可以在屏幕任意位置画任意像素 ⇒ L1 完成，且是"可重复、可复现"的
```
- 下一步（第 51/60 轮）—— L2：让 App 的绘制落到这块缓冲
```
1) interpose `GRM_bitmapCreate`（老菜单有，120B）：
   让返回的 bitmap 的 [bmp+0x08] = 我们 /dev/mem 映射出来的 VA（指向 0x20312000）
   并把 geo+0x18（stride=640）/ geo+0x1C（format=1）写成与显示一致
2) interpose `GRM_gpermFillrect` / `GRM_gpermRectblit` → 用 CPU 实现（swgpe 规格两版通用）
3) 难点：App 目前仍卡在 initializeConfig（未画）——
   这两步先做成"随时可用"，等 App 能走到绘制就能立刻出内容
```

## 2026-10-24 23:30 老菜单：L2 shim 已就位，但 App 仍未调用 GRM_*（卡在初始化，不在绘制层）
- 新增 `libgrm2.so`（-nostdlib）：
```
构造函数：open("/dev/mem") + mmap(0x20312000, 0x1E0000) → fb VA（实测 0x474E000）
interpose GRM_bitmapCreate：真库造对象后，把 [bmp+0x08] 指向 fb，
   并写 geo+0x18=640(stride) / geo+0x1A=480 / geo+0x1C=1(format) / geo+0x24=0x20312000
interpose GRM_bitmapWriteLockBits → 直接返回 fb
interpose GRM_gpermFillrect → CPU 填充（按 bpp_tab / format）
interpose GRM_gpermRectblit → 目前只记录
```
- 实测（ab_v660 / o20）：
```
GRM2 init fb=0x474E000  ✅ shim 正常初始化
但 GRM2 其它日志 = 0 条 ⇒ App 一次都没有调用 GRM_bitmapCreate / gperm*
屏幕：仍以黑色为主（App 自己的零散内容）
```
- 结论：
```
① 显示层（L1）已彻底打通：我们可以把任意像素显示到屏幕（满屏彩条已验证）
② 绘制层（L2）的 shim 已准备好，但 App 不走绘制 —— 因为 initializeConfig 不返回
③ 所以当前唯一瓶颈回到 App 初始化：initializeConfig
```
- 下一步（第 51/60 轮）：
  1) 回到 initializeConfig：既然 dmp_allocator_lib_init 的 sem 已能 interpose 放行，
     下一步用**多探针组合**（INITCFG_EN/EX、WAISEM、RCV、ZZFUTEX）观察它放行后停在哪
  2) 若它实际卡在"等 GPU 事件"，就把 fake 的 event 语义按真驱动常量填对
  3) L2 shim 保留，App 一旦走到绘制即可直接出图

## 2026-10-24 23:40 老菜单：sem bypass + L2 shim 组合仍不出图（瓶颈确认在 App 初始化）
- 组合实验（ab_v661）：`libsemfix`（0x18004B5 直接返回 0）+ `libgrm2`（bitmap→VDF 缓冲 + CPU gperm）+ 探针
```
SEMFIX 命中 1 次 ✅
GRM2 init fb=0x476F000 ✅
但 GRM2 绘制日志 = 0，INITCFG_EXIT = 0，屏幕仍以黑色为主
⇒ 放行 dmp_allocator 的 sem 并不能让 initializeConfig 返回
```
- 结论（本轮最终判定）：
```
显示层：✅ 完全打通（满屏彩条，几何 640×480/stride1280/RGB565）
绘制层：✅ shim 就绪（GRM_bitmapCreate 指向 VDF 缓冲 + CPU gpermFillrect/Rectblit）
应用层：❌ initializeConfig 不返回 ⇒ App 从未进入绘制
⇒ 唯一剩余瓶颈 = App 初始化（libObj 里的配置初始化链）
```
