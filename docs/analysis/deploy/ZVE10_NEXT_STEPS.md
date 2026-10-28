# ZV-E10 QEMU UI —— 接手指南（2026-10-23 收尾）

## 一、当前一句话状态
图形后端**完全可用**（App 给帧描述符 → 屏幕就显示），App 侧**尚未开始绘制**
（已用 12 秒前后源缓冲校验和一致直接证明）。

## 二、复现基线（5 分钟）
```bash
# 1) 单实例跑机（务必用 run_cap.sh，GPE 日志在 QEMU stderr 里）
cd /root/fw/oss
EXTRA='-global driver=bionz-streamfb,property=blank-pattern,value=off \
       -global driver=bionz-streamfb,property=format,value=1' \
INITRD=/root/fw/handoff_p0/initrd_q4.img OUT=/mnt/build/tmp/cXXX \
  /mnt/build/run_cap.sh /mnt/build/ab_v291.img 120
pkill -f qemu-system-arm

# 2) 期望输出
serial.log:  PV i=56 pVM≈1.4M ；无 trap
qemu.out :   ZZGPEREG w off=0x0C00…/0x1800…；ZZGPECMD（11 次）；ZZGPECOPY2（1 次）
```

## 三、已确认的关键事实（不必再查）
| 项 | 值 |
|---|---|
| 帧缓冲呈现地址 | phys `0x20312000`（streamfb，640×480，stride/format 可 -global 覆盖） |
| App 的显示映射 | `/dev/stream` mmap `0x20312000` 32MB |
| App 的帧描述符 | 源 `0x080C4880`、目标 `0x08AEC800`、几何 `0x01E00280`(640×480)、格式 `0x4401`、使能 `0x0C18=1` |
| GPE 寄存器块 | phys `0x50000000`（libObj 通过 /dev/mem 映射后直接写） |
| 提交协议 | `reg[0x1804] ← 命令块 phys` ; `reg[0x1800] ← opcode`（当前仅见 4） |
| 命令块 | 8 word，magic `0x12345678` @ +0x1C |
| 写入顺序 | 先 11 次 kick，**之后**才写帧描述符（所以执行器要挂在 0x0C18 上） |

## 四、App 侧"为什么没绘制"（测量结论 + 候选原因）
1. **测量**：`0x080C4880` 在 t=45s 与 t=57s 校验和完全相同 ⇒ 从未写入。
2. 候选原因（按可能性）：
   - 缺外部驱动：真机 im.elf 的 sync API（`cmd 0x9201xx`，handler `libObj? no → im.elf 0x986C`）
     由外部进程（更新器/应用外进程）调用；我们环境里**该队列从未注册**（21 个已注册队列里没有）。
   - 缺业务状态：`ModelManager / WidgetSystem / Layer / RecordPtr` 未建立，view 无内容可画。
3. 已排除：换 streamfb 地址、拦导出 GRM API（调用数为 0）、`dmode` 变体、
   三种 IMDB 掩码触发、按名 openView、强行 drawDirect、直调 sync API/IMDB 分发器。

## 五、建议的继续方向（按性价比）
1. **反汇编 mf/ud_im 与 im.elf 的 sync API 交互**：确定真机到底发哪条 `0x9201xx`、带什么 arg，
   然后在探针里**先注册该队列**再发（c336 直接发被 -1024 拒绝，因为没有注册方）。
2. **补齐 Model/Widget 最小状态**：从 `View::requestApplicationExecute`(0x89FFE4)、
   `GRM_screenGetOnBitmap`(0x685A20) 的期望入手，构造能被 drawDirect 接受的 view。
3. 一旦 App 往 `0x080C4880` 画，现有执行器会自动把它合成到 `0x08AEC800` 并显示，
   **无需再改 QEMU**。

## 六、工程要点（避免重复踩坑）
- QEMU 改完先 `cp qemu-system-arm qemu-system-arm.bak`（曾因缺依赖链接失败丢过一次二进制）。
- 依赖：gtk3/sdl2/epoxy/pango/cairo（已装）。
- 内核模块重编：
  `make -C /mnt/build/src/a7c-kernel/linux M=/root/fw/fake_gles ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- modules`
