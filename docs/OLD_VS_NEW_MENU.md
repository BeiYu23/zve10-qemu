# 老菜单 vs 新菜单：在 QEMU 里跑的区别

> 结论先行：**平台层共用，视图管理层完全不同。**
> 所以现在 QEMU 攻坚里"平台层"的成果对新菜单仍然有效，但"视图层"的补丁对新菜单无用。

---

## 一、架构对照

| 项目 | **老菜单（ZV-E10 现装）** | **新菜单（ZV-1F）** |
|---|---|---|
| UI 载体 | `libObj.so` **21 MB** + `viewUnified2..8.so`（每代一个视图引擎） | **`appFw.so` 53 MB 单体** + `appFwBoot.so` 8 KB |
| 结构 | **分层**：libObj 提供平台/引擎，viewUnified* 提供视图 | **单体**：平台调用 + 全部 UI 都在一个库里 |
| 视图管理 | **`pViewManager` / `ViewManager`**（老机制） | **自带**，不碰 pViewManager |
| 输入服务 | **`InputService`**（老机制） | 不用；走 `InfraInput::SetCallback` |
| 触摸 | 仅拍摄功能（触摸快门/对焦） | **UI 级触摸**（菜单可直接点） |
| 平台适配 | ASTRA 专用 | **多架构**（内置 `ASTRA` 分支与特性降级） |
| 字体/资源 | 老 `.uxc` | 新 `.uxc` + 新主题 + `SysSound.bin` |

## 二、★ 实测依赖对照（决定哪些 debug 有用）

### 新菜单 `appFw.so` 导入的符号

| 依赖 | 是否使用 |
|---|---|
| `ux::wgtsys::WidgetSystem::sys_getInstance` | ✅ **1 处** |
| `ux::gfxeng::GraphicsEngine::getAreaCheckExtensionManager` / `setScreenRotate` / `TextSprite` 等 | ✅ 大量 |
| `ux::resmgr::ResourceManager::sys_getInstance` | ✅ **1 处** |
| `InfraInput::SetCallback` | ✅ **1 处**（触摸注册） |
| **`ViewManager` / `pViewManager`** | ❌ **完全没有** |
| **`InputService`** | ❌ **完全没有** |

### 老菜单 `viewUnified2.so` 导入的符号

| 依赖 | 引用次数 |
|---|---|
| `WidgetSystem::sys_getInstance` | ★ 1 |
| **`ViewManager`** | ★ **3** |
| **`InputService`** | ★ **61** |

## 三、★ 对当前 QEMU 攻坚的意义（最重要）

| 你们已经做的工作 | 新菜单是否受益 |
|---|---|
| `GraphicsEngine` 手工构造（`0x145CAB8`） | ✅ **受益** |
| `WidgetSystem` 手工构造 + 自指链表头（`0x145CB1C`） | ✅ **受益** |
| `ResourceManager` 惰性创建 / 计数 | ✅ **受益** |
| `InfraInput` / 触摸回调 | ✅ **受益** |
| OSAL/IPC 队列、消息信封（`DataflowInfraMsg`） | ✅ **受益** |
| `dlopen` 配置模块链（`initializeViewConfig` 等） | ⚠️ 部分（新菜单的配置入口不同） |
| **`getLayer` / `createLayer` / Layer 注册表** | ❌ **无用**（新菜单不用 ViewManager） |
| **`InputService` / `pVM` 相关补丁** | ❌ **无用** |
| **`viewUnified2.so` 的视图逻辑** | ❌ **无用**（新菜单整个换掉） |

⇒ **现在的平台层攻坚没有白做，但"为了让 viewUnified2 跑起来"的那部分补丁，对新菜单没有价值。**

## 四、在 QEMU 里"跑起来"的区别

| | 老菜单 | 新菜单 |
|---|---|---|
| `/usr` | **当前原版即可**（ZV-E10 自己的） | **必须挂 hybrid `/usr`**（替换 200+ 文件） |
| 失败模式 | 可能逐层退让（viewport 缺失等） | **全有或全无**：appFw 起不来 ⇒ 完全没有 UI |
| 已知阻塞 | 对象图（RM/Widget/Layer/InputService） | 未验证；可能撞 PRM 参数/硬件门控 |
| 触摸 | 需要额外 shim | **自带**（只要 InfraInput 有事件） |

## 五、战略建议

**如果目标是"验证新菜单能否在 ZV-E10 上跑"**：
1. 现在的平台层工作（引擎/WidgetSystem/RM/InfraInput/IPC）**是必要前提**，继续做有价值
2. 但**可以跳过** ViewManager / InputService / viewUnified 这条路 —— 那只是老菜单的
3. 更直接的办法：**在 QEMU 里挂 hybrid `/usr`**，让 appFw 直接跑
   - 好处：测的正是目标物；appFw 自带视图管理，绕开 pViewManager 那堆坑
   - 代价：hybrid 镜像要正确（`beeplist.txt` 已知需改 TAKE_F）

**如果目标是"先让 QEMU 里出现任何画面"**：
- 继续老菜单这条路更快（已经在跑了，`viewUnified2.so` 都加载了）

⇒ **两条路不冲突：平台层共用，视图层分叉。**
