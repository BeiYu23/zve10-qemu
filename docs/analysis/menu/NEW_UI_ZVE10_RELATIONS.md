# 新 UI 里与 ZV-E10 相关的部分（ASTRA 平台分析）

> 问题：新 UI 内部对 ZV-E10 / ASTRA 到底有多少原生支持？别人"从 ZV-1F + ZV-E10 II 取材"意味着什么？

---

## 一、★ 最重要的结论：**ZV-1F 与 ZV-E10 是同一架构（ASTRA）**

拿两边 `libObj.so` 的 Astra 相关字符串做集合对比：

```
ZV-1F  libObj: 164 条
ZV-E10 libObj: 165 条
ZV-1F 独有   : 0 条
ZV-E10 独有  : 1 条（一条版本/日期串 "for NH ASTRA Jun ..."）
```

⇒ **平台层几乎完全相同**，二者同为 **CXD90045 / ASTRA**。

**这意味着 ZV-1F → ZV-E10 的移植是"同架构内移植"，不是跨架构移植。**
ZV-1F 是索尼"**新 UI 配老 SoC**"的过渡机型 —— 这就是它成为唯一供体的根本原因。

---

## 二、新 UI 只有两种架构，且已为 ASTRA 写了 34 条守卫

`appFw.so` 里架构枚举只有两个取值：

```
VAL_FEATURE_MANAGER_system_architecture_TYPE_SYSTEM_ARCHITECTURE_ASTRA
VAL_FEATURE_MANAGER_system_architecture_TYPE_SYSTEM_ARCHITECTURE_IPSX
```

针对 ASTRA 的显式守卫共 **34 条**（完整清单）：

| 类别 | 条目 |
|---|---|
| **EVF**（我们无 EVF，本就不需要） | `EVF selected in MSG_ID_VDF_DISP_SET_PANEL_BRIGHTNESS_SLOPE` / `…SET_PANEL_OUT` / `Astra not supported(EVF). Skip send MSG(VDF)` / `… and normal return` |
| **HDMI 高规格输出** | `hdmi_set_definition_raw` / `hdmi_set_output_frame_rate_for_raw_output` / `hdmi_set_output_frame_rate_uhd` / `hdmi_set_resolution_raw` |
| **McIf 静态编解码**（7 条） | `MID_STILL_DEC_OPEN/CLOSE/DECODE_FRAME`、`MID_STILL_ENC_OPEN/CLOSE/ENCODE_FRAME`、`still_dec_pi_info_t`、`still_dec_yuv_info_t` |
| **VDF 显示扩展** | `MSG_ID_VDF_DISP_SET_LINE_VALID_BIT_WIDTH` / `VDF_INPUT_SET_KEEP_RESOLUTION_EFFECT_ENABLE` / `VDF_INPUT_SET_OSD_MUTE_NOTIFY` / `VDF_DISP_SET_PANEL_OUT_NOTIFY` / `VDT_SET_OUTPUT_PIN_FORMAT_PARAM` |
| **音视频收尾** | `MSGID_REQ_VIDEO_FINALIZE` / `MSGID_REQ_AUDIO_FINALIZE` / `MSGID_GET_OUTPUT_AUDIO_FORMAT` / `MSGID_SET_RECFORMAT_FREQ` / `SET_REC_FORMAT_INFO` / `MSG_ID_SYSND_GET_SYSTEM_SOUND_ADDRESS` |
| **触摸灵敏度** | `INFRA_TP_UIPC_SETTPSENSITIVITY` |
| **风扇** | `UPM_FAN_MODE`（ZV-E10 无风扇） |
| **拍摄 PID**（4 条） | `PID_SET_BULB_MODE` / `PID_SET_EXPOSURE_CANCEL` / `PID_SET_FLASH_SHUT_SPEED` / `PID_SET_FOCUS_BRK_EXEC_COUNT` / `PID_SET_FOCUS_BRK_EXEC_STATE` |

### ★ 这里有个关键的推论

**ZV-1F 自己就是 ASTRA** ⇒ 这 34 条限制在 ZV-1F 上**本来就生效**。

⇒ **移植到 ZV-E10 不会新增任何限制**：ZV-E10 上跑的仍是同一套 ASTRA 守卫，
菜单能力与 ZV-1F 一致（也是索尼官方在这两个机型上的实际差异）。

