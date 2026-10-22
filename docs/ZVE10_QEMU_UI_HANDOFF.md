# ZV-E10 QEMU UI 逆向 — 交接文档（2026-10-23）

## 0. 目标
让 ZV-E10（CXD90045 / ARMv7）在 QEMU 中跑到 **App 画面正常显示且可交互**。

## 1. 现状（可复现）
| 项 | 状态 |
|---|---|
| 固件启动（内核/initrd/真机分区） | ✅ 正常 |
| App（im.elf → libObj）启动 | ✅ 61 个模块加载完成 |
| gfx / res / wg / ViewManager | ✅ 全部非 0（pVM≈1.35M，稳定 150s+） |
| App 打开设备 | ✅ /dev/hdmi /dev/dmpgles2 /dev/stream /dev/ldec /dev/mem |
| App 加载 UI 资源 | ✅ global.xdb、lang_cmn.uxc、color_cmn.uxc、image_cmn_43.uxc |
| App 提交图形命令 | ✅ dmpgles2 ioctl 0xc0048211/12/0d 持续出现 |
| 屏幕显示 App 画面 | ❌ 仅 QEMU 测试图案 / 噪声 |
| 输入可交互 | ❌ 未开始（InputServant 队列不存在） |

**核心结论：App 在画，QEMU 侧没有人执行 GPE 命令。**

## 2. 已排除的路线（省时间）
1. **不是缺 UI 库**：/usr/lib 有 319 个库；IMDB 里 appFw.so/subapp.so/gui.so/libVssApp.so 属于**其它 target**，本机 UI = libObj。
2. **不是 THREAD_APP 没起来**：它会被创建，但 IMDB 过滤 `entry[1]&arg`，arg=0 时全部条目被跳过（我们传 0 的锅）。
3. **不是 grm_ma 分配**：qid 0x14C0424 是 **GRM 错误上报通道**（消息体是 ASCII 日志 + err -9）；清 status 只会让 App 写垃圾。
4. **不能靠 streamfb 换地址**：0x20560000(黑)/0x20030000(噪声)/0x08AEC800(条纹噪声) 都不是画面 ⇒ 根本没有已画好的帧。
5. **不能靠拦导出 GRM API**：`GRM_bitmapCreate` / `GRM_gpermFillrect` / `GRM_gpermRectblit` 调用次数均为 **0**
   ⇒ 绘制完全走 GPE 硬件通道。
6. **不是探针崩溃**：早期大量失败是探针自己（inline hook 的 Thumb 位、naked asm 等）。

## 3. 已建立的能力（下一位接手可直接用）
```bash
# 内核模块重编（关键！）
make -C /mnt/build/src/a7c-kernel/linux M=/root/fw/fake_gles ARCH=arm \
     CROSS_COMPILE=arm-buildroot-linux-gnueabihf- modules

# QEMU 重编（注意：先备份二进制）
cp arm-softmmu/qemu-system-arm arm-softmmu/qemu-system-arm.bak
cd arm-softmmu && make -j8 qemu-system-arm   # 依赖: gtk3/sdl2/epoxy/pango/cairo

# 单实例跑机
INITRD=/root/fw/handoff_p0/initrd_q4.img OUT=/mnt/build/tmp/cXXX \
  /mnt/build/run_lowlog.sh /mnt/build/ab_vNNN.img 100
```
- 镜像制作：`ab_vNNN.img` = n7 分区(偏移168MB, 8MB) 放探针 `/lib/libgrmlog.so`；n15(偏移732MB, 300MB) 放 `/kmod/grm_gles.ko`、`/bin/bootin.elf` 包装脚本。
- 探针（App-only LD_PRELOAD）：`/mnt/build/menuc*.c`，最近可用：`menuc284.c`（open 全量日志）、`menuc251.c`（grm_ma 状态修）。
- 内核 dump：`fake_gles.c` 里 `GLESDBG/K3/BUF/TAIL` 打印（ioctl 参数、三级指针、ioremap 读物理）。

