# zvegisk 架构说明

> **zvegisk** = ZV-E10 + Magisk。
> 目标：在**不写任何 flash** 的前提下，做到「开机选择模式 + 加载模块 + 崩溃自动回退」。
> 命名副产物是 zvegisk 可以理解成「相机版 systemless 修改框架」。

---

## 一、它解决什么问题

| 痛点 | zvegisk 的答案 |
|---|---|
| 想试新菜单/改 UI，但 `/usr` 只读、`/etc` 一写就砖 | **只写 `/setting` + 内存 bind，重启即恢复** |
| 改坏了要拔电池，拔完还是坏的（循环）| **连续失败 3 次自动回原厂** |
| 每次都要插 USB 才能改 | **改 SD 卡上的文本文件即可** |
| 不知道上次为什么崩 | **`/log/zvegisk.log` 记录每次启动** |

---

## 二、★★★ 为什么它能成（机制根基）

**这是整个架构最重要的一块 —— Sony 自己提供的官方钩子：**

```
/sbin/init ──读──> /setting/mode/dmode      (单字符标志文件)
                            │
                            ▼
            dmode 含 'p' ⇒ bootin.elf 读 /setting/mode/preload
                            │
                            ▼
                    putenv("LD_PRELOAD=<该文件内容>")
                            │
                            ▼
                    execve("/usr/bin/im.elf")
                            │
                            ▼
            ★ ld.so 先加载我们的 .so ⇒ 构造函数运行
                            │
                            ▼
                  ★ im.elf 的 main() 才开始
```

**★★★★ 关键时序：我们的构造函数在 UI 打开任何资源之前运行。**

**⇒ 在构造函数里做 `mount --bind`，UI mmap 到的就是我们的文件。
⇒ 这就是 Magisk「systemless」在相机上的等价物。**

### 与当初变砖那条路的根本区别

| | `/etc/ld.so.preload`（变砖）| **zvegisk（`/setting/mode/preload`）** |
|---|---|---|
| 谁读 | **动态加载器读所有进程** | **只有 `bootin.elf`** |
| init 受影响 | **是 ⇒ 起不来** | **否** |
| 失效范围 | **整机** | **只有 im.elf 一个进程** |
| 恢复 | 要改 flash | **改 `/setting` 两个文本文件** |
| 分区 | `/etc`（与 /tmp /var 共用 12MB）| `/setting`（**独立 20MB vfat**）|

---

## 三、架构总览

```
┌─────────────────────────────────────────────────────────────┐
│  存储层                                                      │
│                                                              │
│  SD 卡（可拔，电脑直接编辑）        /setting（内部，持久）      │
│  /zvegisk/                        /setting/zvegisk/          │
│   ├─ boot_mode   "old"/"new"       ├─ boot_count             │
│   ├─ safemode                      ├─ armed    ★武装开关      │
│   └─ mods/<name>/system/...        ├─ safemode               │
│        └─ 要覆盖到 /usr 的文件      └─ orig_dmode (备份)       │
│                                                              │
│                                    /setting/mode/            │
│                                     ├─ dmode = "3p"          │
│                                     └─ preload = "/setting/   │
│                                                    zvegisk/   │
│                                                    zvegisk.so"│
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼  开机时 init → bootin → LD_PRELOAD
┌─────────────────────────────────────────────────────────────┐
│  zvegisk.so（约 10 KB，零 libc 依赖，纯 syscall）              │
│                                                              │
│  构造函数 __attribute__((constructor))                        │
│    └─ fork() ──┬─ 父进程：立刻返回 ⇒ im.elf 继续  ★安全关键   │
│                │                                             │
│                └─ 子进程：work()                             │
│                     ├─ ① safemode 存在？ → 什么都不做         │
│                     ├─ ② boot_count++ ；>3 → 建 safemode      │
│                     ├─ ③ 挂 SD 卡（只读，毫秒级窗口）         │
│                     ├─ ④ 读 boot_mode                         │
│                     ├─ ⑤ 立刻卸载 SD 卡  ★避免与相机冲突      │
│                     ├─ ⑥ 按模式做 mount --bind（Phase 2）     │
│                     └─ ⑦ 若 armed 存在 → fork watchdog       │
│                                                              │
│  watchdog（只有 armed 时才启动）                              │
│    ├─ 等 90 秒                                               │
│    ├─ 扫 /proc/PID/maps 找 viewUnified / appFw               │
│    ├─ 找到 ⇒ boot_count = 0（成功）                          │
│    └─ 没找到 ⇒ umount 全部 bind + reboot -f（回原厂）          │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  im.elf + libObj.so + viewUnified*.so（Sony 原厂）           │
│  ★ 我们的 bind 已经就位 ⇒ 它读到的是我们的文件                │
└─────────────────────────────────────────────────────────────┘
```

