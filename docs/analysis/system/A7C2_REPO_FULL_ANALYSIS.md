# a7c2-re 仓库完整分析（对 ZV-E10 项目的直接价值）

> 来源：`chocotan/a7c2-re`（A7C II + FX30 逆向与 QEMU 原生相机实验）
> 分析日期：2026-10-25，基于 commit `e43c4d0`

---

## 一、仓库全貌

| 报告 | 核心结论 |
|---|---|
| `2026-10-16-system-dump` | USB 服务模式拿 root + 19 分区整机转储；**记录唯一不可恢复状态的教训** |
| `2026-10-17-qemu-native-camera` | **主成果**：原固件菜单 + 原生窗口 + 九步操作 |
| `2026-10-18-libjit-exec` | **libJit root 入口**（替代 `ld.so.preload`），两台真机验证 |
| `2026-10-18-fx30-service-probe` | 服务链是**协议级**的，跨机型可用 |
| `2026-10-19-cmos-input-path` | 无 V4L2；帧走 `stream2` mmap；拿到 JPEG/NV12，不是 Bayer |
| `2026-10-19-ssh-feasibility` | 原厂 sshd 是 `MaxSessions 0`；自建 2222 端口拿到 root shell |
| `2026-10-21-developer-modes` | dmode 完整表；**preload 只有 4 条白名单** |

---

## 二、★★★ 「被否证的捷径」清单（`BOOT-TROUBLESHOOTING.md` §18）

| 做法/结论 | 为什么不能算修复 |
|---|---|
| 换真机 Backup/setting、显示尺寸、DmmCfg、完整 cmdline 必然出菜单 | 阴性对照做过；**没让链路前进** |
| 只看文件里的零、vptr 命中、固定显存全零 | 可能是 relocation/BSS、非活对象、另一块双缓冲；需运行身份与租期 |
| **「未观察到」=「没有发生」** | **用户态钩子看不到内核消费**；短日志漏后半段；错 VA 哨点不触发 |
| **guest 命令 / OSAL send 返回 0 = 有效动作** | **可能无人接收、invalid qid、上层忽略错误**；必须看消费者与内容 |
| **裸构造菜单、手拼 manager、忽略 NULL mutex** | **会破坏共享池或掩盖缺失初始化**；应恢复原生命周期 |
| **给未知队列空 ACK / 随意 signal / 重复 BB ready** | **能破坏所有权、时序或 promise，只产生假进展或新崩溃** |
| 加载所有真机驱动 = 模拟真机 | USB/MMC/PCIe 曾在应用启动前真实崩溃；硬件依赖必须逐项建模 |
| 九步导航通过 ⇒ Enter 在任何菜单都安全 | 日期设置反例已否证 |
| Fsys 写文件 / F12 截图 = 已拍摄 | 没经过 sensor/encoder/MPR/快门/REC 链 |

### 同页的更正表（重要）

| 早期判断 | 后续纠正 |
|---|---|
| 某队列"无人应答的启动订阅" | **日志注册由 `kikilog.ko` 消费，内核消费不经过用户态接收钩子** |
| send 返回 0 表示投递成功 | **原内核报 invalid qid，不能当作消费者收到了消息** |
| 重复发 BB ready 推动建屏 | **那些 promise 属录像编码器；重复兑现可触发 `future_error(2)`** |

### 第二帧卡死的真实原因（§8）

> ready 信号量 `0x02886047` **已返回**，随后等的是 **`ExternalOSDIF` gate `0x02886044`**
> —— **不能混为一把锁**。

原帧完成链：`grm_gpe/CmdGpeFlush → 同步 Flush → callback → 交换缓冲`。

---

## 三、★★★ 变砖的确切机制与唯一预防

`RECOVERY-AND-LESSONS.md` §1 / §3.6：

```
/tmp、/etc、/var 是【同一块 12 MiB vfat 分区】(nflasha10)
  → 写大文件填满
  → 之后任何写入失败：0x10000009
  → 【连"把 /etc/ld.so.preload 置为中性"的清理写入也失败】
  → 预加载停在不可确认状态
```

**"救不了"的判据（实测）：**

