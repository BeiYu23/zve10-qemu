# ZV-E10 一代（CXD90045 / ASTRA）系统结构测绘

> 本轮目标：**只看结构，不动别人**。另一条线（QEMU 启动）正在改
> `sony_qemu_bionz.patch` / `ZVE10_QEMU_BOOT_PROGRESS.md`，本文件全程**只读**它们引用的原始固件，
> 没有修改、没有触碰任何既有文件（只在 `<repo>/` **新增**了本文件与
> `ZVE10_GEN1_IMDB_TABLE.tsv`）。没有启动 QEMU、没有碰 `<build>`。

**证据基线**（都是机器上现成的、与另一条线共用的原始件）：

| 用途 | 路径 |
| --- | --- |
| 解密解包后的官方固件 Ver.2.03 | `<build>/fw/unpacked_zve10/` |
| `/rootfs`（nflasha7）忠实提取树 | `<repo>/ref_e10/root`（debugfs rdump，保留权限/符号链接） |
| `/usr`（nflasha15）忠实提取树 | `<repo>/ref_e10/usr` |
| 原始分区镜像 | `.../0700_part_image/dev/nflasha{3,5,7,15}` |
| 分区表/挂载表 | `.../0100_config/mount.conf`、`.../0300_partconf/partinf.conf` |
| 升级器镜像（cramfs） | `.../updater.img_unpacked/`（`config/*.xml`、`bin/*.sh`） |

版本标识：`version.txt = 30110_06.2026060501`，`body_version = 100.110.035`，
fdat `model = 0x1030010`，`chassis = CXD90045`。

---

## 1. 一张图看完分层

```
┌── 应用层（老菜单 = ZV-E10 一代自己的 UI） ────────────────────────────────┐
│ /usr/lib/libObj.so            21.3 MB   对象/CamUser 模型，导出 Application_System_Init
│ /usr/lib/viewUnified{2..8}.so 20.7 MB   界面资源解释器/画面（8 个模块）
│ /usr/share/app/*.uxc          299 个     UI 布局/图标/图片（:43 = 4:3 面板）
│ /usr/lib/CautionConfig.so     17.7 MB    提示/配置
│ /usr/sabin, /usr/codec                   ISP / 编解码固件（硬件绑定）
├── 中间件层 ────────────────────────────────────────────────────────────────┤
│ libIMDB.so  ← 217 条记录的模块表（本报告 §6，唯一"启动清单"）
│ OSAL/IPC: libosal_uipc/utm/ulogio  ·  MPR: libmpr.so 13 MB  ·  MWF: libMWF.so
│ 网络/遥控: libInfraRemote 8.7 MB · libInfraPtpControl · libInfraNetConnectionManager …
│ 音视频: libInfraMediaCommon/Detect/Mount · libInfraAvbbc · scenario/*.so（35 个）
├── 内核层 ──────────────────────────────────────────────────────────────────┤
│ Linux 3.0.27_nl-rt106+  #1 SMP PREEMPT RT  (gcc 4.5.1 / Sony CE Linux 8.2.7.6)
│ ARM32, 无设备树（ATAGS 路线）, PAGE_OFFSET=0x60000000
│ 6 个引导期模块在 initrd 内: devno dmac ldec emmc nand lld
│ 103 个功能模块在 /usr/kmod（vermagic 统一 3.0.27_nl-rt106+ SMP preempt mod_unload ARMv7）
│ Sony 私有 /proc/udm/*（init 读 upm_bootmode、drivers）
├── 存储层 ──────────────────────────────────────────────────────────────────┤
│ eMMC: boot0 = nflashaB0（loader, "EXBL" 魔数, DDR training @sector 24）
│ 30 个 nflasha 分区（§3）：/system(3) /rootfs(7) /usr(15) /setting(2) /wbi(5) …
├── 硬件层 (CXD90045 / ASTRA) ───────────────────────────────────────────────┤
│ LIRO/MUSASHI 图像实时核 ← av-cam.bin（魔数 "ORIL"）｜RK / CX88700 / CH88701 协处理器
│ darwin_core.bin（子核启动核）｜sabin（ISP/DSP）｜codec（vcd/bni/bnl 编解码）
│ WiFi BCM(bcmdhd) · BT(bsa_server) · USB(darwin/usbg_*) · HDMI/CEC · GPS/罗盘
└───────────────────────────────────────────────────────────────────────────┘
```

