# 无电脑更新方案 + ZV-1M2 vs ZV-E10 菜单差异

## 一、没有电脑，只有手机 → 只能走 SD 卡

| 更新方式 | 需要什么 | 手机能否完成 |
|---|---|---|
| **SD 卡更新** | 把 `BODYDATA.DAT` 拷到卡根目录 | ✅ **手机 + OTG 读卡器即可** |
| **USB/PC 更新**（ZV-E10 官方方式） | Windows + Sony 更新程序 | ❌ 做不到 |
| fastboot | OTG + 特定工具 | ⚠️ 取决于你的方案 |

**⇒ SD 卡路径对手机完全可行，它只是"复制一个文件"的动作。**

### 1.1 但现在的问题：ZV-E10 老菜单没有 SD 入口

```
ZV-E10：菜单里没有「软件更新」（官方只给 PC 更新程序）
ZV-1M2：菜单 → 设置 → 设置选项 → 版本 → ★软件更新
```

**⇒ 现在的状态是死循环：**
```
想用 SD 卡更新  →  需要新菜单的入口
想要新菜单      →  需要先刷写
刷写            →  没有电脑
```

**⇒ 打破循环只有两条路：**
1. **用 fastboot 刷一次**（你已经验证过能救砖）→ 之后所有更新用手机写 SD 卡
2. 或找到老菜单里的隐藏触发（`up.sh` 机制存在，但没有 UI 入口）

### 1.2 ★ 装上新菜单后的收益

**以后所有固件更新只需：手机写 SD 卡 → 相机菜单点更新。完全不需要电脑。**

---

## 二、菜单架构差异（这是最根本的）

| 项 | ZV-E10 老菜单 | ZV-1M2 新菜单 |
|---|---|---|
| **屏幕定义方式** | **214 个 `view*.uxc`（每屏一个数据文件）** | **0 个 view 文件** |
| UI 实现位置 | 数据驱动（`.uxc`） | **代码驱动（`appFw.so` 64 MB）** |
| 菜单项标识 | 无 | **4,690 个 `BKID_APP_*`** |
| `master_camera.uxc` | 101,804 B | 101,868 B（**几乎相同，差 64 B**） |
| `LayoutMaster_Menu.uxc` | 3,512 B | 3,636 B |

**⇒ 两版的 master 布局文件几乎一样，差异在于：
新菜单把 214 个屏幕搬进了 `appFw.so` 代码里，用 `BKID_APP_*` 标识。**

**⇒ 这就是为什么"新菜单搬到 ZV-E10"本质是【换整套 UI 框架】，而不是改配置。**

---

## 三、ZV-1M2 相对 ZV-1F 增加的菜单能力（416 个新标识）

| 功能族 | 新增项数 | 说明 |
|---|---|---|
| **license / expired_license** | 15 | 授权管理（Cinema Line 类功能） |
| **ftp_server_settings_passive** | 9 | FTP 服务器（被动模式） |
| **image_rating 过滤/复制** | 12 | 按星级筛选/复制 |
| **stream / video_stream / stream_cipher** | 9 | 视频流输出 |
| **extern_auto_customize** | 3+ | **Creative Look 自定义**（亮度/对比/饱和度/锐度/褪色等） |
| **shoot_set_registration_spot_boost** | 4 | 拍摄设置注册 |
| **setting_access_authentication** | 2 | 访问认证 |
| **setting_shooting_screen_swipe** | 2 | **滑动手势** |
| **setting_playback_filter** | 2 | 播放筛选 |
| `auto_nd_filter` / `cinematic_mode_look` / `cinematic_mode_mood` | 各 1 | ND 滤镜、电影感模式（框架能力，未必在本机启用） |

**ZV-1F 独有 30 项**：主要是包围曝光选项（`bracket_option_*`）、S&Q 曝光模式、
`icon_touch_function_switch_button_state`、`setting_dial_wheel_lock`。
**⇒ 多为 ZV-1M2 里改名而非删除。**

**⚠️ 重要caveat：`BKID_APP_*` 是【框架能力词表】，不等于本机菜单里真实出现的项目。
ZV-1M2 是紧凑机，`ND filter`/`Cinematic mode` 这类更可能是共享代码库带的，未必启用。**

---

## 四、用户可见的功能差异（ZV-1M2 vs ZV-E10）

| 功能 | ZV-E10 | ZV-1M2 |
|---|---|---|
| SD 卡固件更新入口 | ❌ | ✅ |
| **触摸手势导航**（滑动/捏合） | ❌ 只有触摸对焦 | ✅ |
| **竖屏布局**（`_vertical` 资源 ×34） | ❌ | ✅ |
| Creative Look 自定义 | ❌ | ✅ |
| Cloud 直传（Creators' Cloud） | ❌ | ✅ |
| Wi-Fi 信号强度显示 | ❌ | ✅ |
| 固件更新通知 | ❌ | ✅ |
| FTP 服务器 | ❌ | ✅ |
| WPA2/WPA3 强制 | ❌ | ✅ |
| **菜单设置项数量** | **214 个独立屏（更细）** | 4,690 个标识（更现代） |
| **Wi-Fi 参数丰富度** | **✅ 有 GOFREQ/MULTIAPSET/MULTIGOWPS** | ❌ 反而没有 |

**⇒ 不是简单的"新的更好"：ZV-E10 老菜单在某些方面（Wi-Fi 参数粒度）反而更全，
但新菜单在交互方式（触摸手势）、显示适配（竖屏）、和现代功能（Cloud/FTP/Creative Look）上领先。**
