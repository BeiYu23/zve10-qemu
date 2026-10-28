# IMDB 补丁：把 10 个"QEMU 里必然失败"的服务摘掉

> 来源：`chocotan/a7c2-re` 的 `prepare-assets.py: patch_imdb`。
> 这是他们让新菜单在 QEMU 里起来的**必要条件之一**（此前我们完全没做这一步）。

---

## 一、他们做了什么

```python
SKIP = {'libUsbBusPower.so', 'libInfraUsbCmn.so', 'libsensapi.so',
        'libUsbControl.so', 'libBusPower.so', 'libHotplug.so', 'libInfraCrypt.so',
        'libInfraMediaPwrMgr.so', 'libNetServiceTethering.so', 'libTetheringResource.so'}
for va in range(0x1db20, 0x21d80, 0x48):     # 64 位：步长 0x48
    mask = number(va+8, '<I')
    pack_into(va+8, mask & ~7)               # 清 bit0/1/2
```

十项、每项只清 3 个 bit，**不改其它任何字节**（他们固定校验输出 SHA）。

## 二、掩码语义（本轮确认）

libIMDB 前三条记录就是 profile 定义：

```
idx=0  "IMDB:default"  mask=0x1
idx=1  "IMDB:qemu"     mask=0x6      (0x2|0x4)
idx=2  "IMDB:nfs"      mask=0x8
```

⇒ 每条记录的 mask 是**profile 位图**。`mask & ~7` = 从 **default + 0x2 + 0x4**
三个档位里移除该模块 —— 也就是 **default/qemu 档位不再加载这些服务**。

这 10 个服务全都是 USB 供电 / tethering / crypt / sensapi / hotplug，
模拟器里没有真实硬件支撑。**在真机上不能这么改**（QEMU 专用）。

## 三、★ 老菜单同样适用（掩码字节级相同）

| 服务 | ZV-E10(老) idx | ZV-1F(新) idx | mask | mask&~7 |
|---|---:|---:|---|---|
| libInfraCrypt | 78 | 79 | `0xFFFFFF9F` | `0xFFFFFF98` |
| libUsbBusPower | 86 | 88 | `0xFFFFFFBF` | `0xFFFFFFB8` |
| libInfraUsbCmn | 87 | 89 | `0x7FFFFFFF` | `0x7FFFFFF8` |
| libsensapi | 88 | 90 | `0x7FFFFFFF` | `0x7FFFFFF8` |
| libUsbControl | 89 | 91 | `0xFFFFFF9F` | `0xFFFFFF98` |
| libBusPower | 90 | 92 | `0xFFFFFF9F` | `0xFFFFFF98` |
| libHotplug | 91 | 93 | `0xFFFFFF9F` | `0xFFFFFF98` |
| libTetheringResource | 129 | 132 | `0x7FFFFFFF` | `0x7FFFFFF8` |
| libNetServiceTethering | 130 | 133 | `0x7FFFFFFF` | `0x7FFFFFF8` |
| libInfraMediaPwrMgr | 135 | 140 | `0xFFFFFF9F` | `0xFFFFFF98` |

**两版掩码完全相同，只有记录索引不同** ⇒ 同一套补丁可直接用于老菜单。

## 四、ARM32 与 64 位的差异

| | a7c2（AArch64） | 我们（ARM32） |
|---|---|---|
| 记录步长 | `0x48` | **`0x2C`** |
| mask 偏移 | `+8` | `+8`（相同） |
| 名字指针 | `+0x10`（8 字节） | `+0x10`（4 字节） |
| 表基址 | 符号 `imdb_raw` 直接定位 | `imdb_raw + 0x84` |

工具：`tools/port/patch_imdb_qemu.py`（自动定位、断言恰好 10 条、校验只改 10 字节）

## 五、产物

| 文件 | md5 | 用途 |
|---|---|---|
| `imdb/libIMDB_zv1f_qemu.so` | `2bc435014c841d7d369886164adac5b4` | 新菜单（ZV-1F 版） |
| `imdb/libIMDB_zve10_qemu.so` | `c8bfbef08453e62d51196650316f690a` | 老菜单（ZV-E10 版） |
| `zve10_hybrid_full_v5.img` | `188a49285044d274d33432b3f3db39be` | 新菜单 + 打了补丁的 libIMDB |
| `zve10_oldmenu_full.img` | `3e6566145663844a1209099688b4b818` | **老菜单 + 打了补丁的 libIMDB**（其余完全是原厂） |

两个 `.so` 都只改了 **10 个字节**（已逐个偏移核对）。

## 六、验证（`zve10_oldmenu_full.img`）

```
/usr  @732 MiB ext2 magic 53ef ✓      /setting @12 MiB FAT eb3c90 ✓
镜像内 libIMDB md5 = c8bfbef08453e62d…（= 补丁版）✓
镜像内 libObj  md5 = 12cf982dd3eaded4…（= ZV-E10 原厂）✓
e2fsck 干净
```

## 七、注意

- **真机绝对不要用这两个补丁版 libIMDB** —— 它们会摘掉 USB 供电/充电相关服务。
  仅用于 QEMU 验证。
- 是否**足以**解决我们的 NULL 崩溃：未知，但这是 a7c2 踩过并写进必需步骤的一条，
  我们此前完全没做，属于"**已知缺失的必要条件**"。
