# QEMU 侧产出的「真机可用」清单（ZV-1M2 新菜单 / v9b）

来源：QEMU 侧 40 轮排查（`docs/QEMU_M2_PROGRESS.md` §1–§134）。
下面每一条都注明**证据强度**，未验证的明确标注。

---

## 0. 立即可跑的三条判据（按顺序，10 分钟内出结论）

### ① appFw.so 到底有没有被加载（最关键，唯一可靠的判据）

```sh
grep -c appFw /proc/$(pidof im.elf)/maps
```
| 结果 | 含义 | 下一步 |
|---|---|---|
| `0` | 真机也没走到 `dlopen("appFw.so")` | 与 QEMU 同因 ⇒ 查加载器停滞（见 §2），**改 libmpr 无用** |
| `≥1` | 真机走到了 | 才需要处理符号/作用域问题（见 §3） |

⚠️ **不要用 `im_manual.log` 里的 `DMP_2D_Initialize : 0` 当证据**——
它是 `appFwBoot` 的 `Main` 里 `uxengine_init2` 的输出，发生在调用 UI 入口 **之前**。
（QEMU 侧反汇编确认：`Main` 先跑 `uxengine_init2 → initializeArena → setDatabase`，
最后才 `if (fn) fn(arg)`。）

### ② ABI 混搭是否存在（已确认存在，此处用于确认修复）

```sh
# 需要的签名：libmprctrl 要 (unsigned long)，尾部是 m
arm-linux-gnueabihf-nm -D --undefined-only /usr/lib/libmprctrl.so | grep SupportContentTypes
# 提供的签名：M2 版 libmpr 给 (unsigned int)，尾部是 j
arm-linux-gnueabihf-nm -D --defined-only  /usr/lib/libmpr.so | grep SupportContentTypes
ls -l /usr/lib/libmpr.so      # 3,038,540 = M2 版；13,021,036 = E10 版
```
**已知事实（真机已确认成立）**：
```
libmprctrl.so (md5 1350def2，两代相同) 需要 …SupportContentTypes(unsigned long) = m
M2 版 libmpr.so (3MB, e7bbaaac) 提供 …SupportContentTypes(unsigned int) = j   ⇒ 不匹配
E10 版 libmpr.so (13MB, ebd9bcba) 提供 …SupportContentTypes(unsigned long) = m ⇒ 匹配
⇒ v9b 里是「E10 的 libmprctrl + M2 的 libmpr」混搭
```

### ③ 整树 ABI 自洽性

```sh
python3 /root/fw/oss/tools/abi_scan.py /usr/lib
```
**真机树跑过的结果**（在 `port_m2/out/usr/lib` 上）：
```
有未解析 App C++ 符号的库：4 个
  AdjChkConfig.so   (1) ds_infra_pin::dump()
  appFw.so          (2) infra_wrapper_bb::adf_ext_type / imaging::set_user_matrix
  libInfraRemote.so (6) InfraRemoteSrcBGRead::{clear,update,entryTarget,…}
  libmpr.so         (1) GpsMgr::SetAllInvalidDefStrGpsInfo
```
⚠️ 这 4 个库**都不是 BIND_NOW** ⇒ 未定义符号只在**被调用时**才报错 ⇒ **潜在坑，不是必然崩**。

---

## 1. 决策树（先跑 ①，再决定方向）

```
① appFw 在 maps 里？
├─ 否（0） ⇒ 加载器根本没走到那一步
│          ⇒ 方向 A：查 IMDB 加载链（§2）
│          ⇒ 不要动 libmpr（QEMU 已证无效：换哪个版本都不影响"是否走到"）
└─ 是（≥1） ⇒ 走到了，但可能 dlopen 失败/符号没解析
           ⇒ 方向 B：查符号作用域（§3）
           ⇒ 这时用 M2 版 libmpr + 让它在 appFw 之前全局可见
```

---

## 2. 方向 A：IMDB 加载链（QEMU 侧的完整认知）

