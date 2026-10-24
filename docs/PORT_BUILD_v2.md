# 新菜单移植：v2 镜像构建记录（2026-10-24）

> 本轮做了三件事：**复核**（发现并修正 1 处误判）、**修正构建器 2 个保真度缺陷**、
> **产出可启动的整机镜像**。全部结果可复现。

---

## 一、复核结论：修正 1 处、确认 1 处

### 1.1 ✅ `beeplist.txt` 确实要取 ZV-1F 版（已修）

| 项 | 值 |
|---|---|
| 旧决策 | `KEEP_E10_REVIEW`（保留 ZV-E10 的 77 行旧格式） |
| 新决策 | **`TAKE_F`**（取 ZV-1F 的 72 行新格式，多 `Size`/`FileOffset` 两列） |
| 依据 | 混合固件里解析该文件的是 **ZV-1F 的 libObj + appFw**（`libObj.so` 已是 TAKE_F） |
| 体积变化 | 5,239 → 15,586 B（+10,347 B，与总增量精确吻合） |

### 1.2 ⚠️ 撤回：`libSTMcmdexecute.so` **不需要**补（我此前判断错误）

初版对比用 `comm` 比较清单，出现 "file is not in sorted order" 警告并产生**假差异**。
用 Python 集合重做后：

```
仅 ZV-1F 独有的 /rootfs 条目 = tmp_cloud/  tmp_remote/  tmp_wbshd/   ← 就这 3 个
lib/libSTMcmdexecute.so：两侧都有，md5 完全相同 66a9271377e8017e…（4,688 B）
```

⇒ **移植方案的原始表述（只补 3 个目录）是正确的。**

---

## 二、修正构建器 2 个保真度缺陷

`tools/port/build_hybrid.py`：

### 2.1 空目录被丢弃

`inventory()` 只跟踪文件与符号链接，`/usr/local/lib` 这类**纯空目录**整条丢失。
⇒ 已在 `materialize()` 末尾按两侧目录集合补建（本轮补建 3 个）。

### 2.2 属主被重置为 0:0

`materialize()` 优先用硬链接保留属主；但 `/mnt/build`（`/dev/sdc`）与源树
（`/dev/nvme0n1p3`）**跨设备**，回退到 `shutil.copy2()`，而 **copy2 不复制 uid/gid**，
导致镜像里全部变成 `0:0`（原厂是 **57285:1000**）。
⇒ 已在回退分支显式 `os.chown()`，并按源目录恢复目录权限/属主。

### 2.3 override 支持完整路径

`--lib-take-f` 原先强制给条目加 `lib/` 前缀，无法覆盖 `share/` 下的文件。
⇒ 改为：**含 `/` 的按路径原样使用**，否则补 `lib/` 前缀。
新增 `tools/port/overrides/force_take_f.json`。

---

## 三、最终产物

| 文件 | 大小 | md5 |
|---|---:|---|
| `zve10_hybrid_emmc.img` | 1,983,905,792 B | `e5d6bcaad3d59a9ad0f47a96afbfa6d1` |
| `nflasha15_hybrid.ext2` | 314,572,800 B（300 MiB） | `f11328198bb6668dce44f6fdbdb32c3e` |
| `nflasha7_hybrid.img` | 8,388,608 B（8 MiB） | `875bce24a910cb223cc861a89812dd87` |

位置：`/mnt/build/fw/port_v2/out/`

### 3.1 整机镜像构成

以 `zve10_delivery/ZVE10_EMMC_FULL.img`（真机 eMMC 全盘 dump）为基底，只替换两个分区：

| 分区 | 偏移 | 操作 |
|---|---|---|
| `nflasha15`（`/usr`） | `0x2D800000` | 写入 `nflasha15_hybrid.ext2` |
| `nflasha7`（`/rootfs`） | `0x0A400000` | 写入 `nflasha7_hybrid.img` |
| 其余全部分区 | — | **未改动**（已逐个 md5 验证：system / setting / tmp+etc 全部一致） |

---

## 四、自检结果（全部通过）

