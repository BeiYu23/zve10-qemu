# zvegisk 模块系统规划（参考 Magisk / Zygisk）

> 目标：**任何模块加载失败/损坏/出错都自动跳过，绝不拖垮相机。**
> 参考 Android 生态里已经解决过同一问题的两个项目：
> **Magisk**（systemless 模块管理 + bootloop 保护）和
> **Zygisk**（代码注入 + 伴生进程隔离）。

---

## 一、与 Magisk / Zygisk 的对照

| Magisk / Zygisk 的做法 | zvegisk 的做法 | 状态 |
|---|---|---|
| **`/data/adb/modules/<id>/`** | **`/log/zvegisk/mods/<name>/`** | ✅ 已实现 |
| **`module.prop`**（id/name/version/versionCode/author/description） | **同格式，加 `type`/`risk`/`verify_port`** | ✅ 部分 |
| **`disable` 标记文件** | **`disabled` 标记文件** | ✅ 已实现 |
| **`remove` 标记（待卸载）** | **待加** | ⬜ |
| **`system/` 目录做文件覆盖** | **`system/` 目录 + bind 覆盖** | ⬜ Phase 2 |
| **overlayfs** | **❌ 内核没有** → 用 **bind + 符号链接农场** | ⬜ Phase 2 |
| **bootloop 保护**（连续失败自动禁用模块） | **`boot_count` + `safemode` + `.failed` 黑名单** | ✅ 已实现 |
| **音量键 safe mode**（开机按住音量下禁所有模块） | **❌ 读不到按键** → 用 **SD 卡 `safemode` 文件** | ⚠️ 降级 |
| **`magisk --remove-modules`**（外部批量禁用） | **改 SD 卡 `safemode` 文件** | ⚠️ 降级 |
| **Zygisk 注入 zygote 子进程** | **LD_PRELOAD 注入 `im.elf`**（只有一个进程） | ✅ 已实现 |
| **Zygisk companion process**（模块的隔离子进程） | **fork+exec 载体进程** | ✅ 已实现 |
| **`zygisk/<abi>.so`** | **`lib/<name>.so`** | ✅ 已实现 |
| **Zygisk `onLoad`/`preAppSpecialize` 等生命周期钩子** | **`__attribute__((constructor))`** | ✅ 够用 |
| **Zygisk DenyList** | **❌ 不需要**（只有一个目标进程） | — |
| **模块仓库 / 在线更新** | **❌ 不做**（相机没有包管理器） | — |

### ★★★ 两个最关键的借鉴

**1. Zygisk 的 companion process 概念** —— 模块的危险工作放在独立进程里，
   崩了不影响主进程。**我们的 `fork+exec 载体` 就是这个。**

**2. Magisk 的 bootloop protection** —— 连续 N 次启动失败就自动禁用模块，
   而不是让人手动救。**我们的 `boot_count` + `.failed` 黑名单就是这个。**

### ⚠️ 两个必须降级的地方（诚实说明）

**① 音量键 safe mode 用不了** —— `/dev/input/event*` = ENODEV，
   `input.ko` 走 Sony 私有 `udif` 框架，构造期读不到按键。
   **⇒ 替代：SD 卡根目录放 `safemode` 文件（拔卡插电脑就能做）。**

**② overlayfs 用不了** —— 内核没有该模块。
   **⇒ 替代：`mount --bind` + 符号链接农场（Magisk 在旧内核上也是这么做的）。**

---

## 二、模块类型

```
type=code       代码模块：LD_PRELOAD 进 fork+exec 的载体进程
type=overlay    资源模块：文件覆盖到 /usr（bind mount）
```

### 两类模块的失败模式**完全不同**

| | type=code | type=overlay |
|---|---|---|
| **失败表现** | **载体进程崩 / 超时** | **UI 显示错 / 引擎崩 / 声音没了** |
| **隔离方式** | **独立进程（天然）** | **覆盖层 + watchdog** |
| **检测方式** | **退出码 + `verify_port`** | **watchdog（UI 是否起来）** |
| **回滚方式** | **天然（进程死了）** | **`umount` 全部 bind** |
| **对 im.elf 的风险** | **★ 零** | **★★★★ 高（坏 .uxc 能让引擎崩）** |

**★ 结论：资源模块比代码模块**更需要**小心。**

### 资源按危险度分级（`risk`）

| risk | 资源 | 改坏的后果 | 默认姿态 |
|---|---|---|---|
| **low** | **`avsystemsound/*.bin`（37 个，共 0.1 MB）** | **最坏没声音** | **默认加载** |
| **low** | **`aveffect/*.jpg`** | **背景花屏** | **默认加载** |
| **mid** | **`*.ltt`/`*.ttf` 字体** | **乱码** | **默认加载 + 记警告** |
| **high** | **`app/*.uxc` `*.uxb` `global.xdb`** | **引擎崩 ⇒ 黑屏** | **需 `risky_ok` 才加载** |
| **never** | **`data/*.pem` `*.key`** | **认证失效** | **拒绝加载** |

**⇒ 高风险模块默认**不加载**，要 `/setting/zvegisk/risky_ok` 存在才放行。
这样第一次部署时不会因为一个坏 `.uxc` 直接黑屏。**

---

## 三、目录结构