**机制**（反汇编 + 静态解析，证据强）：
```
im.elf（23,240 B）= 加载器，读 libIMDB.so 的表，逐条：
    dlopen(模块名, flags) → 若失败：dlerror() 后 return -1（静默）
                          → dlsym(handle, 入口符号)
                          → if (fn) fn(arg)      ← fn==NULL 时静默跳过
```
**表项结构**（静态解析确认）：
```
+0x0E u16  : 0=dlopen(模块名)，非0=dlopen(NULL)
+0x10 ptr  : 模块名
+0x14 ptr  : 入口符号名
+0x24 ptr  : argA
+0x28 ptr  : argB
```
**`appFwBoot.so` 的表项**（已解析，数据正确）：
```
模块名 = "appFwBoot.so"
入口   = "Application_System_Init"
argA   = "AdjChkConfig.so"      ← 走"调整检查 UI"分支
argB   = "AppConfig.so"         ← 走"主 UI"分支（正常路径）
```
**`Application_System_Init` 的行为**（反汇编，证据强）：
```c
if (arg == NULL || strcmp(arg, "AppConfig.so") == 0)
    h = dlopen("appFw.so", RTLD_LAZY|RTLD_GLOBAL);          // 主 UI
    name = "Application_Start";
else
    h = dlopen("AdjChkConfig.so", ...);                     // 调整检查 UI
    name = "app_adjchk_ui::main_loop";
fn = dlsym(h, name);              // 返回值不检查！
osal_create_waitable_thread(Main, fn);   // Main 里 cbz → fn==NULL 就跳过
```

**QEMU 侧观测**：
- `appFwBoot.so` 在 maps 里（作为依赖加载），但入口**从未被调用**；
- `libBizInitializer.so` / `appFw.so` **从未被 dlopen**；
- **加载停点每次不同**（124 条 / 106 条）⇒ 竞态而非确定性死锁。

---

## 3. 方向 B：符号作用域（若 ① ≥1）

**关键事实（QEMU 实测）**：`appFw.so` 依赖 `_ZN4sidb7referer7s_countE`，
**该符号只在 M2 版 libmpr.so 里定义**（E10 版没有）。

```
dlopen("libmpr.so", RTLD_LOCAL)              → 符号局部 ⇒ appFw 解析不到
dlopen("libmpr.so", RTLD_LAZY|RTLD_GLOBAL)   → 全局 ⇒ appFw 能解析 ✓（QEMU 已验）
DT_NEEDED libmpr.so（加在 appFw.so 上）      → 作为依赖加载，符号可见 ✓
LD_PRELOAD=/usr/lib/libmpr.so               → 全局 ✓（但 QEMU 上导致启动 stall，慎用）
```

---

## 4. ⚠️ 明确「不要做」的事

| 做法 | 为什么不行 | 证据 |
|---|---|---|
| **v9b2 = v9b + E10 版 libmpr.so** | QEMU 实测：注册事件 181（看似健康）但 **appFw.so 永远加载失败**（缺 s_count） | §116 |
| 找 125 个"缺失模块" | IMDB 有 6 张按 target 分组的表，200 个是并集；且多数"缺失"是 libObj 内部组件/幽灵 | §125/§126 |
| 用 `DMP_2D_Initialize : 0` 判断 appFw 被尝试 | 那是 `fn()` 之前的输出 | §127 |
| 期望 `libmprdummy.so` | 各树里都不存在 | §123 |

---

## 5. 已排除的假设（省时间）

缺内核模块 · 缺真实 `dma330` · `0x7a0020` 未注册（**伪 bug**：reg 表是回调注册清单，两代都不注册它）· QEMU 缺硬件原理不可行 · "卡是慢" · 启动器版本混搭 · 日期服务（`0x90013d` 只读，写操作被 -1024 拒绝，a7c2 的 seed-date-clock 不适用）· `target=qemu` vs `product` 参数 · IMDB 表数据错误 · dlsym 静默失败（表项 entry 非空）

---

## 6. 工具与产物

| 文件 | 用途 |
|---|---|
| `tools/abi_scan.py` | 整树 ABI 符号扫描（自动解析 libstdc++、排除误报） |
| `tools/guest/m2probe.c` | QEMU 探针（M2LIB/M2RCV/M2MAPS/M2DL/M2SHIM 等） |
| `tools/guest/m2-init-known-good.sh` | 已知可用的 init 版本 |
| `docs/QEMU_M2_PROGRESS.md` | 全部排查记录（§118 事实链、§125–134 加载器分析） |

---

## 7. 如果要继续 QEMU 侧，需要什么

1. 真机 ① 的结果（最重要）；
2. 真机能起 UI 时的 `/proc/<im.elf>/task/*/stack` 或完整日志；
3. 允许一次性外部 ptrace 注入（不改目标时序的观测方式，尚未尝试）；
4. 若确实缺模块 ⇒ 完整的 M2 固件树。
