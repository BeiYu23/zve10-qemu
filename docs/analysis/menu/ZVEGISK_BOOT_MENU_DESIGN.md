# zvegisk：开机选择 + 崩溃自动回滚（完整设计）

> 需求原话：「开机时选择启动新菜单-老菜单，然后崩溃时返回启动菜单那种」

---

## 一、★ 关键约束决定了设计形态

| 约束 | 影响 |
|---|---|
| **无 CPU 可见帧缓存**（`/dev/fb0` ENXIO）| **不能画图形菜单** |
| **`/dev/input/event0..2` 存在** | **✅ 可以读物理按键** |
| **`/dev/uinput` 存在** | **✅ 可以注入虚拟按键** |
| **`dmode` 只能是单字符集**（0/k/g/d/e + 数字）| 选择逻辑得靠**文件**，不能靠 dmode |
| **构造函数在 `im.elf` main() 之前跑** | **✅ 可以在 UI 读资源前 bind** |

**⇒ 所以"开机菜单"做成：物理按键选择 + 配置文件，不画界面。**

---

## 二、整体架构

```
/setting/zvegisk/
├── boot_mode        ← "old"（默认）/ "new"     人选的
├── boot_count       ← 连续启动尝试次数
├── boot_limit       ← 阈值，默认 3
├── last_good        ← 上次成功的模式
├── safemode         ← 存在则强制老菜单、完全不 bind
└── zvegisk.log

/setting/mode/dmode    = 3p
/setting/mode/preload  = /setting/zvegisk/zvegisk.so
```

### 启动流程

```
init 读 dmode=3p → 设 LD_PRELOAD=/setting/zvegisk/zvegisk.so
    ↓
execve(/usr/bin/im.elf) → ld.so 加载 zmagisk.so
    ↓
★ 我们的构造函数（在 main() 之前）
    ├─ ① safemode 存在？→ 直接 return（纯老菜单）
    ├─ ② boot_count++ 写回            ← 必须在 bind 之前
    ├─ ③ if (boot_count > boot_limit)
    │       → boot_mode = last_good（通常 old）
    │       → 创建 safemode
    ├─ ④ 读 /dev/input/event* 短窗口 500ms：
    │       按住了"新菜单键"？→ 本次强制 new
    ├─ ⑤ 按 boot_mode 决定：new → mount --bind 新菜单资源
    │                        old → 什么都不做
    └─ ⑥ fork 出 watchdog（不阻塞 im.elf）
    ↓
im.elf main() → UI 读资源（读到的是我们 bind 的）
    ↓
watchdog：睡 90 秒 → 扫 /proc/*/maps 找 viewUnified/appFw
    ├─ 找到 ⇒ boot_count = 0；last_good = boot_mode；退出
    └─ 没找到 ⇒ umount 全部 bind；reboot -f ⇒ 下次回到老菜单
```

---

## 三、★★★ 这个设计解决的真实痛点

| 痛点 | 现在 | 有 zvegisk 后 |
|---|---|---|
| 试新菜单崩了 | **拔电池，下次还是崩，循环** | **崩 3 次自动回老菜单** |
| 每次要插拔 USB | **是** | **不用，自动恢复** |
| 不知道上次为什么崩 | **只能猜** | **`zvegisk.log` 记录每次尝试** |
| 想立刻回原厂 | 要电脑 + agent | **拔 SD 卡放个 safemode 文件，或连按 3 次** |

---

## 四、三种"开机选择"的实现（可叠加）

### ① 配置文件（最灵活，可远程改）

```bash
# 通过 agent 改，立刻生效（下次开机）
cam.py run 'echo new > /setting/zvegisk/boot_mode'
cam.py run 'echo old > /setting/zvegisk/boot_mode'
```

### ② 物理按键（不需要电脑，最适合现场）

```
按住 C1（或任意键）开机 500ms 内 → 强制 new
不按 → 用 boot_mode 文件的值
```

**★ 构造函数里用非阻塞 open + read 读 `/dev/input/event*`，
超时 500ms 就放弃（不阻塞 boot）。**

**⚠️ 待验证：event0/1/2 分别对应什么设备、按键码是多少。**

### ③ SD 卡（最不需要技术，适合救砖场景）

```
SD 卡根目录放 /zvegisk/safemode  ⇒ 强制老菜单
SD 卡根目录放 /zvegisk/boot_mode ⇒ 指定模式
```

**★ 构造函数里也检查 SD 卡（如果已挂载）。**

---

## 五、安全性设计（这次不会砖）

| 风险 | 应对 |
|---|---|
| **库加载失败** | ld.so 打警告后继续 ⇒ 相机正常启动 |
| **构造函数崩溃** | **纯 syscall + 静态缓冲 + 无 malloc** ⇒ 实质不可能崩 |
| **bind 后 UI 崩** | **watchdog 90 秒后 umount + reboot** |
| **反复崩** | **boot_count > 3 ⇒ 强制老菜单 + safemode** |
| **完全进不去** | **拔 SD 卡 / 断电重启仍会走 boot_count 逻辑** |
| **想彻底回原厂** | **`echo 3 > /setting/mode/dmode` + 删 preload** |

**★★★ 最坏情况：即使 zmagisk.so 完全失效，**相机也只会以老菜单正常启动** ——
因为 preload 失败不影响 bootin 继续 exec im.elf。**

---

## 六、实施步骤

```
Phase 1（最简，先跑通回滚）
  · zmagisk.so 只做三件事：boot_count++ / 判断超限 / fork watchdog
  · 不 bind 任何东西
  · 验证：故意让 watchdog 失败 ⇒ 确认 3 次后自动回老菜单

Phase 2（加选择）
  · 加 boot_mode 文件读取
  · 加按键检测（需先摸清 event* 与键码）

Phase 3（加内容）
  · 加 mount --bind 新菜单资源
  · 这一步才真正"启动新菜单"

Phase 4（可选）
  · 加 GRM 绘制注入（画框线/指示条）
```

**★ 每阶段都可以独立验证，且 Phase 1 完全无害（不 bind 任何东西）。**

---

## 七、待验证清单

| 项 | 怎么验 |
|---|---|
| `event0/1/2` 各是什么设备 | 读 `/proc/bus/input/devices`（当前为空，可能要装驱动后才填）|
| 按键码 | 构造函数或 agent 里非阻塞读 event*，按键看数值 |
| `ui_running()` 的判定是否可靠 | 用映射里有 `viewUnified`/`appFw.so` 判断（agent 已验证有效）|
| reboot 是否需要延迟 | `reboot -f` 立刻生效；建议先 `sync` |
| `/dev/input` 是否被 im.elf 独占 | 若 open 失败，改用 `/dev/uinput` 或 hook `infra_input` |
