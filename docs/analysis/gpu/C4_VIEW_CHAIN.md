# C4 结果：视图创建链（`global.xdb` → 工厂 → `dlsym`）

> 纯离线解析，基于 `zve10_delivery/nflasha15.img`。
> 目的：回答「`global.xdb` 之后是什么、谁创建第一个 view」。

---

## 一、`global.xdb` 是**视图资源注册表**（`uxa` 容器）

```
文件: /share/app/global.xdb   12,252 B
头:   "uxa\x07" + u32 version(0) + u32 ?(9) + u32 ?(0x1de0ef) + u32 0
结构: 0x14 起为递增的 u32 偏移表，指向若干 entry；
      每个 entry = 小 header（4×00 + 1×0x80，实测）+ NUL 结尾的 .uxc 文件名
内容: 310 个 .uxc 文件名（viewXxx.uxc / image_*.uxc / LayoutMaster_*.uxc）
```

**它是"有哪些视图资源"的索引**，不是参数库。

## 二、xdb 与磁盘的交叉核对（重要）

| 项 | 数量 |
|---|---:|
| `global.xdb` 列出的 `.uxc` | **310** |
| 磁盘上实际存在的 `.uxc` | **299** |
| 列出但不存在 | **11** |
| 存在但未列出 | **0** |

那 11 个缺失项：

```
_169（16:9 面板变体，4:3 机器不需要）：
  image_cmn_169.uxc            image_Fn_Menu_169.uxc
  image_DSC_Entry_Only_169.uxc image_DSLR_Only_169.uxc
  image_DSC_Only_169.uxc       image_Rich_Help_Guide_169.uxc
  image_cmn_o_direct_169.uxc
非面板变体（4 个）：
  LayoutMaster_Other.uxc
  image_For_DSC.uxc
  image_For_DSLR.uxc
  viewUUG_Update.uxc
```

> ⚠️ **逻辑关键**：真机用**同一份 `/usr`、同一份 `global.xdb`、同样缺这 11 项**，
> 而真机 UI 完全正常 ⇒ **这些缺失项不是卡点**，加载器能容忍。
> 不要让 QEMU 侧去补这些文件，那是伪路径。

补充：`master_normal.uxc` 在 ZV-E10 上**既不在磁盘也不在 xdb**（那是 ZV-1F 的文件），
`LayoutMaster_Menu.uxc` 在磁盘上（菜单布局主表，存在）。

## 三、视图创建的真实机制：**按名字 `dlsym`**

关键证据：

1. `viewUnified2.so` 导出 **36 个 `View*ToInstance` 工厂函数**（各 36 B 的薄跳板）
2. 这些工厂地址**没有任何重定位引用**（`R_ARM_ABS32`/`RELATIVE` 里都查不到）
   ⇒ 不存在静态函数指针表
3. `libObj.so` 和 `viewUnified2.so` **都导入 `dlsym` + `dlopen`**
4. `viewUnified2.so` 里有 `cannot find key: ` 与 `global.xdb`、`/usr/share/app/area_check_data.dat`、`/tmp/area_check_log.dat`

⇒ 合理架构：**xdb 给名字 → 派生出 `ViewXxxToInstance` → `dlsym` 取函数 → 调用得实例**。

### 36 个工厂函数（可直接用于下断）

