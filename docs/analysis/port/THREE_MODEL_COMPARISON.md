# ZV-E10 / ZV-1F / ZV-1M2 三机型完整对比

## 一、基础

| 项 | ZV-E10 | ZV-1F | ZV-1M2 |
|---|---|---|---|
| 发布 | 2021-07 | 2022-10 | **2023-05** |
| **SoC** | **ASTRA** | **ASTRA** | **ASTRA**（资源名 `*_ASTRA.uxc`） |
| model ID | `0x01030010` | `0x11020010` | `0x21020010` |
| crypter | `CXD90045` | `CXD90045_k8` | `CXD90045_k8` |
| 菜单 | **老菜单** | 新菜单 | 新菜单 |
| 镜头 | 16-50mm 变焦 | 20mm 定焦 | **18-50mm 变焦** |

## 二、UI 架构规模（实测）

| 项 | ZV-E10 | ZV-1F | ZV-1M2 |
|---|---|---|---|
| **主框架** | **`libObj.so` 21.3 MB（单体）** | `appFw.so` **53.2 MB** | `appFw.so` **64.3 MB** |
| `libObj.so` | 21,305,528 | 3,929,768 | 3,931,624 |
| `viewUnified*.so` | **8 个** | **0** | **0** |
| UND 导入 | **1,940** | 718 | 754 |
| 导出 FUNC | **1,904** | 2 | 1 |
| `.uxc` 资源 | 299（`view*`） | 152 | **224**（含 `_vertical`） |
| `/usr` 分区 | 300 MB | 300 MB | **506 MB** |

**⇒ ZV-E10 是【单体巨型 libObj + 8 个 viewUnified】的老架构；
ZV-1F/1M2 是【appFw 主框架 + 精简 libObj 3.9 MB】的新架构。**

## 三、★ 渲染接口：两版 appFw 完全相同

```
ZV-1F appFw 的 GRM 导入:
  GRM_bitmapCreateFromPhys
  GRM_bitmapRelease
  GRM_fbrmCheckUpdateFinished
  GRM_fbrmRegisterTriggerFrameworkCb
  GRM_fbrmSetScreenPreference

ZV-1M2 appFw 的 GRM 导入:
  ← 完全相同的 5 个
```

**⇒ 渲染路径一致 ⇒ `swgpe_shim.c` 对两版都适用。**

**ZV-E10 的 libObj 不导入任何 `GRM_*`（它自己定义 `GRM_gpermFillrect` 等）⇒ 自包含。**

## 四、★ ZV-1M2 独有的资源结构

```
/share/app（ZV-1M2）: 224 个 .uxc/.uxb

框架（与 ZV-1F 同名）:
  LayoutMaster_Menu.uxc   LayoutMaster_Other.uxc
  master_browser / camera / network / normal / other.uxc
  color_cmn.uxc  style_cmn.uxc  lang_cmn.uxc
  color.uxb  lang.uxb  style.uxb  image.uxb

★ 34 个文件各有 3 种变体：
  _f        34   （ZV-1F 只有这个）
  _g        34   ← 新变体
  _vertical 34   ← ★★ 竖屏布局（对应固件 2.00 版"横竖布局"功能）
```

## 五、功能差异

| 功能 | ZV-E10 | ZV-1F | ZV-1M2 |
|---|---|---|---|
| Cloud 直传（Creators' Cloud） | ❌ | ❌ | ✅ 2.00 |
| Wi-Fi 信号强度显示 | ❌ | ❌ | ✅ 2.00 |
| **拍摄信息横竖布局** | ❌ | ❌ | ✅ 2.00（`_vertical` 资源） |
| 固件更新通知 | ❌ | ❌ | ✅ 2.00 |
| 触屏 | ❌ ZV-E10 **无触摸屏** | ✅ | ✅ |
| WPA2/WPA3 强制 | — | — | ✅ 2.01 |

## 六、⇒ 结论

**1. ZV-1M2 是同平台（ASTRA）的更完整版本**
- `appFw.so` 比 ZV-1F 大 **21%**（64.3 vs 53.2 MB）
- 资源多 **47%**（224 vs 152），且含 `_vertical` 竖屏变体
- `/usr` 分区大 **69%**（506 vs 300 MB）

**2. 对移植的意义**
| 用途 | 判断 |
|---|---|
| 当供体 | **ZV-1M2 更优**（功能全、资源多、同平台、GRM 接口相同） |
| `swgpe_shim.c` | **两版都可直接用**（GRM 导入完全相同） |
| 格式兼容 | 同一套 `.uxc` 语法（已解出） |
| 官方更新包 | model ID 可改（`up.sh modmod`），三机型通用机制 |

**3. ZV-E10 与两者的根本差异**
**老菜单是单体 `libObj` 21 MB 自包含架构；新菜单是 `appFw` 主框架 + 3.9 MB `libObj`。
所以"新菜单搬到 ZV-E10"本质是【换整套 UI 框架】，不是改配置。**

**⚠️ 且 ZV-E10 无触摸屏，而 ZV-1F/1M2 的新菜单是触摸设计 —— 这是移植的真正难点之一。**