**一句话**：这不是一台"Linux 小电脑"，而是一套 **Sony 自研的单 init + 模块表（libIMDB）驱动**的
相机固件——没有 `/etc`、没有任何 shell 启动脚本，PID 1 是索尼自己写的 C 程序，所有子系统
都由一张 217 条记录、分 9 段的静态模块表按"启动档位"逐段拉起。

---

## 2. 引导链（6 级，逐级有证据）

```
① 上电 → BootROM @0xffff0000
② eMMC 引导块 nflashaB0：loader（pformat.elf 写入；升级脚本 loader_writer.sh 里
   `pformat.elf /dev/nflashaB0`，第 24 扇区写 DDR training 数据）
③ 从 /system(nflasha3, FAT12) 取 vmlinux.bin(4.1 MB, 裸 ARM 镜像) + initrd.img(2.18 MB, ext2)
④ 内核 3.0.27_nl-rt106+ 起来，initrd 挂为 /，跑 /sbin/init
⑤ init：mount /proc /sysfs → insmod /kmod/*（6 个）→ mount /setting /system /tmp /usr
   /log /cert /lens /PMBP … /rootfs → pivot_root /rootfs → mount /usr（ext2→cramfs→vfat 回退）
   → insmod /usr/kmod/*（init 里写死 31 条，逐条带参数）→ exec /usr/bin/bootin.elf
⑥ bootin.elf 读 /setting/mode/dmode 决定档位 → fork /usr/bin/im.elf <后缀>
   im.elf：读 libIMDB.so 模块表 → 逐段 dlopen/insmod → LIRO(/usr/kmod/liro.ko + av-cam.bin)
   → 老菜单应用 /usr/lib/libObj.so（导出 Application_System_Init）
```

证据要点：

| 事实 | 证据 |
| --- | --- |
| initrd 的 `/sbin/init` 与 `/rootfs` 的 `/sbin/init` **同一个二进制** | md5 均 `0ff6dd725d90ebd67ecc44839be58f3f` |
| init 是**自研 C 程序**（非 busybox，带 debug_info 未 strip） | `file sbin/init` = ELF 32-bit LSB ARM EABI5, interpreter `//lib/ld-linux.so.3` |
| init 内部符号 | `mount_partition, ko_insmod, check_qemu, check_bootmode, set_mindrv, check_nfs, run_bg_process, parse_cmdline, profile_entry/exit` |
| init 读 Sony 私有 procfs | 字符串 `/proc/udm/upm_bootmode`、`/proc/udm/drivers`、`/setting/mode/dmode`、`/proc/snsc_boot_time` |
| init 支持 NFS 开发启动 | 符号 `is_nfs/check_nfs` + `IMDB:nfs` profile |
| init 自带 QEMU 分支 | 符号 `check_qemu` + `im.elf` 里的 `IMDB:qemu`/`qemu` 字符串 |
| init 拉起后续 | 字符串 `/usr/bin/bootin.elf`、`/usr/bin/sen.elf`、`/usr/bin/launch_shell.elf`、`/bin/ash --login` |
| bootin 的档位分派 | `bootin.elf { file=<mode_file> } { dmode=… } { amode=… } { target=<im_target> } { sim } { lt\|nolt } { wfree } { killall }`；默认 `file=/setting/mode/dmode`；`/usr/bin/im.elf [s\|g\|r\|u]` |
| im.elf 是模块加载器 | 导入 `IMDB_find_entry/IMDB_get_entries/IMDB_find_target_bit`、`dlopen/dlsym`、`osal_*`；字符串 `/usr/lib/libObj.so` |
| 老菜单应用 = libObj.so | `readelf -sW libObj.so` 导出 `Application_System_Init`；im.elf 直接引用其路径 |