**这一条把"运行时接缝"的风险又降了一档。**

---

## 三、能力表不落盘 —— 它来自平台，不是数据文件

新 UI 的机型能力抽象：

```
VAL_FEATURE_MANAGER_*  → 仅 3 组：camera_parameters_ / lens_mount_ / system_architecture_
VAL_HAITA_*            → 198 个能力项（AF/曝光/防抖/媒体/网络/显示…）
```

**全盘搜索**：`VAL_HAITA` 串只出现在 **`appFw.so`**，
ZV-1F 的 `/usr/share/app/` 下**没有任何能力表数据文件**
（那里只有字体 `.ltt/.ttf`、`area_check_data.dat`、`color.uxb`、`global.xdb`、`image.uxb`、`lang.uxb`、`style.uxb`、`fontlist.dat`）。
ZV-E10 的 `nflasha3/7/15` 里 `VAL_HAITA` 命中数均为 **0**。

⇒ **能力键在代码里拼出来，值由平台运行时提供**（PRM / 内核参数）。
⇒ **ZV-E10 II 的固件同样不会提供这张表。**

---

## 四、新 UI 是控件超集，ZV-E10 的控件全覆盖

`appFw.so` 里的物理控件标识统计：

| 控件 | 出现次数 | ZV-E10 是否有 |
|---|---:|---|
| `TOUCH` | 620 | ✅ |
| `REAR_DIAL` | 104 | ✅ |
| `MODE_DIAL` | 103 | ✅ |
| `MULTI_SELECTOR` | 62 | ✅ |
| `STILL_MOVIE`（S&Q 开关） | 54 | ✅ |
| `ZOOM_LEVER` | 46 | ✅ |
| `FRONT_DIAL` | 41 | ❌ **我们没有**（说明 UI 是跨机身超集） |
| `MOVIE_BUTTON` / `REC_BUTTON` | 3 / 2 | ✅ |
| `CONTROL_DIAL` | 2 | ✅ |

⇒ 机身没有的控件靠能力位裁剪，**不需要为 ZV-E10 单独准备 UI 二进制**。

---

## 五、"从 ZV-1F + ZV-E10 II 取材"到底是什么意思（我的推断）

结合上面的事实：

| 来源 | 提供什么 | 依据 |
|---|---|---|
| **ZV-1F** | **可执行代码 + 平台层** | 同为 ASTRA / CXD90045；libObj Astra 串 164 vs 165 几乎相同 |
| **ZV-E10 II** | **机身形态正确的 UI 资源** | 与 ZV-E10 **机身完全同款**（APS-C、E 卡口、无 EVF、同按键布局）；且是新 UI 资源集里更新的版本 |

即：**代码走 ZV-1F（架构对得上），资源和机身相关数据走 ZV-E10 II（形态对得上）**。
这是很合理的工程拆分，也解释了为什么单靠 ZV-1F 会有"机身不匹配"的残留问题。

### ⚠️ 一个我无法验证的前提

ZV-E10 II 用的是 **BIONZ XR（CXD90057 世代）**，按代际推断它应该是 **IPSX** 而不是 ASTRA。
若如此，**它的可执行代码不能用于我们**（架构不同），只能取其**资源**。
这一点必须有它的固件才能确认。

### ⚠️ 我们目前**没有** ZV-E10 II 固件

```
_archive/files/unpacked_zve10m2/      → 空目录
_archive/files/ZVE10M2_BODYDATA.DAT   → 仅 34 字节（占位/下载失败）
```
这是我们与"别人"之间最直接的差距。

---

## 六、建议的下一步

1. **补齐 ZV-E10 II 固件**（Sony 官网可下 `Update_ILCE10M2*.exe`，用 `fwtool.py` 解包）——
   才能验证 §五的两个假设，并对比它的 UI 资源与 ZV-1F 的差异
2. **在拿到之前，ZV-1F 单源方案在架构上是成立的**：同 ASTRA、控件全覆盖、能力表来自平台
3. **优先复核的资源类差异**（ZV-1F vs ZV-E10 II 一旦拿到）：
   `image.uxb` / `lang.uxb` / `style.uxb` / `global.xdb` / `color.uxb` / `area_check_data*.dat`
   —— 这些直接决定菜单外观与文案是否贴合 ZV-E10 机身
