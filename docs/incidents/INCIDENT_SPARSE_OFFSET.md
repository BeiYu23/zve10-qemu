# 事故记录：sparse 单块写入的绝对/相对偏移混淆

> 2026-10-27。`host/clear_update_flags.py` 首次运行时把 4 KB 写到了错误位置。

## 发生了什么

`stage2` 的 `flash:<partition>` 总是从**分区第一个字节**开始写 sparse 镜像，
所以 sparse 的 RAW 块位置必须是**分区内相对偏移**。

工具里 `build_single_block()` 被传入了**绝对字节偏移**（0x810000），
于是偏移被多算了一个分区起点（0x00800000）：

```
期望写入: /setting 内偏移 0x010000  （updater 目录所在块）
实际写入: /setting 内偏移 0x810000  （多算了 0x800000）
```

## 影响

* 目标是 `/setting` 的簇 516，FAT 标记为 **0x000（空闲）**
* 该块在原厂备份里是 **全 0xFF**，属于未使用空间
* **没有丢数据**；被覆盖的只是空余区域

## 为什么没造成更大问题

两道保护同时起了作用：

1. 工具写完立即读回比对 ⇒ 不符即中止（`readback mismatch`）
2. `sparse_validate` / `write_range` 在设备侧双重边界检查 ⇒ 不可能写到分区外

## 修复

```python
PART_START_BYTES = 0x00800000
rel = base - PART_START_BYTES        # 传给 build_single_block 的必须是这个
if base < PART_START_BYTES:
    raise SystemExit(...)            # 防止再算错
```

同时把 `BASE` 从错误的 `0x6000` 改为 `PART_START_BYTES // 512 = 0x4000`
（`nflasha2` 起始是 `0x00800000`，不是 `0x00600000`）。

## 教训

* 分区内偏移与绝对偏移必须显式区分命名，不能都叫 `offset`
* 单块写入前先用 `--list`（只读）确认目录内容与簇号
* 任何写入都要先读原内容并保存；本项目里 `/tmp` 会被清理，改为保存到备份目录
