# fastboot 操作手册（现行可用版，2026-10-28 核对过源码）

> 本文每一行都对着仓库源码核过。**旧文档里推荐过 `host/fb_partition.py`，它已经跑不了**
> （依赖 `stage2/build/stage2.bin`，该目录不存在）—— 本文给出替代工具。
> 出处：`fastboot/README.zh-CN.md`、`pico/PROTOCOL.md`、`docs/usb-success.md`、
> `docs/recovery.zh-CN.md`、`docs/rounds/TOMORROW_RUNBOOK.md`，以及
> `host/{bootstrap,flash_partition,verify_partition}.py` 的实际代码。

---

## 0. 硬件与前提

| 项 | 要求 |
|---|---|
| Pico | RP2040，固件 `zve10_pico_od_reset` v15（`zve10_uart_od 15`），插上后 `/dev/ttyACM0` |
| 接线 | `GP28 → D−(A7)`；`GP27 → 470Ω → D+(A6)`；`5V → VBUS`；`GND`；`CC1/CC2` 并接 5.1k 下拉；`GP29` 悬空 |
| 串口参数 | Pico 侧已固定 48000 8O1 + carrier 常开，脚本不需要传 |
| 相机 | **装电池、开关 ON** |
| 主机 | root（libusb 权限）；`python3` + `pyusb` |
| 必须 | `mkdir -p pico/logs logs`（少了注入脚本会在写日志时崩） |

```bash
cd /root/fw/oss/fastboot
mkdir -p pico/logs logs
```

## 1. 注入 stage1（Pico → 相机，串口）

```bash
python3 pico/inject_payload.py \
    --port /dev/ttyACM0 \
    --file prebuilt/stage1.bin \
    --duration 600
```

- `--duration` = **反复重放整组的秒数**，不是超时。整组重放才能覆盖相机上电后的接收窗口，给足 600 秒最稳。
- 参数默认值：`--record-gap 0.0144`、`--group-gap 0.05`、`--reset-delay 0.1`、`--no-reset`（关掉 GP27 复位）。
- 日志写到 `pico/logs/inject-payload-*.json`。
- 成功后 stage1 在 SRAM 里跑起来，相机 **长闪**（LED）。

灯码对照（`pico/PROTOCOL.md` §7）：

| 灯 | 含义 |
|---|---|
| 1 长 7 短 | 正常（自证明 payload） |
| 1 长 2 短 | unlock token 不对 |
| 1 长 3 短 | CRC 错 |
| 1 长 4 短 | 记录不完整 / 同步失败 |
| 0 长 13 短 | 奇校验错 |

## 2. 换线（最容易忘的一步）

**看到长闪后，把相机 USB-C 从 Pico 转接板拔下来，插到电脑上。** payload 有约 25 秒窗口检测换线。

⚠️ 换线后 **`/dev/ttyACM0` 会消失、Pico 从 `lsusb` 掉线** —— 这是正常现象
（Pico 的 5V 和电脑的 5V 经相机 VBUS 顶在一起），**不代表注入了失败**，后续也不需要 Pico。

换线成功的标志：

```bash
lsusb | grep 18d1        # 18d1:4ee0  Google fastboot
```

## 3. 加载 stage2（电脑 → 相机，USB fastboot）

```bash
python3 host/bootstrap.py
```

它做这些（全部只读，**不刷写**）：

1. 等 `18d1:4ee0` 出现（最多 120 s，`--seconds` 可调）
2. 校验 stage1 的文本/rodata 与 `prebuilt/stage2-flash.bin` 的配对关系
3. 下载 stage2 并跳到 stage2
4. `oem emmc-init` → 读 `emmc-status / part-count / mmc-ocr …` 诊断
5. `getvar:partition-size:nflasha1`
6. 日志：`logs/bootstrap-YYYYmmdd-HHMMSS.log`

想顺手读一个 LBA（内容是 hex + sha256，**不落文件**）：

```bash
python3 host/bootstrap.py --read-lba 0x56006
```

`part-count: 0x1e`（30）才是分区表好了；出现 `part-count: 0` 说明这次 `emmc-init` 没读到表。

## 4. 工具清单（哪些能用）

| 工具 | 状态 | 用途 |
|---|---|---|
| `host/bootstrap.py` | ✅ | 接力 stage2 + 诊断 + 读单个 LBA |
| `host/verify_partition.py` | ✅ | **全量逐字节校验分区**（唯一可信的"刷没刷进去"判据） |
| `host/flash_partition.py` | ✅ | sparse 写入（整片 `--full` 或按差异块） |
| `host/read_updater_log.py` | ✅ | 通用"读任意 LBA 范围到文件"（`--lba --sectors --out`），比想象中好用 |
| `host/read_tmp.py` | ✅ | 读 nflasha10（`/tmp`）并解析里面有没有启动痕迹 |
| `host/dump_and_scan.py` | ✅ | dump `/setting` 与 `/log` 并扫描启动失败证据 |
| `host/read_setting_mode.py` | ✅ | 读 `/setting/mode/`（`dmode`、`preload`） |
| `host/restore_rescue.py` | ⚠️ | 需要 `offline-verification.json` + `nflasha.read1.bin` 格式的备份，**我们没有**，别用 |
| `host/fb_partition.py` | ❌ | 依赖 `stage2/build/stage2.bin`，**该目录不存在**，一跑就 `FileNotFoundError` |

