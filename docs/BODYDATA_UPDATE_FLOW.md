# BODYDATA 官方更新流程（完整解析）

> 结论：**官方更新通道可定制** —— 容器校验是普通 CRC32，FDAT 加密密钥已知，
> ZV-E10 用的正是我们掌握密钥的那个 crypter。

---

## 一、容器层：UFU 格式

```
0x00  8 字节魔数: 89 55 46 55 0D 0A 1A 0A   "\x89UFU\r\n\x1a\n"
之后重复: [大端 u32 长度][4 字节 ASCII 标签][该长度的数据]

段表（ZV-1F / ZV-E10 / ZV-E10M2 三样本一致）:
  DATV  len=4        01 00 00 00           数据版本
  PROV  len=4        01 00 00 00           profile
  UDID  len=188      23 个 (USB VID/PID, 标志) 对
  FDAT  len=<大>     ★ 固件载荷（加密）
  DEND  len=4        CRC32(前面所有字节)，大端   ← ★ 普通 CRC32，非签名，可重算
```

**⇒ 容器完整性校验 = CRC32 ⇒ 改完数据重算即可。**

---

## 二、FDAT 层：AES 分块加密（已完全逆转）

```
块大小      : 1024 字节
尾部        : 0x110 字节"trailer"，其【前 16 字节】是 IV（不是最后 16 字节）
block 0     : 低半 = AES-ECB(KEY_AES)；高半 = AES-CBC(KEY_CXD90045, IV)
后续每块    : AES-CBC 继续链
解密后每块  : u16 校验和 + u16 size|endflag + 数据；校验和验证 block[2:]
魔数        : "UDTRFIRM" + 版本 "0100"
```

密钥（来自前人研究，非本机秘密）：
```
KEY_AES       = E3B0C44298FC1C149AFBF4C8996FB924   （SHA-256("") 的前 16 字节，固定常数）
KEY_CXD90045  = C1AA8F7C…D303E06B                    （32 字节，AES-256-CBC）
```

**★ 关键：ZV-E10 的 `crypterName` 就是 `CXD90045` —— 正是我们有密钥的那个。**

| 机型 | model ID | crypterName |
|---|---|---|
| **ZV-E10** | **`0x1030010`** | **`CXD90045`** ✅ 密钥已知 |
| ZV-1F | `0x11020010` | `CXD90045_k8`（另一变体，密钥未见） |
| ZV-E10M2 | `0x30030002` | `CXD90057_k8`（ARGO，不同 SoC） |

---

## 三、载荷层：tar 结构（99 项）

```
0100_config/       mount.conf（分区表）、big_backup.conf
0101_config_sum/   config.sum           ← 每文件 CRC32
0110_backup/       MainDest.txt + SYSASTRA-DSC/...   ★ 实际镜像
0111_backup_sum/
0300_partconf / 0301_partconf_sum
0600_gps / 0601_gps_sum
0630_ca / 0631_ca_sum
0650_prfile / 0651_prfile_sum
0700_part_image / 0701_part_image_sum     ★ 分区镜像
0800_appli / 0801_appli_sum
1030_genlock / 1031_genlock_sum
```

**每个 `0x00_` 目录都配一个 `0x01_*_sum` 目录存放 CRC32。**

### 3.1 校验和格式（已实测验证）

```
config.sum 里：
0,9aa3816e,000004e7,000001a4,00000000,000003e8,root,root,-1,-1,mount.conf,
  ↑CRC32   ↑大小(0x4E7=1255)                ↑属主

验证：zlib.crc32(mount.conf) = 9aa3816e  ✅ 完全匹配
```

**⇒ 普通 CRC32，可重算。**

### 3.2 `mount.conf` 分区表

```
/dev/nflasha2 /setting  vfat 20,971,520
/dev/nflasha3 /system   vfat 50,331,648
/dev/nflasha5 /wbi      wbi  62,914,560
/dev/nflasha7 /rootfs   raw   8,388,608
...
```

---

## 四、⇒ 完整更新流程（相机侧）

```
1. SD 卡根目录放 BODYDATA.DAT（文件名固定，不可改）
2. 菜单 → 设置 → 版本 → 软件更新 → 执行
3. 校验 UFU 容器：magic + 段结构 + DEND(CRC32)
4. 校验 model ID 与本机是否匹配
     ✗ 不匹配 ⇒ "更新文件版本不正确"（我们以前遇到过这个提示的来源）
5. 按 crypterName 解密 FDAT（AES 分块）
6. 得 UDTRFIRM → 解 tar（99 项）
7. 读 mount.conf，按 0700_part_image / 0110_backup 写各分区
8. 每写一个文件，用 0x01_*_sum 里的 CRC32 校验
9. 全写完 → 重启
```

---

## 五、★★★ 战略含义：可以造"官方更新包"

**若要给 ZV-E10 刷自定义 `/usr`：**

```
① 取 ZV-E10 官方包（Ver 2.03）作为骨架（crypter 正确、model 正确）
② 替换 tar 里的 /usr 分区镜像为我们构建的混合版
③ 重算该文件的 CRC32 → 更新对应 *_sum
④ 重新打包 tar
⑤ 用 CXD90045 密钥重新加密 FDAT（我们有两把钥匙）
⑥ 重建 UFU 容器 + 重算 DEND(CRC32)
⑦ 命名 BODYDATA.DAT 放 SD 卡 → 走官方菜单更新
```

**⇒ 这比 fastboot 安全得多：**
- 走 Sony 自己的 updater（有失败恢复逻辑）
- 所有校验都是我们可以正确计算的
- 不需要拆机 / 不需要 USB 特殊模式

---

## 六、⚠️ 尚未确认的风险点

| # | 风险 | 验证方法 |
|---|---|---|
| 1 | FDAT 头部（0x208 字节）内是否有**签名/机型绑定** | 解析 UDTRFIRM 头字段 |
| 2 | updater 是否拒绝**降级**或同版本 | 看 libupdaterbody/libupdatertalk 逻辑 |
| 3 | EPROM/OTP 是否有防回滚计数 | 需实机 |
| 4 | ZV-1F 的 `_k8` 变体密钥是否需要（若直接用 ZV-1F 包） | 用 ZV-E10 骨架则不需要 |

**⇒ 第 1、2 条可以离线查；第 3 条只能实测。**

---

## 七、前人工具（`/mnt/build/ref-qn06142-zv-e10-re/`）

| 工具 | 作用 |
|---|---|
| `research/firmware/fdat_decrypt.py` | FDAT 解密（含密钥、分块规则、校验） |
| `research/firmware/forge_dat.py` | 从 firmwareData blob 构建 .dat 容器 |
| `research/firmware/updater_key.py` | 从 `libupdatertalk.so` 读 AES 密钥 |
| `research/firmware/fwtool_ma1co.py` | 基于 ma1co fwtool 的完整解包/打包 |
| `fw/ufu_tool.py` | UFU 容器 info/check/extract/**repack** |

**⇒ `ufu_tool.py` 已支持 repack，是现成的起点。**
