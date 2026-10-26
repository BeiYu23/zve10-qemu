# .uxc 资源深入：竖屏实现、schema 版本、.uxb 引用

## 一、竖屏 UI 的实现方式（回答"有没有 LayoutMaster_Vertical"）

**答：没有。竖屏 = 同一套 master 布局 + 方向化的资源变体。**

```
ZV-1M2 global.xdb 引用统计：
  _vertical    34 个（string_*_vertical.uxc）
  _horizontal  34 个
  LayoutMaster 引用 = 0        ← ★ global.xdb 不索引 LayoutMaster
```

**⇒ `LayoutMaster_Menu.uxc` / `LayoutMaster_Other.uxc` 是【硬编码名字】加载的。**
**⇒ `area_check_data_vertical.dat`（1,024,593 B）是竖屏的触摸区域定义。**

## 二、★ `style_cmn.uxc` 的 64 字节差异 = **schema 版本升级**

按偏移表语法解析三代：

| 版本 | 大小 | 偏移表项数 | 条目步长 | `+12` 高16 | `+12` 低16 |
|---|---:|---:|---:|---:|---:|
| ZV-E10 | 1,888 | 60 | 9 | 3 | 49212 |
| ZV-1F | 1,940 | 62 | 9 | 3 | 57406 |
| **ZV-1M2** | 2,004 | **64** | 9 | **4** | **64** |

**⇒ 每条记录 9 字节，每代 +2 条（60→62→64）。**
**⇒ `+12` 的高 16 位是 schema id：ZV-1M2 从 3 升到 4，且低 16 位语义变为记录数。**

### 2.1 其他资源的 schema id 三代一致（无变化）

| 资源 | ZV-E10 | ZV-1F | ZV-1M2 |
|---|---|---|---|
| `color_cmn.uxc` | hi=2 | hi=2 | hi=2（**与 ZV-1F 字节相同**）|
| `lang_cmn.uxc` | hi=3 | hi=3 | hi=3 |
| `LayoutMaster_Menu.uxc` | hi=1 | hi=1 | hi=1 |
| `master_camera.uxc` | hi=21 | hi=21 | hi=21 |

**⚠️ ⇒ `style_cmn.uxc` 是唯一的硬不兼容点：M2 的 schema 4 不能给 ZV-1F 的引擎用。**

## 三、`image.uxb` 的加载方式

```
引擎字符串里 "image.uxb":   ZV-E10=0   ZV-1F=0   ZV-1M2=0
global.xdb 里 .uxb 引用:     只有 lang.uxb / color.uxb / style.uxb（两版均 3 个）
```

**⇒ 既不被引擎字符串引用、也不被 global.xdb 索引**
**⇒ 只能由代码用构造名加载（如 `"image" + ".uxb"`），或作为 appFw 内部图集**
**⇒ 移植时保守起见应包含（2.28 MB）**

## 四、★★★ 资源不能单独换：引擎必须配套

### 4.1 索引规模完全对不上

```
ZV-E10 global.xdb:  引用 310 个 .uxc，其中 view* = 215 个
ZV-1M2 global.xdb:  引用 154 个名字，其中 view* = 0
ZV-1M2 .uxc 总数:   224，其中 view* = 0
```

**⇒ ZV-E10 的老引擎按 `view*.uxc` 组织菜单；换成 M2 的 global.xdb 后
老引擎找不到那 215 个屏 ⇒ 菜单空白或崩溃。**

### 4.2 必须整套替换的五项

| 文件 | ZV-E10 | ZV-1M2 | 后果（混用） |
|---|---|---|---|
| `libIMDB.so` | 注册 `appFw.so` | 注册 **`appFwBoot.so`** | **名字不同 ⇒ 断链** |
| `appFwBoot.so` | 无 | 8,312 B | 启动桥接 |
| `appFw.so` | 无 | **64,273,528 B** | 新 UI 全部逻辑（4,690 个 `BKID_APP_*`）|
| `libObj.so` | 21,305,528 | 3,931,624 | 精简版 |
| `share/app` | 299 `.uxc`（view*）| 224 `.uxc` | 配套资源 |

**⇒ `libIMDB.so` 在 ZV-1F 与 ZV-1M2 之间字节完全相同（185 个服务一致），这一环无风险。**

## 五、★ ZV-1M2 版混合镜像已构建

```
/mnt/build/fw/port_m2/out/nflasha15_hybrid.ext2
  314,572,800 B (300 MiB)  内容 227.60 MiB  余量 72 MiB  e2fsck 干净

appFw.so      64,273,528  md5=f079cda0  = M2 原版 ✅
appFwBoot.so       8,312  md5=bb4cdbab  ✅
libObj.so      3,931,624  md5=738a2331  ✅
libIMDB.so        40,596  md5=91eca62f  ✅
属主 57285:1000 ✅    资源 224（_vertical 35 + _g 34）✅
```

**23 个保留 ZV-E10 的项全部是硬件绑定：20 个 `kmod/*.ko` + `bt_firm.hcd` 等。**

## 六、推荐的分层测试

```
第 1 步：libIMDB + appFwBoot + appFw + libObj 四件套
        + 最小资源（global.xdb + master_* + color/style + 英语字串 + 6 个 image_ASTRA）
        ⇒ 才能看到"M2 引擎在 E10 上启动"的真实结果
第 2 步：补语言包
第 3 步：补 screenreader（207 MB，看分区容量）
```

**⚠️ `style_cmn.uxc` 必须用 M2 版（schema 4）。**