## 5. 地址换算（关键）

**fastboot 的 LBA = 用户区字节偏移 ÷ 512**（不含 eMMC 前面的 boot0 2 MiB + boot1 2 MiB）。
反证：10-27 用 `--part-start 0x2D800000` 校验 nflasha15，4800 个 64 KiB 块全一致 ⇒ 基准正确。

```
nflasha10  = 0x0AC00000 → LBA 0x56000   (/tmp + /etc，12 MiB → 24576 扇区)
nflasha2   = 0x00800000 → LBA 0x04000   (/setting，20 MiB)
nflasha15  = 0x2D800000 → LBA 0x16C000  (/usr，300 MiB)
nflasha11  = 0x0B800000 → LBA 0x170000  (/log，500 MiB)
```

nflasha10 内部（FAT12，`rsvd=1, nfats=1, fatsz=4, rootent=512, spc=32`）：

```
分区偏移 0x000   FAT 引导扇区        → LBA 0x56000
分区偏移 0x400   FAT 表（4 扇区）     → LBA 0x56002
分区偏移 0xA00   根目录区（16 KiB）   → LBA 0x56005
分区偏移 0x4A00   数据区
分区偏移 0x0C60   /etc/ld.so.preload 的目录条目 → LBA 0x56006，扇区内 0x60
                  其 size 字段在条目 +28 = 扇区内 0x7C
```

## 6. 读回 / 校验（刷写前后都跑）

```bash
# ① 全量校验 nflasha10 是否等于原厂镜像（最可信，按 64 KiB 块报告每一处不符）
python3 host/verify_partition.py \
    --part-start 0x0AC00000 \
    --image /root/fw/zve10_backup/nflasha10.img

# ② 读一个扇区到文件（看 dirent 的 size 字段）
python3 host/read_updater_log.py --lba 0x56006 --sectors 1 --out /tmp/n10_dir.bin
xxd -s 0x60 -l 32 /tmp/n10_dir.bin      # 偏移 0x7C 起 4 字节 = size

# ③ 整片 dump 下来离线比对
python3 host/read_updater_log.py --lba 0x56000 --sectors 24576 --out /tmp/n10_full.bin
cmp /tmp/n10_full.bin /root/fw/zve10_backup/nflasha10.img && echo "整片一致"
```

## 7. 刷写（要修的时候）

```bash
python3 host/flash_partition.py \
    --part nflasha10 \
    --image /root/fw/zve10_backup/nflasha10.img \
    --part-start 0x0AC00000 \
    --full \
    --chunk-kb 768 --block-kb 64 \
    --resume-file /tmp/flash_n10.json
```

必须遵守（源码级理由）：

1. **`--part-start` 一定要给**，否则 host 端回读校验读的是绝对 LBA 0，报假 `MISMATCH` 然后中途退出。
2. **不要给 `--reference`**。一旦 `--reference` 的内容和 `--image` 相同，差异块为 0，
   输出 `0.0 MiB written` —— **一个字节都不会写**。
3. **`--full`** 强制整片覆盖，绕开任何"只写差异块"的跳过逻辑。
4. 中途断了就用同一个 `--resume-file` 重跑，会从断点续。

刷完**立刻**跑第 6 节的 ① 全量校验，报 0 处不符才算完。

## 8. 会话时限

- **每次 fastboot 会话 < 3 分钟**。相机约 4 分钟后外部电源侧硬复位，`keepalive` 是**累计**计时，
  不是空闲计时 —— 大分区要拆成多次，每次重新注入 stage1。
- 12 MiB 的 nflasha10 约 4 秒读完；300 MiB 的 nflasha15 全量校验约 42 秒。

## 9. 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| `Device never appeared` | 换线没成功，或没在长闪后 25 秒内换；重新从第 1 步开始 |
| `lsusb` 里 Pico 不见了 | **正常**，换线后必然掉线 |
| `SerialException: device reports readiness to read but returned no data` | 端口被占 / 旧进程没退；`pkill -f inject_payload`，拔插 Pico |
| `FileNotFoundError .../pico/logs/*.json` | 没建 `pico/logs` |
| `oem emmc-init: FAIL unknown oem command` | stage1 还在跑，stage2 没接力上；先跑 `host/bootstrap.py` |
| `oem peek:… unknown cmd` | 反过来 —— 已经是 stage2 了，不要再 bootstrap |
| `part-count: 0` | 这次 `emmc-init` 没读到分区表；重开会话 |
| `readback mismatch at 0x0` | **几乎总是** `--part-start` 没给（host 在读绝对 LBA 0），不是写坏了 |
| `0.0 MiB written` | `--reference` 与 `--image` 内容相同（常常是同一个文件），什么都没写 |

## 10. 一句话纪律

> 写入永远是 `flash:<分区名>`，由设备按分区表从**分区首字节**开始、并被分区边界硬限制 —— 碰不到分区表和别的分区。
> 会碰分区表/LBA 0 的只有绝对 LBA 裸写，本项目没用过，也别用。
> 唯一可信的"刷进去了吗"判据是 `verify_partition.py` 全量校验；采样一律不算。
