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

## 155. 2026-10-28 ★ 控制通道彻底打通（关键修复：常开 fd）

```
KEYINJECT tty ok fd=4
KEYINJECT first-read n=3        ← 收到 host 的 "#S\n"
KEYINJECT state inst=0001d4a8 cands=0
```

**根因**：注入库每轮 `open/read/close` tty，tty 在无读者时丢输入。
改成**一次打开、持续读**后立刻可用。控制通道 = **ttyAM1（第二路串口，cfg.uart=3）**，
与相机控制台完全隔离，用 `socat - UNIX-CONNECT:/mnt/build/tmp/kbd.sock` 发命令。

### InfraInput 对象实测布局（菜单已绘制时）

```
+00 061674b0  vtable（libObj 内）
+04 00000003
+08 0000644c
+0c 0000477f  thumb 低地址代码
+18 0001d6c8
+1c 0000027f  thumb 低地址代码   ← SetCallback 曾把第三个参数写到这里（+0x1c）
+20 00000000
+24 00024ea0
+28 0001d150
+2c 0002b4d8
+30 00000005
+34 00000005
```

`SetCallback` 反汇编：`str r6,[r0,#28]`(=+0x1c)、`str 0,[r0,#32]`(=+0x20)
⇒ **+0x1c 极可能就是 App 注册的输入回调**。

### 新增命令

`#W` dump 对象、`#Q <addr> <hex>` 以 (inst, ev) 调用任意地址、`#C <id>` 调 RmCallback、
`#I <idx> <hex>` 调扫描候选。下一轮用 `#Q 27f <hex>` 直接试注入并比对画面。

## 156. 2026-10-28 首次"运行时注入"尝试：调用执行了但画面未变

在菜单已绘制（`export_ready=0x1`）时，通过控制通道执行：

```
#Q 27f 0100000000000000
KEYINJECT Q call 0000027f len=8      ← 调用真的执行了
（没有 crash，也没有 "Q returned" ⇒ 该函数阻塞不返回）
画面 SHA256 前后一致（6f2d7602ca7e75c2 ...）
```

⇒ 说明"控制通道 + 任意调用"这套机制是活的，但 `+0x1c=0x27f` **不是**输入回调（它阻塞）。
第二个候选 `+0x0c=0x477f` 的调用没能执行（前一个调用把注入线程卡住了）。

### 下一轮

1. 用静态办法**找 `SetCallback` 的调用者**（全 libObj 搜 `bl 8c2824`），
   从调用点看它传的第二个/第三个参数是什么 —— 那个才是 App 注册的回调；
2. 把 `#Q` 改成**在独立线程里调用**（避免一次阻塞把注入通道卡死），并加超时日志；
3. 拿到真回调后，用 `#Q <回调> <事件字节>` 试各种 `_InfraInputEvent` 布局，
   以画面 SHA256 变化作为"注入生效"的判据。

## 157. 2026-10-28 输入分发结构进一步明确

* `0x8c2890`（`SetCallback+0x6c`）是**链表查找**：`for (n=this+0x9c; n; n=n->next)
  if (n->id(+0x14) == key) return n;` —— 按 id 找注册节点；
* `GetEventStatus` 也走它找节点，然后调用内部 `0x8c1740` 读状态；
* `RmCallback` 找到节点后：`osal_valloc_msg_wait()` → `osal_snd_msg(node+0x18)`，
  即**用 osal 消息通知监听者**，事件细节由监听者再查。

⇒ 注入要么"造硬件状态"让 `GetEventStatus` 读到，要么"投递 osal 消息 + 造细节"。

### view 层可用的入口（viewUnified2.so 导出）

```
WrapperSettingUtil::getKeyEventStatus(unsigned, int)   0x6e4b7d
ViewBaseProduct::getRepeatKeyEventStatus(unsigned)     0x59d31d
CmnViewExpMode::mapKeyCode(int)                        0x6d1b59   ← 键码映射
ViewBaseForMR::attachEventId(int)                      0x436807
```

### 下一轮

1. 反汇编 `CmnViewExpMode::mapKeyCode` 得到键码空间；
2. 用 gdb 对"注册节点 +0x1c"下**硬件写/读观察点**，在调用 `RmCallback` 时看谁读它 —— 
   找到真正的消费者路径；
3. 或直接调用 view 层入口（`#Q`）模拟按键，观察画面 SHA256 是否变化。

## 158. 2026-10-28 ★ 找到"输入状态查询"的真实机制：同步 osal 消息