| 检查项 | 结果 |
|---|---|
| `e2fsck -fn`（ext2 镜像） | ✅ 干净：`905/76912 files, 219814/307200 blocks` |
| `e2fsck -fn`（rootfs） | ✅ 干净：`636/1256 files, 4741/5012 blocks` |
| **逐文件内容校验**（4,330 条决策） | ✅ **853 个文件/链接内容与源一致；0 缺失、0 不符、0 该删未删** |
| `lib/appFw.so` | ✅ 53,189,440 B = ZV-1F 版，md5 一致 |
| `lib/libObj.so` | ✅ 3,929,768 B = ZV-1F 版（非 ZV-E10 的 21 MB），md5 一致 |
| `share/avsystemsound/beeplist.txt` | ✅ 15,586 B = ZV-1F 版，md5 一致 |
| 老 UI 残留 `viewUnified*` | ✅ **0 个** |
| 属主/权限 | ✅ **57285:1000**，与原厂一致 |
| `/usr/local/lib` 空目录 | ✅ 已保留 |
| `/rootfs` 新增 | ✅ 恰好 3 个目录，无其它改动 |
| 剩余空间 | `/usr` 剩 **87,386 块（≈85 MiB）** |

关键文件 md5 抽验（镜像内 vs ZV-1F 源）：

```
appFw.so      09a3e9e284418ca6…  ✓一致
libObj.so     7749275ac3051eda…  ✓一致
beeplist.txt  6d069929ee6bdcae…  ✓一致
```

---

## 五、给 QEMU 侧的使用说明

启动脚本 `scripts/run_qemu_zve10.sh` 用 `EMMC` 变量指向整盘镜像：

```sh
EMMC=/mnt/build/fw/port_v2/out/zve10_hybrid_emmc.img \
ZIMAGE=<你那份打好补丁的 zImage> \
INITRD=<真机导出 initrd> \
BACKUP=<修正过的 BACKUP.BIN> \
./scripts/run_qemu_zve10.sh
```

### ⚠️ 三条重要提醒

1. **这份镜像里没有你们之前的字节补丁**（`libosal_uipc.so @0x26c0`、`im.elf @0x2f46` 之类）。
   那些补丁打的是 **ZV-E10 原版 `/usr`**；本轮 `/usr` 已整体换成 ZV-1F 版，
   原偏移**不再存在**。要跑新菜单，需要针对 **ZV-1F 的二进制**重新定位补丁点。

2. **启动链没变**：`/system`（内核+initrd）、`/setting`、`/rootfs` 的其余部分全部保留 ZV-E10 原样，
   initrd 与 ZV-1F 的差异仅 `/sbin/init` 112 字节（且差异内容是可选模块与 tmpfs 挂载）。

3. **预期第一个观察点**：新 UI 起来后应加载 `lib/appFw.so`（53 MB）而非 `viewUnified*.so`（已删除）。
   如果 `/usr` 挂载失败或 `appFw` 起不来，会表现为明显的启动异常，便于二分。

---

## 六、可复现命令

```sh
# 1) 生成合并树 + ext2 镜像
cd tools/port
python3 build_hybrid.py \
  --e10 /root/fw/ref_e10/usr --zv1f /root/fw/ref_zv1f/usr \
  --out /mnt/build/fw/port_v2/out \
  --lib-take-f overrides/force_take_f.json \
  --hash --materialize --image

# 2) 生成 hybrid rootfs（补 3 个空目录）
cp /root/fw/zve10_delivery/nflasha7.img <out>/nflasha7_hybrid.img
for d in tmp_cloud tmp_remote tmp_wbshd; do
  debugfs -w -R "mkdir /$d" <out>/nflasha7_hybrid.img
done

# 3) 拼整盘
cp /root/fw/zve10_delivery/ZVE10_EMMC_FULL.img <out>/zve10_hybrid_emmc.img
dd if=<out>/nflasha15_hybrid.ext2 of=<out>/zve10_hybrid_emmc.img bs=1M seek=$((0x2D800000/1048576)) conv=notrunc
dd if=<out>/nflasha7_hybrid.img  of=<out>/zve10_hybrid_emmc.img bs=1M seek=$((0x0A400000/1048576)) conv=notrunc
```

---

# 七、v3 修正（2026-10-24 晚）：镜像分区错位，已修

> QEMU 侧实测反馈 `/setting` 挂载失败（`errno(2)`），bootin 读不到 dmode 后走异常分支退出。
> **根因在我这边的镜像构造，与 /usr 内容无关。**

