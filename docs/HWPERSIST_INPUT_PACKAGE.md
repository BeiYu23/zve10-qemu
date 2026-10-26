# 硬件持久化审计：提供给审计线的信息包

> 用途：协助"硬件持久化写入检查"专项审计。
> 收录在 `docs/hwpersist_input/`，与审计线自建的 `docs/hwpersist/` 分开，互不改动。

## 一、交付文件

```
docs/hwpersist_input/IMDB_REGISTRY_M2.txt          6,539 B   (198 libs / 87 init / 42 exit)
docs/hwpersist_input/DEVICE_ACCESS_MATRIX_M2.txt   9,917 B   (57 个库)
```

**⚠️ 两份均为字符串级证据：只证明串存在于该二进制，未证明调用点、时序、是否真写入。**

## 二、供体启动链与初始化入口

```
initrd /sbin/init
  → chroot /rootfs
  → /usr/bin/bootin.elf        （读 /setting/mode/preload；白名单 4 项）
  → /usr/bin/im.elf            （DT_NEEDED 含 libIMDB.so ⇒ 启动期必加载）
       → libIMDB 按注册表 init（198 项，87 init / 42 exit）
             → appFwBoot.so → appFw.so
       → libObj.so / appFw.so 为运行时 dlopen（libIMDB 的 NEEDED 无 libObj）
```

**⚠️ 未解析二进制配对表 ⇒ `lib ↔ init ↔ exit` 对应关系未确定，仅名字集合。**

### 2.1 与持久化相关、且启动即 init 的注册项

```
InfraUpdaterDPro_init / InfraUpmPro_init
libupdater0604.so / libupdaterufp.so / libosal_udate.so
libInfraExtBackupCommunicator.so / libbackup.so
libSaveLoadSettings.so / libNVM.so / libUniqIDGen.so
libI2CRight.so / libi2cuipc.so
libInfraFsys.so / libFsysPosixWrapper.so
```

**"注册了服务" ≠ "init 里会写 flash"，需反汇编确认。**

## 三、分区级写入嫌疑（优先查）

| 库 | 目标 | 说明 |
|---|---|---|
| **`libsencore.so`** | `/dev/nflasha5` | **`/wbi` 分区（60 MB）** |
| **`libWlanAdjust.so`** | `/dev/nflasha23/INFO/WIFI_INF.TXT`<br>`/dev/nflasha23/INFO/WPS_PIN.TXT` | **`/PMBP` 分区，写具体文件** |
| `appFw.so` | `/dev/nflasha23`、`/dev/nflasha30` | PMBP + 未知 |
| `libInfraMediaCommon.so` | `/dev/nflasha*`、`/dev/mcmn`、`/dev/ioexp`、`/dev/dma330` | 多种硬件 |

## 四、原始内存 / 总线访问（最高权限类）

| 库 | 入口 | 备注 |
|---|---|---|
| 多库 | **`/dev/mem`** | 可写任意物理内存 |
| `libInfraSKD.so` / `libInfraCryptCore.so` | **`/dev/spidev0.0`** | **SPI —— 通往 MCU/EEPROM 的典型路径** |
| `libInfraMediaCommon.so` | `/dev/ioexp`、`/dev/mcmn` | I/O 扩展 |
| `libmpr.so` / `libInfraFsys.so` / `libInfraRemote.so` | `/dev/dma330`、`/dev/dmac` | DMA 内存搬运 |
| `libsencore.so` / `libObj.so` / `libInfraUsbCmn.so` | `/dev/gpio` | |
| `libsencore.so` | `/dev/mode` | |

## 五、`/setting` 写入候选

```
libObj.so         /setting/gps/assistme.dat  /setting/mode/cmode  /setting/sen/smode  /setting/.hasp
appFw.so          /setting/acctime1.bin  acctime2.bin  facedmy.bin  facerec
                  /setting/sen/smode  /setting/sig.txt
libsencore.so     /setting/.etools  /setting/.hasp  /setting/sen/smode
libupdaterufp.so  /setting/updater/dat4  /tmp_updater/Firm.dat
                  /tmp_updater/updater/update/accsy/ACCYFIRM.BIN
libInfraIPTC.so   /setting/IPTC_001.XMP
CameraOnDemandSaver.so  /setting/psd.bin
libGetLog.so      /setting/kemco.txt
```

**★ `/setting/updater/dat4` 是版本号记录（`up.sh modver` 写入对象）。**

## 六、更新相关函数

```
libupdaterufp 导出:
  udtr_ufp_init / udtr_ufp_exit
  udtr_get_firm_info_inmedia      ← 从 media 读固件信息
  udtr_write_firm_from_media      ← 从 media 写固件
  udtr_cmd_search_chank
  udtr_alloc_dmm / free_dmm / copy2dmm

crypter.elf (39 KB) 走 Dec_Scramble 解密写入
up.sh 用 /setting/updater/mode* 标志: mode / mode1 / mode3 / mode4 / mode5 / mode6
```

## 七、样本路径

```
/mnt/build/fw/ref_m2/                        ZV-1M2 /usr 保真树
/mnt/build/fw/ref_m2/{lib,kmod,bin,share}/
/mnt/build/fw/unpacked_zv1m2/firmware.dec    解密 UDTRFIRM（624,726,016 B）
/mnt/build/fw/unpacked_zv1m2/firmware.tar    101 项
/mnt/build/fw/unpacked_zv1m2/0700_part_image/dev/nflasha15   530,345,984 B
/root/fw/ref_e10/usr/lib/                    ZV-E10 老菜单对照
/root/fw/zve10_delivery/*.img                15 个分区 + boot 区 + 整盘
```

**分区映射：**
```
nflasha2  → /setting  20,971,520        nflasha5  → /wbi     62,914,560
nflasha7  → /rootfs    8,388,608        nflasha11 → /log    524,288,000
nflasha12 → /cert     41,943,040        nflasha15 → /usr
nflasha18 → /lens     20,971,520        nflasha23 → /PMBP    67,108,864
nflasha30 → （未挂载）
```

## 八、建议优先查的五条（按风险）

```
① libsencore.so → /dev/nflasha5 (/wbi)         独立分区写
② libWlanAdjust.so → /dev/nflasha23/INFO/*      PMBP 写具体文件
③ /dev/spidev0.0 (libInfraSKD / libInfraCryptCore)   MCU/EEPROM 典型路径
④ libNVM.so / libSaveLoadSettings.so 的 init 是否写 NVM/flash
⑤ 更新服务 init 是否检查媒体并自动开始写
```

**⚠️ 全部只是"字符串存在"。要确认启动期是否真写，必须反汇编对应 init 函数看调用链。**
