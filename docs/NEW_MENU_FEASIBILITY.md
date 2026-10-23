# ZV-E10 适配新菜单（ZV-1F）可行性研究 · 2026-10-24 复核

> 本文在既有移植报告（`ZV1F_PORT_MASTER_REPORT.md`）基础上，
> 用**今晚新拿到的证据**重新核对结论。分三层判定：ABI / 体积 / 运行时。

---

## 结论先行

| 层 | 判定 | 依据 |
|---|---|---|
| **L1 ABI 闭包** | ✅ **闭合**（本次实测） | appFw 717 个导入，628 由 ZV-1F 自带库解析，剩余 89 全是标准运行时（见 §2） |
| **L2 体积** | ✅ **装得下** | 合并 207.71 MiB 装入 300 MiB 分区，余量 64.25 MiB |
| **L3 运行时接缝** | ⚠️ **未验证，唯一真风险** | 参数查询（`PRM_*`）+ 硬件能力门控，只能在 QEMU/真机验证 |
| **变更范围** | ✅ **只动 `/usr`（nflasha15）** | 启动链（nflasha1/3、B0/B1）、`/setting` 均不动 |
| **回滚** | ✅ **已具备实证能力** | eMMC 整区写回（今晚已验证写路径） |

**一句话**：**技术上可行已不成问题；成败取决于运行时参数能否对上，
而验证它 prerequisite 是 QEMU 里先能把 UI 跑起来——那正是当前卡点。**

---

## 一、供体唯一性（复核，无变化）

| | ZV-E10（目标） | ZV-1F（供体） | ZV-E10 II |
|---|---|---|---|
| SoC | CXD90045 | **CXD90045** | CXD90057 ❌ |
| 内核 | 3.0.27_nl-rt106+ | **同** | 4.9.337 ❌ |
| 工具链 | gcc 4.5.1 / glibc 2.11.2 | **同** | gcc 6.4 ❌ |
| 面板 | 4:3 | **4:3** | 3:2 ❌ |

⇒ **ZV-1F 仍是唯一可用供体。**

---

## 二、ABI 闭包：本次实测（新数据）

用 `ref_zv1f/usr/lib` 全部 196 个库建提供集合，检查 `appFw.so` 的导入：

```
appFw.so 导入符号          717
由 ZV-1F 自带库解析        628  (87.6%)
残余                        89
```

**残余 89 个逐个看过，没有一个是 Sony 专有符号**，全部是标准运行时：

| 类别 | 例子 | 由谁提供 |
|---|---|---|
| ARM EABI 辅助 | `__aeabi_idiv` / `__aeabi_d2lz` / `__aeabi_uldivmod` … | `libgcc_s.so.1` |
| libstdc++ 模板/内部 | `_ZNK…ParamStd_Template…` / `__cxa_*` | `libstdc++.so.6` |
| glibc | `strcmp` / `strlen` / `usleep` / `__errno_location` / `__tls_get_addr` … | `libc.so.6` |
| 弱符号 | `_Jv_RegisterClasses` / `__gmon_start__` | 链接器按 0 处理 |

**两台机器这三个运行时完全相同**（同内核版本、同 Sony CE Linux 8.2.7.6 / gcc 4.5.1、
同 glibc 2.11.2，`libstdc++.so.6` 两边都在）⇒ **A 级闭合，无真实缺口。**

> 与旧报告"714/717（99.6%）"一致：那 3 个是 2 个弱符号 + 1 个仅作字符串出现的
> `infra_wrapper_bb::detail::adf_ext_type`。

**今晚新增的一条 ABI 证据**：新菜单的触摸注册走 `InfraInput`，
`appFw.so` 导入的 4 个 `InfraInput` 符号（`SetCallback` / `GetInstance` /
`RmCallback` / `ReleaseInstance`）**在 libObj 里全部有导出** ⇒ 触摸通路 ABI 也对得上。

---

## 三、体积：装得下（沿用已完成的装配核算）

| 项 | 值 |
|---|---|
| ZV-E10 `/usr` 内容 | 215.57 MiB |
| ZV-1F `/usr` 内容 | 309.79 MiB |
| **合并后** | **207.71 MiB** |
| ZV-E10 `/usr` 分区 | **300 MiB** |
| 余量 | **64.25 MiB** |

两刀裁剪（必须）：
- 删 ZV-1F 的 `share/screenreader/`（107.9 MiB，屏幕朗读语音库，ZV-E10 无此功能）
- 删 ZV-E10 老菜单资源 238 个文件（97.2 MiB，新 UI 不引用）