⚠️ 一个**家族构建残留**要注意：`bootin.elf` 会去起 `/usr/bin/qsi.elf`、init 字符串里有
`/usr/bin/net_cmdd.elf`，这两个文件在 ZV-E10 一代上**不存在**（`netblog.elf` 在）。
实测 `im.elf`/`bootin.elf` 都能在缺件时继续（qsi 是新菜单的 quick-start）。

---

## 3. 分区与文件系统（eMMC 布局全表）

来自 `0100_config/mount.conf` + `0300_partconf/partinf.conf`（2020/11/13 生成，Macro Ver. 2011/06/14）。
`partinf.conf` 给的是**绝对偏移**（`start,size,type,flag`），`mount.conf` 给挂载点/大小。

| 分区 | 偏移 | 大小 | 格式 | 挂载 | 角色（实测内容） |
| --- | --- | --- | --- | --- | --- |
| nflasha1 | 0x00010000 | 7.9 MB | vfat | none | **升级器分区**（升级包 `0400_updater/dev/nflasha1` → cramfs/initrd） |
| nflasha2 | 0x00800000 | 20 MB | vfat | `/setting` | 机型/环境/模式标志；初始内容来自 `0800_appli/setting`（DmmConfig.bin、ulogio.bin、facedmy.bin、kemco.txt、`appfw_infra/`） |
| nflasha3 | 0x01C00000 | 48 MB | vfat(FAT12) | `/system` | **内核与固件**：vmlinux.bin、initrd.img、av-cam.bin、bonobo/dfe/lif/wole/ldr_drv、`sabin/`、`lensbin_lensproperty/` |
| nflasha4 | 0x04C00000 | 12 MB | raw | none | MPR Flash Backup |
| nflasha5 | 0x05800000 | 60 MB | **wbi** | `/wbi` | **Warm Boot Image**（镜像 36 MB，魔数 `WBI1` + ARM 代码）→ LIRO 热恢复 |
| nflasha6 | 0x09400000 | 16 MB | vfat | none | Jiritsu（自持/独立模式） |
| nflasha7 | 0x0A400000 | 8 MB | **ext2** | `/rootfs` | 最小根：136 文件 / 86 符号链接 / 4.5 MB（bin、sbin/init、kmod/6、lib/glibc-2.11.2） |
| nflasha8 | 0x0AC00000 | 0 | — | none | 长度 0；`swap_prepare.sh` 的本意是拿它当 compswap 后备（`/dev/nflasha8`，≤60 MB），本机为 0 ⇒ 不启用 |
| nflasha10 | 0x0AC00000 | 12 MB | vfat | `/tmp` | 初始内容 `0800_appli/tmp`（font 缓存、config.spec、toprc、nsswitch.conf、services） |
| nflasha11 | 0x0B800000 | 500 MB | vfat | `/log` | 日志 |
| nflasha12 | 0x2AC00000 | 40 MB | vfat | `/cert` | 证书/网络；rootfs 里 `network -> cert` 符号链接 |
| nflasha13 | 0x2D400000 | 4 MB | vfat | none | Updater work（`/tmp_updater/storage/nonvolatile`，hash/history） |
| nflasha15 | 0x2D800000 | **300 MB** | **ext2** | `/usr` | 用户态主体：**908 文件 / 65 符号链接 / 215.6 MiB** |
| nflasha16 | 0x40400000 | 140 MB | raw | `/android/system` | 家族预留（本机内容为空占位） |
| nflasha17 | 0x49000000 | 100 MB | vfat | `/android/data` | 同上 |
| nflasha18 | 0x4F400000 | 20 MB | vfat | `/lens` | 镜头数据（升级包 `0800_appli/lens/VX*.bin`） |
| nflasha23 | 0x50800000 | 64 MB | prfile | `/PMBP` | `share/pmbp/DeviceInfo.xml` 一类机型信息 |
| 8/9/14/19–22/24–30 | 0x50800000 起 | 0 | — | — | 全部 size=0 的保留槽（`nflasha25/26` 在 mount.conf 里写作 `/MP3` `/BGM`，本机未分配） |

