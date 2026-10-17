# ZV-E10 QEMU 仿真 —— 交接文档（2026-10-17）

## 当前最优镜像
`<build>/ab_v3b.img`（1,983,905,792 B）
- 运行到 **52.5s**，加载 **70 个模块**，**43 种 UIPC 处理器**
- HDMI 驱动 `udm_hdmi`、CEC 驱动 `udm_cec` 均初始化成功
- GPU 驱动 `grm_gles` 已加载，`/dev/dmpgles2` 节点就位
- **零堆损坏、零 abort（已用自旋替代）**

## 完整复现命令
```sh
cd /root/fw
Q=<build>/qemu-build/arm-softmmu/qemu-system-arm
Z=<build>/src/a7c-kernel/linux/arch/arm/boot/zImage
CMD="earlyprintk $(cat real_cmdline.txt)"
$Q -M cxd90045 -display none -serial file:/tmp/run.log -monitor none \
   -kernel $Z -initrd handoff_p0/initrd.img \
   -drive if=mtd,file=<build>/ab_v3b.img,format=raw \
   -device loader,file=<repo>/BACKUP.BIN.fixed,addr=0x02E80000,force-raw=on \
   -append "$CMD"
```

## 9 项修法（全部在 ab_v3b.img 里）
| # | 修法 | 位置 |
|---|---|---|
| 1 | liro `b 2e0` + 固件前注入 complete | nflasha15:/kmod/liro.ko (file 0x875c, 0x88d4) |
| 2 | backup.ko 补丁版（73,764 B） | nflasha15:/kmod/backup.ko |
| 3 | libosal_uipc.so @0x26c0 = 7a68 | nflasha15:/lib/ |
| 4 | im.elf @0x2f46 = 00bf | nflasha15:/bin/ |
| 5 | `/dev/tty` → `/dev/null` 符号链接 | rootfs:/dev/ |
| 6 | libc `__assert_fail` @0x1f98c: 80b5→7047 | rootfs:/lib/libc-2.11.2.so |
| 7 | 68 个库符号链接 `/lib/X → /usr/lib/X` | rootfs:/lib/ |
| 8 | libc `abort` @0x25ae0: 684d→fee7 (`b .` 自旋) | rootfs:/lib/ |
| 9 | （教训）abort 不能改成返回，只能自旋 | — |

## 唯一剩余卡点（已 100% 定位）
```
崩溃: page translation fault at 0x00000106 (pc=0x106, r0=0x107)
线程: THREAD_INFRA2
路径: libbackup.so+0x1227 (return addr) -> backup_sync_msg@0xf64
      0x100c: blx osal_snd_sync_msg@plt
      -> PLT@0xe08 -> ldr pc, [GOT] -> 跳到 0x106
性质: GOT 条目被破坏（含 0x107 小整数而非地址）
证据: 用户栈是结构化数据模式 (0x0107 每项+2: 01070e85/01070e87/...)
      两次运行库内偏移完全一致 => 崩溃路径确定性
```

## 诊断工具（均已验证）
| 工具 | 位置 | 作用 |
|---|---|---|
| `ZZTGKILL` / `ZZSTACK` | kernel/signal.c | 抓 abort + 用户栈 |
| `ZZEXCSTACK` | arch/arm/kernel/exception.c `em_show_syndrome()` | 页错误时 dump 用户栈 |
| `IMOPEN/MODLOAD/MODARGS` | 内核 | 库/模块加载追踪 |
| `MISSSO/MISSDEV` | 内核 | 探测失败追踪 |
| `ZZFUTEX` | kernel/futex.c | futex 追踪 |
| `ZZCOMPLETE` | kernel/sched.c | completion 追踪 |
| `QMMAP` | 内核 | mmap 追踪 |

## 下一步建议（按优先级）
1. **QEMU gdb 精确追踪**：`-S` 启动 → 等 libbackup.so 映射后（从日志看 base）
   → `add-symbol-file libbackup.so <base>` → `b *<base+0x100c>` → 单步看 GOT[rGOT] 的值和被谁改
2. **检查 `abort→自旋` 的副作用**：自旋线程可能持锁 ⇒ 其他线程看到不一致状态
   ⇒ 试：把自旋改成 `exit_group`（只杀进程，不留脏状态）对比
3. **`/dev/dmpgles2` 之后的工作**（GPU 建模）—— 必须在 App init 完成后才轮到
4. 显示链路的现实评估：`grm_gles.ko` 是 Sony 的 SUGILITE 驱动，
   真正出画面需要 Mali/Sugilite 建模（研究级）或 stub 驱动 + framebuffer 模拟

