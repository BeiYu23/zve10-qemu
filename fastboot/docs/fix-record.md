# ld.so.preload 修复记录（2026-10-22 23:05）

> 本次即 10-18/10-20 在 QEMU 端到端验证过的 minimal_fix 语义（清 size 字段），
> 换到 USB fastboot + stage2 通道执行，校验强度更高。**净字节变化：1 字节。**

## 改动

| 项 | 值 |
|---|---|
| 目标目录项 | 根目录 entry 35，LFN 精确匹配 `ld.so.preload`（短名 `LDSO~1.PRE`） |
| size 字段位置 | 块 `0x56006` 偏移 `0x7C`（绝对 `0x0AC00C7C`） |
| 变化 | `0x14` → `0x00`（size 20 → **0**） |
| 效果 | `/etc/ld.so.preload` 变 0 字节 ⇒ glibc 读到空 preload ⇒ `zve10_agent.so` 不再注入任何进程 |

未触碰：`.orig` 备份条目/内容（仍是 size=20 的原文件副本）、LFN 链、
first_clus=393（簇保留，同 minimal_fix 做法）、FAT、文件内容、
`zve10_agent.so` 本体（12652 B，仍在盘上）。

## 六道硬门（host/disable_preload.py，全部通过）

```
A. 引导扇区 EB3C90…55AA + BPB(512/32/1/1/3/512) 不变
B. 0x56006 / 0x56005 / 0x59104 与 ground truth 逐字节相同
C. 本会话写路径健康探针：0x58DC4 原样写回 + 读回一致（零变化）
D. 全根目录重扫：恰好一个精确匹配 @0x460（attr=00 clus=393 size=20），
   .orig @0x3C0 完好，与 22:30 转储零漂移
E. 写入：mod 块与 GT 恰差 1 字节（断言）；write_status=0、write_block=0x56006；
   两次独立读回 == 目标块
F. 终检：boot/FAT(367,393=EOF，邻簇空闲)/GT 三块全部原样；
   全根目录与写前恰差 1 字节（0x47C）；ld.so.preload size==0
```

## 校验和

```
GT  root_e35_0x56006.bin  sha256:807c1ba6…(stage2) / c2a0423e7368ba7a(块)
写后块 0x56006            416556df6ccb03d2
根目录 写前/写后           f1a58ebefa7d0aa9 → dcdb7167b004cc24
写后根目录转储             build/rootdir-post-fix.bin
```

独立复核（read_root_dir.py 重扫）：entry 35 `LDSO~1.PRE` size=0；
entry 30 `.orig` size=20；`zve10_agent.so` 12652 B——全部符合预期。

## 交叉证据链

- 10-16 分区镜像（`zve10_delivery/nflasha10.img`，md5 3a75a7c0…）BPB 与
  真机逐项一致；镜像里没有 preload 文件（满是 `.zv10_ran_v1..v12` 状态文件）
  ⇒ preload 创建于 10-16→17 抢救期，即变砖源；本次改动恰好回到"无 preload"方向。
- 变砖机制与修复方案见 `/root/fw/抢救状态_2026-10-18.md`、
  `/root/fw/minimal_fix/VALIDATION_size_clear.log`（VM 端到端）。

## 恢复（如需重新启用 agent 注入）

1. fastboot 在线时：`python3 host/restore_preload.py`（GT 块写回，含读回验证）
2. 相机有 shell 时：`cp /etc/ld.so.preload.orig /etc/ld.so.preload`
3. 兜底：整分区镜像重刷（`zve10_delivery/nflasha10.img`，10-16 状态）

## 开机验证 ✓（2026-10-22 23:20，用户实机确认）

断电重启 ⇒ **相机正常出 UI**。preload 已空（出厂语义），agent 不再注入，
无副作用。`zve10_agent.so`（12652 B）与 `.orig` 备份仍留在盘上，随时可用
`host/restore_preload.py`（fastboot 通道）或相机侧 `cp /etc/ld.so.preload.orig
/etc/ld.so.preload` 重新启用。