反汇编 `InfraInput` 内部的 `0x8c1740`：

```
osal_valloc_msg_wait(...)      ← 申请消息
osal_snd_sync_msg(qid, msg)    ← ★ 同步问"输入服务"要状态
osal_free_msg(...)
```

⇒ App 是**主动向另一个服务同步询问输入状态**的，不是等硬件中断。
这意味着我们可以在 **PLT 层冒充那个服务**（`osal_snd_sync_msg` 是导入符号，
LD_PRELOAD 拦截即可，完全不用改代码段），给它回一个"按键按下"的状态。

### 已做

* 新增 `libsynclog.so`：拦截 `osal_snd_sync_msg`，把**每个不同的 qid** 和前 5 个消息字
  打到日志（`SYNC qid=... w0..w4=...`）；
* 已加入 `/etc/ld.so.preload`（与注入库并存）。

### 状态

本次运行日志只有 10 KB、`export_ready=0`（还没画到菜单），暂未出现 `SYNC` 行 ——
需要更长运行才能抓到。

### 下一轮

1. 跑长一点（或先不加新库做对照），确认 `SYNC` 行的 qid；
2. 找到输入服务的 qid 后，在拦截器里**回填按键状态**并返回成功，观察画面是否变化；
3. 若 App 用 `osal_snd_msg`（异步）而非 sync，同样拦截。

## 159. 2026-10-28 `osal_snd_sync_msg` 拦截成功（单库合并），但 App 仍会卡在早期

### 结果

把 sync 拦截**合并进同一个 preload 库**（两个 preload 库会让 App 卡住），运行后终于抓到：

```
SYNC native=0x418a3ec
SYNC qid=003e014d w0=00000003 w1=00000000 w2=00000002 w3=003e014d
```

* `native` 解析成功（不是 NULL，说明拦截本身没问题）；
* 目前只看到 **qid=0x003e014d**，这是 **backup 服务**的队列（之前搞 MonDrv 时见过），
  还不是输入服务；
* 该次运行日志停在 ~12.8 KB（`export_ready` 未出现），App 卡在早期初始化。

### 下一轮

1. 判定"加了 sync 拦截就卡"是不是真的（做同配置不加拦截的对照，各跑 3 次）；
2. 若确认是拦截导致卡：把日志改成**只在收到非 backup qid 时**才输出，减少早期开销；
3. 继续等/筛出**输入服务**的 qid（比 0x3e014d 更靠后的调用），那才是我们需要回答的那个。

## 160. 2026-10-28 拦截 `osal_snd_sync_msg` 会导致 App 卡死（3/3 复现）

对照实验（同一套 initramfs、`-smp 4`）：

| 库内容 | 结果 |
|---|---|
| 只有注入库（无 sync 拦截） | ✅ 180 秒画出菜单 |
| 注入库 + **纯透传** sync 拦截 | ❌ 3/3 次都停在 ~12.6 KB（`infra_lanc_init` 之前） |

即使拦截函数只是 `return native(qid,msg);` 也会卡，说明**经 PLT 替换该符号本身**
就破坏了调用约定（可能该符号在 libosal 内部被自调用、或 ABI 不止 2 个参数）。

⇒ **放弃"拦截 osal_snd_sync_msg"这条**。

### 新方案：不拦、只用 gdb 观察，然后写"输入服务"进程

1. 在注入库里新增 `#M` 命令：**不拦截**，只用 `dlsym` 报出 `osal_snd_sync_msg` 的真实地址
   （以及 libosal 基址）；
2. 用 `-s` 起 QEMU，gdb 在 `osal_snd_sync_msg` 下断，命中时打印 r0..r3 —— 
   直接读出输入查询的 **qid 与消息格式**（只读，无副作用）；
3. 拿到协议后，写一个**独立 guest 进程**充当"输入服务"：在该 qid 上收请求、回"某键按下"，
   完全不碰 App 的库与代码。

这是最贴近真机（真机就是 keyscan 服务应答 InfraInput 的查询）的做法。

## 161. 2026-10-28 OSAL 同步接口的真实位置与注册 API

* `osal_snd_sync_msg`、`osal_reg_sync_msg_queue_cb` 都在 **libosal_uipc.so**（不在 libosal_utm）；
* 运行时地址已能打印：`OSAL h=... sync=0x407cec9 reg=0x4022ae1 (/usr/lib/libosal_uipc.so)`；
* 该库还导出 `__osal_snd_sync_msg`、`osal_snd_sync_direct`、`osal_reg_sync_msg_queue_cb`
  —— 即**服务端注册回调**接口，正是"输入服务"该用的 API。

