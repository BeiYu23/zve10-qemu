# 零售启动链与服务清单（回答 QEMU 侧"要补哪些守护进程"）

> 纯离线，基于 nflasha3 / nflasha7 / nflasha15 原始镜像与 10-16 真机快照。
> **结论先说：不存在"263 个用户态守护进程"要补。真正的入口是 `bootin.elf`，
> QEMU 直接跑 `im.elf` 绕过了它。** 用户态服务程序总共只有约 14 个 ELF。

---

## 一、零售启动链（实测）

```
内核 (nflasha3/vmlinux.bin + INITRD.IMG)
  └─ initrd /sbin/init  (984 B, chroot + execl)
       └─ chroot /rootfs (nflasha7)  →  真正的 init
            └─ … → /usr/bin/bootin.elf        ★ 入口
                 ├─ 读 /setting/mode/dmode
                 ├─ 设 dmode= 与 LD_PRELOAD=（来自 /setting/mode/preload）
                 ├─ 尝试起 /usr/bin/qsi.elf     ← 本机型没有，无害
                 └─ 起 /usr/bin/im.elf <模式字母>
                      └─ 之后 bootin 进入 osal_rcv_msg_tmo 监督循环（可重启 im.elf）
```

证据：

| 项 | 内容 |
|---|---|
| `nflasha3/INITRD.IMG` | **2,183,168 B，ext2**，版本 `30110_06.2026060501` |
| initrd `/sbin/init` | **28,580 B**（早期误记为 984 B），内含 `<<insmod start>>` 模块表与分区挂载回退逻辑 |
| initrd 内目录 | 只有 `/dev /kmod /lib /proc /rootfs /sbin`（**无 /bin、无 /etc**） |
| `nflasha7`(/rootfs) | 有 `/bin /etc /kmod /lib /lens /log …`（`/etc` 实测为空） |
| `/usr/bin/*.elf` | 见下（nflasha15） |

## 二、★ `bootin.elf` 才是入口（QEMU 现在跑的是 im.elf）

`bootin.elf`（15,336 B）关键字符串与反汇编：

```
用法:  bootin.elf { file=<mode_file> } { dmode=<debug_mode> }
  <mode_file> 默认 /setting/mode/dmode
  <im_target>: im.elf target=<...>
  killall : kill existing bootin.elf

它设置的环境:
  dmode=...
  LD_PRELOAD=...           ← 从 /setting/mode/preload 读入
  "Bb clearing env"

它拉起的进程:
  /usr/bin/qsi.elf &        ← ★ ZV-E10 上不存在（全盘搜索 0 命中，仅 bootin 自身引用）
  /usr/bin/qsi.elf noevent0 &
  /usr/bin/im.elf
  /usr/bin/im.elf s &
  /usr/bin/im.elf g &       ← gdb
  /usr/bin/im.elf r &       ← 可能是 RTOS 相关
  /usr/bin/im.elf u &
  /usr/bin/im.elf l &       ← LeakTracer
  /usr/bin/im.elf i &
  /usr/bin/im.elf a &
```

`main`(0x9db1) 结构：

```
解析参数 → 失败则退出
初始化 (0x92dc)
再校验 (0x991c)
sleep(1)
0x9404  ← 决定并启动目标（内部按 dmode 字符分支：'0'/'1'/'2'/'3'/'p' …）
然后进入监督循环: osal_rcv_msg_tmo → 0x9bdc 处理
   循环条件 [r3+4] != 3（某个状态变量变成 3 才退出）
```

dmode 字符分支实测（0x948e 起）：

```
'0'(0x30) → 0x94e6     '1'(0x31) → 0x94ea
'2'(0x32) → 0x94ee     '3'(0x33) → 0x94f2
'p'(0x70) → 0x94a8     （对应"自定义 LD_PRELOAD"路径）
```

⇒ 这与外部资料一致（dmode 0/1/2 = 分别停在 内核后/infra 后/RTOS 后，3 = 正常，
`p` = 自定义 preload）。

