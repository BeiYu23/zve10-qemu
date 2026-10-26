# zvegisk 模块系统：加载失败自动跳过，绝不拖垮系统

## 一、核心安全原理：**进程隔离**

```
zvegisk.so 构造函数
  └─ fork() ⇒ 父进程（im.elf）立刻返回，UI 照常启动
       │
       └─ 子进程 work()
            ├─ load_modules()
            │    └─ 每个模块：再 fork 一次 → exec 载体 → LD_PRELOAD=模块.so
            │         ★ 模块崩了 ⇒ 只死载体进程 ⇒ im.elf 完全无感
            └─ 后面才是 mask/overlay 等
```

**★★★ 关键差别：**

| 做法 | 模块崩溃后果 |
|---|---|
| **dlopen 到 im.elf 里** | ❌ **im.elf 死 ⇒ 黑屏，只能拔电池** |
| **fork+exec 载体 + LD_PRELOAD** | ✅ **只死载体 ⇒ 相机照常开机** |

**⇒ 铁律：模块**绝不能** dlopen 进 zvegisk.so（它本身活在 im.elf 里）。**

---

## 二、四层防护

```
① 进程隔离   fork+exec 载体 → 模块崩溃只死载体
② 超时收割   载体 8 秒不退 ⇒ SIGKILL + 标记失败
③ 失败黑名单 失败写 /log/zvegisk/state/<name>.failed，下次开机跳过
④ 全局熔断   失败数 > 3 ⇒ 之后所有模块全部跳过
```

**加上顺序保证：`agent` 永远**第一个**加载，
即使后面模块把系统搞坏，agent 已起来 ⇒ 你有远程通道排查。**

---

## 三、模块格式

```
/log/zvegisk/
├── agent.so            ← agent 模块（特殊位置，最先加载）
├── agent_enable        ← 内容 "true" 才加载
├── state/              ← 失败标记（自动创建）
│   └── <name>.failed
└── mods/
    └── <name>/
        ├── module.prop     type / order / verify_port
        ├── disabled        存在即禁用（删掉即恢复）
        ├── lib/<name>.so   代码模块
        └── system/...      资源模块（Phase 2）
```

### `module.prop` 支持的键

```
verify_port=7777     载入后检查该端口是否在 LISTEN（验证"真的起来了"）
type=code|overlay    留作以后
```

### 代码模块的硬性要求

**模块必须**自己 daemon 化**（fork+setsid），让载体进程尽快退出。
否则载体不退 ⇒ 8 秒后被 SIGKILL ⇒ 标记失败。**

**★ `agent_v14.so` 天然满足**：它的构造函数就是
`fork → setsid → 再 fork → agent_main()`，父进程立即返回。

---

## 四、加载顺序（为什么 agent 在前）

```
work()
 ├─ safemode 检查
 ├─ boot_count++
 ├─ ★ load_modules()        ← agent 在这里起来
 │                            ★ 放在挂 SD 之前：模块都在 /log，不依赖 SD
 ├─ 挂 SD（只读）→ 读 boot_mode → 立刻卸载
 ├─ overlay 处理（Phase 2）
 └─ watchdog（仅 armed）
```

**★★★ 关键：`load_modules()` 必须在 `mount_sd()` **之前**
—— 否则 SD 卡没插时 agent 永远不加载。**

---

## 五、安装 agent 模块

```bash
# ① 把 agent 放上去（改名成 agent.so）
#    本地文件：tools/payload/vendor/agent_v14.so
python3 cam.py push agent_v14.so /log/zvegisk/agent.so

# ② 打开开关
python3 cam.py run "echo -n true > /log/zvegisk/agent_enable"

# ③ 验证（手动加载一次，不用重启）
python3 cam.py run "LD_PRELOAD=/log/zvegisk/agent.so /bin/busybox true"
python3 cam.py run "cat /proc/net/tcp | grep ':1E61'"   # 0x1E61 = 7777
```

---

## 六、⚠️ 诚实的边界

### 进程隔离保护的是「崩溃」，不是「破坏」

**模块以 root 运行，理论上可以：**
- **写坏 /setting**
- **杀别的进程**
- **改 /usr（如果 remount）**

**⇒ 模块是**可信代码**，隔离是为了"写错不崩系统"，不是为了防恶意模块。**

### 但有一条强保护

**模块无法让相机**开不了机**，因为：**
```
模块崩 → 只死载体进程 → im.elf 继续 → UI 正常
模块超时 → SIGKILL → 标记失败 → 下次跳过
```

**唯一能让相机起不来的是**zvegisk.so 自身**（在 fork 之前崩），
而它的父进程路径只有一个 `fork()` 调用 ⇒ 几乎不可能。**

---

## 七、状态查询（出问题时看什么）

```bash
cat /log/zvegisk.log                  # 全流程日志
ls /log/zvegisk/state/                # 哪些模块被拉黑了
rm /log/zvegisk/state/<name>.failed   # 解除拉黑（修好之后）
```

**★ 日志里每个模块都会打一行 `mod: <name>` + `OK` / `FAIL -> blacklist`。**