ext2 身份：`/rootfs` UUID `423ca85f-ead4-4c48-9a70-026003d7954f`，
`/usr` UUID `c509f8ed-a432-4966-beb5-25ba065631ea`。
升级包只带 **nflasha3 / nflasha5 / nflasha7 / nflasha15** 四个镜像 + updater（nflasha1 内容），
其余分区由相机本地生成/保留。

---

## 4. `/rootfs`（nflasha7）——没有 `/etc` 的根

```
/            136 文件, 86 链接, 4.5 MB, version.txt=30110_06.2026060501
├─ bin/      53 条目 = 11 个真文件(busybox 433 KB 动态链接 + top/ps/cat/vmstat/hostname/
│            dnsdomainname/chattr/lsattr/compile_et/mk_cmds) + 42 个 busybox 符号链接
├─ sbin/     17 条目 = 4 个真文件(init 28 KB 索尼自研 / dosfsck / mkdosfs / sysctl) + 13 链接
├─ kmod/     devno dmac ldec emmc nand lld   ← 引导期必需，必须放 initrd 同款
├─ lib/      glibc-2.11.2 全家(libc/ld/libpthread/libm/libgcc_s) + libSTMcmdexecute.so + xtables/
├─ root/     chassis("CXD90045") + common.src(升级器公共 shell 函数)
├─ dev/      proc/sysfs/usb/net 等空目录 + ttycons→ttyAM0、ttyBT→ttyAM1 等链接（**无静态设备节点**）
└─ 空挂载点目录：/etc /system /setting /var /log /cert /lens /mnt /root /tmp /tmp_* 
   /initrd /usr …（其中 /initrd 供 init 做 move_partition）
```

要点：**`/etc` 是空的，没有 inittab/rcS/initscript**——启动逻辑 100% 编译在 `/sbin/init` 里；
`/bin/sh` 只在 `launch_shell.elf`（串口救援 shell）时用得上。

---

## 5. `/usr`（nflasha15）——用户态主体

```
/usr  908 文件 / 65 链接 / 215.6 MiB
├─ bin/       50 个：bootin.elf im.elf sen.elf launch_shell.elf scenario.elf mpr_viewLog.elf
│             bk.elf adjstctl.elf gpioctrl.elf Wltestcmd bsa_server dhd wl wpa_supplicant
│             iperf openssl tcpdump 类工具 + 调试/工厂脚本(appdbglv.sh change_mode.sh up.sh
│             usb_insmod.sh compat_firmware.sh doemparse.sh swap_prepare.sh us_remove_modes.sh)
├─ lib/       145 文件 + 58 链接（§6 的模块 + UI + 第三方：curl/expat/ssh2/pcap/pcre/opus/nl/jsoncpp/openssl3）
├─ kmod/      103 个 .ko（驱动/OSAL/网络/USB/音频/显示/日志…）
├─ scenario/  35 个 *.so —— 场景插件（AVBB_SCN_START_* / CAMERA_SCN_* / MPR_SCN_*）
├─ share/     app(299 uxc+3 ltt+ttf+xdb, 81 MB) / aveffect / avsystemsound / data(pub.key,kikilog_pub.pem)
│             / network(dlna,mtp,nvram) / pmbp / startbinary(logo.bin) / terminfo
├─ sabin/     ISP/DSP 固件 77 个（sa_aed/dfdet/dcv/aaclcenc…；/system 里另有一份 4 个）
├─ codec/     vcd_firmware(.bin/_testing) 5.4 MB、bni/bnl_firmware + ipl
├─ tool/      LeakTracer.so / libjemalloc_tsh.so（内存调试与分配器）
├─ local/lib/ 空（新菜单会往这里放 libVssApp.so——本机没有）
├─ wlan/bcm/ · wifi-scripts/  博通 WiFi 固件与启动脚本
└─ sbin/      dhcpd iptables-multi pump tcpdump
```

---

## 6. ★ libIMDB.so 模块表 —— 这套系统的"启动清单"