> `/bin/busybox` 是**动态链接**、`/bin/sh` 是它的符号链接、`init`/`init2` 的 shebang 是 `#!/bin/busybox sh`
> ⇒ **坏库让每个动态程序失败 ⇒ init 都起不来 ⇒ 救砖钩子永不执行**
> ⇒ **只能预防：永不把未校验的库留在武装状态。**

### 替代方案：`/etc/profile`（已验证）

```
init/init2 结尾是 while true; do ash --login; done
/etc 可写
⇒ 写脚本进 /etc/profile ⇒ 登录 shell source 它，【以 root 身份】
✓ 跨断电重启持久  ✓ 覆写 2 字节即失效
✓ 语法错误只让登录 shell 报错，不会像坏库那样连 busybox 都起不来
```

### 救砖网（必须健康时预装）

| 位置 | 作用 |
|---|---|
| `/etc/profile` | 钩子：挂卡 → 执行卡上 rescue 脚本 |
| 卡上 rescue 脚本 | **先把 `/etc/ld.so.preload` 截断为 0 字节**，再还原备份 |
| `ld.so.preload.saved` | **仅作参考、永不还原** |

---

## 四、★★★ libJit root 入口（完整机制）

```
cmd 0x10 (adj_cmd_set_file2):
  mount /dev/nflasha6 → /tmp/jiritsu      (25 MiB; EBUSY 视为成功)
  open("/tmp/jiritsu/libJit.so", 0x241)   (O_WRONLY|O_CREAT|O_TRUNC)
  write → fsync → close
  strcmp(path, "/tmp/jiritsu/libJit.so")  ← 全串相等，`..` 无效
  dlopen(path, RTLD_LAZY=1)               ← flags = 1
  dlclose()
```

| 事实 | 含义 |
|---|---|
| **无 `dlsym`、无符号名要求** | **只要求 `_init` 能跑** |
| `dlopen` 先跑 `DT_INIT` | **文件写成 + 能 dlopen ⇒ 代码一定先执行** |
| 127 个 UND 符号中无 `setuid/setgid` | 密码学意义上不降权 |
| `/tmp/jbusy` 存在 ⇒ 拒绝（0x11，不写字节） | 忙锁 |
| `dlopen` 失败 ⇒ 文件已写+fsync，返回 `0x10000000` | "返回非 0" ≠ "没执行" |
| `/tmp/jiritsu` 全目录 + 载荷 ≤ 25 MiB | 容量上限 |

**已在 ILCE-7CM2 (2.01) 与 ILME-FX30 (`31182_15.2025122501`) 两台真机验证 `euid=0`。**

---

## 五、★★ 固件自带的工程工具（可替代自建探针）

| 工具 | 用途 | 价值 |
|---|---|---|
| **`/usr/scenario/` 26 插件 + `scenario.elf`** | 可编排原生测试链：音频/HDMI/面板/EVF/录像启停/变焦/人脸/工厂模式；`CAMERA_SCN_MOVIE_REC_START.so::scenario_run` **证实能向相机队列发消息驱动固件** | 高 |
| **`app_monio.elf`** | **应用内部命令与观测（DMM 缓冲 + OSAL 消息）** | 高 |
| **`mwf_monio.elf`** | **中间件状态 dump/历史/轮询**，支持 scenario 文件 | 高 |
| **`adjstctl.elf`** | 调整命令客户端（block/code + 负载 → `testcmd_sndmsg`） | 高 |
| `/usr/sbin/sshd`、`tcpdump`、`dhcpcd` 等 | 网络诊断集 | 中 |

---

## 六、★ AdjChkConfig.so —— 可能是更简单的目标

> **`amode=adj` 是死参数**（写入 `0x419668` 后全固件无读取者）—— **不是**校准界面入口。
>
> **真正的调整检查 UI 在 `AdjChkConfig.so`**：**独立 `main_loop`**，初始化输入/触摸/显示/sensor 通信，
> 含 AF 调整命令与 `Backup_reset_subsystem(0x2cf, 2)` 持久数据重置能力，
> 由 IMDB 的 `0x4000` 标志（im.elf 的 `cho` 参数）选择加载。

