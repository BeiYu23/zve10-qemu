# ZV-E10 QEMU 长期目标工作日志（2026-10-28 起）

目标：除取景/成像外，原厂菜单可正常操作（显示 + 按键导航 + 页面切换），
按 chocotan/zve10-qemu 的方法；150 轮上限，可提前完成。

---

## 144. 2026-10-28 完整系统路线：他们的 SD 方案在内核侧是**未完成**的

### 144.1 已排除的假象

* 之前"挂 SD 就极早期死机"是**我漏了内核命令行**：不带 `mem=1G@0 ...memrsv...` 时内核在
  `Memory policy` 之后就停；补回完整 cmdline 后可正常走到 `init_post`。
* 我的 init 之前没有日志：它写 fd 11 但从未 open `/dev/kmsg`。改为**先挂 devtmpfs 再 open kmsg** 后，
  `FULLINIT start` 正常输出。

### 144.2 真正的卡点：mmci 驱动 probe 返回 -EINVAL

```
mmci-pl18x: probe of mmci failed with error -22
```

`drivers/mmc/host/mmci.c:1005`：

```c
/* must have platform data */
if (!plat) { ret = -EINVAL; goto out; }
```

而对方的补丁是这样注册的（`arch/arm/mach-cxd900x0/cxd900x0.c`）：

```c
static struct amba_device mmci_device = {
    .dev = { .init_name = "mmci" },
    .res = { .start = 0xf1005000, .end = 0xf1005fff, .flags = IORESOURCE_MEM },
    .irq = { 41 },
};
```

**没有 platform_data，也没有 clock**。而探测路径后面还有：

```c
host->clk = clk_get(&dev->dev, NULL);
if (IS_ERR(host->clk)) { ret = PTR_ERR(host->clk); goto host_free; }
```

⇒ 就算补上 platform_data，`clk_get` 也会失败（mach-cxd900x0 没有给 "mmci" 注册时钟）。

**结论：他们的 SD 路线只是注释里的一句话，内核侧从未打通。**
所以"完整系统"要落地，必须补上这两块（用我们自己的补丁叠加，不改他们的文件）。

### 144.3 下一步（本轮结束时的状态）

1. 新增我们自己的内核补丁 `0002-zve10-qemu-sd-support.patch`：
   * 提供 `mmci_platform_data`（ocr_mask / gpio_cd=-1 / gpio_wp=-1 / capabilities）；
   * 为 `mmci` 注册一个固定频率时钟（clkdev / clk_register_clkdev + dummy clk）；
   * 用 `clkdev_add()` 或 `clk_register_clkdev()` 绑定 dev_id "mmci"。
2. 重编内核（干净内核 8 MB，重链接只要十几秒），验证 `mmci-pl18x: ... using DMA` 出现、
   `/dev/mmcblk0` 建立；
3. 之后 SD 上 `/system` `/usr` 可挂载 → 厂商 `/sbin/init` 启动 → 看到"完整系统"日志。

这同时会带来**改文件即生效**的迭代能力（文件在 SD 镜像里，不用重编 kernel/initramfs）。

---

## 145. 2026-10-28 ★ SD 卡终于可用：补 platform_data + 时钟 + 第二条 IRQ

### 145.1 三处缺失（全部在他们补丁之外，我们自己补）

| # | 缺什么 | 症状 | 补法 |
|---|---|---|---|
| 1 | `mmci_platform_data` 未提供 | `probe ... error -22`（`mmci.c:1005` 的 `if (!plat)`） | 在 `cxd900x0.c` 给 `mmci_device.dev.platform_data` 填 `ocr_mask=0x300000`、`gpio_cd/wp=-1`、`MMC_CAP_4_BIT_DATA` |
| 2 | `mmci` 没有时钟 | `clk_get()` 会失败 | 在 `mach-cxd900x0/clock.c` 增加 `mmci_clk`（50 MHz，enable 空实现）+ `clk_lookup{ .dev_id="mmci" }` |
| 3 | **只给了一条 IRQ**（`.irq = {41}`） | 驱动第二条 `request_irq(dev->irq[1])` 传入 0 → `-EINVAL` | 改为 `.irq = {41, 42}`（QEMU 侧本来就是 `pic[9], pic[10]`） |