## 三、用户态服务程序全清单（`/usr/bin`，共 36 项）

```
★ 启动/主程序:  bootin.elf   im.elf   sen.elf
★ 日志类:      ulogio.elf   ujlog.elf   mpr_viewLog.elf   btblog.elf   netblog.elf
监控/命令:      mpr_monio.elf  mwf_monio.elf  rcvcmd.elf  sndcmd.elf  testcmd.elf
调试/工具:      launch_shell.elf  adjstctl.elf  scenario.elf  gpioctrl.elf
               GPIO2SDIO.elf  SDIO2GPIO.elf  SetPort.elf  crypter.elf
               ud_datcnv.elf  ud_send_lsi.elf  bk.elf  NsBkSet.elf
               btRFTest.elf  compat_firmware.sh
脚本:          change_mode.sh  up.sh  appdbglv.sh  doemparse.sh
               startBTCore.sh  swap_prepare.sh  usb_insmod.sh  us_remove_modes.sh
```

**注意**：`qsi.elf` 不在这份清单里，也**不在任何分区**上 ⇒ bootin 对它的尝试必然失败
（本机型特性，不影响启动）。

## 四、内核模块清单（真机 lsmod，43 个，按加载序）

```
devno dmac ldec emmc osal_utm osal_uipc utimer backup osal_ulogio input
compat udate indctr i2c sircs stream stream2 usbg_sen usb_extcmd kikilog
dmm upm grm_ma grm_gles accel mag compass mcmn_drv ms_drv mmc_drv
usbg_stillimage dma330 codec_drv liro usbg_buspower usb_darwin usb_buspwr
hdmi cec usbg_storage cfg80211 mmc_core bcmdhd
```

（尺寸见 `zve10_delivery/a_p2/p2_misc.txt` 的 `$ lsmod` 段。）

## 五、"263 个进程"是怎么回事

真机 `ps` 快照**我们手上没有**（`a_p2/p2_misc.txt` 只抓了 mount/lsmod/ifconfig/
ld.so.preload 四类命令）。但按上面证据，"263"几乎不可能是用户态进程数：

- 用户态服务程序总共约 14 个 ELF
- 已加载内核模块 43 个（每个模块常带 1~数个内核线程）
- `/proc/tmonitor` 显示 **60+ RTOS 任务**（liro/编解码/流等）

⇒ 263 ≈ 用户态(14) + 内核线程(43 模块派生) + RTOS 任务(60+) + 各类 worker。
**不是 263 个要拉起的服务。**

## 六、给 QEMU 侧的可执行建议

1. **改跑 `bootin.elf`，不要直接跑 `im.elf`** —— 它负责：
   - 读 `/setting/mode/dmode`（QEMU 里需要有这个文件；dmode 内容 `3` = 正常）
   - 设置 `dmode=` 与 `LD_PRELOAD=`（**这可能是 App 初始化分支的关键差异**）
   - 给 `im.elf` 传模式字母
2. 若不便跑 bootin，则**手工复现它的环境**：`dmode=3`、`LD_PRELOAD=`（空或按 preload 文件）、
   并给 `im.elf` 传对应字母
3. 补 `ulogio.elf`（日志服务，`osal_ulogio` 模块存在；alog 由它写入）—— 这也解释了
   为什么之前抓的 alog 环全零
4. `qsi.elf` 不用管（本机没有）
5. 内核模块 43 个按上面顺序加载（QEMU 已加载 33 个，可对照差集）

## 七、仍缺的真机证据（需硬件）

- `busybox ps` 全量（确认用户态进程数）
- `im.elf` 的 `/proc/<pid>/maps`（确认 view*.uxc 是否被映射）
- `alog` 环（UI 起来后抓）

采集脚本已备好：`tools/z10_capture.sh`（在相机服务 shell 里跑，一次抓齐）。