**⇒ 它是一个不依赖 appFw 生命周期和对象图的【独立固件 UI】** —— 可能是"能显示能操作"的捷径。

---

## 七、dmode 完整表（代码证实）

| dmode | 语义 | 可用 |
|---|---|---|
| `0` | **不装第二批驱动、不启动 bootin**（只留 insmod_drv1 + getty shell） | ✓ |
| `1/2/3` | 阶段累进：Infra(`s g r u`) → +LIRO(`l`) → +并行 infra+appFw(`i a`)；3=默认 | ✓ |
| `4/5/6` | `1n/2n/3n` —— `n` 无解析分支，**等同 1/2/3** | 无效 |
| `7` (`3L`) | **LeakTracer**（`/tmp/lt` tmpfs+fifo，`LT_CHECK_PROCESS=1`） | ✓ |
| `f` (`3p`) | **jemalloc**（`MALLOC_CONF=narenas:1024,tcache:false`） | ✓ |
| `8` (`3w`) | 只设 `MALLOC_OPTIONS=2W`，**无读取者** | 存疑 |
| `9` (`3k`) | KMC ICE —— **死代码** | ✗ |
| `a` (`3g`) | gdb —— **被拒**（bootin `0x401ae0` 要求命令以 `/` 开头） | ✗ |
| `b` (`3s`) | gdbserver —— 同样被拒，端口 10000 | ✗ |
| `d` (`3d`) | DUMA —— 死代码 | ✗ |
| `e/g` (`3p`) | efence / ASan —— **库未发布** | ✗ |

**target 选择顺序（代码证实）：** `Backup(0x92001b)=resub → qemu 内核 → tmode TT=test → 默认`

**⚠️ `/setting/mode/preload` 只认 4 条固定白名单路径 —— 不是任意注入通道。**

---

## 八、PMCA 命令被拒的确切机制

```
0xFED8: catalog_code & 0xE000 == 0x8000 → reject
```

**⇒ 带 `0x8000` 标记的命令被 USB 分派器一律拒绝**（即使实现完整）。
**这解释了 `FileControl 0xFF01` / `MemoryDump 0xFF03` 返回 `0x80` 的现象 —— 不是"不支持"，而是"被门禁拒绝"。**

**可用的替代通道**：`0x603/*`（备份/调整数据转发）、`0x601/0x11,12,14`（服务/工厂/测试启动标记）。

---

## 九、加载顺序：`Unknown symbol` / `No snd_msg registered` 的真因

> 另一批失败来自 harness 把 **input/ioexp/indctr/udate 放在 Backup 之前**，indctr 又早于 ioexp
> 并不是所有失败都源于硬件。

**⇒ 解决动作：按原脚本依赖重排 Backup、DMM 及附加模块；按模块各自证据保留或跳过，不以统一假成功替代。**

**可用的顺序来源**：`libIMDB.so` 的 **199 条清单**（每条带 kmod 路径 / 库名 / init-suspend-resume 入口 / 模式掩码）。

---

## 十、汇总：别做 / 可以做

### 别做

1. `ld.so.preload` + 未校验库（唯一不可救状态）
2. 批量数据写相机内部分区（12 MiB 填满即死）
3. 空 ACK / 随意 signal 未知队列
4. 裸构造 manager、手拼对象图
5. 「没观察到」=「没发生」
6. send 返回 0 = 投递成功
7. 为 KMC ICE / LIQUID / DUMA / Wfree / amode=adj 投入
8. 通过 USB 开 DBG_MODE（`0x8000` 被拒）
9. 把 `target=qemu` 当"跳过硬件"的钥匙（只改 9 条目，方向相反）

### 可以做

1. `/etc/profile` 持久 root 入口
2. libJit 一次性 root（`/tmp/jiritsu/libJit.so`）
3. `mwf_monio.elf` / `app_monio.elf` 固件自带内省
4. `scenario.elf` + 26 插件逐项验收
5. `AdjChkConfig.so` 独立 UI
6. dmode 1/2 分层截停；7/f 内存诊断
7. 镜像里直接改 `/setting/mode/dmode`（QEMU 不需要 root）