## 7.1 根因：`ZVE10_EMMC_FULL.img` 是「boot 分区 + 用户区」拼接

排查结果：

```
偏移 0x00000000 : "EXBL" ← eMMC **boot 分区**的引导块（4 MiB）
偏移 0x400018   : 分区描述符 magic 0x04100548   ← 用户区的表头（+0x18）
偏移 0x4010000  : nflasha1 (FAT)
偏移 0x4080000  : nflasha2 /setting (FAT)
偏移 0x2DC00000 : nflasha15 /usr (ext2)
```

⇒ 该 dump = **4 MiB boot 区 + 用户区**，用户区内各分区相对分区表**整体 +4 MiB**。
我此前直接 cp 整个 dump 再按**分区表偏移**写 hybrid 分区，
于是 `/usr` 因为我覆盖了表偏移处而"看起来对"，**其余分区全部错位 4 MiB** ⇒ `/setting` 读到垃圾。

**为什么之前没发现**：分区表在 `0x18` 处读 magic 得到 0（真实值在 `0x400018`），而我先做了 md5 局部比对、没有逐分区验证类型。

## 7.2 修正：跳 4 MiB 取用户区

```sh
# boot 区单独留档（QEMU 若需要 boot partition 可用这个）
dd if=ZVE10_EMMC_FULL.img of=zve10_boot_area.img bs=1M count=4

# 用户区镜像 = 跳过 4 MiB
dd if=ZVE10_EMMC_FULL.img of=zve10_hybrid_emmc.img bs=1M skip=4

# 按分区表偏移写入 hybrid 分区
dd if=nflasha15_hybrid.ext2 of=zve10_hybrid_emmc.img bs=1M seek=$((0x2D800000/1048576)) conv=notrunc
dd if=nflasha7_hybrid.img  of=zve10_hybrid_emmc.img bs=1M seek=$((0x0A400000/1048576)) conv=notrunc
```

修正后**逐项验证（16/16 符合分区表）**：

```
nflasha1  @0x00010000 FAT    nflasha2  @0x00800000 FAT    nflasha3  @0x01C00000 FAT
nflasha7  @0x0A400000 ext2   nflasha10 @0x0AC00000 FAT    nflasha13 @0x2D400000 FAT
nflasha15 @0x2D800000 ext2   nflasha17 @0x49000000 FAT    nflasha18 @0x4F400000 FAT
（4/5/11/16/23 为非 FAT/ext2 的原始格式，符合预期）
描述符 magic @0x18 = 0x04100548 ✓
```

## 7.3 附带：写入显式 `dmode=3`

`/setting/mode/` 在 dump 里是**空目录**（无 dmode 文件）。虽然既有结论是"缺失不影响、启动已在默认档位"，
但为消除不确定性，已在 `/setting/mode/dmode` 写入 `3`：

| 依据 | 内容 |
|---|---|
| `change_mode.sh` 自带说明 | **dmode=3 = "kernel boot → global infra → LIRO boot → appFw boot（默认）"** |
| 结论 | **新菜单同样用 3，不需要改值** |

## 7.4 最终产物（v3）

| 文件 | 大小 | md5 |
|---|---:|---|
| **`zve10_hybrid_emmc.img`**（用户区，给 QEMU） | 1,979,711,488 B | `7bba051455b08958ddad9f120f1f5b45` |
| `zve10_boot_area.img`（boot 分区 4 MiB） | 4,194,304 B | `360beb1378b2a33552c1ba551b8b3b19` |
| `nflasha15_hybrid.ext2` | 314,572,800 B | `f11328198bb6668dce44f6fdbdb32c3e` |
| `nflasha7_hybrid.img` | 8,388,608 B | `875bce24a910cb223cc861a89812dd87` |

**未改动分区核对（13/13 逐分区 md5 一致）**：
`nflasha1 / 3 / 4 / 5 / 6 / 10 / 11 / 12 / 13 / 16 / 17 / 18 / 23` 全部与原 dump 相同。

## 7.5 关于 `net_cmdd.elf`（QEMU 日志里那条）