### 观测结果

menu 已绘制后，用 gdb 在 `osal_snd_sync_msg` 下断，90 秒内**一次都没命中**：
说明 App 在菜单空闲态并不轮询输入状态（或轮询频率极低/由其它事件触发）。

### 下一轮

1. 改为在 **`InfraInput::GetEventStatus`（libObj+0x8c2a00）** 下断，观察它是否被周期性调用、
   参数与返回的事件结构 —— 这能直接告诉我们"键事件怎么进来"；
2. 若它被调用，就用 gdb **在返回后写入伪造的 `_InfraInputEvent`**（纯宿主侧、不改 guest 代码），
   验证"按键 → 菜单变化"整条路径是否成立；
3. 路径验证通过后，再把伪造逻辑做成 guest 侧的输入服务（`osal_reg_sync_msg_queue_cb`）。

## 162. 2026-10-28 直接调用 `GetEventStatus` 可以（但参数形式要改）

从控制通道执行 `#G 0`：

```
KEYINJECT GetEventStatus id=0 ret=2147483646 (0x7FFFFFFE)
KEYINJECT ev+00=00000000 ev+04=00000000 ev+08=7ffffffe ev+0c=00000000
```

`ret` 与写入 `ev+0x08` 的值都是 `0x7FFFFFFE`，明显是**错误码**（不是事件）。

原因：函数签名是 `GetEventStatus(int, int, _InfraInputEvent*)`（mangled 名里只有 3 个参数，
**没有 this**），而我按 `(inst, id, 0, ev)` 传了 4 个参数，把参数全错位了。

### 下一轮

1. 改成 `GetEventStatus(a, b, ev)` 三参数调用，遍历 a/b 的小整数组合，
   看哪个返回 0 并且 `ev` 被填出合理内容（键码/状态）；
2. 若返回 0 的调用存在，说明**我们能读事件**；然后尝试把"按下的键"写进 `ev` 再交给
   内部派发（同样是只读+调用，不改代码）；
3. 另可对照 `RmCallback(inst,id)`（#C）与 `SetEventKeyList` 等导出方法。

## 163. 2026-10-28 会话harness + 一次调用把注入线程打死

新增 `/mnt/build/session.sh`：**重试启动直到 `export_ready=1`**（最多 5 次，每次最多 ~5 分钟），
成功后保留 QEMU 与控制通道 —— 以后所有实验都基于它，避免被启动方差拖死。
本次首次尝试即成功（`READY /mnt/build/tmp/sess/s1.log`）。

### 现象

发送 `#G 0 0` 后：日志只出现一次 `KEYINJECT first-read n=7`，
**没有 `KEYINJECT GES ...` 输出**，但 App 仍在正常绘制（16 帧）、QEMU 未退出。

⇒ 说明我们的注入线程在**调用那一刻就挂了**（日志写在调用之后，所以什么都没留下），
其余线程不受影响。

### 下一轮

1. 把日志改成**调用前**先打印参数与目标地址（`g_base+0x8c2a00`），确认基址算对了；
2. 把 `#G/#Q/#C` 的调用放到**独立可丢弃线程**（已在 #Q 做过，但 #G 还没）并加返回日志；
3. 若确认是地址错误，用 `#M` 那套 dladdr 结果反推 libObj 真实基址（而非用 dladdr(getter)）。

## 164. 2026-10-28 同步回调注册被拒；qid 很可能来自 `SetOsalId`

* 用 `osal_reg_sync_msg_queue_cb` 批量注册 `0x3e0100..0x3e01ff` 共 256 个 qid：
  **全部返回非 0（注册失败，registered 0）**；
* `InfraInput::SetOsalId(int*, int)` 的存在说明 **qid 是 App 在初始化时传进来的**，
  存在 InfraInput 实例（或其子对象）里；
* 我们只 dump 了实例的前 16 个字，还没覆盖到存 qid 的字段。

### 下一轮

1. 把 `#W` 扩展到 dump 实例前 0x100/0x200 字节，找形如 `0x003E____` / `0x00____` 的 qid 值；
2. 用找到的 qid 精确注册回调（而不是盲扫），再让 App 查询时进我们的回调；
3. 若注册接口始终被拒，改在**事件结构层**注入：
   把伪造的 `_InfraInputEvent` 写进实例登记的缓冲区，再调用分发（只读+调用）。