```
/log/zvegisk/                      ← 内部存储（500 MB，不依赖 SD）
├── agent.so                       ← agent 模块（特殊位置，最先加载）
├── agent_enable                   ← 内容 "true" 才加载
├── state/                         ← 状态（自动创建）
│   ├── agent.failed               ← 失败黑名单
│   ├── <name>.failed
│   └── overlay_stack              ← 本次启用的覆盖模块列表（自动二分用）
└── mods/
    └── <name>/
        ├── module.prop
        ├── disabled               ← 存在即禁用
        ├── remove                 ← 存在即卸载（删除目录）
        ├── lib/<name>.so          ← type=code
        └── system/                ← type=overlay，对应 /usr 下相对路径
            └── share/avsystemsound/Dslr_Emnt_Shutter_Ka.bin
```

### `module.prop` 格式（对齐 Magisk）

```
id=markersound
name=快门音替换
version=1.0
versionCode=1
author=cokee
description=把「咔」换成自定义音效

type=overlay
risk=low
```

**代码模块再加：**
```
type=code
verify_port=7777
```

---

## 四、加载流程

```
work()
 ├─ ① safemode 检查（/setting 或 SD 卡）
 ├─ ② boot_count++ ；> 3 ⇒ 建 safemode，什么都不做
 ├─ ③ ★ load_modules()
 │      ├─ agent 最先（读 agent_enable）
 │      └─ 遍历 /log/zvegisk/mods/
 │           ├─ disabled?  → 跳过
 │           ├─ .failed?   → 跳过
 │           ├─ type=code  → fork+exec 载体，超时 8s，verify_port
 │           └─ type=overlay → 收集到列表（Phase 2 才 bind）
 ├─ ④ 挂 SD（只读）→ 读 boot_mode → 立刻卸载
 ├─ ⑤ overlay bind（Phase 2）
 └─ ⑥ watchdog（仅 armed）
```

**★★★ `load_modules()` 必须在 `mount_sd()` **之前** ——
模块都在 `/log`，不依赖 SD。这样**没插卡时 agent 照样起来**。**

---

## 五、四层防护（对标 Magisk bootloop protection）

```
第 1 层  进程隔离   fork+exec 载体 → 模块崩溃只死载体，im.elf 无感
第 2 层  超时收割   载体 8 秒不退 ⇒ SIGKILL → 标记失败
第 3 层  失败黑名单 写 state/<name>.failed，下次开机直接跳过
第 4 层  全局熔断   失败数 > 3 ⇒ 之后全部跳过
```

**加上顺序保证：`agent` 永远第一个加载 —— 
即使后面模块搞坏系统，你有远程通道排查。**

---

## 六、★ 资源模块的自动二分

**这是 Magisk 没有、但我们需要的能力（因为资源坏了会黑屏）。**

```
开机：
  启用 overlay 模块 → 列表写入 state/overlay_stack
  watchdog 90 秒后检查 UI
     ├─ UI 正常 ⇒ boot_count=0，删除 .failed（清空）
     └─ UI 没起 ⇒ ★ 把 overlay_stack 里【最后一个】标 .failed
                  → umount 全部 bind → reboot
                  → 下次开机自动跳过它
                  → 再失败就再往前推一个
```

**⇒ 你不用一个个手动试，日志里会直接出现：
`mod: xxx FAIL -> blacklist`。**

---

## 七、⚠️ 边界（模块系统**不能**保证的）

### 进程隔离保护的是「崩溃」，不是「破坏」

**模块以 root 运行，理论上能：**
- **写坏 `/setting`**
- **杀别的进程**
- **remount /usr 后写坏系统文件**

**⇒ 模块是**可信代码**。"自动跳过"是为了**写错不崩系统**，
不是为了防恶意模块。**

### 但有一条强保护

**模块**无法**让相机开不了机：**
```
模块崩   → 只死载体 → im.elf 继续 → UI 正常
模块超时 → SIGKILL → 标记失败 → 下次跳过
资源坏了 → watchdog → umount + reboot → 下次跳过那个模块
```

**唯一能让相机起不来的是 `zvegisk.so` 自身（fork 之前崩），
而它父进程路径只有一个 `fork()` ⇒ 几乎不可能。**

---

## 八、分阶段实施

### 阶段 1（已完成 ✅）
- **fork+exec 载体加载**
- **超时收割 + 失败黑名单 + 全局熔断**
- **`verify_port` 验证**
- **agent 最先加载 + 不依赖 SD**

### 阶段 2（下一步）
- **`type=overlay` 的风险分级**（`risk=low/mid/high/never`）
- **`risky_ok` 门槛**
- **`remove` 标记（自卸载）**
- **`overlay_stack` 记录 + 自动二分**

### 阶段 3
- **overlay 实际 bind（`/tmp/stock` + `/tmp/zovl` + 符号链接农场）**
- **首个验证模块：快门音替换（128 B 文件，零黑屏风险）**

### 阶段 4
- **`module.prop` 完整对齐 Magisk 格式（便于以后互通/迁移）**
- **`service.sh` / `post-fs-data.sh` 脚本钩子（可选）**

---

## 九、首个验证模块（建议）

**用快门音，不用 `color_cmn.uxc`：**

| | color_cmn.uxc | **Dslr_Emnt_Shutter_Ka.bin** |
|---|---|---|
| 大小 | 312 B | **128 B** |
| 改坏了 | **引擎可能崩 ⇒ 黑屏** | **只是没声音** |
| 验证 | 看框线颜色 | **听声音** |
| risk | **high** | **low** |

**★ 换掉 128 字节的快门音，按一下快门听声音变了
⇒ 覆盖机制被证明，而且零黑屏风险。**