### 145.2 结果

```
mmci-pl18x mmci: mmc0: PL181 manf 41 rev0 at 0xf1005000 irq 41,42 (pio)
mmc0: new SD card at address 811a
mmcblk0: mmc0:811a QEMU! 512 MiB
 mmcblk0: p1 p2 p3 p4
FULLINIT sd-present
FULLINIT mounts-done          ← p1=/system p2=/usr p3=/setting p4=/tmp 全部挂上
```
厂商 `/sbin/init` 已 exec（无 exec-failed 输出），但**静默无输出**——下一步要查它卡在哪
（很可能缺 `/etc`（真机由 nflasha10 同时挂到 /etc）或某些必需的 kmod）。

### 145.3 快速迭代通道打通（重要工程收益）

新增 `libsdloader.so`（极小，`/etc/ld.so.preload` 加载）：
* 挂 devtmpfs/proc → **等待并挂载 SD 的 p2 到 `/mnt/sd`**（EBUSY 时直接复用已有挂载）；
* `dlopen("/mnt/sd/zve10/libkeyinject_impl.so")` —— 真正的注入实现放在 **SD 镜像里**。

⇒ 之后改注入代码**只改 sd.img，不用重编 243 MB 内核**（迭代从 4 分钟降到秒级）。

### 145.4 按键注入的 hook 已成功装上

```
SDLOADER mount r=0 errno=17 / dlopen ok
KEYINJECT hooked base=0xa283000        ← InfraInput::SetCallback 已打补丁
```

App 仍在跑初始化（`InfraFsys_Init`/`infra_lanc_init` …），本轮未见崩溃。
下一步：给它足够时间跑到菜单，再通过控制串口发 `#K<code>,<down>` 触发 `InfraInput` 回调，
观察画面变化。

---

## 146. 2026-10-28 按键注入：hook 装上但控制台通道与稳定性有问题

### 146.1 已完成的工程改动

* 注入库改为**数据驱动**：控制台命令 `#S`（打印状态）、`#K <code> <down>`、`#M <hexbytes>`
  —— 事件结构不再编进代码，可以直接在运行时试各种字节布局；
* 只让**找到 libObj 基址的那个进程**（App）去读控制台，避免 init/sh 抢走输入；
* 新增 `/mnt/build/run_ui.sh`（等价于对方启动脚本，但 `-serial unix:` 便于注入）
  和 `consd.py`（单连接的串口守护：读日志 + 从 `cmd.txt` 取命令写入）；
* `KEYINJECT hooked base=0xa567000`、`tty open fd=8` 都出现了。

### 146.2 当前问题

1. 串口日志在 `tty open` 之后就停住（两次运行都停在同样大小 25707 字节），
   **疑似 hook（对 `Infobase+0x8c2824` 打 thumb 跳转）后 App 停摆**；
2. 通过 unix socket 发的 `#S` 没有被读到（无 `KEYINJECT read` 日志）。

### 146.3 下一轮计划

1. 先验证"是否 hook 破坏了 App"：加环境变量开关，不 hook 跑一次 → 应能画出菜单；
2. 若确认是 hook 破坏，改用更安全的注入方式：
   * 不 patch 代码，而是 hook `InfraInput::SetCallback` 的 **PLT/GOT** 表项；
   * 或直接调用 App 注册的回调（先通过只读手段拿到指针，例如解析 libObj 里保存回调的全局变量）；
3. 串口方向改用**内核 kmsg 作为输入**不可行，改为：把控制命令写进 SD 卡文件（SD 已可用），
   由注入库轮询读取 —— SD 迭代通道已打通（`/mnt/sd` 挂载于 `/usr` 分区）。

---

## 147. 2026-10-28 注入 hook 的 A/B 结论：**是"打补丁"本身让 App 停摆**