| 事实 | 说明 |
|---|---|
| ZV-E10 `/usr/bin` | **不存在** |
| ZV-1F `/usr/bin` | **不存在** |
| initrd 的 `/sbin/init` | **表里有这一项**（与 `bootin.elf` / `sen.elf` 并列） |

⇒ 它是 init 编译进去的**可选执行项**，两边固件都不带 = 正常。
日志里看到它被 exec，是 **bootin 退出(255) 后的兜底路径**，不是原因。

---

# 八、v4：换上 ZV-1F 的 im.elf / bootin.elf（+ 回答 QEMU 侧三问）

## 8.1 为什么值得换：两版 im.elf 差 59%

| 项 | 值 |
|---|---|
| 大小 | 两版都是 23,240 B |
| **字节差异** | **13,795 / 23,240 = 59.36%**（同尺寸但代码大改） |
| 导入符号集 | **完全相同（64 个，差集为空）** |
| dlopen 目标 | 两版都含 `/usr/lib/libObj.so` + libIMDB/libbackup/libosal_utm/libosal_uipc/libSTMcmdexecute |
| 字符串差异（实质） | ZV-1F 版多出 **`a_thread APP idx err`**、**`a_thread INFRA_END idx err`**（新的线程索引逻辑）；ZV-E10 版有 `/dev/nflasha18`，ZV-1F 版是 `/dev/NONE` |

⇒ **导入符号相同 ≠ 启动行为相同**。ZV-1F 的 im.elf 与 ZV-1F 的 libObj 是配套构建，
混用 ZV-E10 的 im.elf 属于跨版本组合，**应当换成 ZV-1F 版**。

## 8.2 v4 产物

| 文件 | 大小 | md5 |
|---|---:|---|
| **`zve10_hybrid_full_v4.img`**（boot 区 + 用户区，推荐直接给 QEMU） | 1,983,905,792 B | `0f6245dc2ea825b66efa8de7d4c5deb0` |
| `zve10_hybrid_emmc_v4.img`（仅用户区） | 1,979,711,488 B | `fc47088aac184b4d37afbbbea92c29ee` |
| `nflasha15_hybrid_v4.ext2`（v4 的 /usr） | 314,572,800 B | `2ea8c4a10812bdca7e953139ac2ef56b` |
| `bin_zv1f/im.elf`（单独取用） | 23,240 B | `a48240083e68a58ed2cf0e8870dcd895` |
| `bin_zv1f/bootin.elf`（单独取用） | 15,336 B | `3545261cc8ec1ebc77d476cde9d0e66a` |

**v4 相对 v3 只改两个文件** `/bin/im.elf` + `/bin/bootin.elf`（换成 ZV-1F 版），
属主已修正回 **57285:1000**，`e2fsck` 干净（905/76912 files）。

两版布局均已验证：
- 用户区镜像：`/usr` @ **728 MiB** ext2 ✓
- full 镜像（前 4 MiB boot 区）：`/usr` @ **732 MiB** ext2 ✓，`/setting` @ 12 MiB FAT ✓，nflasha1 @ 4 MiB FAT ✓

## 8.3 关于 `backup.ko`（QEMU 侧的 upm 卡点）

| 模块 | ZV-E10 | ZV-1F | 关系 |
|---|---:|---:|---|
| `backup.ko` | 73,764 | 73,796 | **字节差 27.1%，但字符串集完全相同** ⇒ 同源重编 |
| `accel.ko` | 8,012 | 8,012 | **字节完全相同** |
| `mag.ko` | 10,500 | 10,500 | **字节完全相同** |
| `compass.ko` | 5,412 | 6,592 | 不同 |
| `grm_ma.ko` | 24,800 | 24,800 | 不同 |
| `upm.ko` | 137,260 | 146,552 | 不同 |

⇒ **ZV-1F 的 `backup.ko` 与原厂同源，在 QEMU 里同样会卡 upm。**
⇒ **stub 是 QEMU 环境的必需品，不是移植决策。**
⇒ **真机上必须用原厂模块**（stub 只用于 QEMU 验证）。

## 8.4 交付约定更新

按 QEMU 侧建议：**默认交付「boot 区(4 MiB) + 用户区」的整份镜像**（`zve10_hybrid_full_v4.img`），
与 QEMU 侧既有偏移约定（/usr @ 732 MiB）一致；同时保留纯用户区版本以备不同机器模型。
