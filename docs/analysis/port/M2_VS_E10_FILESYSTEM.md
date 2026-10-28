# ZV-1M2 vs ZV-E10 文件系统对比

## 一、分区布局：**完全不同**

| | ZV-E10 | ZV-1M2 |
|---|---|---|
| `/usr` (nflasha15) 偏移 | `0x2D800000` | `0x2E800000` |
| `/usr` 大小 | `0x12C00000` = **300 MB** | `0x2BC00000` = **700 MB** |
| `/rootfs` (nflasha7) | 8 MB | 8 MB |
| 整体分区表 | 各自独立，偏移全不同 | |

**⇒ 两台的整个分区布局不是同一套。**

## 二、`/usr` 总规模：主体几乎一样大

| | ZV-E10 | ZV-1M2 |
|---|---:|---:|
| 文件数 | **908** | **49,921** |
| 总大小 | **215.6 MiB** | **425.5 MiB** |
| —— 去掉 `screenreader` —— | 215.6 MiB | **218.3 MiB（只差 2.7 MiB）** |

**★★★ 差异 209.9 MiB 中，207.2 MB 全部来自 `share/screenreader`（49,124 个文件）——
语音引导（TTS）数据。ZV-E10 完全没有这个目录。**

## 三、`/usr` 架构：**两代不同的组织方式**

### 3.1 目录级对比

| 目录 | E10 文件 | M2 文件 | E10 大小 | M2 大小 |
|---|---:|---:|---:|---:|
| `bin` | 48 | 42 | 9.2 MB | 9.1 MB |
| `lib` | 168 | 149 | 102.7 MB | **111.9 MB** |
| **`share`** | 450 | **49,483** | 84.5 MB | **293.3 MB** |
| `sbin` | 4 | 4 | 3.5 MB | 3.5 MB（相同）|
| `codec` | 6 | 6 | 13.0 MB | 13.0 MB（相同）|
| `kmod` | 103 | 103 | 5.1 MB | 5.1 MB（几乎相同）|
| `sabin` | 77 | 86 | 6.2 MB | 8.6 MB |
| `scenario` | 35 | 29 | 556 KB | 320 KB |
| `tool` | 3 | 5 | 147 KB | 173 KB |
| `wlan` / `wifi-scripts` | 14 | 14 | 1.13 MB | 1.13 MB（相同）|

### 3.2 ★ lib 层：架构完全不同

| 组件 | ZV-E10 | ZV-1M2 | 说明 |
|---|---|---|---|
| **UI 框架** | `libObj.so` **21.3 MB**（单体，含引擎） | **`appFw.so` 64.3 MB** | 新老两代核心差异 |
| **引擎** | （在 libObj 内） | `libObj.so` **3.9 MB** | 新架构拆分出来 |
| **菜单视图** | `viewUnified2.so` **14.3 MB**<br>`viewUnified4.so` 3.3 MB | **无** | 老菜单专属 |
| **警告/引导配置** | `CautionConfig.so` **17.7 MB** | **无** | 老菜单专属 |
| **媒体处理** | `libmpr.so` **13.0 MB** | `libmpr.so` **3.0 MB** | M2 版小得多 |
| 系统定义 | `libSysDef.so` 3.3 MB | `libSysDef.so` 6.0 MB | M2 版大 |
| 加密/安全 | — | `libInfraCryptCore.so` 7.8 MB | M2 新增 |
| | | `libInfraSKD.so` 7.3 MB | |
| lib 合计 | 168 文件 / 102.7 MB | 149 文件 / 111.9 MB | |

**⇒ 老菜单把 UI 拆成 `libObj + viewUnified×8 + CautionConfig`；
新菜单合并成单个 64 MB `appFw.so` + 3.9 MB 引擎。**

### 3.3 `share` 子目录

| 子目录 | E10 | M2 |
|---|---|---|
| `app` | 308 文件 / 83.8 MB | 232 文件 / 85.5 MB |
| **`screenreader`** | **0** | **49,124 文件 / 207.2 MB** |
| `network` | 96 / 292 KB | 100 / 316 KB |
| `avsystemsound` | 37 / 168 KB | 18 / 116 KB |
| `aveffect` | 4 / 131 KB | 4 / 131 KB（相同）|
| `data`/`pmbp`/`terminfo` | 相同 | 相同 |

**⇒ `/share/app`：M2 文件更少但更大（232 vs 308，85.5 vs 83.8 MB）——
新菜单把 214 个 `view*.uxc` 换成了少量 `master_*.uxc` + 更大的图像资源。**

## 四、★★ `rootfs`（nflasha7）：**几乎完全相同**

| | ZV-E10 | ZV-1M2 |
|---|---:|---:|
| 文件数 | 136 | 136 |
| 总大小 | 4,509,455 B | 4,509,591 B |
| 条目（含符号链接）| 220 | 220 |
| **字节级完全相同** | **218 / 220** | |
| **不同** | **2 个** | |

### 4.1 唯一不同的两个文件

| 文件 | E10 | M2 | 差异 |
|---|---|---|---|
| `version.txt` | `30110_06.2026060501` | `22110_05.2025042601` | 机型/版本号 |
| **`sbin/init`** | 28,580 B | 28,716 B | 见下 |

### 4.2 `sbin/init` 的实质差异（M2 多两件事）

```
① WBSHD=/tmp_wbshd/shared.XXXXXX
   tmpfs → /tmp_wbshd          （M2 的 init 挂一个 tmpfs 共享目录）
② /usr/kmod/iris_dial.ko      （M2 的 insmod 列表多一项）
```

**⚠️ 但 `iris_dial.ko` 在两版固件里【都不存在】⇒ 那一行 insmod 会失败但被容忍，
不是真正的缺口。**

**⇒ `/tmp_wbshd` 是 M2 新增的共享目录机制（可能是 web 分享/传输用），
E10 的 init 不挂它。**

### 4.3 M2 rootfs 多 3 个空目录

```
tmp_cloud  /  tmp_remote  /  tmp_wbshd
```

**⇒ 都是空目录，应由 init 挂载。我们在混合 rootfs 里已经补建了这 3 个。**

## 五、⇒ 对移植的意义

| 层 | 结论 |
|---|---|
| **rootfs** | **基本不用动**（218/220 字节相同）。唯一功能差异是 `/tmp_wbshd` 和 `iris_dial.ko`（后者两版都无，无害）|
| **`/usr`** | **必须整套换**（架构不同：appFw vs libObj+viewUnified+CautionConfig）|
| **容量** | M2 的 `/usr` 去掉 screenreader 后 218.3 MiB，E10 分区 300 MB ⇒ **装得下（余 82 MB）** |
| `kmod` / `codec` / `wlan` / `wifi-scripts` | 两版几乎相同（kmod 103:103、codec 相同、wlan 相同）|
| `sabin`（DSP 固件） | M2 多 9 个 / +2.4 MB |

**⇒ 这也解释了为什么混合镜像能装进 300 MB：主体本来就只有 218 MiB。**
