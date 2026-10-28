# ZV-1M2 混合镜像的部署说明（给真机侧）

## 一、镜像性质

```
/mnt/build/fw/port_m2/out/nflasha15_hybrid.ext2
  314,572,800 B  = 分区表值，完全一致
  Block count 307,200 × 1024 = 314,572,800  ✅
  Inode 76,912   空闲块 66,918（余量 65 MiB）
  文件系统 ext2（无 journal）

  md5    : 16e6b6f13c4bd045128c3af0d6a5be4d
  sha256 : becb4821f293e889f4e513175f145d63...
```

**⇒ 这是 `mke2fs -d <合并树>` 生成的【完整分区镜像】，根就是 /usr 的内容。**
**⇒ 挂载后可 bind 到 /usr（或其子目录）。**

## 二、⚠️ 关键：这是"裁剪过的 /usr"

| 目录 | 混合镜像 | ZV-1M2 原始 |
|---|---:|---:|
| `share/app` | **232** | 232 ✅ |
| `share/network` | 100 | 100 ✅ |
| `share/avsystemsound` | 53 | 18（多带 E10 版）|
| `share/aveffect`/`data`/`pmbp`/`terminfo` | 全在 | ✅ |
| **`share/screenreader`** | **0** | **49,124（207 MB）** ❌ |
| `lib` | 228 | 207（多带 E10 版）|

**总文件：923（858 文件 + 65 符号链接） vs 原始 49,986 ⇒ 缺 49,139（几乎全是 screenreader）**

### 2.1 缺失的实际影响：低

```
libIMDB 里 screenreader 注册 : 无
libObj  里 screenreader 引用 : 0
appFw   里 screenreader 引用 : 3（文案/ID，非库依赖）
ZV-1M2 /lib 下 screenreader 库: 无
ZV-E10  本身没有 screenreader（0 目录 0 库）
```

**⇒ `share/screenreader` 只是语音引导的 TTS 数据，无对应库。
   缺了 = 语音引导不可用，菜单本身不受影响。**

### 2.2 但仍建议**选择性 bind**

```c
/* 只换两棵子树（正好覆盖目的） */
mount --bind /tmp/m2usr/lib        /usr/lib
mount --bind /tmp/m2usr/share/app  /usr/share/app
```

**保住 `/usr/share/screenreader`、`/usr/bin`、`/usr/sbin` 等原厂内容 ⇒ 少一个变量。**

## 三、loop 挂载要点

```c
mknod("/tmp/zdev/loop0", S_IFBLK, makedev(7,0));
fd  = open("/log/zvegisk/m2usr.img", O_RDONLY);
lfd = open("/tmp/zdev/loop0", O_RDWR);
ioctl(lfd, LOOP_SET_FD /*0x4C00*/, fd);
mount("/tmp/zdev/loop0", "/tmp/m2usr", "ext2", MS_RDONLY, 0);
```

### ⚠️ 三个坑

1. **`mknod` 需要 CAP_MKNOD**；拿不到时可走 `/dev/loop-control`
   （major 10 / minor 237）+ `LOOP_CTL_GET_FREE` 拿空闲编号。
2. **只读挂载**：loop 用 `O_RDONLY` 打开源文件，mount 加 `MS_RDONLY`。
3. **★ 时序最关键**：`mount --bind` 必须在 **`im.elf` dlopen `libIMDB` 之前**完成。
   否则 appFw 已按老路径加载 ⇒ 换挂载无效。
   **⇒ 这也是当初用 `ld.so.preload` 的原因：preload 库在进程早期就能执行 bind。**

## 四、完整性校验清单

```
/mnt/build/fw/port_m2/out/MANIFEST.json
  923 条（858 文件：size + md5 + mode；65 符号链接：target）
  133,358 B
```

关键条目：

| 路径 | 大小 | md5 |
|---|---:|---|
| `lib/appFw.so` | 64,273,528 | `f079cda0633b6e0a990bb46d4028f0b4` |
| `lib/appFwBoot.so` | 8,312 | `bb4cdbabc23194998c97a6b4cfe8b629` |
| `lib/libObj.so` | 3,931,624 | `738a23316b0b8689370b0a8a1d5636d7` |
| `lib/libIMDB.so` | 40,596 | `91eca62f9cae6ee2e7ee34c31002a968` |
| `lib/libMWF.so` | 247,864 | `1aea74a8337172fc96c3bcb297f58ae0` |
| `share/app/global.xdb` | 16,112 | `d9505dfa49dead95b7665d98eddeeaba` |
| `share/app/style_cmn.uxc` | 2,004 | `b26bd4864468764ba07c4b3c7f9354ba` |
| `share/app/image.uxb` | 2,281,172 | `77f06dd90c1338fa527fd5ff02bd784c` |

## 五、新增两库不是 appFw 的硬依赖

```
appFw.so 的 DT_NEEDED（15 个）不含 libInfraBluetoothClassic / libInfraMFi
两者是 libIMDB 的 dlopen 服务目标
混合镜像里都已包含：libInfraBluetoothClassic.so 130,548 B  /  libInfraMFi.so 86,244 B
```
