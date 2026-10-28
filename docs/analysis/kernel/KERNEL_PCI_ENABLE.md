# 打开 PCI/PCIe（Round 7 关键修复）

## 问题
`mmc_drv.ko`（SD 卡 host，含 PCIe 桥）需要 15 个符号：
`pci_bus_read_config_*`, `pci_set_master`, `pci_enable_device`, `__pci_register_driver`,
`pci_request_regions`, `pci_release_regions`, `pci_disable_device`, `pcie_slot_ctrl`,
`pcie_root_ctrl`, ... —— 我们的内核**一个都没有**，因为 `# CONFIG_PCI is not set`。

而 `libOnDemandLoader.so` 的 `Odl_MemArea::openMemUnit()` 会打开
`/dev/mmca`、`/dev/mmcb`、`/dev/ms1`、`/dev/stream`、`/dev/mem`
⇒ **`mmc_drv` 加载失败 → `/dev/mmca` 不存在 → App 的按需加载器崩溃**
（`InfraOnDmdLdr` 段转换失败 @0x029c19e0）。

## 修复
1. `arch/arm/mach-cxd900x0/Kconfig`：把 Sony PCIe 驱动的依赖
   `depends on PCI && !CXD900X0_QEMU` 改为 `depends on PCI`
   （我们的 defconfig 是 `CONFIG_CXD900X0_QEMU=y`，否则该驱动不编译）
2. `.config`：`CONFIG_PCI=y`（`make oldconfig` 会自动带上 `CONFIG_PCI_SYSCALL=y` 等）
3. 重编内核：
   ```sh
   TC=<build>/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin/arm-buildroot-linux-gnueabihf-
   make ARCH=arm CROSS_COMPILE=$TC -j4 zImage
   ```

## 效果
```
Unknown symbol: 0
<7>PCI: CLS 0 bytes, default 32
!!! GPE HW: bar remapped 0xf1000000->0xf1000000 size 4000
[MMC_OS] slot0 Bus type = 1
[MMC_DRIVER] Slot 0 is PCI Bus
[MMC_DRIVER] Slot 1..3 is Local Bus
```
`mmc_drv` 从「加载失败」变成「正常初始化并开始探测」。

## 补丁文件
- `kernel_kconfig_pcie_qemu.patch`（Kconfig）
- `kernel_config_with_pci`（完整 .config）
- `zImage_with_pci`（编译好的内核）
