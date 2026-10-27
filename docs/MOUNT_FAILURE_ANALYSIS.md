# 刷入后 /usr 分区不被挂载 —— 分析

> 现象：刷入 M2 混合镜像后，分区一直不被相机挂载，无任何 UI；
> 刷回老菜单正常。**说明问题在分区挂载层，不在运行时。**

## 一、已排除的因素

| 项 | 原厂 | 混合镜像 | 判断 |
|---|---|---|---|
| 分区大小（partinf.conf） | `nflasha15` offset `0x2D800000` size `0x12C00000` = 314,572,800 B | 镜像 314,572,800 B | ✅ 一致 |
| UUID | `c509f8ed-…` | 同 | ✅ |
| magic | 0xEF53 | 同 | ✅ |
| 特性位 compat | `0x38` | 同 | ✅ |
| 特性位 incompat | `0x02` (FILETYPE) | 同 | ✅ |
| 特性位 ro_compat | `0x03` (SPARSE_SUPER\|LARGE_FILE) | 同 | ✅ |
| block size | 1024 | 1024 | ✅ |
| `e2fsck -fn` | 通过 | 通过 | ✅ |
| **本机 loop 挂载** | **成功** | **成功** | ✅ |

**⇒ 文件系统本身是完好的（本机可挂载、fsck 通过、特性位与 UUID 一致）。**

## 二、发现的三处差异

| 字段 | 原厂 | 混合镜像 |
|---|---|---|
| **`s_inode_size`** | **128** | **256** ← 内核含 `error: unsupported inode size: %d` |
| **`s_blocks_count`** | **243,264（237.56 MB）** | **307,200（300 MB，占满分区）** |
| `s_volume_name` | 空 | `usr` |

**内核侧证据（`handoff_p0/vmlinux.bin`）：**
```
"error: unsupported inode size: %d"      ← ext2 驱动的 inode_size 校验
ext2 相关串 37 处   ext3 相关 1 处
ext4 相关 0 处      jbd2 0 处             ← 没有 ext4/jbd2
```

**⇒ 内核只有 ext2 驱动，且带 inode_size 校验。
   若它拒绝 256 字节 inode，这足以单独导致挂载失败。**

**⚠️ 但用户反馈"inode_size 128 也没用" ⇒ 需确认那一版的其它参数（是否同时改了 blocks_count）。**

## 三、★ 已生成"几何参数完全对齐原厂"的测试镜像

```
/mnt/build/fw/port_m2/out_geom/nflasha15_geom.ext2
  249,102,336 B (237.56 MiB)
  md5 8a36ab7d3ef9bb0c3fcf97176f06eb03
```

**超级块与原厂逐字段对比：**

| 字段 | 原厂 | 新镜像 |
|---|---|---|
| blocks | 243,264 | **243,264** ✅ |
| inodes | 60,960 | **60,960** ✅ |
| inode_size | 128 | **128** ✅ |
| blocks_per_group | 8,192 | 8,192 ✅ |
| inodes_per_group | 2,032 | 2,032 ✅ |
| features (三级) | 0x38 / 0x02 / 0x03 | 同 ✅ |
| UUID | `c509f8ed-…` | 同 ✅ |
| volume_name | 空 | **空** ✅ |

**超级块仅剩 31 字节差异（6 段），全部是空闲块/空闲 inode 计数、时间戳、目录 hash seed
—— 无任何结构性差异。**

```
内容: 228,650 / 243,264 blocks 已用（空闲 14 MB）
文件: 985 个（原厂 1,022 个）
核心库 md5 与 ZV-1M2 原版一致 ✅
本机 loop 挂载成功 ✅（appFw 64,273,528 B，资源 224 个）
```

## 四、⇒ 需要确认的关键问题（决定下一步方向）

```
① 怎么刷的？
   fastboot flash? dd? 写到哪个偏移？
   ★ 若偏移错了，任何文件系统改动都不会有用

② 怎么判断"分区没被挂载"？
   有 shell 吗？看的是 /proc/mounts、dmesg，还是从"无 UI"推断？

③ "inode_size 128 也没用"那一版：
   是否【确实重新刷入并重启测试】？
   那一版的 s_blocks_count 是 243,264 还是 307,200？

④ 有没有串口 / console 输出？
   内核挂载失败通常会有 "EXT2-fs: ..." 或 "VFS: Cannot open root device" 之类的打印
```

## 五、若新几何版仍不挂载，剩余可能

| # | 可能 | 验证方式 |
|---|---|---|
| 1 | **写入偏移错误** | 核对 fastboot 分区名 / dd 偏移 |
| 2 | 写入不完整 | 刷后回读前 4KB 比对 md5 |
| 3 | eMMC 某些块不可写，尾部结构损坏 | 回读校验整个分区 |
| 4 | 相机对 /usr 有额外校验（版本/签名标记） | 需看 initrd 逻辑（Sony 自定义格式，暂未解开）|
| 5 | 分区表在相机上与此处 partinf 不一致 | 用 `cat /proc/partitions` 核对实际大小 |

**⇒ 第 1、2 条最应该先排除：若能用 shell，直接
`dd if=/dev/nflasha15 bs=1024 count=4 | md5sum` 与镜像前 4KB 比对即可。**
