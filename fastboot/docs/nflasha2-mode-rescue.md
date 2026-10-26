# nflasha2 `/setting/mode/preload` 救援工具

> 2026-10-26。**上机前必须先有这条后路**：现有 12 个 fastboot 脚本全部针对
> `/etc/ld.so.preload`（nflasha10），没有一个能改本次启动入口。

## 一、为什么必须单独做

```
/etc/ld.so.preload   → nflasha10（全局 loader preload）  ← 旧脚本只管这个
/setting/mode/dmode      → nflasha2  ← 本次要动的
/setting/mode/preload    → nflasha2  ← 本次要动的
```

如果 `preload` 里放进一个会卡住 `im.elf` 的值，**拔 SD 卡无效**（文件在内部 eMMC），
**旧 fastboot 脚本也无效**（目标分区不同）。这时唯一的退路就是这个工具。

## 二、已核实的布局（对照 `zve10_backup/nflasha2.img`，20 MiB）

```
分区基址绝对 LBA    0x6000
FAT12: bps=512 spc=32 rsvd=1 nfats=1 fatsz=4 rootent=512 media=0xf8
根目录绝对 LBA      0x6005   (= 基址 + 5)
数据区绝对 LBA      0x6025   (= 基址 + 37)
MODE 目录          cluster 468 → 绝对 LBA 0x9A65（字节偏移 0x134CA00）
MODE 目录项本身      根目录 off=0x2E0（LFN "mode"，短名 MODE，attr 0x10）
FAT[468]            0xFFF（已分配）
```

**★ 注意：本地备份镜像里的 `MODE/` **不是空的**，含 `zve10_net`（4 字节）。
之前"出厂为空"的说法是错的。**

## 三、工具做什么

```bash
python3 host/nflasha2_mode.py --list            # 只读列目录（默认，永远安全）
python3 host/nflasha2_mode.py --dump            # 另存 root.bin / mode.bin 供离线比对
python3 host/nflasha2_mode.py --disable-preload # 删除 preload 目录项
python3 host/nflasha2_mode.py --restore <sector.bin>
```

**删除的语义**：把 `preload` 的目录项（连同 LFN 链）首字节标 `0xE5`。

`bootin.elf` 随后 `open()` 得到 `ENOENT`，其代码路径在 `+0x99aa` 的 `blt 0x9a1c`
直接跳到跳过 `putenv` 的分支 ⇒ **下次开机就是原厂引导**。

**不动的地方**：文件数据簇、FAT、根目录、`dmode`、其它目录项。

## 四、六道闸门（任一失败都在写入前中止）

| 闸门 | 检查 |
|---|---|
| **A** | boot 扇区 `EB 3C 90` + `55AA`，且 BPB 六项**完全等于**期望值 |
| **B** | 根目录扫描：MODE 项**恰好一个**，位置 `0x2E0`、cluster 468、attr 0x10 |
| **C** | 本次会话写通路健康（对 FAT[0] 做零改动写回并读回比对） |
| **D** | MOOE 簇内 `preload` **恰好一个精确匹配**；**写入前先把该扇区存盘** |
| **E** | 真正的写入，然后**两次独立读回** |
| **F** | 写后扫查：boot/FAT/根目录未变，MODE 差异**恰好等于**预期字节，`preload` 不再可见 |

**★ 没有 `preload` 时直接返回，**零写入**。**

## 五、离线演练结果（无相机参与）

用 `/root/fw/zve10_backup/nflasha2.img` 顶替相机，`Fake stage2` 把读写映射到镜像文件：

```
正向（test_nflasha2_mode.py）
  1. 原厂状态：list 正常，disable 拒绝写入（0 次写）
  2. 伪造 dmode + preload（含 LFN 链）：
     闸门 A–F 全过，全镜像差异 = 2 字节
     0x74CA40（LFN 条目）、0x74CA60（短名条目）
     dmode 完好，preload 消失
  3. restore：与写入前**逐字节完全一致**（0 字节差异）

负向（test_nflasha2_mode_negative.py）
  ✓ BPB 被改         → ABORT
  ✓ boot 签名损坏     → ABORT
  ✓ MODE cluster 漂移 → ABORT
  ✓ MODE 目录项漂移   → ABORT
  ✓ 无 preload        → 零写入
  ✓ restore 越界      → 拒绝
```

## 六、仍需在真机上确认的（离线无法证明）

```
□ stage2 能否读写 nflasha2 的绝对 LBA（旧脚本只验证过 nflasha10/nflasha15）
□ 实机 MODE 目录的当前内容（--list 一次即可）
□ 写后 readback 与实际 flash 一致（脚本已做双读回，但未在真机跑过）
```

**⇒ 第一次真机使用建议只跑 `--list`，确认能正确读出当前 `/setting/mode/`。**

## 七、限制（必须写清）

- 只能改**目录项**，不能修复已损坏的数据块。
- 不验证 `dmode` 的值是否正确（只保证 `preload` 不再生效）。
- **不能恢复 MCU / 镜头 / 其它外设内部 NVM** —— 本工具只作用于 eMMC 的 nflasha2。
- 依赖 BootROM fastboot 通道；若该通道本身不可达，本工具也无法使用。
