# 外部参考项目：dino2048/zv-e10-toolkit（只读引用）

> **这不是我们的仓库。** 上游：<https://github.com/dino2048/zv-e10-toolkit>（MIT License，
> Copyright (c) 2026 dino2048）。本机副本在 `/root/fw/zv-e10-toolkit`，**只读，禁止写入/提交/推送**。
> 我们的成果仓库是 `cokeenet/zve10-qemu`（`/root/fw/oss`），两者不要混。

## 1. 为什么要参考它

它的 **BootROM 串口注入 → USB → stage2 → eMMC 读写** 那条链是最早跑通的实现，
而且带了若干我们没有的**专用工具**和**硬件/方法文档**。可以直接补我们的短板：
FAT 目录解析、`ld.so.preload` 定向修复、单级 fastboot 调试原语、备份与校验方法论。

## 2. 已引进到本仓库的东西（2026-10-28）

复制时保留了原文件头和注释；许可证副本放在 `fastboot/LICENSE.toolkit-dino2048`。

| 引进的文件 | 用途 | 我们的改动 |
|---|---|---|
| `fastboot/host/read_root_dir.py` | 解析 FAT12 BPB → 读整个根目录 → 解 LFN 链 → 报条目/簇号/size 字段位置 | 无 |
| `fastboot/host/read_preload.py` | 读 `ld.so.preload`(簇 393) 与 `.orig`(簇 367) 内容 + FAT12 链 | 无 |
| `fastboot/host/disable_preload.py` | 把 `ld.so.preload` 的 dirent size 置 0（净改 1 字节），A–F 六道硬门禁 | 无（但见 §4 注意事项） |
| `fastboot/host/restore_preload.py` | 从 ground truth 写回一块，撤销上面的改动 | 无 |
| `fastboot/host/zve10_fb.py` | 单级 fastboot 客户端（`oem peek/poke/call/load`） | 无 |
| `fastboot/host/stage2_if.py`、`run_stage2.py` | 最小 stage2 的加载与调用接口 | 无 |
| `fastboot/host/n15_check.py` | `/usr`(nflasha15) 检查 | 无 |
| `fastboot/host/stage2_selftest.py`、`emmc_write_test.py`、`idle_timeout_test.py`、`darwin_probe.py`、`wdt_evidence.py` | 写路径自检 / 会话时限 / 看门狗取证 | 无 |
| `fastboot/stage2/build/stage2.bin` | **最小 stage2 二进制**（1,860 B，sha256 `01f4f6d9…`），上面这些工具依赖它 | 原先 oss 缺失，本次补入（`.gitignore` 忽略 `*.bin`，需本地保留） |

**补齐后立刻验证过**：`stage2/build/stage2.bin` 的 sha256 前缀 `01f4f6d9` 与
`fb_partition.py` 里的 `EXPECT_STAGE2_SHA` 匹配，五个依赖脚本均可 import。

## 3. 与我们自己那套的分工

| 场景 | 用哪套 | 为什么 |
|---|---|---|
| 刷分区、全量校验 | **我们自己的** `flash_partition.py` / `verify_partition.py` + `prebuilt/stage2-flash.bin`（10,588 B） | 有 sparse、resume、逐片回读，适合几百 MB |
| 改**单个块**（如 dirent 的 size 字段） | **toolkit 这套** `read_root_dir.py` + 最小 stage2（1,860 B） | 只动一个扇区，门禁密，风险小 |
| 通用 fastboot 命令 / 寄存器调试 | `zve10_fb.py`、`bootstrap.py` | 前者 peek/poke，后者完整握手 |

两套 stage2 **加载地址相同（0xFE300000）但二进制不同**，一次会话里不要混着加载。

## 4. 注意事项

1. `disable_preload.py` 的 Gate B/D 是**为"size=20、其余字段未动"的干净坏状态写死的**，
   并且需要 `host/build/emmc-groundtruth/` 下的 4 个基准块 + `/tmp/rootdir.bin`。
   我们的相机此时 `ld.so.preload` 的内容是 `/etc/ldtest.so`（长度不同）且分区里还有
   `ldtest.so` 本体 ⇒ **直接跑会 fail**。要么重新采集 GT，要么照它的原语另写一个
   "接受任意当前 size"的版本 —— 见 `docs/FASTBOOT_OPERATIONS.md` §4.1。
2. 上游 2026-10-26 把 `fastboot/` 目录整体更名为 `bootrom/`（commit `bd2b4ac`）。
   我们本地这份仍是 `fastboot/` 布局，**不要跟着改名**，否则我们所有脚本路径全断。
3. 它文档里 `restore_rescue.py` 的正规恢复需要 `offline-verification.json` +
   `nflasha.read1.bin/read2.bin`（双遍读取）格式；我们的 `zve10_backup/` 不是这种格式，
   所以那条路径对我们暂时不可用。

## 5. 它有哪些经验值得抄（不涉及代码）

- **双遍读取 + 每 1 MiB 哈希 + 主机独立重读**：证明"主机保存的正是它收到的字节"。
  我们 10-27/10-28 两次翻车，本质都缺这一层。
- **写之前先做零改动写回自检**（写一个必然不变的块再读回）：在写任何真实数据前
  先证明这次会话的写路径是通的。
- **门禁式写入**：每一步都断言前置条件，任何一条不满足就在碰 eMMC 之前退出。
- **在相机侧用 `cp /etc/ld.so.preload.orig /etc/ld.so.preload` 一句话恢复**
  （需要相机能起来 + root 终端，见 `docs/ROOT_TERMINAL_AND_SERVICE_MODE.md`）。
- 硬件接线与协议细节：`/root/fw/zv-e10-toolkit/fastboot/docs/hardware.zh-CN.md`、
  `pico/PROTOCOL.md`（我们已有一份 `fastboot/pico/PROTOCOL.md`）。

## 6. 纪律

- **只读引用**。不向 `/root/fw/zv-e10-toolkit` 写入、不 commit、不 push。
- 复制代码必须保留原版权声明与许可证；新增/修改自己的版本时在文件头注明差异。
- 不要把本仓库（`cokeenet/zve10-qemu`）的内容推到它的 remote。
