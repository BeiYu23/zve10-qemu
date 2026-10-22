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
