# 启动必需模块清单（真机零售 initrd 的 insmod 顺序）

> 来源：`nflasha3/INITRD.IMG`（ext2，零售 initrd）里的 **`/sbin/init`（28,580 B）**，
> 程序内有 `<<insmod start>>` / `<<insmod end>>` 标记与完整模块表。
> 需要最小混合集时以此为准。

## ★ 更正

早前文档说零售 initrd 的 `/sbin/init` 是"984 B 的 chroot 启动器"——**错了**。
实测 `ls -l /sbin` → `init 28,580 B`、`dosfsck 64,790 B`。它装载模块、按序挂载分区、
最后 chroot，是有实质逻辑的 init。

## 一、initrd 自带的模块（`INITRD.IMG:/kmod`，6 个）

```
nand.ko 152,212   lld.ko 78,636    dmac.ko 16,896
emmc.ko 212,324   ldec.ko 15,812   devno.ko 4,368
```

## 二、`/sbin/init` 的 insmod 顺序（35 条，实测字符串表）

```
 1  /kmod/devno.ko              19  /usr/kmod/usb_extcmd.ko
 2  /kmod/dmac.ko               20  /usr/kmod/kikilog.ko
 3  /kmod/ldec.ko               21  /usr/kmod/dmm.ko
 4  /kmod/emmc.ko               22  /usr/kmod/upm.ko
 5  /usr/kmod/osal_utm.ko       23  /usr/kmod/nfc.ko          ✗ 盘上无
 6  /usr/kmod/osal_uipc.ko      24  /usr/kmod/grm_ma.ko
 7  /usr/kmod/utimer.ko         25  /usr/kmod/grm_gles.ko
 8  /usr/kmod/backup.ko         26  /usr/kmod/accel.ko
 9  /usr/kmod/osal_ulogio.ko    27  /usr/kmod/mag.ko
10  /usr/kmod/input.ko          28  /usr/kmod/compass.ko
11  /usr/kmod/udate.ko          29  /usr/kmod/mcmn_drv.ko
12  /usr/kmod/indctr.ko         30  /usr/kmod/ms_drv.ko
13  /usr/kmod/i2c.ko            31  /usr/kmod/mmc_drv.ko
14  /usr/kmod/sircs.ko          32  /usr/kmod/sata_drv.ko     ✗ 盘上无
15  /usr/kmod/stream.ko         33  /usr/kmod/msen_drv.ko     ✗ 盘上无
16  /usr/kmod/stream2.ko        34  /usr/kmod/dma330.ko
17  /usr/kmod/usb_portMonitor.ko 35 /usr/kmod/codec_drv.ko
18  /usr/kmod/usbg_sen.ko
```

**注**：`nfc.ko` / `sata_drv.ko` / `msen_drv.ko` 在本机 `/usr/kmod` **不存在**
（家族超集，同 IMDB 的 108 条缺失）——init 显然容忍装载失败并继续。

## 三、init 的挂载回退逻辑（同文件里实测）

```
/usr   : ext2 → ext2(no-compress) → cramfs → vfat
/system: compressed → vfat
/setting: 直接挂
失败会打印 "mounting ... failed. errno(N)" 到控制台
```

⇒ **对 QEMU 有用**：如果 /usr 的 ext2 挂不上，它会去试 cramfs/vfat，
日志里能看到具体走了哪条分支。

## 四、后加载的模块（chroot 之后）

### 4.1 由 im.elf 按 IMDB 装载（16 条 `kind=kernel-module`）

```
idx 10  dmm.ko        idx 15  mcmn_drv.ko    idx 20  hdmi.ko
idx 11  upm.ko        idx 16  ms_drv.ko      idx 21  cec.ko
idx 13  stream.ko     idx 17  mmc_drv.ko     idx 46  liro.ko
idx 14  stream2.ko    idx 18  sata_drv.ko    idx 155 usbg_storage.ko
                      idx 19  msen_drv.ko    idx 156 usbg_stillimage.ko
                                             idx 157 usbg_sen.ko
                                             idx 158 unified_jet.ko
```

### 4.2 由脚本/服务装载

`/usr/bin/usb_insmod.sh` 装 USB 相关：
```
/kmod/usb_event.ko  /kmod/usb_scd.ko   /kmod/ups.ko
/kmod/usb_otgcore.ko /kmod/usb_gadgetcore.ko
/kmod/usb_portMonitor.ko /kmod/usbg_sen.ko /kmod/usb_extcmd.ko
```

其余（`compat` / `usbg_buspower` / `usb_darwin` / `usb_buspwr` /
`cfg80211` / `mmc_core` / `bcmdhd`）不在 IMDB 表里，由对应服务按需装载
（`bcmdhd`+`cfg80211` 是 WiFi，`mmc_core` 是 MMC 栈依赖）。

## 五、给"最小混合集"的分层建议

| 层 | 模块 | 说明 |
|---|---|---|
| **T1 引导必经** | `devno` `dmac` `ldec` `emmc` `nand` `lld` | initrd 内，没有它连 /usr 都挂不上 |
| **T2 UI 前必需** | `osal_utm` `osal_uipc` `utimer` `backup` `osal_ulogio` `input` `indctr` `i2c` `sircs` `kikilog` `dmm` `upm` `grm_ma` `grm_gles` `mcmn_drv` `ms_drv` `mmc_drv` `dma330` `codec_drv` `stream` `stream2` | 真机 lsmod 里都在；缺任何一个 UI 都起不来 |
| **T3 硬件强绑定** | `udate` `liro` `hdmi` `cec` `accel` `mag` `compass` `usbg_*` `bcmdhd` `cfg80211` | **这些最可能在仿真机上 trap**（`udate` 已经是实测崩点） |
| **T4 缺失容忍** | `nfc` `sata_drv` `msen_drv` | 盘上本来就没有 |

**建议顺序**：先用 T1+T2 的原厂模块跑起来；`udate` 换成我们的 stub；
若 dmm/grm_ma/codec_drv 也 trap，再逐个换。**T3 里越靠后的越可以先不装。**

> 注意：`grm_gles` 是我们一直用 fake 替换的那个——它正好在 T2 里，
> 说明"替换 grm_gles 不影响 UI 起来"这件事是有依据的（真机 UI 不需要真实 GPU 命令流）。
