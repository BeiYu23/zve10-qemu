# 让 QEMU 的 eMMC 模型对齐真机：实测寄存器与卡身份

> 目的：QEMU 侧的仿真卡报 `SDR mode` + `Software Tuning is forbiden for
> non-HS200`，而真机是 `HS200 mode`。本文给出真机的**实测值**，
> 让模型能报出同样的身份与能力。

## 一、真机控制器寄存器（stage2 实测，`emmc-success.md`）

```
CAPABILITIES   0x64EEC000     base clock 192 MHz（bits15:8 = 0xC0）, bit23 = embedded slot
HOST_VERSION   0x0102         SD Host 3.0
PRESENT_STATE  0x01FF0000     CARD_INSERTED=1
HOST_CONTROL   0x34           8-bit + high-speed
POWER_CONTROL  0x0B
```

## 二、真机卡身份（CMD1/CMD2/CMD9 实测）

```
OCR   0xC0FF8080      ready=1, CCS=1（扇区寻址）
CID   f1b6986a 345201da 34465445 00150100
CSD   ef8e4040 fff6dbff 320f5903 00d02701
RCA   1
R1    0x00000900      state=TRAN(4), READY_FOR_DATA=1
```

⇒ **仿真模型应直接报这组 CID/CSD/OCR**。QEMU 侧的日志里出现
`eMMC: 1888MiB, Unknown PEF1G4 (rev 0x4d...)` 说明模型自造了一个卡身份，
驱动走的是"未知卡"分支——这本身就可能让人走不到 HS200 路径。

## 三、★ HS200 的两道门（Sony `emmc.ko` 里的原文告警）

从 initrd 的 `/kmod/emmc.ko`（212,324 B）提取：

```
Warning : Software Tuning is forbiden for non-HS200 mode
Warning : HS200 mode is not enabled in device controller
```

⇒ 驱动分别检查**卡**与**控制器**：

1. **卡侧**：`switch_to_hs200_mode` / `set_hs_timing_interface` /
   `write_ext_csd_byte` / `set_ext_csd_bits` —— 即通过 **CMD6 写 EXT_CSD**
   的 `HS_TIMING` 与 `DEVICE_TYPE`。
   ⇒ **模型的 EXT_CSD 必须声明支持 HS200**，且 CMD6 写 `HS_TIMING=2` 要成功。
2. **控制器侧**：`HS200 mode is not enabled in device controller`
   ⇒ 主机控制器必须有对应的能力位/使能位（SDHCI 3.0 下 HS200 走厂商寄存器，
   真机 `HOST_VERSION=0x0102` 是 SD Host 3.0）。

相关驱动符号（便于定位）：
```
set_tuning_point  current_tuning_point  retuning_mode  force_retuning_mode2
execute_tuning  start_tuning  stop_tuning  tuning_succeed
set_hs200_tuning_block_size  switch_to_hs200_mode  init_emmc_access_mode
read_ext_csd  read_ext_csd_for_init  emmc_seq_read_ext_csd  print_ext_csd
verify_speed_mode  emmc_seq_switch_to_hs_mode
```

## 四、还缺的一项：真机的 EXT_CSD

模型的 EXT_CSD 目前是自造的。**真机的 EXT_CSD 我们还没读**——但我们的
stage2 有能力读（`CMD8 SEND_EXT_CSD`，512 字节，走同一条 PIO 路径）。

**建议排进下次硬件会话**：用 payload 发 CMD8 把真机 EXT_CSD 读出来存档，
然后 QEMU 模型直接照抄 —— 这样 `verify_speed_mode` / HS200 判定的输入
就与真机完全一致，不用猜。

## 五、其他被模型报错的项（真机没有）

QEMU 侧日志里这些行，在真机 klog 中**均不出现**：

```
BKOPS / HPI / Sanitize not supported
Discrepancy between OCR/EXT_CSD
eMMC: 1888MiB, Unknown PEF1G4
```

⇒ 它们都是"模型能力不足"或"卡身份不对"的副产物；把上面两组值对齐后
应当自然消失。对齐后仍出现才需要单独处理。
