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
