# ZV-E10 防砖设计（实机改动安全基石）

> 触发：用户要求「加一个超级防砖，比如几次启动失败自动禁用我们的更改」
> 结论：**启动计数器在本机不可行**，但存在一套更强的保护 —— 且已在 `agent_v14` 里实现。

---

## 一、先明确威胁模型：当初为什么变砖

**对比两种改动方式：**

| 方式 | 持久性 | 失败后果 |
|---|---|---|
| **`/etc/ld.so.preload` + 坏 .so**（当初事故） | **持久写入 `/etc`** | **永久变砖** —— 库坏了连 `init` 都起不来，且没有可运行的东西去修它 |
| **`mount --bind` + 看门狗**（现在的方案） | **仅内存** | **90 秒自动回滚 + 重启回原厂** |

**关键差别：`/etc` 是**持久 vfat 分区**（`nflasha10`，12 MB）。
写入的东西在**动态加载器执行任何用户代码之前**就已生效 ⇒ 一旦它是坏的，没有恢复机会。**

**★ 而 `/usr` 是 `ext2 ro`（内核只读挂载，LZP 解压分区）——
所以「改 /usr 再重启恢复」这条路根本不存在，写操作直接失败。**

---

## 二、★★★ 为什么「启动失败计数」在本机做不了

**这类机制（Android A/B 回滚那样）要求在「我们的改动生效**之前**」有代码能运行。**
**本机没有任何这样的位置：**

```
/etc 的真实内容在 nflasha10（已核实）：
  ✗ 无 profile
  ✗ 无 init.d
  ✗ 无 rc.local
  ✗ 无 inittab
/etc/jiritsu  → 空目录
/etc/config.spec → 空文件

/  (nflasha7)  → ext2 ro   ⇒ 改不了 /sbin/init
/usr (nflasha15) → ext2 ro ⇒ 改不了 /usr/bin/bootin.elf
```

**⇒ 启动过程完全由 `/sbin/init` + `bootin.elf` 驱动，没有脚本层钩子。
⇒ 「启动计数」没有地方落脚。**

**★ 但不需要它** —— 下面的 `mount --bind` 方案在**重启时天然恢复**，比计数器更强。

---

## 三、三层保护（现有机制）

### 第 1 层：改动不落盘（最强，架构性）

```c
apply_binds(on):
    for 每行 "源<TAB>目标" in /log/zv1f_ui/bind.txt:
        on  : mount --bind 源 目标      /* 运行时覆盖，不写磁盘 */
        off : umount 目标
```

**⇒ 重启后所有 bind 消失，回到原厂文件。**
**⇒ 即使看门狗也挂了，**一次断电重启就恢复**。**

### 第 2 层：看门狗自动回滚

```c
ui_running():
    扫 /proc/*/maps，找 "viewUnified" 或 "appFw.so"

watchdog():
    if (!存在 /log/zv1f_ui/bind.txt) return;      /* 没改过就不管 */
    for (i=0; i<9; i++) {                          /* 共 90 秒 */
        if (ui_running()) { 记录 "UI ok"; return; } /* 成功 ⇒ 保留 */
        sleep(10);
    }
    apply_binds(0);              /* 失败 ⇒ umount 全部 */
    unlink(BINDF);               /* 删开关 */
    sync();
    system("/bin/busybox reboot -f");   /* 强制重启回原厂 */
```

**⇒ UI 没起来 ⇒ 自动撤销 + 重启 ⇒ 相机自己回到原厂状态。**

### 第 3 层：武装前预检（本次新增）

**现有机制只判断「UI 有没有起来」，不判断「我们给的替换文件本身对不对」。
当初变砖正是一个坏 `.so` 被直接武装。**

**`payload_guard.c` 补上这道闸门：**

```
① 安全模式：/log/zv1f_ui/safemode 存在 ⇒ 无条件拒绝武装
② 读清单：/log/zv1f_ui/manifest.txt（每行：源<TAB>目标）
③ 逐项预检：
     · 能打开
     · 大小 > 4 字节
     · ELF magic（7f 45 4c 46）
     · EI_CLASS == 1（32 位）
     · e_machine == 40（ARM）
     · 非全零
④ 全部通过 → 写 bind.txt（交给 agent_v14 武装）
   任一失败 → 删除 bind.txt（保持不武装）+ 详细日志
```

