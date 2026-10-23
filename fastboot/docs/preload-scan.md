# ld.so.preload 只读根目录扫描（2026-10-22）

> 全程 CMD17 只读，未写 eMMC 任何字节。
> **后记（2026-10-22 23:10）**：当晚已按 [write-success.md](write-success.md)
> 的写路径执行修复——size 字段 20 → 0（净变化 1 字节），`ld.so.preload`
> 变 0 字节，preload 失效。详见 [fix-record.md](fix-record.md)。

## 方法

不复用固定偏移：读 block `0x56000` 的 FAT 引导扇区，程序化解析 BPB，
算出根目录位置后再扫描全部 512 个目录项（工具 `host/read_root_dir.py`）。

## BPB（nflasha10 = 12 MiB FAT12）

| 字段 | 值 |
|---|---|
| bytes/sector | 512 |
| sectors/cluster | 32（簇 16 KiB） |
| reserved | 1 |
| FAT 数 × 大小 | 1 × 3 扇区 |
| 根目录项 | 512（32 扇区，block `0x56004` 起） |
| 总扇区 | 24,576（12 MiB） |
| FAT 类型 | FAT12（≈766 簇 < 4085） |

数据区起始 = 引导扇区 + 36；cluster N 的首块 = `0x56000 + 36 + (N-2)×32`。

## 根目录中与 ld.so.preload 相关的条目

| LFN | 8.3 名 | 绝对字节 | 块 + 偏移 | 簇 | size |
|---|---|---|---|---|---|
| `ld.so.preload.orig` | `LDSOPR~1.ORI` | `0x0AC00BC0` | `0x56005`+`0x1C0`（第 31 项） | 367 | 20 |
| **`ld.so.preload`** | `LDSO~1.PRE` | **`0x0AC00C60`** | **`0x56006`+`0x60`**（第 36 项） | 393 | 20 |

- `ld.so.preload` 的 **size 字段在绝对 `0x0AC00C7C`**（块 `0x56006` 偏移 `0x7C`），当前值 **20 (0x14)**。
- `.orig` 的 size 字段在 `0x0AC00BDC`（块 `0x56005` 偏移 `0x1DC`），同为 20。
- `ld.so.preload` 原始目录项（36 字节）：
  `4c44534f7e3150524500 00e8 0000 2100 2100 00000000 2100 6f01 14000000`

## 内容核对（read-only）

| 文件 | 簇 → 块 | 前 20 字节 | FAT12[簇] |
|---|---|---|---|
| `ld.so.preload.orig` | 367 → `0x58DC4` | `/etc/zve10_agent.so\n` | `0xFFF`（EOF） |
| `ld.so.preload` | 393 → `0x59104` | `/etc/zve10_agent.so\n` | `0xFFF`（EOF） |

**结论：size 字段（20）与文件内容（19 字符 + LF = 20 字节）完全一致，
FAT 链健康（单簇 EOF，邻簇空闲）。** 目录项、FAT、内容三者互相印证，
`ld.so.preload` 目前没有可见损坏。

同目录可见 `zve10_agent.so`（`ZVE10_~1.SO`，簇 377，12652 B），
与 preload 指向的 `/etc/zve10_agent.so` 对应（该分区挂在 `/etc`）。

## 产物

- `host/read_root_dir.py` — BPB 解析 + 根目录只读扫描
- `host/read_preload.py` — 两个 preload 文件内容 + FAT 项只读核对
- `build/rootdir-20261022.bin` — 根目录 16 KiB 原始转储