`/usr/lib/libIMDB.so`（36 KB）只导出 3 个函数（`IMDB_find_entry/IMDB_get_entries/
`IMDB_find_target_bit`）与 1 个数据对象 `imdb_raw`，但 `.data.rel.ro` 里躺着
**9680 字节 = 132 字节 `imdb_raw` 头 + 217 条 × 44 字节记录**（1511 条 `R_ARM_RELATIVE`
重定位把每条记录的字段指到 `.rodata` 的字符串）。本轮已把它**完整解析**并落盘
（解析脚本按 44 B 对齐，故 TSV 前 3 行是 `imdb_raw` 头的原始字节，已标 `kind=meta`）：

- **产物**：`ZVE10_GEN1_IMDB_TABLE.tsv`（220 行，列：`idx section kind flags enable bitmask target path
  init exit suspend resume inact_act on_zve10`）
- 记录字段（按观测）：`flags | enable | bitmask | target | 库/模块路径 | init | exit | suspend | resume | inact | act`
- `flags` 高字节：`0x08..` = **分段头**，`0x10..` = 内核模块，`0x20..` = 用户态条目

**分段结构（217 条记录 = 9 个段头 + 7 条 meta + 201 条模块项；模块项 = 16 个 .ko + 180 个 .so + 5 个外部程序）：**

| 段 | idx | 内容 | 在机数量 |
| --- | --- | --- | --- |
| meta | 0–8 | 6 个 profile：`IMDB:default/qemu/nfs/set/USBJ/RE-SUB` → target bit `1/6/8/16/32/64`；以及 `sim/nfs/usbj/resub/adjust/test` 选项名 | — |
| sec1 | 10–11 | dmm.ko、upm.ko（内存管理 + 电源管理） | 2/2 |
| sec2 | 13–21 | stream/stream2/mcmn_drv/ms_drv/mmc_drv/sata_drv/msen_drv/hdmi/cec | 7/9 |
| sec3 | 23–44 | 底座：osal_uipc/utm/ulogio、backup、OnDemandLoader、kikilog、AccessLog、SysDef、MWF、Jiritsu、GetLog… | 11/22 |
| **sec4** | 46 | **`/usr/kmod/liro.ko`（单条成段 = "LIRO boot" 档）** | 1/1 |
| sec5 | 48–136 | 主体中间件（89 条）：图形(grm/gpe/DDS/uxengine)、触摸/输入、hotshoe、媒体、MPR、网络、蓝牙、USB… | 33/89 |
| sec6 | 138–139 | `InfraMediaPwrMgr` / `InfraCameraBackground`（带 inact/act = 低功耗前后台） | 0/2 |
| sec7 | 141 | `libBizInitializer.so`（`BIZ_Init`） | 0/1 |
| sec8 | 143–146 | **应用层**：`appFw.so`(`Application_System_Init`)、`subapp.so`、`/usr/local/lib/libVssApp.so`、`gui.so` | 0/4 |
| sec9 | 148–218 | 外设/网络服务 + 5 个外部程序：`/usr/bin/{WebApiLauncher.sh,startBTCore.sh,qsi.elf×2,network.sh}`，以及 `libIMStress*.so` 压测库 | 32/71 |
| — | 219 | 表尾记录（`flags=0x00000001`，用途未定） | — |

**关键结论（实测，不是推测）：**

1. 这张表是**家族超集**：201 条模块项里，**只有 86 条在 ZV-E10 一代上真有对应文件，
   115 条不存在**（例如 `libNVM.so`、`libDDS.so`、`libuxengine.so`、`libbkdmn.so`、
   `appFw.so`、`gui.so`、`libInfraWebApi.so`……已用 debugfs 对原始 `nflasha15` 逐个复核）。
   ⇒ 加载器必然按 `target`/机型/选项做门控（`libInfraOptionInstall*` 这一族就是选项安装）。
2. **sec4 只有一条记录且是 liro.ko**，与 `change_mode.sh` 的档位说明
   （`2: kernel boot → global infra → **LIRO boot**`）严格对应。
3. **sec8 的应用入口写的是 `appFw.so`，但 ZV-E10 一代的 `/usr/lib` 没有 `appFw.so`**；
   本机真正的应用是 `libObj.so`（同样导出 `Application_System_Init`），而 `im.elf` 里
   硬编码了 `/usr/lib/libObj.so`。即：**老菜单靠 im.elf 直接加载 libObj.so，
   表里的 appFw.so 是新菜单（ZV-1F / ZV-E10 II）用的条目**。
4. 表里 `/kmod/xxx.ko` 的路径与实际不符（这些 .ko 都在 `/usr/kmod/`，与 init 的 `drivers2`
   列表一致）⇒ **推断**：加载器会给路径加基址前缀（由 im.elf 传入），表里的 `/kmod/` 只是
   逻辑分组名；这一点尚未在二进制里定位到拼接点，标为待验证。

---

## 7. 运行档位（两套模式系统，别混）

### 7.1 相机主系统档位：`/setting/mode/dmode`（`change_mode.sh` 写入）

| 值 | 含义 |
| --- | --- |
| 0 | 只到内核 |
| 1 | 内核 → 建立 global infra |
| **2** | → **LIRO boot**（insmod liro.ko + 载入 av-cam.bin/MUSASHI） |
| **3** | → **appFw boot**（默认，出画面） |
| 4/5/6 | 同上但用 "nounified driver" |
| 7/a/b/d/e/f/10 | 3 档 + 调试：LeakTracer / target gdb / gdbserver / DUMA / eFence / jemalloc / mcheckb |

`p` 档可用 `/setting/mode/preload` 注入任意 `LD_PRELOAD`。

### 7.2 升级器档位：`/setting/updater/mode*`（`common.src`：`MODE_USER=0 PROD=1 SERVICE=2 MINOR=3 MSCRYPT=5 VERSKIP=6`）

前者管"平时怎么开机"，后者管"要不要进升级/服务模式"；两者都在 **`/setting` = vfat 明文分区**，
所以有存储写权限就能切模式（这条已在既有报告里写过，本轮只是确认分区归属）。

---

## 8. 子系统清单（按 libIMDB 段 + /usr/kmod 归类）

| 子系统 | 代表文件 | 说明 |
| --- | --- | --- |
| 内存/电源 | `dmm.ko`+`DmmConfig.bin`、`upm.ko`+`/proc/udm/*`、`libBusPower.so`、`usb_buspwr.ko` | Sony UDM/统一驱动模型；init 直接读 `/proc/udm/upm_bootmode` |
| 存储 | `emmc.ko`(212 KB)、`nand.ko`、`mmc_drv.ko`、`ms_drv.ko`、`sata_drv.ko` | nflasha/emmc 抽象 |
| 图像/相机 | `liro.ko`(78 KB)+`av-cam.bin`、`libObj.so`、`libmpr.so`、`libgrm*.so`、`scenario/*.so`、`sabin/*` | LIRO=载入 MUSASHI 实时核；scenario=场景化流水线 |
| 音视频 | `codec_drv.ko`、`codec/vcd_*`、`snd-*.ko`、`libInfraAvbbc.so`、`share/avsystemsound` | 编解码固件与音频通路 |
| 显示 | `grm_ma.ko`/`grm_gles.ko`、`libgpe.so`、`viewUnified*.so`、`share/app/*.uxc` | 面板由运行时探测，资源按比例取（`_43` = 4:3） |
| 网络 | `bcmdhd.ko`(523 KB)+`wlan/bcm`、`dhd`、`wpa_supplicant`、`libNetService.so`、`dhcpd`、`iptables`、`libInfraIPsec.so` | WiFi/BT 组合方案，含 4G(`libInfraCellularService`, 本机缺) |
| 蓝牙 | `bsa_server`(5.4 MB)、`libInfraBluetooth*.so`、`bt_firm.hcd`/`bt_firm_sar.hcd`、`startBTCore.sh` | Broadcom BSA 栈 |
| USB | `usb_darwin.ko`(Sony USB 控制器)、`usbg_{storage,stillimage,sen,video,audio,buspower}.ko`、`usb_extcmd.ko`、`usb_portMonitor.ko`、`libInfraPtpControl/MtpServer/MscServer/ExtCmdSvc` | PTP/大容量/工厂 EXTCMD 三条通道 |
| 日志/取证 | `kikilog.ko`、`ulogio.elf`、`libGetLog.so`、`libInfraAccessLog.so`、`ujlog.elf` | `share/data/kikilog_pub.pem` 验签日志 |
| 杂项 | `hdmi.ko`/`cec.ko`、`i2c.ko`、`sircs.ko`(红外)、`accel/mag/compass.ko`、`indctr.ko`(指示灯)、`nfc.ko`、`usb_wwan/option/asix/r8168` | 部分为家族共用，本机是否用要按 PRM 能力查询 |

---

## 9. 对"QEMU 起机"这条线的结构性提示（不介入、只列事实）

1. **出货 initrd 就在手边**：`/system(nflasha3)/initrd.img` 是 2.18 MB **ext2**，
   内含 glibc 2.11.2 + `/sbin/init` + 6 个引导期 `.ko` + `/rootfs` `/initrd` 空目录。
   它才是真机走的 initrd（另一条线目前用的是自建 busybox cpio）。
2. **内核期望的外部接口**：`/proc/udm/upm_bootmode`、`/proc/udm/drivers`（Sony UDM 补丁），
   以及 `libIMDB` 的 6 个 profile（`default/qemu/nfs/set/USBJ/RE-SUB`）——init 里带
   `check_qemu`/`check_nfs`，`bootin.elf` 有 `target=<im_target>`。**索尼自己就为 QEMU 留了路径。**
3. **要到"有画面"最小集**：`nflasha3`(内核+initrd+av-cam) + `nflasha7`(/rootfs) +
   `nflasha15`(/usr) + 可空 `nflasha2`(/setting) `nflasha10`(/tmp)；
   LIRO 档还需要 `nflasha5`(`WBI1`) 只在热恢复时用。
   `/wbi` 的 `WBI1` 镜像里带 ARM 代码与 `/proc/cpuinfo` 字样 → 是 LIRO 的快速恢复镜像。
4. **显示仍是硬骨头**：结构上画面 = `grm_ma/grm_gles` 内核模块 + `libgpe` + `viewUnified*.so`
   + `share/app/*.uxc`；QEMU 的 `cxd90045` 模型目前只有 A9MPCORE/UART/SDHCI/L2x0 等
   （另一条线的记录），**没有 GRM/显示控制器**，所以到用户态也只会卡在 `ioctl`。
5. **`/usr` 与 `/rootfs` 版本必须配对**：`version.txt` 两边都是 `30110_06.2026060501`；
   `/rootfs` 的 `kmod/*.ko` 与 initrd 内的是同一批（`devno dmac ldec emmc nand lld`）。

---

## 10. 复现命令（全部只读）

```sh
# 1) 看分区表/挂载表
cat <build>/fw/unpacked_zve10/firmware.tar_unpacked/0100_config/mount.conf
cat <build>/fw/unpacked_zve10/firmware.tar_unpacked/0300_partconf/partinf.conf

# 2) 看两个文件系统是什么
file .../0700_part_image/dev/nflasha{3,5,7,15}
xxd -l 32 .../0700_part_image/dev/nflasha5      # → "WBI1"

# 3) 直接从原厂镜像校核某个文件在不在（不信解包树的话）
debugfs -R "stat /lib/appFw.so" .../dev/nflasha15

# 4) init 的全部字符串与符号
strings -a <repo>/ref_e10/root/sbin/init | grep -E '^/'
readelf -sW <repo>/ref_e10/root/sbin/init | grep -E 'mount_partition|ko_insmod|check_qemu'

# 5) 重生成模块表（需要 readelf）
python3 /tmp/zve10_imdb_tsv.py        # → <repo>/ZVE10_GEN1_IMDB_TABLE.tsv
```

---

## 附：本轮新增文件（未改动任何既有文件）

| 文件 | 说明 |
| --- | --- |
| `ZVE10_GEN1_SYSTEM_STRUCTURE.md` | 本报告 |
| `ZVE10_GEN1_IMDB_TABLE.tsv` | libIMDB 模块表 220 行全解析（前 3 行为 `imdb_raw` 头；含"本机是否存在"列） |
