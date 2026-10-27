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
