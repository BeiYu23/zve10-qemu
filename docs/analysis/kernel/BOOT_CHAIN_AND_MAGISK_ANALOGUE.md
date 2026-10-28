# 启动链解构 & 能不能做 Magisk/KernelSU 式挂载

> 问题：「我们能不能接管启动链条，从而实现类似 magisk/kernelsu 的挂载」
> 结论：**原生 Magisk 机制全部不成立，但解出了完整的启动链和三个真实切入点。**

---

## 一、Magisk/KernelSU 的核心机制 —— 逐条对照

| Magisk 机制 | 本机情况 | 能否 |
|---|---|---|
| `overlayfs` 覆盖 `/system` | **内核无 overlay 支持**（`vmlinux` 无 overlay 字样）| ❌ |
| `magiskinit` 替换 `/init` | `/` = `ext2 ro` | ❌ |
| `post-fs-data.sh` 脚本钩子 | `/etc` 无 `init.d`/`profile`/`rc.local`/`inittab` | ❌ |
| `magiskd` 常驻守护 | 可以自己做（agent），但**不随开机自启** | ⚠️ |
| 持久化模块目录 | `/setting`、`/log` 可写 | ✅ |

---

## 二、★★★ 完整启动链（解出来的）

```
BootROM → EXBL(nflashaB0/B1) → kernel → initrd → /sbin/init
                                                        │
                                    init (28,580 B)  main() 流程：
  ① check_forced_senser  ← /setting/sen/smode          【可写】
  ② check_mode_file      ← /setting/mode/dmode         【可写】
  ③ check_qemu           ← 检测虚拟化
  ④ check_bootmode       ← /proc/udm/upm_bootmode
  ⑤ set_oreore_env       ← 设 pri_mode_ch_0_3=1 等环境变量
  ⑥ im_oreore_system('/usr/bin/bootin.elf')   前台 或 run_bg_process 后台
  ⑦ run_bg_process('/usr/bin/sen.elf')         条件
  ⑧ run_bg_process('/usr/bin/net_cmdd.elf')    ★ 设备上【不存在】
  ⑨ putenv("LD_PRELOAD=")                      ← 清 preload
  ⑩ launch_shell()                             ★ 无条件调用
       → /usr/bin/launch_shell.elf (5,140 B)
          → open("/dev/ttycons" 或 "/dev/console")
          → setsid() → fork() → dup2() → execv("/bin/ash", "--login")
```

---

## 三、`dmode` 文件格式（已完全解出）

**`check_mode_file` @ init+0x93B0：**

```c
fd = open("/setting/mode/dmode", O_RDONLY);
if (fd < 0) return -1;
n = read(fd, buf, 1024);
close(fd);
for (i = 0; i < n; i++) {
    c = buf[i];
    if      (c == '0')            flag_A = 1;
    else if (c=='k' || c=='K')    flag_B = 1;
    else if (c=='g' || c=='G')    flag_C = 1;
    else if (c=='d' || c=='D')    flag_D = 1;
    else if (c=='e' || c=='E')    flag_E = 1;
}
// 写入 5 个全局变量，供 main 判断
```

**⇒ **dmode 是"单字符标志文件"**，不是数字/字符串。**

**main 里对应的分支：**
```
flag1 → profile_entry("GDB") + im_oreore_system(bootin.elf) 前台   （否则 run_bg_process）
flag2 → run_bg_process(sen.elf)
flag3 → 跳过 mount_partition
```

**★ 相关 LD_PRELOAD 目标（`/usr/tool/` 下）：**
```
LD_PRELOAD=/usr/tool/libkmcsup.so.2.0.0
LD_PRELOAD=/usr/tool/libsonyefence.so
LD_PRELOAD=/usr/tool/libduma.so
DUMA_MALLOC_0_STRATEGY=1  DUMA_PROTECT_FREE=1  DUMA_ALIGNMENT=4
DUMA_OUTPUT_FILE=/root/dumaLog.txt   GDB
```

**★ 设备上实际存在：`/usr/tool/LeakTracer.so`、`libjemalloc_tsh.so.1`。**

---

## 四、三个真实切入点

### ① `/setting/mode/dmode`（可写）—— 控制调试标志

**✅ 每次开机 init 都读**
**❌ 只切换**固定**调试行为（GDB profile / sen.elf / mount），**不能注入任意代码**

### ② `/setting/mode/preload`（可写）—— **这才是 Magisk 的等价物**

**`bootin.elf` 读它并设置 `LD_PRELOAD`：**
```
<mode_file> : default is /setting/mode/dmode
/setting/mode/preload
LD_PRELOAD=
```

**⇒ 每次开机 bootin 会按这个文件设 LD_PRELOAD ⇒ **加载我们的 .so** ⇒ 真正的开机自启。**

**❌ **这正是当初变砖的路径。****
**❌ 若 .so 坏了，动态加载器失败 ⇒ 连 init 都起不来 ⇒ **不可恢复****

### ③ `/usr/bin/launch_shell.elf` —— 固件自带的 root shell 入口

**每次开机无条件在 `/dev/ttycons` / `/dev/console` 上 `execv("/bin/ash","--login")`。**

**⇒ **只要物理接触串口控制台，就能直接拿 root shell。**
⇒ 这是 Sony 自己的调试后门，不是我们能改的，但**知道它存在很有价值**。**

---

## 五、⇒ 如果要做 Magisk 式持久化，唯一路径 + 硬伤

**路径：写 `/setting/mode/preload` 指向我们的 .so**

**✅ 每次开机自动加载**
**❌ 变砖风险不可消除 —— 存在一个**鸡生蛋窗口**：**

```
如果 .so 加载失败，它自己的"自我删除 preload"代码也跑不了
⇒ 下一轮开机仍然失败
⇒ 无法自愈
```

**a7c2 的做法（constructor 第一件事删 preload）只能覆盖"加载成功"的情况。**

---

## 六、我的建议

**不建议走 preload 持久化，除非同时满足：**

| 条件 | 现状 |
|---|---|
| 完整分区备份 | ✅ **18/18 已备份，含 SHA-256** |
| **硬件恢复手段**（Pico / BootROM） | ❌ **没有** |
| 用户明确同意 | ❌ **按规矩 0 不得写新 .so** |

**★★★ 关键判断：**

**我们现在**已经有更好用的东西** —— TCP 7777 的 agent：**
```
· 通过 USB 维护模式几秒就能拉起（libJit）
· 拉起后毫秒级、不占 USB、不用插拔
⇒ 日常研究完全够用
```

**⇒ 「为了开机自启去冒变砖风险」是**不划算的**。**
**⇒ 等有了硬件恢复手段（Pico/BootROM），再考虑 preload 持久化。**

---

## 七、附带发现（有价值）

| 发现 | 意义 |
|---|---|
| **`launch_shell.elf` 每次开机起 root shell** | 有串口就能直接用 |
| **`net_cmdd.elf` 被 init 启动但不存在** | 无网络命令守护 |
| **`qsi.elf` 被 bootin 启动但不存在** | 无 shell 接口 |
| **`/usr/tool/` 有 LeakTracer/jemalloc** | Sony 内部调试工具的残留 |
| **`/setting/sen/smode`** | 可强制 Senser 模式（另一个可写开关）|
