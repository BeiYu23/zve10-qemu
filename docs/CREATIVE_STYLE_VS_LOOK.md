# 创意风格（Creative Style）vs 创意外观（Creative Look）

> ZV-E10 的 **Creative Style（创意风格）** 与 ZV-1M2 的 **Creative Look（创意外观）**
> 不是同一功能的改名，而是**两代完全不同的色彩体系**。

## 一、结论速览

| | ZV-E10 创意风格 | ZV-1M2 创意外观 |
|---|---|---|
| 预设数 | **14** | **12** |
| 命名逻辑 | **按拍摄场景** | **按胶片/影调美学** |
| 可调参数 | **3 个** | **8 个** |
| 参数档位 | 粗（传统 ±3 级） | **细（各 64 档）** |
| UI 形态 | 单个 `viewCreativeStyle.uxc`（1,564 B） | `view_creative_look` + `view_creative_look_option`（两级） |
| 固件实现深度 | 仅 Exif 元数据标签（libObj 里 3 处） | **完整框架（appFw 里 875 处）** |
| 是否并存 | — | **完全替换**（E10 风格名在 M2 里 0 命中）|

## 二、预设列表（从字符串资源实测）

### ZV-E10 创意风格 —— 14 个，按场景
```
Standard          Vivid             Portrait          Landscape
Sunset            Night Scene       Autumn leaves     Black & White
Sepia             Neutral           Deep              Light
Clear             Twilight
```
**⇒ 逻辑：你想拍什么，就选对应场景（"拍日落选 Sunset"）。**

### ZV-1M2 创意外观 —— 12 个，按影调/胶片
```
ST (Standard)     PT (Portrait)     NT (Neutral)      VV  (Vivid)
VV2(Vivid 2)      FL (Film)         FL2(Film 2)       FL3 (Film 3)
IN (Instant)      SH (Soft Highkey) BW (Black & White) SE (Sepia)
```
**⇒ 逻辑：你想要什么样的色彩风格（"Film 给你胶片感 / 电影感"）。**

### 2.1 ★ 关键验证：不是并存，是替换
```
在 ZV-1M2 的字符串资源里搜索 E10 的风格名：
  Landscape 0 次   Sunset 0 次   Night Scene 0 次
  Autumn    0 次   Twilight 0 次  Deep 0 次   Light 0 次
⇒ E10 那套 14 个预设在新菜单里【完全不存在】
```

## 三、可调参数 —— 这是最实质的差异

### 3.1 ZV-E10 创意风格：传统三要素
```
Contrast（对比度）
Saturation（饱和度）
Sharpness（锐度）
```
**⇒ JPEG 直出时代的经典三参数。**

### 3.2 ZV-1M2 创意外观：8 个，视频调色思路
```
contrast      64 档      ← 全局对比
highlight     64 档      ← ★ 高光区独立控制（分离色调曲线顶端）
shadow        64 档      ← ★ 阴影区独立控制（分离色调曲线底端）
fade          64 档      ← ★ 褪色 / 黑位抬升（胶片感）
saturation    64 档
sharpness     64 档
clarity       64 档      ← ★ 清晰度 / 局部对比（中频）
fineness      33 档      ← ★ 细腻度
```

**⇒ 新增的 Highlight / Shadow / Fade / Clarity / Fineness 是**调色软件（如 Lightroom）的
语言**，不是相机传统参数。**
**⇒ 这说明 Creative Look 的设计目标是【影视/创作风格】，而 Creative Style 是【JPEG 直出场景匹配】。**

### 3.3 参数规模差距
```
E10：3 参数 × 约 ±3 级
M2 ：8 参数，其中 7 个各 64 档 + 1 个 33 档
⇒ 组合空间从 ~343 种 提升到 64^7 × 33 量级（名义上）
```

## 四、固件实现深度的差距

```
ZV-E10 libObj：
  "creative_style" 相关字符串 3 处，全部是 Exif MakerNote 标签
  （bMkNoteCreativeStyle / mkNoteCreativeStyle）
  UI 只在 viewCreativeStyle.uxc（1,564 B，单个屏）

ZV-1M2 appFw：
  "creative_look" 相关字符串 875 处
  专用两级界面：view_creative_look / view_creative_look_option
  每个参数都有独立选项 ID：BKID_APP_creative_look_option_<param><0..63>
```

**⇒ E10 老菜单里，创意风格的实际色彩处理大概率在 **CP（`av-cam.bin`）** 里，
libObj 只负责记录/传递选择。**

---

## 五、⇒ 对移植的影响（重要）

**M2 的 Creative Look 界面在 ZV-E10 上会正常显示 12 个预设和 8 个参数滑条，
但 E10 的 CP（`av-cam.bin`）里没有对应的色彩处理实现。**

| 层面 | 会发生什么 |
|---|---|
| UI 显示 | ✅ 正常显示 12 个预设 + 8 参数 |
| 参数选择 | ✅ 可操作、可存储 |
| **实际成像效果** | ❌ **E10 的 CP 不认识这些参数 ⇒ 不生效** |
| Exif 记录 | ⚠️ 可能写入新格式标签，老菜单回退后读不出 |

**⇒ 与 ND 滤镜同类：**功能可见但不生效**，属于"能显示不能用"的项。**
**⇒ 不会导致崩溃（参数只是数值，CP 忽略即可）。**

## 六、一句话总结

> **ZV-E10 的创意风格是"选场景"（14 个 ≠ 3 参数）；
> ZV-1M2 的创意外观是"选影调"（12 个 + 8 参数，含高光/阴影分离与褪色）。
> 二者是索尼从 JPEG 直出时代向影视创作时代的整体换代，不是同一个功能的改名。**