## 165. 2026-10-28 OSAL 注册被拒（-1024）；考虑改走"直接驱动视图层"

* `osal_reg_sync_msg_queue_cb(qid, cb)` 对 `0x00410024/25/26`、`0x003e014d` 全部返回 **-1024**
  ⇒ 该接口不能从我们的进程里随意注册（很可能要求同域/先建队列）。
* 实例 dump（0x200B）里没有明显 qid，只有 `/tmp/app_fw_infra/iloga_data_00000000.bin` 这类字符串；
  子对象里出现 `0x00410025`，但也注册不上。

### 转向：直接驱动视图层（仍在"用户空间 API 边界"，不改代码）

对方 `try_original_menu()` 已经证明**可以调用视图层函数把菜单打开**：

```
root  = 扫描 manager(+0x149221c) 的槽位找 id==0xa3 的对象
(views+0x590f35)(1)                      // 进入菜单模式
(views+0x5910b5)()                       // wifi guard
(views+0x47de27)(root+0x154, 0)          // ★ 请求菜单（最后一个参数可能是页面/转场选择）
```

**假设**：把最后一个参数换成别的值，或者随后调用翻页/高亮移动的视图函数，
就能切换到**不同菜单页面**——这同样能达成"菜单可操作（多页面导航）"，
而且完全走原厂视图代码。

### 下一轮

1. 在我们的库里复刻 `root` 的发现逻辑（纯内存读），把 `root`、`views_base` 打日志确认；
2. 用 `#Q` 调 `views+0x47de27(root+0x154, N)`，N 取 0..8，逐次抓帧比对 SHA256；
3. 若画面随 N 变化 ⇒ 多页面切换打通（再考虑把它映射成"按键"语义）。

## 166. 2026-10-28 视图库定位：dlopen 失败、maps 里也没有名字

* `dlopen("/usr/lib/viewUnified2.so")` 失败：
  `/usr/lib/CautionConfig.so: undefined symbol: _ZN2ux4core2ID4ZEROE`
  —— 因为 libObj 是 `RTLD_LOCAL` 加载的，依赖它的符号对 CautionConfig 不可见；
* 改用 `/proc/self/maps` 找 `viewUnified2` 的 r-xp 段：**在 App 进程里根本没找到这个名字**。

⇒ 说明视图库要么以别的名字映射、要么是匿名映射（dlopen 后 unlink/替换过），
需要先把 App 进程的真实映射表打出来看。

### 下一轮

1. 在 `#V` 失败分支里 dump 所有含 `.so` 的 map 行（前 30 条），确认视图库的真实文件名/基址；
2. 拿到基址后用 `g_views+0x590f35 / +0x47de27` 驱动视图（这两个偏移来自对方的 hook，
   同版本 2.03 应当一致；届时先用 `native.menu.views.base` 与我们的计算值交叉验证）。

## 167. 2026-10-28 视图基址已可注入；root 发现读到 0

链路终于打通到"能调用视图层函数"：

```
menu=1
views=0x30d38000              ← 从 libuiobserve 日志取的 native.menu.views.base
KEYINJECT set views=30d38000  ← #B 命令生效
KEYINJECT V views=30d38000 root=00000000 n=2
KEYINJECT no manager          ← *(libObj+0x149221c) == 0
```

* 之前几次失败的真因：**跑的内核镜像比库旧**（Image 10:35 / lib 10:40），`#B` 压根没编进镜像；
  重新生成 `initramfs_data.o` + relink 后 `#B` 立即可用（已加"改库必须重编镜像并 strings 校验"的检查习惯）；
* `manager = *(libObj_base + 0x149221c)` 读出 0，而对方的 `try_original_menu()` 在**同一个环境**
  里能算出 `native.menu.root=0x2258a8` —— 说明**读取时机**或**偏移**还有差异。

### 下一轮

1. 把 `manager` 原始值、`g_base` 一起打日志，并在菜单绘制后**多次重试**（间隔 1 秒 ×10），
   判断是不是"某一瞬间才存在"；
2. 若始终为 0，则对照对方源码里的确切偏移（0x149221c）与我们的 libObj 版本是否一致
   （用 `nm`/`readelf` 交叉验证该地址附近的符号）。

## 168. 2026-10-28 manager 连续 10 次读都是 0 ⇒ 偏移/对象不一致

```
KEYINJECT mgr try0..try9 = 00000000     （每秒一次，持续 10 秒）
KEYINJECT V views=30a98000 root=00000000 n=0..2
```