决策统计（788 个文件）：`TAKE_F 152` / `ADD_F 123` / `KEEP_E10 480` /
`DROP_OLD_UI 238` / `DROP_LEGACY 5` / `DROP_F_HARDWARE 14` 等。

**产物已存在**：`nflasha15_hybrid.ext2`（314,572,800 B，`e2fsck` 通过）。

---

## 四、★ 真正的风险：L3 运行时接缝（只能运行验证）

### 4.1 参数查询

新 UI 的机型自适应走 `PRM_*` / `VAL_*` 查询，由 `infra_wrapper_mpr_param_builder`
从 MPR 取。**参数定义在新 UI 里，值由平台侧给。**

对 ZV-E10 而言：UI 与中间件取 ZV-1F 版，但**平台侧（LIRO / global infra /
scenario / 内核驱动）保持 ZV-E10** ⇒ 这条问询链是唯一没有静态证据的接缝。

### 4.2 硬件能力门控（接缝大小实测）

`appFw.so` 里相关字符串计数：

| 特性 | 出现次数 | ZV-E10 是否具备 |
|---|---:|---|
| `EVF` | **244** | ❌ 无 EVF（ZV-1F 也没有，属家族超集残留） |
| `HOTSHOE` | **170** | ✅ 有 MI 热靴（ZV-1F 无） |
| `STEADY`（防抖） | 167 | ✅ 有机身防抖 |
| `SHUTTER_TYPE` | 67 | ✅ 机械+电子快门 |
| `LENS_MOUNT` | 13 | ✅ E 卡口可换镜头 |

⇒ 新 UI **带着全家族的特性代码**，靠运行时 PRM 裁剪。
⇒ 风险不是"崩溃"，而是**菜单项出现/消失不符合预期**（例如出现 ZV-E10 没有的功能项）。

### 4.3 全有或全无

新菜单**删掉了 `viewUnified*.so`**（老 UI 引擎），换成了 `appFw.so`。
⇒ **`appFw` 起不来 = 完全没有 UI**（不是"退回老菜单"）。

这是必须先在 QEMU 里验证、而不能直接刷真机的核心理由。

---

## 五、与"当前卡点"的关系（关键判断）

| 事实 | 影响 |
|---|---|
| QEMU 里**连老 UI 都还没跑起来**（`pViewManager` 恒 0，卡在消息池耗尽） | 新菜单无法在 QEMU 里验证 |
| 已确认 `Application_System_Init` 本来就不建 ViewManager，卡点是 `osal_valloc_msg_wait` 池耗尽 | 这是**平台层**问题，与菜单新旧无关 |
| 新菜单同样依赖 `libObj`（InfraInput / ux::wgtsys / MWF） | **旧链跑通 ⇒ 平台层被证明完整 ⇒ 新菜单接缝少一大块** |

⇒ **顺序上必须先解决 QEMU 的旧 UI 卡点**，否则无法验证新菜单的 L3 风险，
只能盲刷真机（而"全有或全无"让盲刷代价很高）。

---

## 六、建议路径

```
① 【现在】QEMU 里解决 pViewManager 卡点（消息池/0x13A）
      ↓ 旧 UI 能起来 = 平台层验证通过
② QEMU 里挂 hybrid /usr（把 nflasha15_hybrid.ext2 当作 /usr）
      ↓ 看 appFw 能否起来、PRM 问询是否有回音
③ 真机：先 eMMC 整区 dump nflasha15（300 MiB，多块读）
④ 真机：写 hybrid 镜像（300 MiB，多块写）
⑤ 若异常：写回 ③ 的原始镜像（已验证的写路径 + 只读通道校验）
```

**真机风险已被我们有意识地压到最低**：只改 `/usr`、不动启动链、
dump 先行、写路径已实证、只读通道可独立校验任意文件。

---

## 七、诚实的不确定性

1. **PRM 问询链**（最高）：ZV-E10 平台能否回答新 UI 的全部问题 —— 无静态证据
2. **`im.elf` / `bootin.elf`**：这两个是 ZV-E10 的，启动新 `appFw` 的跳板在
   `appFw.so` 自己导出的 `Application_System_Init` 里；但 im.elf 走 IMDB 判定，
   在 ZV-E10 上是否会调用到 appFw 版本 —— 需实测
3. **`/rootfs` 需要补 3 个空目录**（`tmp_cloud` / `tmp_remote` / `tmp_wbshd`）——
   在 nflasha7 上，属启动链外，但**要动 nflasha7**
4. **面板/色彩**：`color_cmn.uxc` 等主题资源属新 UI，需整体替换（已在 manifest 里）
