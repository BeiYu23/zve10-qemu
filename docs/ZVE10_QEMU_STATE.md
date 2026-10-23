# ZV-E10 QEMU 当前状态速查（2026-10-23 22:30）

> 目的：**上下文丢失也能在 5 分钟内接手**。只写"现在成立的事实"和"下一步做什么"。
> 逐轮细节见 `ZVE10_PORT_ROADMAP.md`；协议数值见 `ZVE10_QEMU_UI_HANDOFF.md`。

## 一、一句话状态

```text
目标（App 画面显示+可交互）：未达成，屏幕仍黑、view 从未创建。
但底座已从"脏镜像+自制 stub 库"换成"纯净真机 /usr + 零售 initrd + 与真机一致的内核"，
并在 c419 首次跑通 retail init → chroot → bootin → im.elf（随后 im.elf 用户态 NULL 解引用）。
```

## 二、当前使用的镜像与组件（一律用这套，别再回旧的 ab_v291 基线）

| 组件 | 来源 / 状态 |
|---|---|
| `/usr` 分区 | **纯净真机 dump** `/root/fw/zve10_delivery/nflasha15.img`（sha256 `d9ad9954…`，207 项；**没有** libBizInitializer.so） |
| initrd | 零售 `nflasha3/INITRD.IMG`（2,183,168 B ext2，`/sbin/init` 是 28,580 B 二进制，内含 35 条 insmod 表） |
| 内核 | 自建 `a7c-kernel`，**release = `3.0.27_nl-rt106+`（与真机 vermagic 一致）**，已打 `MODLOAD` 日志补丁 |
| 内核配置 | `CONFIG_LOCALVERSION="+"`、`LOCALVERSION_AUTO=n`（`localversion-rt` 提供 `-rt106`） |
| 探针 | `/lib/libgrmlog.so`（放 n7 分区）；**纯净 /usr 下尚未成功注入**（probe=0） |

**关键构建命令（缺一不可）**

```bash
export PATH=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin:$PATH
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/lib
export ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
cd /mnt/build/src/a7c-kernel/linux && make -j8 zImage
```

## 三、已替换成 stub 的模块（都在 `/usr/kmod/`，逐个因崩溃而换）

| 模块 | 原因 |
|---|---|
| `udate.ko` | 内核线程 `udm_udate` trap |
| `upm.ko` | 装载后卡住（其后无任何 MODLOAD） |
| `accel.ko` / `mag.ko` / `compass.ko` | `udm_mag` 内核线程崩 |
| `backup.ko` | 内核 oops：`backup_suspend+0x828` NULL 解引用 |
| `grm_gles.ko` | 换成我们的 `fake_gles.ko`（提供 `/dev/dmpgles2`） |

stub 模板：`/mnt/build/tmp/udate_stub/`（复制改名即可，`module_init` 直接 return 0）。
名字↔尺寸对照表：`/mnt/build/tmp/kmodlist.txt`（103 个模块）。

## 四、装载/崩溃演进（用来判断是否退化）

```text
c412 (udate)                  → 22 模块，停在 upm
c417 (+upm)                   → 26 模块，udm_mag 崩
c418 (+accel/mag/compass)     → 32 模块，**im.elf 启动**，用户态 trap 0x0e
c419 (+backup)                → 32 模块，im.elf 启动，用户态 NULL 解引用；内核 oops 消失
```

## 五、下一步（正在做 ①）

```text
① 给 backup 提供"最小可用 /dev/backup 语义"（而不是纯 stub）：
   libbackup.so 的 Backup_read 需要能读到数据，否则 im.elf NULL 解引用
② 把探针装回去（验证 /setting/mode/preload 是否被 bootin 使用；否则退回包壳注入）
③ 回到 UI 主线：资源已就绪却不建 view → 查 UIPC 等待（0x13A 积压 609 / 0x3E014D 重发 17493）
```

## 六、已知的"假线索"（别再走）

- **BBC / libBizFw / SeqController 与 UI 无关**（相机音视频控制层），`CreateAndAddSequence` 不是绘制入口
- 旧镜像里的 `libBizInitializer.so` 是**我们自己写的 stub**，不是索尼的；旧基线 `kicks=11` 不可信
- `mounting1 /dev/dnflasha* failed errno(6)` 在历史成功 run 里也有，**不是**故障原因
- `/usr/lib/appFw.so`、`gui.so`、`libNVM.so`、`libSaveLoadSettings.so` 在本机**根本不存在**，不要等它们
- `libObj.so` 不经 IMDB，im.elf 里是**硬编码路径** `/usr/lib/libObj.so`

## 七、跑机命令

```bash
cd /root/fw/oss
EXTRA='-global driver=bionz-streamfb,property=blank-pattern,value=off -global driver=bionz-streamfb,property=format,value=1' \
INITRD=/root/fw/handoff_p0/initrd_q4.img OUT=/mnt/build/tmp/cXXX \
  /mnt/build/run_cap.sh /mnt/build/ab_v419.img 150
pkill -f qemu-system-arm
```
判读：`serial.log` 看 `MODLOAD read ok` 序列 / `program:`+`address:` / `PV i=`；`qemu.out` 看 `ZZGPEREG`/`ZZGPECMD`。
