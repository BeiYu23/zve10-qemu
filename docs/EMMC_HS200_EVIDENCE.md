# 真机 eMMC 走 HS200；仿真机不是 —— 很可能是"卡在装载阶段"的真因

> 回答 QEMU 侧的观察：`AL_EMMC_SEQ ... Software Tuning is forbiden for non-HS200`
> 反复出现，是不是仿真 eMMC 时序与真实不符？

## 一、真机 klog 的对照证据（`klog_mem_COLDBOOT_1M.bin`，1 MB 冷启动日志）

```
[    0.065864] eMMC: HS200 mode
[    0.067143] nflasha: detected capacity change from 1954545664 to 1979711488
[    0.067180] nflashaB0: detected capacity change from 4194304 to 2097152
[    0.067212] nflashaB1: detected capacity change from 4194304 to 2097152
[    0.812740] eMMC: HS200 mode      （暖启动/唤醒时再次出现）
[    0.832309] eMMC: HS200 mode
[    0.832552] eMMC: HS200 mode
```

关键点：

| 检查 | 真机 | 仿真机（据 QEMU 侧） |
|---|---|---|
| `eMMC: HS200 mode` | **出现 4 次** | 未出现 |
| `AL_EMMC_SEQ` / `Software Tuning is forbiden for non-HS200` | **0 次** | 反复出现 |
| eMMC 初始化后 | 0.067 s 就辨识出 nflasha / nflashaB0 / nflashaB1 容量 | 停在装载阶段 |

⇒ **真机的 eMMC 一上电就在 HS200 模式**（8-bit + 200 MHz），驱动因此走
硬件 tuning；仿真的 eMMC 不是 HS200，驱动只能走软件 tuning，而软件 tuning
对 non-HS200 **被明确禁止**，于是反复重试/告警。

**这条很可能才是"卡在模块装载/挂载阶段"的真正原因**——不是模块缺失，
而是底层块设备没有真正可用（或极慢），导致后续 insmod/挂载全部拖住。

## 二、建议的验证顺序

1. **先确认块设备本身是否可读**：在 QEMU 里 `dd if=/dev/nflasha of=/dev/null bs=4096 count=100`
   计时。若极慢或卡住 ⇒ 是 eMMC 模型问题，优先修它，别再折腾模块。
2. **给 eMMC 模型补 HS200 能力**（或至少让驱动相信它是 HS200），
   目标是让 klog 里出现 `eMMC: HS200 mode`、且不再出 tuning 禁止告警。
3. 只有在块设备确认可用之后，才继续"模块级 trace"（`MODLOAD attempt` 打名字）。

## 三、附带发现：updater initrd 里有 Darwin 固件脚本

`nflasha1/boot/INITRD.IMG`（cramfs，4,943,872 B，**updater 的** initrd，不是零售的）
内部含一整套 shell 脚本（名字未压缩、内容 cramfs 块压缩）：

```
insmod.sh  UdtrMain.sh  execute_prod.sh  execute_ufp.sh  hash_check.sh
hash_drop.sh  hash_update.sh  is_valid_boot.sh  mkramdsk.sh
prepare-usb.sh  up.sh  updater_log.sh  updater_log_rotate.sh
updater_storage_backup.sh  updater_storage_mount.sh  updater_storage_restore.sh
★ us_darwin_firm.sh     ★ us_darwin_init.sh
```

⇒ **`us_darwin_firm.sh` / `us_darwin_init.sh` 说明 updater 阶段会更新 Darwin
（电源 MCU）固件**。这对我们"Darwin 会话限制"那条线是直接相关的线索：
脚本里很可能写着 SIO 的初始化顺序、固件地址、握手方式。
（内容需要 cramfs 解压才能读；两块小文件，后续可专门解。）

## 四、关于"零售 initrd 的 insmod.sh"

**不存在**。零售 initrd（`nflasha3/INITRD.IMG`，ext2）里**没有 `/bin` 目录、
没有任何 `.sh` 文件**。模块装载逻辑是**编译进 `/sbin/init`（28,580 B）**的：

```
符号 : insmod_drivers / ko_insmod
字符串: <<insmod start>>  /  init %s:(%d): error(%d): insmod %s %s  /  <<insmod end>>
```

⇒ 没有脚本原文可以给；但**那 35 条的顺序就是 `init` 里的实际顺序**
（见 `docs/MODULE_BOOT_LIST.md`），直接照它写一个带日志的 wrapper 即可。

`insmod.sh` 只存在于**updater 的 cramfs initrd**里，加载的是 updater 阶段的
模块集，与零售启动链不同——**不要照抄它**。