---

## 四、核心组件

### ① 配置层：两个存储位置的分工

| 位置 | 存什么 | 为什么 |
|---|---|---|
| **SD 卡 `/zvegisk/`** | **boot_mode、模块文件** | 电脑可直接编辑；**拔卡 = 强制回原厂** |
| **`/setting/zvegisk/`** | **boot_count、armed、safemode** | 内部持久，**不依赖 SD 是否插着** |

**★★★ 这个分工是刻意的：**
- **计数和武装标志必须在内部**（否则拔卡就绕过保护）
- **模式选择和模块文件在 SD**（方便修改，且拔卡即失效）

### ② 三个控制文件

```
boot_mode   "old" | "new"       ← 人选的模式
armed       （存在即武装）        ← 看门狗开关 ★测试时绝不创建
safemode    （存在即停用）       ← 优先级最高，任何情况都不动作
```

**优先级：`safemode` > `armed` > `boot_mode`**

### ③ 双层保护

```
第 1 层：boot_count 计数
    每次加载 ++ ；超过 3 ⇒ 创建 safemode ⇒ 永久停止
    ⇒ 治「反复失败」这个循环

第 2 层：watchdog 90 秒检查
    只 bind 不验证是不够的，所以要有"事后确认"
    没看到 UI ⇒ 撤销 bind + 重启
    ⇒ 治「绑了坏文件但当次没发现」
```

---

## 五、完整开机流程（含所有分支）

```
开机
 │
 ├─ init 读 dmode = "3p"
 │   └─ 不是 "3p"？ ⇒ 完全不加载 zvegisk ⇒ 原厂启动（恢复方式③）
 │
 ├─ bootin 读 preload → LD_PRELOAD=/setting/zvegisk/zvegisk.so
 │   └─ 文件损坏/不存在？ ⇒ ld.so 警告，im.elf 照常启动（安全）
 │
 ├─ ld.so 加载 zvegisk.so
 │   └─ 构造函数的父进程立刻返回 ⇒ im.elf main() 开始
 │
 └─ 子进程 work()
     ├─ safemode 存在？ ──是──> 只写日志，什么都不做 → 原厂启动
     │
     ├─ boot_count++ ；> 3？ ──是──> 建 safemode → 原厂启动
     │
     ├─ 挂 SD（只读）
     │   └─ 失败（没插卡/坏卡）？ ⇒ 只写日志 → 原厂启动（恢复方式①）
     │
     ├─ 读 boot_mode
     │   ├─ "old" ⇒ 什么都不做 → 原厂启动
     │   └─ "new" ⇒ 做 mount --bind（Phase 2）→ 我们的界面
     │
     ├─ 立刻卸载 SD（释放给相机自己用）
     │
     └─ armed 存在？ ──是──> fork watchdog
                              ├─ 90 秒后 UI 起来了 ⇒ boot_count=0（记住成功）
                              └─ 没起来 ⇒ umount 全部 + reboot -f
```

---

## 六、★★★ 安全模型（为什么它不会变砖）

### 结构性保证

| 保证 | 怎么做到的 |
|---|---|
| **零依赖** | **`-nostdlib` + 纯 syscall，未定义符号 0，无 NEEDED** |
| **父进程路径只有一个 syscall** | **`fork()` → 判断 → 返回，零阻塞** |
| **所有工作都在子进程** | **子进程崩/挂都不影响 im.elf** |
| **段对齐正确** | **off/vaddr 差 0x10000（a7c2 记录过错位会被 SIGKILL）** |
| **init_array 正确** | **entry = zvegisk_init 地址，已验证** |

