# 救援：把"preload 事故"变砖的相机救回来

## 故障形态

`/etc/ld.so.preload` 指向一个**无法加载**的动态库后：

- `/bin/busybox` 是**动态链接**的，`/bin/sh` 只是它的符号链接
- 于是 busybox 起不来 ⇒ `/sbin/init` 起不来 ⇒ **开机循环、无数据模式、无 shell**

> **判据**：若那个库是"未定义符号 / 重定位失败"级别的错误，则**任何软件层救援都不可能**
> （连 `rm` 都执行不了）。只有"库不存在"这类非致命情况才可能靠 shell 抢救。
> 社区在 a7C II 上独立得出同样的结论（见 `../docs/ACKNOWLEDGEMENTS.md`）。

## 为什么能救：只改 1~2 个字节

`/etc` 位于一块 **FAT** 分区（`nflasha10`）。FAT 里"删除一个文件"就是
**把目录项的首字节改成 `0xE5`**。于是：

```
读 FAT 根目录扇区 → 找到 ld.so.preload 的短名项(与它的 LFN 项) → 首字节写 0xE5 → 写回
```

- 只读写 **1 个 512 字节扇区**
- 不改文件内容、不重建文件系统、不动其他任何分区
- **实测：整个 12MB 分区最终只变了 1~2 个字节**（见 `payloads/minimal_fix/VALIDATION*.txt`）

## 路线：BootROM 串口启动 + 裸机 payload

相机 SoC 的 BootROM 支持通过 USB/串口加载并执行代码，于是不需要系统能启动：

```
上电 → BootROM 串口协议 → 下载 payload → payload 直接操作 SDHCI 控制器
     → 读 FAT → 改 1~2 字节 → 写回 → 断电重启
```

payload 是**零依赖裸机程序**（`-nostdlib -ffreestanding`），只用三个硬件地址：

| 外设 | 物理地址 |
|------|---------|
| SDHCI (eMMC) | `0xF0304000` |
| UART0 | `0xF2000000` |
| 目标分区 | `nflasha10` 用户区 `0x0AC00000` |

> ⚠️ **两个偏移不要混**：
> - 真机 eMMC **用户区**偏移：`0x0AC00000`
> - 整盘 `.img` **文件**偏移：`0x0B000000`（`.img` 前面有 4MB boot0/boot1）
>
> 分区换算规则见 [`../data/partition_map.tsv`](../data/partition_map.tsv)。

## payload 演进（`payloads/`）

| 目录 | 作用 |
|------|------|
| `stage1_probe/` | 第一阶段：最小初始化 + 读一块，验证 BootROM 加载链路 |
| `write_payload/` | 完整 `mmc_init` + **只读**一块（零写入），打印 hex/ASCII |
| `payload_fixed/` | 修正偏移后的读块版本 |
| `rescue_final/` | 定稿版救援 payload |
| **`minimal_fix/`** | ★ **最终修复版**：读 FAT 根目录 → 标记 `ld.so.preload` 项为已删除 → 写回 |

### 构建

```sh
cd payloads/minimal_fix
make TC=<你的 armv7 toolchain 前缀>      # 产出 payload.bin / payload.elf
```

## 验证

`payloads/minimal_fix/VALIDATION.txt` 是一次完整运行记录：

```
[1] mmc init OK
[2] read blk 0x56008 ret=0
  [3] first=0x4c attr=0x20 name=LDSO~1  PRE     ← 找到 ld.so.preload
[3] FOUND ld.so.preload at entry 3
[4] mark LFN entry 2 / mark short entry 3
[5] write back ret=0
```

**另外已在 QEMU 里做过端到端验证**：同一套 1 字节改动应用到镜像后，
整块 12MB 分区逐字节比对**只变化了 1 个字节**，且系统行为符合预期。

## 其他文档（`docs/`）

| 文档 | 内容 |
|------|------|
| `CXD90045_串行启动分析.md` | 串口启动协议与 SoC 行为分析 |
| `PROTOCOL.md` / `串行启动协议_权威版.md` | 启动协议细节（握手、下载、跳转） |
| `ARMv7_方案_定稿.md` | ARMv7 payload 的定稿方案 |
| `README_抢救包.md` / `行动简报.md` / `STATUS.md` | 现场行动手册与状态记录 |

## 风险提示

- 这些 payload **直接写 eMMC**，写错分区/偏移会造成不可逆损坏
- 操作前务必：**完整备份整盘**、核对分区偏移、先在 QEMU 里演练
- `/tmp`、`/etc`、`/var` 同属一块小 vfat 分区；写满会导致**所有**写入失败，
  所以救援必须坚持"最小写入"，不要往该分区拷大文件