**⇒ 坏文件在武装**之前**就被拦下，而不是等 UI 起不来再回滚。**

---

## 四、四道闸门总览

```
第 0 道：libJit 不写 /etc
          ⇒ 当初变砖的那条路径在架构上不存在

第 1 道：mount --bind 不落盘
          ⇒ 重启即恢复，与看门狗无关

第 2 道：看门狗 90 秒超时
          ⇒ UI 起不来就自动撤销 + 重启

第 3 道：payload_guard 预检
          ⇒ 坏文件在武装前被拦截
```

**★ 任何一层单独失效，其余三层仍能保证相机可恢复。**

---

## 五、操作纪律（硬性）

### 允许

```
✓ 只读操作：dd if=/dev/nflashaN、读 /proc、读 /log
✓ libJit 载荷（运行时，不落盘）
✓ mount --bind 覆盖（运行时，重启即恢复）
✓ 写 /log、/setting（独立分区，不碰 /etc 所在的 12 MB 分区）
```

### 禁止

```
✗ 写 /etc/ld.so.preload          ← 当初变砖的直接原因
✗ 往 /etc 或 /tmp 写大文件        ← 二者与 /var 共用 12 MB（nflasha10）
                                    写满会导致 /etc 无法清理 ⇒ 不可恢复
✗ 修改 /usr（本来就是只读，写了也失败）
✗ 未预检就把文件武装为 UI 替换
```

### 恢复手段（按优先级）

| 情况 | 手段 |
|---|---|
| 改动导致 UI 异常 | **等 90 秒** —— 看门狗自动回滚重启 |
| 手动想回原厂 | 断电重启（bind 消失） |
| 陷入反复回滚 | 插 SD 卡放 `/log/zv1f_ui/safemode` ⇒ 永久拒绝武装 |
| 完全无响应 | 拔电池 / 断电重启 |

---

## 六、`payload_guard.c` 用法

### 清单格式

```
# /log/zv1f_ui/manifest.txt，每行一项，TAB 分隔
/fs/new/viewUnified2.so	/usr/lib/viewUnified2.so
/fs/new/libObj.so	/usr/lib/libObj.so
```

### 流程

```bash
# 1. 写清单（可用 Jiritsu 的 SET_FILE 写，或由前一个载荷写）
# 2. 部署 guard 载荷
./run_payload.py payload_guard.so --read /tmp/jiritsu/probe.txt --keep
# 3. 读结果
#    GUARD_RESULT=ARMED                     → 已武装，看门狗接管
#    GUARD_RESULT=REFUSED_PRECHECK         → 文件有问题，未武装
#    GUARD_RESULT=REFUSED_SAFEMODE         → 安全模式，未武装
#    GUARD_RESULT=NO_MANIFEST              → 没有清单，什么都没做
```

### 输出示例（预检失败）

```
GUARD_START
MANIFEST_BYTES=86
BINDF_FD=93
CHECK /fs/new/viewUnified2.so -> /usr/lib/viewUnified2.so rc=0
   size=14284732
CHECK /fs/new/libObj.so -> /usr/lib/libObj.so rc=-3      ← 不是 ELF
   size=4096
LINES=2 PASSED=1 FAILED=1 TOTAL_BYTES=14284732
PRECHECK_FAILED -> bind.txt removed, NOT armed
GUARD_RESULT=REFUSED_PRECHECK
```

---

## 七、待加强项（已知弱点）

1. **`ui_running()` 只检查一次进程是否加载了 `viewUnified`/`appFw.so`，
   不检查它是否「稳定运行」**（比如起来 10 秒后又崩）
   ⇒ 可扩展为多阶段检查（30s / 60s / 90s 各查一次）

2. **没有回滚计数熔断**（连续回滚 N 次后自动进 safemode）
   ⇒ 当前需人工放 `safemode` 文件

3. **`payload_guard` 没有 SHA-256 校验**（只做 ELF 结构检查）
   ⇒ 加 FNV-1a 或 SHA-256 可防「同架构但内容错」的文件
