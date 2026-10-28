# ZV-E10 权威分区表（真机 sysfs 实读，2026-10-28）

> 来源：agent 读 `/proc/partitions` + `/sys/class/block/*/start|size` + `/proc/self/mountinfo`
> **不是推测，是真机数据。**

## 一、完整分区表

| 设备 | 起始扇区 | 字节偏移 | 大小 | 挂载点 / 用途 |
|---|---:|---|---:|---|
| `nflasha` | 0 | 0x00000000 | 1888 MB | 整盘（1,933,312 KB） |
| `nflasha1` | 128 | 0x00010000 | 8 MB | — updater 载荷（含 `boot/initrd.img`、`boot/kemco.txt`） |
| **`nflasha2`** | 16384 | **0x00800000** | 20 MB | **`/setting`** |
| **`nflasha3`** | 57344 | **0x01C00000** | 48 MB | **`/system`** |
| `nflasha4` | 155648 | 0x04C00000 | 12 MB | —（FAT，未挂载） |
| `nflasha5` | 180224 | 0x05800000 | 60 MB | —（未挂载） |
| `nflasha6` | 303104 | 0x09400000 | 16 MB | `/tmp/jiritsu` |
| **`nflasha7`** | 335872 | **0x0A400000** | 8 MB | **`/`（rootfs）** |
| **`nflasha10`** | 352256 | **0x0AC00000** | 12 MB | **`/tmp` + `/etc`（同一分区挂两次）** |
| **`nflasha11`** | 376832 | **0x0B800000** | 500 MB | **`/log`** |
| **`nflasha12`** | 1400832 | **0x2AC00000** | 40 MB | **`/cert`** |
| `nflasha13` | 1482752 | 0x2D400000 | 4 MB | —（未挂载） |
| **`nflasha15`** | 1490944 | **0x2D800000** | **300 MB** | **`/usr`** ← 移植目标 |
| `nflasha16` | 2105344 | 0x40400000 | 140 MB | —（未挂载，Linux 视角全零） |
| `nflasha17` | 2392064 | 0x49000000 | 100 MB | —（未挂载，Linux 视角全零） |
| **`nflasha18`** | 2596864 | **0x4F400000** | 20 MB | **`/lens`** |
| `nflasha23` | 2637824 | 0x50800000 | 64 MB | — PMBP/PMHOME（大容量模式暴露） |
| `nflashaB0` | — | — | 1 MB | eMMC boot 分区 0（硬件分区） |
| `nflashaB1` | — | — | 1 MB | eMMC boot 分区 1（硬件分区） |
| `mmca10` | — | — | 58 GB | SD 卡 |

## 二、直接回答：`/system` 没有独立分区

```
mountinfo 原文:
  15 11 252:3 / /system rw,noatime - vfat /dev/nflasha3 rw,sync,dirsync,...

⇒ /system 就是 nflasha3（48 MB vfat），既是 FAT 启动区又是 /system，同一个分区
⇒ 不存在第二个"system"分区
```

### `/system` 实际内容（agent 实读）

```
av-cam.bin        17,289,388   CP/DSP 固件 —— 平台层核心，绝不可动
bonobo.bin           172,144
bt_firm.hcd           61,787   蓝牙固件
dfe_app.bin            1,956
dfe_dat.bin           65,536
initrd.img         2,183,168   init（launch_shell）所在
ldr_drv.bin            7,012
lensbin_lensproperty/           镜头光学矫正表
lif_app.bin            6,716
sabin/                          DSP 算法固件（sa_*.bin）
vmlinux.bin        4,117,760   内核
wole_app.bin          10,848
```

## 三、四机型 /usr 分区大小对比

| 机型 | nflasha15（/usr） | nflasha3（/system） | nflasha5 |
|---|---:|---:|---:|
| ZV-E10 | **237 MB** | 28 MB | 34 MB |
| ZV-1F | 341 MB | 26 MB | 32 MB |
| ZV-1M2 | **505 MB** | 27 MB | 40 MB |
| ZV-E10M2 | 726 MB | 30 MB | 217 MB（+nflasha8 22 MB） |

**虽然 /usr 大小差 2 倍，但 Linux 可用内存三者相同（141.7 MB，见 MEMORY_HYPOTHESIS_REFUTED.md）
⇒ /usr 大小差异只说明固件内容量不同，不代表运行内存不同。**

## 四、未挂载分区清单（找"还原源"时要重点查这些）

```
nflasha4    12 MB    FAT，未挂载
nflasha5    60 MB    未挂载
nflasha13    4 MB    未挂载
nflasha16  140 MB    未挂载，Linux 读出的块全零
nflasha17  100 MB    未挂载，Linux 读出的块全零
nflasha23   64 MB    PMBP/PMHOME（相机插 USB 大容量模式时暴露给电脑）
nflashaB0/B1 各 1 MB  eMMC 硬件 boot 分区
```

**⇒ 如果存在"开机还原 /usr 超块"的机制，备份最可能在这几个未挂载分区里，
   或者根本不在 eMMC 上（而在 CP/RTOS 侧或 boot1 引导器里）。**