| 变体 | 结果 |
|---|---|
| 完全 no-op（不 hook） | ✅ 菜单画出 `export_ready=0x1`，日志 826 KB |
| hook 但不读串口 | ❌ 卡在 `infra_lanc_init`，日志 ~25 KB |
| hook + trampoline 链式调用原函数 | ❌ 同样卡住 |
| hook + capture 里不打日志（静默） | ❌ 同样卡住 |

⇒ 排除"日志阻塞"与"替换导致原函数未执行"，**问题在 patch 本身**。

### 待查（下一轮）

1. `InfraInput::SetCallback` 的指令集状态：`readelf -s` 里该符号值是 0x8c2825（thumb 位），
   entry 应为 0x8c2824；需要确认它是否真为 Thumb-2 代码 —— 用 `objdump -d`（默认 ARM）
   对照看哪种解码合理；
2. 若为 ARM 状态，则我们的 Thumb 跳转板（`ldr.w pc,[pc,#0]`）会把 ARM 代码打断 → 需换 ARM 版跳板；
3. 也可以改用"不改代码"的注入点：
   * 通过 PLT/GOT 替换（只能替换导入符号）；
   * 或找到 InfraInput 内部保存回调/事件队列的**全局变量**，直接写数据触发，
     完全不碰代码段。

## 148. 2026-10-28 结论：不做代码补丁，改走"读指针 + 直接调用"

试过的全部失败（App 都停在 `infra_lanc_init`）：替换式 hook、trampoline 链式调用、
修正 trampoline 的 Thumb 位、静默 capture。而完全 no-op 的同一套 preload 库**能正常出菜单**。

⇒ 结论：**在这棵 3.0.27 + Sony 原库上直接改写 `.text` 风险过高**（Thumb-2 指令边界、
GOT/PLT 与共享页都在和我们作对）。放弃补丁方案。

### 新方案（只读 + 调用）

1. `InfraInput::GetInstance()`（libObj 导出，0x8c22a0）可在我们的库里**直接 dlsym 调用**，拿到单例；
2. 单例对象里保存着 App 注册的回调（`SetCallback` 反汇编显示它会往对象 +0x1c/+0x20 附近存 ctx/fn）；
3. 我们的线程只**读取**这些字段，确认指针落在 libObj 的代码区间后，直接调用它注入事件
   —— 全程不写任何代码页；
4. 事件字节仍通过数据驱动（`#M <hex>`）在运行时试布局。

### 另外要查的输入来源

libObj 里有 `/dev/gpio`、`EXEC_POWERON_REC_KEYSCAN_STOP` 等字符串，说明真机是
**keyscan（另一进程）+ InfraInput** 的结构。如果确认 App 最终从某个 fd 拿键，
用"喂 fd"的方式注入会比任何 hook 都稳。

## 149. 2026-10-28 注入器改用"只读发现"：进展与新的坑

### 149.1 无侵入版本可以正常出菜单

新库 `libkeyinject3.so` **完全不改代码段**，App 依然能画出菜单（`export_ready=0x1`），
说明"预加载本库"本身是无害的——问题只在改写 `.text`。

### 149.2 发现链路上的三个坑

1. `dlsym(RTLD_DEFAULT, "InfraInput::GetInstance")` 失败 → App 用 `RTLD_LOCAL` 加载 libObj，
   符号不在全局作用域；
2. 拦截 `dlopen` 捕获 handle 也没成（`handle=(nil)`），而且这一版很可能把 App 的
   dlopen 弄坏了（该次运行菜单没画出来）——`dlvsym(RTLD_NEXT,"dlopen","GLIBC_2.4")`
   失败时就 return NULL，等于禁掉 dlopen。**必须加 dlsym 兜底**；
3. `/proc/self/maps` 里找不到 `libObj.so` 字符串（3.0 内核对路径显示与预期不同）。

### 149.3 下一轮