## 重要资产位置
```
<repo>/ZVE10_QEMU_BOOT_PROGRESS.md     13,000+ 行完整过程记录
<repo>/crash_with_stack.log            含完整用户栈的崩溃日志
<repo>/ref_e10/                        解包的真机固件（/usr 内容）
<build>/lf/tree/                      nflasha15 解包树（可改后重打包）
<build>/lf7/n7.img                    原始 rootfs（含设备节点）
<build>/src/a7c-kernel/linux          内核源码（含所有 trace 补丁）
<build>/qemu-build/                   QEMU 构建目录（源码 <build>/src/qemu-sony）
<build>/ab_v3b.img                    ★ 最优镜像
```

---

## 🔗 重要参考：chocotan/a7c2-re（a7C II 逆向 + 原生 QEMU）

**仓库**：`https://github.com/chocotan/a7c2-re`（441 文件）

### 他们已打通的显示链（可直接借鉴）
```
软件 GPE 模型（gpe-raster.c / gpe-sprite.c）
+ Sony LZ77 CPU 解码（sony-lz77.c）
+ DMM/HDMI 服务模拟（dmm-service.c / hdmi-format-service.c）
+ OSD 队列（headless-osd.c）
+ ramfb-viewer.c 读帧缓冲 → RGBA4444 → XRGB8888
+ QEMU fw_cfg DMA 配 etc/ramfb → GTK 显示
虚拟 scanout: [0x41e100000, 0x41e600000)
```

### ★ 他们对"砖机"的结论（与我们的分析一致）
```
/bin/busybox 是动态链接的，/bin/sh 是它的符号链接
⇒ 致命损坏的 preload 库会阻止 busybox 启动 → /sbin/init 起不来
⇒ 【只能预防，无法救援】
⇒ 我们的 BootROM 串行启动 + eMMC 直写是唯一路径 ✓
```

### 他们的修复工具
```
jiritsu_preload_clear.py : 用 512 个换行中和 preload（glibc 按空白分割）
  PRELOAD_WIRE_PATH = "/tmp/jiritsu/../../etc/ld.so.preload"
  经验：固件覆盖只在已有文件大小内可行（对 0 字节文件写失败 0x10000009）
⇒ 但需要相机能启动并跑 JIRITSU 链 ⇒ 对 boot-loop 相机无效
```

### 关键教训（对我们写 eMMC 有直接价值）
```
/tmp、/etc、/var 是同一块 12 MiB vfat 分区（nflasha10）
写满后【所有】写入都失败（0x10000009）
⇒ 我们的"1-2 字节修复"是最优方案（不写大文件）
⇒ 若必须写 12MB，要一次性写完整个分区（不是追加）
```

---

## 2026-10-17 追加：三个重要进展与一个换算规则

### 1) ★ 分区偏移换算规则（已验证）
```
镜像偏移 = PARTITION_TABLE.txt 里的表内偏移 + 0x400000   （boot0 2MB + boot1 2MB）
例：nflasha2 /setting 表 0x00800000 → 镜像 0x00C00000 ✓ 实测 FAT 在此
```
完整表见 `zve10_delivery/PARTITION_TABLE.txt`。

### 2) ★★ /log 分区（nflasha11）修复 —— 效果显著
```
问题：镜像里 nflasha11（500MB，镜像偏移 0x0BC00000）整块全零
      ⇒ FAT-fs (nflasha11): bogus number of reserved sectors ⇒ /log 挂不上
修复：mkfs.vfat -F 32 出 500MiB 镜像，dd 到 0x0BC00000 (bs=512 seek=385024)
效果：/log 挂载成功；guest 时间 52.5s → 70.6s；page fault 1 → 0
```
**⇒ 新镜像：`<build>/ab_log.img`（= ab_mode.img + /log 修复）**

### 3) ★★ 更正：UI 主库 libObj.so **是被加载的**
```
[16.858105] IMOPEN OK /usr/lib/libObj.so    ← 16.85s 就加载了
（之前用 IMOPEN OK //usr/lib 双斜杠 grep 漏掉了它）
App 打开的设备只有：/dev/uipc、/dev/mem、/dev/usb/kikilog
⇒ 还没打开任何显示设备
```

### 4) 已排除的假设
- `dmode=3` 写入 `/setting/mode/dmode` ⇒ 行为无变化（启动已在默认档位）
- `/setting/mode/cmode` 缺失 ⇒ 属工厂调整模式（InfraAdjMediaBase），缺失正常
- `qsi.elf` 不在镜像里（bootin 想启动它）⇒ 待评估影响

### 5) 下一步最该做的事（按价值排序）
1. **内核侧转储 klog/alog 环缓冲到串口**
   `alog.addr=0x03524FC0 size=0x60040` / `klog.addr=0x035A5000 size=0x20000`
   —— 这是唯一能看到 App 内部日志的通道（QEMU monitor 的 HMP 回显会打乱命令，
   改用内核 kthread 或 `-qmp`）
2. 定位 App 在 ~33s 后停止产生 console 活动的原因（但进程仍活到 70s）
3. 评估 qsi.elf 缺失的影响
