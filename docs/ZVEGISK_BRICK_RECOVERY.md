# zvegisk 变砖恢复路线（七层）

> 先明确：**zvegisk 在结构上几乎不可能让相机开不了机**（见 §一）。
> 但既然要上真机，恢复路径必须逐层写清、逐层验证。

---

## 一、为什么"变砖"概率极低

**唯一能让相机起不来的路径是 `zvegisk.so` 在 `fork()` **之前**崩溃。**

```c
__attribute__((constructor)) static void zvegisk_init(void)
{
    int pid = sy0(SYS_fork);      // ← 父进程路径只有这一个 syscall
    if (pid == 0) { work(); ... } // 所有工作都在子进程
    /* 父进程立刻返回 */
}
```

**⇒ fork 之前只有函数序言（压栈/调 SP），除非 ELF 加载本身有问题，否则不可能崩。
而 ELF 加载失败是**非致命**的（ld.so 打警告后继续）。**

**★★★ 其余所有故障都在子进程里 ⇒ 只死自己，im.elf 照常启动。**

---

## 二、七层恢复路径

### 第 0 层：自动自愈（无需任何干预）★ 覆盖 90%

```
boot_count > 3
  ⇒ 写 /setting/zvegisk/safemode
  ⇒ ★ 删掉 /setting/mode/preload
  ⇒ 下次开机 bootin 读不到 preload ⇒ 干脆不加载我们
  ⇒ 纯原厂启动
```

**★ 关键：日志先刷盘再删，事后能看到 `SELF-DISABLE: N fails`。**

**覆盖场景：** .so 能加载、能跑到检查点，但 UI 起不来（资源覆盖坏掉等）。

---

### 第 1 层：拔 SD 卡 / 卡上写 `boot_mode=old`

```
拔卡 ⇒ .so 挂不到 SD ⇒ 不应用任何 overlay ⇒ 原厂 UI
或：卡上 /zvegisk/boot_mode 写 "old"
```

**覆盖场景：** 资源覆盖（overlay）导致的问题。
**❌ 不覆盖：** .so 自身有问题。

---

### 第 2 层：SD 卡放 `safemode` 文件 ★ 已实现

```
卡上建 /zvegisk/safemode（内容随意）
插回相机 → 开机
  ⇒ .so 挂载 SD 后立刻检查 /tmp/zsd/zvegisk/safemode
  ⇒ 存在 ⇒ 立刻 unmount + return
```

**★ 这是"没有 USB、没有串口"情况下最方便的救援手段。**

**⚠️ 必须挂载后才能读 —— 所以代码里检查点放在 `mount_sd()` **之后**。**

---

### 第 3 层：`/setting/zvegisk/safemode`

```
存在即什么都不做（优先级最高，在 boot_count 之前检查）
```

**❌ 局限：** 相机起不来时你无法写 `/setting`。
**★ 所以它只是第 0 层的配套，不是独立救援手段。**

---

### 第 4 层：USB 服务模式 + shell / agent

```
① USB 插电脑，相机进大容量存储（054c:0d95）
② 注入 agent（libJit 通道）
③ 拔线，WiFi 起来，agent 在 7777 端口
④ cam.py run "echo 3 > /setting/mode/dmode"
```

**❌ 局限：** 如果 `.so` 坏了导致 im.elf 起不来，agent 也起不来
（agent 是靠 `LD_PRELOAD` 加载的，同样挂在 dmode 上）。

---

### 第 5 层：★★★ UART 串口 —— 终极保险

**固件自带，不需要任何破解：**

```
硬件：USB-TTL 适配器接到相机的服务口
参数：115200 8N1
```

**证据（cmdline + 固件）：**
```
exception.console_option=115200n8     ← 异常时串口输出
console=ttynull                        ← 正常时控制台丢弃
/dev/ttycons → ttyAM0                  ← 串口设备
launch_shell.elf 每次开机在串口上起 /bin/ash --login
```

**⇒ 插上就能拿到 root shell：**
```sh
# 在串口 shell 里
echo 3 > /setting/mode/dmode      # 关掉 preload
rm -f /setting/mode/preload       # 或直接删
reboot
```

**★★★ 这是真正意义上的"不会变砖"保证 —— 只要硬件在，就永远能救。**

---

### 第 6 层：更新模式（UFP）

**固件里有 `up.sh` 管理更新模式：**
```sh
UPDIR=/setting/updater
UPMODE=$UPDIR/mode       # 存在 ⇒ 下次开机进 UFP（更新）模式
```

**触发：**
```sh
touch /setting/updater/mode
reboot
```

**★ 但触发它本身需要 shell ⇒ 依赖第 4 或第 5 层。**

**配套：**
```
/usr/bin/crypter.elf     更新器的加解密
/usr/bin/udtrbody.bin    143,360 B，可写
IAMUPDATER               /root/IAMUPDATER
body magic               0100UDTRFIRM，Compressed ROMFS
完整性校验               CrcChecker / uc_crc32sum.elf ← CRC，可伪造
```

---

### 第 7 层：官方固件刷写

**最后一招。需要官方工具/固件包。
我们已有完整备份：`/root/fw/zve10_backup/`（18/18 分区，SHA-256 在 manifest.json）。**

---

## 三、恢复路径速查表

| 现象 | 用哪层 |
|---|---|
| **UI 起来但显示错乱** | **① 拔卡/`boot_mode=old`** |
| **连续黑屏（3 次内）** | **等第 0 层自愈** |
| **连续黑屏（超过 4 次）** | **第 0 层已自动删 preload，应该已恢复** |
| **相机完全起不来** | **⑤ 串口，或 ④ USB** |
| **只是想让 zvegisk 停下** | **卡上放 `safemode`（第 2 层）** |
| **想彻底卸载** | **删 `/setting/mode/preload`（串口或 USB）** |

---

## 四、⚠️ 恢复不了的情况（诚实）

**① `.so` 在 fork 之前崩溃 + 且 boot_count 逻辑也没跑到**
- **理论上几乎不可能**（fork 是第一条语句）
- **⇒ 落到第 5 层（串口）**

**② `/setting` 分区本身被写坏**
- **我们只写 `/setting/zvegisk/*` 和 `/setting/mode/preload`**
- **⚠️ 如果写 preload 时断电，dmode/preload 可能是垃圾**
- **⇒ 第 0 层用 `unlink` 删除而不是覆写，风险更低**
- **⇒ 真正坏了要第 5 层**

**③ 硬件损坏**
- **与 zvegisk 无关**

---

## 五、★★★ 上真机前的必查清单

```
□ 确认 /setting/mode/dmode 原值已备份
□ 确认 /setting/mode/preload 原值（应为空或不存在）
□ 确认 /setting 剩余空间足够（20MB，我们只写几十字节）
□ 确认第 0 层（自杀式恢复）代码路径无 bug
□ 确认串口适配器可用（第 5 层）— 强烈建议先验证这个
□ 部署前先跑一次 dry-run（不写 dmode，只看日志）
```

**★★★ 其中"先验证串口可用"最重要 ——
那是唯一不依赖软件的救援路径。插一次 USB-TTL，看到 shell 提示符，才敢动 dmode。**