1. 修 `dlopen` 兜底：`native = dlvsym(...) ?: dlsym(RTLD_NEXT,"dlopen")`，仍失败则直接
   用 `syscall(SYS_open)` 无用——改用在 `dlopen` **之前**记录 path，失败也不影响 App；
2. 完全照抄对方 `ui-observe.c` 的做法：`dlopen` 里 `strstr(path,"libObj.so")` → 拿到 handle →
   `dlsym(handle,"ux::wgtsys::WidgetSystem::sys_getInstance")` → `dladdr` 得 base
   （他们验证过 offset 0x616fe4）→ 再用 base+0x8c22a0 调 `InfraInput::GetInstance`；
3. 拿到实例后扫描代码指针（`#P`），用 `#I <idx> <hex>` 试注入。

## 150. 2026-10-28 注入器 v4：**基址与单例都拿到了**（只读，不碰代码）

做法（照对方 `ui-observe.c`）：
1. 只用 **App 进程**（读 `/proc/self/cmdline` 含 `bootin`）才启动线程 —— 否则会在 sh/init 里
   误 dlopen libObj 并执行它的构造函数，直接把系统打崩（实测 `Code: bad PC value`）；
2. 在 App 进程里 `dlopen("/usr/lib/libObj.so")`（已加载，返回同一 handle）；
3. `dlsym(handle, "ux::wgtsys::WidgetSystem::sys_getInstance")` + `dladdr` → **base=0x04dd6000**；
4. `base + 0x8c22a0` 调 `InfraInput::GetInstance()` → 返回 `0x0001d4a0`。

### 待解释

`inst=0x1d4a0` 看起来更像**句柄/索引**而不是对象指针（App 低地址布局），
且这次 `cands=0`（没有扫到代码指针），菜单也没画出（可能只是慢，也可能调 GetInstance
时机太早干扰了初始化）。

### 下一轮

1. 把 `GetInstance` 的调用推迟到"App 已经开始绘制之后"（例如等 libuiobserve 输出
   第一条 `drawFramebuffer` 再调），避免干扰初始化；
2. dump `inst` 前后 0x100 字节，确认它是对象还是句柄；若是句柄，找 `0x1d4a0` 对应的真实对象
   （很可能在 App 的固定映射区，参考 `CONFIG_TASK_MMAP_OFFSET=0x04000000` 那类布局）；
3. 目标仍是：拿到 App 注册的输入回调并直接调用（不覆写任何指令）。

## 151. 2026-10-28 InfraInput 单例对象布局（只读 dump）

`InfraInput::GetInstance()` 返回 `0x1d4a0`（App 低地址区的真实对象，不是句柄），
其前 0x28 字节：

```
+00 = 061444b0   ← vtable，落在 libObj 代码区（base+0x134??，校验一致）
+04 = 00000001
+08 = 000063d8
+0c = 00004769   ← 奇数，thumb 代码指针
+10 = 00000000
+14 = 00000000
+18 = 0001d6c8   ← 另一个低地址对象
+1c = 0000027f   ← 奇数，thumb 代码指针（可能就是回调或 vtable 槽）
+20 = 00000000
+24 = 00024ea0
```

⇒ 注入所需的一切都**只读可得**：vtable 在 libObj、两个 thumb 代码指针在 +0x0c/+0x1c。

### 下一轮

1. 反汇编 `base + 0x0c`、`base + 0x1c` 指向的函数（它们是低地址区的代码，需要用 App 的映射基址换算），
   确认哪个是"事件回调"、参数是什么；
2. 用同样的只读方式拿到 `_InfraInputEvent` 的字段含义（从回调函数体里看它读哪些偏移）；
3. 然后直接调用回调注入 —— 全程不写代码。

## 152. 2026-10-28 输入管线结构（反汇编结论）

`InfraInput::RmCallback(int)`（0x8c189c）做的事：

```
遍历 inst+0x9c 处的链表
  → 节点匹配后：osal_valloc_msg_wait() 申请消息
  → osal_snd_msg([node+0x18] 队列) 把消息投递给监听者
  → osal_dynamic_id(73,…) 生成动态 id
```