| 函数 | 地址 | 函数 | 地址 |
|---|---|---|---|
| ViewBaseMenuToInstance | 0x2de0f7 | ViewNtscPalSEL_DToInstance | 0x2dd45d |
| ViewBatteryAuthenticationToInstance | 0x2e039b | ViewObjecttrackingBaseToInstance | 0x2dbfdd |
| ViewBeautyEffectToInstance | 0x192a49 | ViewObjectTrackingToInstance | 0x237a3d |
| ViewCautionToInstance | 0x231a13 | ViewPanoramaStlToInstance | 0x20e185 |
| ViewCreativeStyleToInstance | 0x8cbc57 | ViewRootPowerSwToInstance | 0x329165 |
| ViewDisplayQRCodeToInstance | 0x2e1cf1 | ViewRootRemoteToInstance | 0x3326d5 |
| ViewDummyBlackToInstance | 0x236b0b | ViewRootSelectorToInstance | 0x41b6a9 |
| ViewExpModeToInstance | 0x8cf63b | ViewRootToInstance | 0x324a55 |
| ViewFtpSettingTransferToInstance | 0x19654b | ViewRootUtilityToInstance | 0x4288b9 |
| **ViewGlobalMenuToInstance** | **0x2e4c0f** | ViewScnSelectionToInstance | 0x23afbb |
| ViewIndividualRecognitionToInstance | 0x2d94c7 | ViewSettingMenuToInstance | 0x2f36f5 |
| ViewIREditOneToInstance | 0x8d25d5 | ViewShootingInfoDispToInstance | 0x23d1c9 |
| ViewIRShootingToInstance | 0x8d821f | ViewShutterSpeedToInstance | 0x23e92b |
| ViewMovieExpModeToInstance | 0x19ec01 | ViewSinglePb4KToInstance | 0x3227ff |
| ViewMovieRecPatchToInstance | 0x1f5799 | ViewSinglePbToInstance | 0x24ace5 |
| ViewSmileShutterToInstance | 0x24002f | ViewSnapShotBootToInstance | 0x3240df |
| ViewSpecialModeToInstance | 0x25614d | ViewStlrecToInstance | 0x28cba1 |
| ViewVoiceMemoToInstance | 0x76aa9f | ViewZoomPbToInstance | 0x8c6ca3 |

（地址为 `viewUnified2.so` 内偏移；该库导出 7 个更大，另 6 个 `viewUnified3..8.so` 各有自己的工厂集。）

## 四、给 QEMU 侧的可执行建议

**不要去解 299 个 .uxc**（收益低）。改为：

1. **hook `dlsym`** —— 记录每次请求的符号名。这会直接显示：
   - App 请求了哪个 `View*ToInstance`
   - 是否返回 NULL（找不到）
   - 若返回 NULL，那就是"view 从未创建"的直接原因
2. 若 `dlsym` 都没被调用到 `View*` 名，说明卡在更早的 `cannot find key:` 查表
   —— 那就 hook 那条日志，看它查的是什么 key
3. 同时抓 `dlsym` 之前的环境：`global.xdb` 是否成功解析出表（310 项 vs 读到的项数）

## 五、与 UIPC 消息风暴的关系

QEMU 侧观测到 `qid 0x3E014D` 被发送 17,493 次（`ret=-1024 队列不存在` 25 次）。
这与我方在 `libWlanAdjust` 看到的 `osal_valloc_msg_wait`（UIPC 阻塞等回复）同族。

**两条线索的关系**：很可能是 App 在"公共资源就绪 → 请求创建 view"之间，
先向某个 UIPC 服务要状态（`0x3E014D`），拿不到就重试，永远走不到 dlsym。
⇒ 建议**同时 hook `osal_valloc_msg_wait` 的 qid 与 `dlsym`**，看哪个先卡住。

## 六、结论摘要

| 问题 | 答案 |
|---|---|
| `global.xdb` 之后是什么？ | `lang.uxb`/`color.uxb` 等公共资源 → `viewUnified2.so` → **按名字 dlsym 取 `View*ToInstance`** |
| 谁创建第一个 view？ | `ViewBaseMenuToInstance` / `ViewGlobalMenuToInstance` 这类工厂，经 `dlsym` 定位 |
| 缺 11 个 uxc 是原因吗？ | **不是**，真机同样缺且正常 |
| 最有效的下一步 | hook `dlsym`（看请求的符号名与返回值）+ hook `osal_valloc_msg_wait`（看 qid） |