- 镜像制作：n7 分区(偏移 168MB,8MB) 放探针 `/lib/libgrmlog.so`；n15(偏移 732MB,300MB) 放 `/bin/*`、`/kmod/grm_gles.ko`。
- 探针规则：无强未定义符号；不拦 ioctl；不在 loader 锁内递归 dlsym。

## 七、产物索引
```
ZVE10_QEMU_UI_HANDOFF.md   总交接（协议数值/工具/现状）
ZVE10_PORT_ROADMAP.md      逐轮实验记录（含失败原因）
../qemu/QEMU_BOOT_PROGRESS.md      阶段总结（Round 379-391）
artifacts/c321/gpe_fill_red.png 执行器填充实证
artifacts/c345/gpe_copy.png     合成（src→dst）实证
artifacts/libObj_rm_reg.dis     GPE 接口反汇编（12 处 ioctl 调用点）
/mnt/build/tmp/cx/              cramfs 解包（mf / ud_im.elf / 官方脚本）
镜像 ab_v291.img（基线）
```


## 八、2026-10-23 最终补充：序列/业务层定位到 **libBizFw.so**
用"运行时 dlsym + /proc/self/maps 归属"法（探针 c356）得到决定性结果：
```
dlsym(RTLD_DEFAULT,"_ZN17OBJAVBBCBASECLASS13SeqController20CreateAndAddSequenceEjPN3MWF6ObjMsgE")
  = 0x085B0D41
所属映射: 08571000-085BD000 r-xp  /usr/lib/libBizFw.so
```
- ⇒ **BBC/序列/绘制请求的实现层 = `/usr/lib/libBizFw.so`**（不是 libObj、不是 libmpr/libDDS/libMprSeq——后三者是 stub 或无关）。
- 下一步（下阶段主线）：
  1) 用已打通的"guest 文件抽取通道"取出 `libBizFw.so`（探针写 phys 0x21312000 + QMP pmemsave）；
  2) 在 libBizFw 里定位 `SeqController` / `DefaultSeqController` 的构造与初始化路径，
     弄清它依赖哪些服务/配置（很可能需要 Backup/DDS/Infini 的模型状态）；
  3) 一旦能触发一次 `CreateAndAddSequence`，App 就会往帧源缓冲（0x080C4880）落笔，
     现有 GPE 执行器会自动把画面合成到 0x08AEC800 并显示——**无需再改 QEMU**。


## 九、libBizFw.so 已成功抽取（2026-10-23，318,780 字节）
产物：`/mnt/build/tmp/libBizFw.so`（可用 `nm -D -C` 直接看符号）。

关键导出（`T`，说明是真实现）：
```
OBJAVBBCBASECLASS::SeqController::CreateAndAddSequence(unsigned, MWF::ObjMsg*)     @0x3FD40
OBJAVBBCBASECLASS::SeqController::GenerateSequence(unsigned, MWF::ObjMsg*, MWF::ObjMsg*) @0x3FE88
OBJAVBBCBASECLASS::SeqController::DelayCreateSequence(...)                        @0x3FDE0
OBJAVBBCBASECLASS::SeqController::SeqController(unsigned, ObjActiveBaseAvbbc*, GlobalAccessor*) @0x3FCDC
OBJAVBBCBASECLASS::DefaultSeqController::CanExecute / EnqueueMsg / DequeueMsg
```
⇒ **构造签名揭示了依赖**：`SeqController(uint, ObjActiveBaseAvbbc*, GlobalAccessor*)`
   - 需要一个 **ObjActiveBaseAvbbc\***（BBC 活动对象，通常随 view/model 初始化创建）
   - 和一个 **GlobalAccessor\***
⇒ 因此"让 App 落笔"的正确顺序是：**先有 view/model 侧的 ObjActiveBaseAvbbc 实例**，
   才能构造 SeqController 并提交序列；这也解释了为什么直接 openView 之后仍然不画。

下阶段第一步：在 libBizFw 里反汇编 `ObjActiveBaseAvbbc` 的初始化路径（谁 new 它、依赖什么服务），
再决定补服务还是补状态。