对方 `try_original_menu()` 在**同一进程、同一环境**里能算出 `native.menu.root=0x2258a8`，
而我们读 `*(libObj_base + 0x149221c)` 恒为 0。

可能原因：

1. 偏移不对（他们的 libObj 与我们的 2.03 libObj 不是同一构建）；
2. manager 存在别的全局（他们代码里那个常量可能来自另一版本）。

### 下一轮（不依赖猜测）

在 `0x1492000..0x1492600` 范围内扫描所有字，找满足：

* 值像指针（`0x10000 < v < 0x10000000`）
* 且 `*(unsigned*)v == 43`（manager 的魔术数）

的那个字 —— 这样**自动定位**真正的 manager 槽位，不靠硬编码偏移。
找到后再按对方的槽位结构（`slots = *(mgr+32)`，`id = slots[i*16+8]`，`root = slots[i*16+4]`，
`id==0xa3`）取 root。

## 169. 2026-10-28 manager 扫描把注入线程打挂了（需要安全读）

`#V 0` 之后日志里既没有 `mgr found` 也没有 `no mgr in window`，连 `V views=... root=...`
都没打印 ⇒ **vcall 在 scan_manager 里解引用坏指针，SIGSEGV 直接把线程打死**
（App 本身不受影响，但注入通道哑了）。

同时记录一个工程教训：这次改源码时用"按行号切片"导致函数重复定义/结构破坏，
连修了 3 次才编译通过——**以后对 keyinject5.c 的修改必须整块替换 + 立即编译校验**。

### 下一轮

1. 先解析 `/proc/self/maps` 建一张"可读区间表"，写 `safe_u32(addr, &out)`：
   只有落在某个 `rw/r-` 区间内才解引用；
2. scan_manager 全部改用 safe_u32（窗口扫描 + slots 遍历）；
3. 再跑 `#V 0..3`，用画面 SHA256 判断是否切页。

## 170. 2026-10-28 直接调视图函数：能调，但**在错误线程里阻塞**

用日志里的 `native.menu.views.base` / `native.menu.root` 直接调用：

```
menu=1 views=0x30bed000 root=0x1b5240
KEYINJECT E call fn=3117df35 a1=00000001 a2=00000000    ← views+0x590f35(1) 进入菜单模式
（随后 futex_wait_queue_me —— 卡住）
```

⇒ 视图层函数**必须在 UI 线程**里调用；从我们的线程调会等待某个被 UI 线程持有的锁/信号量
（对方的 `try_original_menu()` 之所以能成功，是因为它从 `drawFramebuffer` 的 hook 里调用，
天然在 UI 线程上下文）。

这也解释了之前所有"调用后线程挂死"的现象。

### 下一轮（可行方案）

1. 用**信号**把执行挪到 UI 线程：我们的库 `sigaction(SIGUSR1)` 装一个 handler，
   用 `tgkill(pid, ui_tid, SIGUSR1)` 让 handler 在 UI 线程里运行，在 handler 内调用视图函数；
   `ui_tid` 从 libuiobserve 日志取（如 tid=214），通过新命令 `#T <tid>` 传入；
2. 或者更干净：等对方 hook 的下一次 `drawFramebuffer`（UI 线程）里执行我们的请求队列
   —— 需要 hook，暂不走。

## 171. 2026-10-28 信号跳板：tgkill 成功但 handler 没跑

```
KEYINJECT U send tid=215 n=0 ret=0      ← tgkill(268,...) 成功
KEYINJECT U send tid=215 n=1 ret=-1     ← 后续都失败
（日志里没有任何 "U exec" / "U done"）
```

* 第一次投递成功（ret=0），但**handler 没执行**（无 `U exec` 日志）；
* 之后的投递返回 -1（很可能第一发之后该线程状态变化/信号被屏蔽）。

⇒ 该进程/线程很可能**屏蔽或改写了 SIGUSR1**（Sony 的 UI 线程常见做法），
或者我们的 `sigaction` 被之后 App 自己的 `sigaction(SIGUSR1,...)` 覆盖。

### 下一轮

1. 先做 self-test：给自己的某个线程发同一个信号，确认 handler 通路本身没问题；
2. 换更"冷门"的信号（`SIGURG`=23 / `SIGIO`=29），并在 `#U` 里允许指定信号号；
3. 若仍不行，改用"**在对方 hook 的 drawFramebuffer 时机执行请求队列**"——
   但这需要 hook（代码补丁）；折中方案：把请求写进一个 App 会读的地方（如 view 的输入队列），
   让 UI 线程自然消费。