## 4. 关键地址/结构（已确认）
```
display: streamfb 呈现 phys 0x20312000（640x480，stride/format 由 -global 指定）
dmpgles2: char dev major 248 minor 29；每 open 一块 256KB DMA 缓冲（phys 0xa7c0000 / 0x940000 …）
kick ioctl(0xc0048212) arg 结构:
  H = {1, desc_ptr, desc_ptr, ctx_ptr, code, 0x36, ...}
  desc[desc_ptr] = {0x013D7AA0(缓冲phys), 0x0003A788(len), 0x0400F5C4, …}
0x013D7AA0 处为 9-word 一组链式节点：
  00000400 00000001 FFFFFFFF 00000000 00000000 00000000 <next> <next> <id 0x223..0x23D>
（a7c2 文档提示 0x223 更像 **selector/消息号**，因此该区可能是消息队列而非图形命令）
DDR 描述符（0x10070C00）: {0x080C4880, 0x08AEC800, 0x01E00280(=640x480), 0x01E00280, 0x44 01, 1}
```

## 5. 下一步建议（按性价比排序）
1. **在 kick 处理里加"命令体快照"**：同一条目在 kick 进入/返回各 dump 一次（当前都是空槽，怀疑命令体在别处）；
2. **从 App 侧找提交函数**：`libObj` 内调用 `osal_snd_sync_msg`/`ioctl` 前的构造点（用 GOT/PLT 或性能采样定位），
   直接 dump 构造出来的命令结构（而不是从硬件侧猜）；
3. **参考 a7c2 的软件 GPE**：他们把 GPE 换成软件模型 + OSD 消息拦截；ZV-E10 的等价拦截点在 dmpgles2 的 kick 语义；
4. 一旦 GPE 能出图，**输入**再单独做（menu raw 0x1AB、upm/dmpgles2 注入）。

## 6. 纪律
- 单 QEMU 实例、跑完 `pkill -f qemu-system-arm`；
- 所有缓存放 `/mnt/build/tmp`（不要 `/tmp`，tmpfs 会撑爆）；
- 每轮结论写入 `docs/ZVE10_PORT_ROADMAP.md` / `docs/QEMU_BOOT_PROGRESS.md` 并 push。

## 7. 2026-10-23 新增（GPE 协议 + 执行器）
### 7.1 GPE 提交协议（已实测）
```
GPE 寄存器块: phys 0x50000000（libObj 用 /dev/mem 映射后直接读写）
提交方式:
   reg[0x1804] <- 命令块物理地址（32 字节对齐，通常是 0x2056Exxx 起、每次 +0x20）
   reg[0x1800] <- opcode（当前只观察到 4）
命令块（8 word，magic 在 +0x1C = 0x12345678）:
   0116f000 02056db0 0117f000 00000001 0044f000 00000000 0010f000 12345678
帧描述符寄存器（App 初始化时写）:
   0x0C00 = 源缓冲 phys (0x080C4880)
   0x0C04 = 目标缓冲 phys (0x08AEC800)
   0x0C08/0x0C0C = 0x01E00280 (=480<<16 | 640)
   0x0C10 = 0x00004401 (格式)
   0x0C18 = 0x00000001 (使能)
```
### 7.2 已可用的执行器骨架（QEMU `hw/arm/bionz.c`）
- `zz_gpe_write()` 中：写 0x1800 时读 `zz_gpe_regs[0x1804]` 得到命令块 phys，
  用 `zz_watch_ram + ptr` 直接取 8/24 word（已实现 `ZZGPECMD` 打印 + 可插入光栅逻辑）；
- **已验证**：把命令块 word[4] 当颜色填满 640×480 → 屏幕整屏红（`oss/artifacts/c321/gpe_fill_red.png`）；
- **必须**带 `-global driver=bionz-streamfb,property=blank-pattern,value=off`，否则全同色缓冲会被测试彩条覆盖；
- QEMU stderr 里有 `ZZGPEREG/ZZGPECMD`，用 `/mnt/build/run_cap.sh` 跑（保存到 `qemu.out`），
  默认 runner 会丢弃这些日志。

### 7.3 当前唯一阻塞
App **只提交 1 条 opcode=4 的配置命令**（200s 长跑统计），之后不再产生任何绘制命令；
帧描述符指向的两个缓冲（0x080C4880 / 0x08AEC800）里是**未初始化垃圾**。
⇒ 需要让 App 的 UI/场景渲染循环真正跑起来：
- 手工触发 IMDB 应用阶段（`0xA1B0(arg)`）时 arg=0 全部过滤、arg=1 → 78 模块后崩、arg=0xff → 61 模块后崩；
- 崩溃点都在 `THREAD_INFRA0`，下一步应定位该崩溃（很可能是应用阶段真正的入口）。