⇒ **输入事件是走 OSAL 消息队列**（不是直接函数调用），InfraInput 只是把硬件/keyscan 的
变化转成 osal 消息广播给注册者。

### 关键修正：候选回调指针在**低地址区**

单例 dump 里的 `+0x0c=0x4769`、`+0x1c=0x27f` 都是**奇数（thumb）且远小于 libObj 基址**，
说明 App 在低地址还有一段自己的代码（`0x0000_0000` 附近的固定映射）。
之前的扫描只接受 `[base, base+21MB)` 内的指针，所以 `cands=0` —— 这是错的。

### 下一轮

1. 扫描条件放宽：**任何奇数且缓存在可执行映射内**的值都算代码指针（含低地址区）；
2. 先把候选逐一 dump 出来（不调用），再用 `#I` 逐个试；控制台若仍不通，
   改成"就绪后延时自动试第一个候选"并把结果打日志；
3. 同时查 `osal_snd_msg` 路线：如果 InfraInput 的输入线程是靠 osal 队列唤醒的，
   直接从我们的库里调用导出的 `osal_snd_msg(queue, msg)` 投递一个合成事件，
   可能比调回调更贴近真实路径（还需要 `_InfraInputEvent` 的消息格式）。

## 153. 2026-10-28 控制通道实验：unix socket 能读不能写，pty 也不通

| 通道 | 读（guest→host） | 写（host→guest） |
|---|---|---|
| `-serial unix:...` + 单连接守护 | ✅ 正常（日志完整） | ❌ App 的 `/dev/ttyAM0` read 永远收不到（无 `first-read` 日志） |
| `-serial pty` | ⚠️ 只读到 5 字节 | 未验证 |

推测原因：QEMU 的 serial0 就是**内核控制台**，console 输入被 tty 层/其它进程（init-ui.sh 里
的 `sh`）竞争消费；App 进程里的读线程拿不到字节。

### 候选方案（下一轮按顺序试）

1. **用相机 initramfs 起一个交互 shell**（对方 `init-shell.c`）验证 `-serial unix` 的写入是否
   真能到 guest —— 这能一次判定"通道本身通不通"，与 App 无关；
2. 若通道本身是通的，就在 App 侧改用**独占**方式读：先 `tcsetattr` 关掉回显/规范模式，
   并在 App 启动前把 shell 的 stdin 关掉；
3. 若通道本身不通，改走 **SD 卡命令文件**（hotplug + 轮询），或给"我们自己的机型副本"
   增加第二路串口（不改他们的 `zve10.c`）。

## 154. 2026-10-28 第二路串口已可用（ttyAM1），但 host→guest 数据仍未到达

### 关键突破

1. 对方机型的 `pl011_create` 只建了 UART0；我们在**自己的 QEMU 构建副本**里补上
   `pl011_create(0xf2001000, pic[151-32], serial_hd(1))`；
2. 内核侧 `cfg.uart` 是**位掩码**（`cxd900x0_uart_bitmap & (1 << i)`），
   把 cmdline 改成 **`cfg.uart=3`** 后：

```
uart1: ttyAM1 at MMIO 0xf2001000 (irq = 151) is a AMBA/PL011
KEYINJECT tty ok fd=4        ← 注入库成功打开 /dev/ttyAM1
```

⇒ **专用控制串口存在且可打开**，与相机控制台完全隔离。

### 仍未解决

从 host 经 `-serial unix:` / `-serial tcp:` 写入的数据，guest 侧 `read(/dev/ttyAM1)` 收不到
（无 `first-read` 日志）。说明问题在 **QEMU chardev → PL011 RX** 这一段的配置，而不是
guest 或我们的库。

### 下一轮

1. 试 `-chardev socket,id=ctl,path=...,server=on,wait=off,mux=off -serial chardev:ctl` 的显式写法；
2. 试 `-serial mon:stdio` / `-chardev stdio` 的对照；
3. 若都不通，改用 **SD 卡命令文件**（SD 已可用）+ QMP 热插，彻底绕开串口。
