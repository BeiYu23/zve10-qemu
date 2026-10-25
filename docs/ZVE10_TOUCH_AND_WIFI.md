# ZV-E10 触摸屏与 Wi-Fi 5GHz：核实结果

> 我此前说"ZV-E10 无触摸屏"是**错的**。以下是固件里的实际证据。

---

## 一、触摸：硬件支持（用户正确），老菜单缺"手势层"

### 1.1 硬件与基础框架都在

```
内核模块: input.ko
libObj 字符串:
  ModelTouchPanel            ← 触摸面板模型类
  TOUCH_PANEL
  modelTouchPanel.so         ← 运行时加载的面板模块
  ModelTouchPanelToInstance
  OBJDISPLAY::ExecCmdFactoryComponentTouchPanel
  OBJDISPLAY::SequenceFactoryComponentTouchPanel
  ActionExeTouchScan / ActionExeTouchModeOff / ActionExeTouchSpotColor
  AirGestureAnalyzer         ← 空中手势分析器
  ObjectTouchOffCmp / ObjectTouchScanCmp
```

**⇒ 触摸面板是完整实现的：有模型、有命令工厂、有序列工厂、有触摸动作。**

**这正是"触摸对焦 / 图片缩放拖动"能工作的原因 —— 用户观察正确，我错了。**

### 1.2 ★ 真正的差异：老菜单没有【菜单手势导航】层

| 能力 | ZV-E10（老菜单） | ZV-1F（新菜单） |
|---|---|---|
| 触摸动作（对焦/扫描/取色） | ✅ `ActionExeTouchScan` 等 | ✅ |
| 面板模型 | ✅ `ModelTouchPanel` | ✅ |
| **捏合缩放控制器** | ❌ 无 | ✅ `zoom_playback_pinch_controller` |
| **上滑手势** | ❌ 无 | ✅ `BKID_APP_setting_swipe_up` |
| **捏合参数结构** | ❌ 无 | ✅ `model::ds_pinch_param` |

**⇒ ZV-E10 有触摸硬件和基础框架，但老菜单只在"拍摄辅助"里用（对焦/取色），
没有把触摸用于【菜单导航】；新菜单才有 pinch/swipe 那套手势 UI。**

**⇒ 对移植是好消息：新菜单的手势 UI 在 ZV-E10 硬件上跑得起来。**

---

## 二、Wi-Fi 5GHz：硬件/驱动/软件框架**全支持**

### 2.1 芯片：Broadcom BCM4339（双频 802.11ac）

```
驱动模块: bcmdhd.ko (522,740 B) + cfg80211.ko + wlanIrq.ko + wlanMemoryAllocator.ko
固件路径: /mnt/bcm4339.bin  （或 /usr/wlan/bcm/bcm4339.bin）
```

**⇒ BCM4339 是双频芯片，硬件具备 5GHz 能力。**

### 2.2 ★ 固件自带 3 种 NVRAM 变体，**全部含 5GHz 配置**

```
/usr/share/network/nvram/
  2_4GHz_high_5GHz_high/   bcm4339_H_H_01..05.txt
  2_4GHz_low_5GHz_high/    bcm4339_L_H_01..05.txt
  2_4GHz_low_5GHz_low/     bcm4339_L_L_01..05.txt
```

**⇒ 没有任何"仅 2.4GHz"的变体 —— 三种都带 5GHz。**

NVRAM 内容里的 5GHz 参数齐全：
```
ccode=XR  regrev=75  boardflags=0x10080401
pdgain5g=7  extpagain5g=2  rxgains5g...=...    ← 5GHz 增益/功率参数都在
```

驱动加载串（`libWlanResource.so`）：
```
firmware_path=/mnt/bcm4339.bin nvram_path=/usr/share/network/nvram/2_4GHz_high_5GHz_high/bcm4339_H_H_%02d.txt
（另有 _L_H_ / _L_L_ 两条）
```

**⇒ 驱动层没有限制 5GHz。**

### 2.3 ★ 软件框架也有频段参数

ZV-E10 `libObj` 里的 WiFi 参数帮助：
```
<settingTarget>  WIFI/ETHER/INVALID
<settingTypeWifi> WPS/APSET/SSIDPW/DEVNAME/POWERMODE/MULTIWPS/
                  MULTIAPSET/MULTIGOWPS/★GOFREQ/INVALID/NOPARAM
<frequency>      ★2_4GHZ/5GHZ          ← 参数帮助里明确列了两个频段
```

**⇒ `GOFREQ`（Group Owner / 热点频率）参数存在，且注释写的是 `2_4GHZ/5GHZ`。**

### 2.4 ⚠️ 有趣的对照：ZV-1F 反而**没有** GOFREQ

```
ZV-1F appFw 中搜 GOFREQ / 2_4GHZ / 5GHZ → 无
```

**⇒ 老菜单的 WiFi 参数集比新菜单更丰富（有 GOFREQ/MULTIGOWPS/MULTIAPSET）。**

---

## 三、⇒ 结论

| 层 | ZV-E10 5GHz 支持 | 证据 |
|---|---|---|
| 芯片 | ✅ | BCM4339 双频 |
| 驱动/NVRAM | ✅ | 3 种变体全含 5GHz |
| 软件参数框架 | ✅ | `GOFREQ` + `<frequency> 2_4GHZ/5GHZ` |
| **UI 是否暴露** | ❓ **待查** | 菜单里可能只显示 2.4GHz 信道 |
| **法规限制** | ⚠️ `ccode=XR` | Sony 自定义法规码，可能限制 5GHz 信道 |

**⇒ 用户的怀疑有依据，但限制不在硬件/驱动，而在【UI 暴露】或【法规码 XR】。**

### 下一步可查（都有明确入口）
1. **`GOFREQ` 参数的允许值表** —— 看是否只列了 2.4GHz 信道
2. **`ccode=XR` 对应的信道白名单** —— 在 NVRAM 的后续行里（per-channel 功率表）
3. **菜单资源里 AP 模式的候选频率列表** —— `.uxc` 里的选项数据
