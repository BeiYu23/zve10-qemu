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