### 失效场景对照

| 场景 | 后果 |
|---|---|
| **.so 文件损坏** | **ld.so 警告 → 相机正常启动** |
| **构造函数崩溃** | **父进程已 fork ⇒ im.elf 继续** |
| **fork 失败** | **走父进程路径 ⇒ 直接返回** |
| **子进程崩溃** | **只死自己** |
| **子进程 hang** | **im.elf 无恙；可能持有短暂 mount** |
| **写 /setting 失败** | **计数不生效（保护失效），无副作用** |
| **watchdog 误触发** | **已加 `armed` 开关，测试时不启动** |

### 三种恢复方式（都不需要 USB）

```
① 拔 SD 卡           → 找不到配置 → 原厂启动
② 卡上放 safemode    → 跳过加载 → 原厂启动
③ echo 3 > dmode     → 不加载 preload → 原厂启动
```

**★ 最可靠的是 ①：物理拔卡，完全绕过软件。**

### ⚠️ 已发现并修掉的两个真实风险

| 风险 | 修法 |
|---|---|
| **SD 卡读写挂载会堵住相机自己的挂载**（vfat 不支持多重挂载）| **改成只读挂载 + 读完立刻卸载（毫秒级窗口）** |
| **watchdog 在测试时也会 90 秒后重启** | **必须存在 `armed` 标志才启动** |

---

## 七、当前状态（截至本次会话）

### ✅ 已验证（在真机上跑通）

```
zvegisk start
boot_count=1
sd=250:11              ← 找到 SD 分区
sd_mounted             ← 挂载成功
boot_mode=old          ← 读到配置
mode=OLD -> stock boot ← 分支正确

（连跑 3 次后）
boot_count=4
over limit -> safemode ← ★ 自动回退正确触发

（safemode 存在时）
safemode -> do nothing ← ★ 保护生效
```

**⇒ Phase 1（计数 + 回退 + 读配置）**逻辑全部验证通过**。**

### ❌ 还没做

| 项 | 说明 |
|---|---|
| **mount --bind（Phase 2）** | 代码里还是 TODO，只写日志 |
| **watchdog 实际验证** | 需要 90 秒，且需要 armed 标志（测试会消耗计数）|
| **部署到开机** | 还没设 `dmode=3p` + `preload` |
| **物理按键选择** | 需要 hook `APPFWINFRA_INPUT_GETEVENT`（未开始）|

### ⚠️ 待处理

```
/log/zvegisk.so        相机上仍是旧版（watchdog 无开关）
                       源码已修好，新版 10,084 B 未上传
/setting/zvegisk/      boot_count 需要重置为 0，删掉 safemode
```

---

## 八、下一步（按风险从低到高）

```
① 【零风险】上传修好的 .so，用 agent 隔离跑一次
   ⇒ 确认日志出现 "not armed -> no watchdog"，且相机 2 分钟内不重启
   ⇒ 这一步证明「测试不会再触发重启」

② 【低风险】实现 Phase 2 的 mount --bind
   ⇒ 先只 bind 一个无害文件（如 /usr/share/app/style.uxb，540 B）
   ⇒ 验证 bind 生效 + watchdog 正常

③ 【中风险】真部署到开机
   ⇒ 备份原 dmode → 创建 armed → 写 dmode=3p + preload → 重启
   ⇒ 出问题：拔卡 / safemode / 改 dmode

④ 【高难度】物理按键选择
   ⇒ 需要逆向 Sony 的输入 API
```

---

## 九、这个架构的边界（诚实说明）

**zvegisk 能做的：**
- **换 UI 资源**（`.uxc` / `global.xdb` / 字体 / 配色）
- **换/替换库文件**（如 `viewUnified2.so`）
- **开机模式选择 + 崩溃回退**

**zvegisk 做不到的（已实测排除）：**
- **自己画像素** —— 真机无 CPU 可见帧缓存（`/dev/fb0` ENXIO、
  `0x20312000` 实测是描述符+压缩数据、四种像素格式解码全失败）
- **开机前显示图形菜单** —— 无可用显示通道

**⇒ 所以 "cokee-zvegisk UI" 在真机上的现实形态是：
**「换资源 + 换库」，不是「自己画」。**
