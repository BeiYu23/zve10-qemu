# 启用 ulogio telnet：真机单扇区写入记录

> 2026-10-24。目标：把 `/setting/ULOGIO.BIN` 里 `TELN` 记录的值从 0 改成 1，
> 让相机启动后开 telnet。**只写 1 个 512 字节扇区，净变化 1 字节。**

## 一、★ 重要发现：分区镜像过期，簇号必须从真机重算

最初按 10-16 的 `nflasha2.img` 算出 `ULOGIO.BIN` 在簇 389 → 块 `0x07085`，
但真机读出来是**全零**。改从 **live eMMC** 读 BPB 与根目录重算：

| 项 | 旧镜像 (10-16) | **真机 (2026-10-24)** |
|---|---|---|
| FAT 类型 | FAT16（按旧参数推） | **FAT12**（clusters=1278） |
| spf | 4 | 4 |
| data_sec | 37 | 37 |
| KEMCO.TXT 簇 | 93 | **18** → 块 `0x04225` |
| **ULOGIO.BIN 簇** | **389** | **19** → 块 **`0x04245`** |

⇒ **`/setting` 分区在 10-16 之后被重写过**（文件内容相同，但簇分配变了）。
⇒ 教训：任何"按镜像算出来的 eMMC 位置"在写之前**必须用真机数据重算并交叉验证**。

## 二、写前交叉验证（双通道）

```
eMMC  block 0x04245 前 16 字节: 20000000 4c474e4d 31393148 5f44736c
只读通道 ULOGIO.BIN[0:512]    : 20000000 4c474e4d 31393148 5f44736c
逐字节一致 ✓    偏移 0x9C = 0x00（telnet 关）✓
```

两个**完全独立**的通道（eMMC stage2 读 vs JIRITSU GET_FILE 读）给出相同内容，
证明簇链解析与定位都正确。

## 三、写入

```sh
python3 tools/emmc_sector.py patch \
    --block 0x04245 --offset 0x9C --value 1 --expect /tmp/ulogio_cam.bin
```

工具内置的硬门：
1. 读该扇区，与参考文件前 512 字节**逐字节比对**，不一致就拒绝写
2. 断言只有目标偏移一个字节会变
3. 写后**重新读取**并比对

实测输出：
```
read block 0x04245 md5 47b128e0b4496969a922dc755ad7d8f6
sector matches the reference file's first 512 bytes OK
changing byte 0x9c: 0x00 -> 0x01 (only these offsets: ['0x9c'])
write + readback verified: block 0x04245 now md5 8954a62458e1e07d5d2fcb74860e9e1b
```

- 写前 md5：`47b128e0b4496969a922dc755ad7d8f6`
- 写后 md5：`8954a62458e1e07d5d2fcb74860e9e1b`
- **净变化：1 字节（0x9C: 0x00 → 0x01）**
- 备份：`/tmp/sec_4245.bin`（原扇区）、`/tmp/ulogio_cam.bin`（整个文件）

## 四、为什么走 eMMC 而不是 JIRITSU 写入

本轮实测了 JIRITSU 写入通道（`SET_FILE_PATH` 0x400f + `SET_FILE` 0x4007）：

| 观察 | 结果 |
|---|---|
| `SET_FILE_PATH` payload[0]=0 | ✅ `00000000` |
| `SET_FILE_PATH` payload[0]=1 | ❌ `01020010` |
| `SET_FILE` area=2 | 写入 `/tmp/jiritsu/libJit.so`（固定槽位） |
| `SET_FILE` area=0/1 | 返回成功但**文件不出现** |
| SET_PATH 设的路径 | **不被 SET_FILE 采用** |

⇒ 该通道只能写 `/tmp/jiritsu/` 下的固定文件，**无法写任意路径**；
且它正是 2026-10-17 变砖的机制（写 `/etc/ld.so.preload` + `JIRITSU_DEBUG`）。
**结论：不用它写 `/setting`。**

## 五、后续步骤

1. 相机接回电脑 → 用只读通道复核 `/setting/ULOGIO.BIN`（应只有 0x9C 变 1）
2. 重启相机 → `ulogio` 在启动时读到 `TELN=1` → telnetd 起来
3. 相机联网（WiFi）→ 扫端口确认（最可能 23）
4. 用 `tlist` / `it` / `ot` 选终端（详见 `REMOTE_DEBUG_TELNET.md`）

## 六、回滚

把那一字节改回 0 即可：
```sh
python3 tools/emmc_sector.py patch --block 0x04245 --offset 0x9C --value 0
```
