# ZV-E10 固件签名链：可移植的绕过路径（参考 A7M4 社区方法）

> 参考：`Erlla/sony-a7m4-custom-firmware` 方法 1（替换更新公钥 + 修改 `verifyKey`）
> 本文只做**离线静态分析**，未在真机执行任何写操作。刷机有永久变砖风险，见文末。

## 一、A7M4 的方法（已验证发布）

1. 更新器从 `nflasha1` 的 FAT 文件 `boot/initrd.img` 加载 SquashFS，其中
   `/etc/VerificationKey.pem` 是升级包验签公钥。
2. `Updater::SignatureVerification::verifyKey(char const*)` 对**公钥本身**做真实性检查
   （PEM 的 SHA-1 + 跨处理器返回值，请求 `0x025906BE`）。仅换 PEM 会被这一步拒绝。
3. 把若干库中该函数入口改成 `mov w0,#0; ret`（AArch64 `00 00 80 52 C0 03 5F D6`）。
4. **升级包自身的 RSA-2048 / PKCS#1 v1.5 / SHA-256 验签仍然保留** ——
   DAT 必须用对应私钥签名。此补丁不等于关闭 BootROM Secure Boot。

## 二、ZV-E10 的对应情况（本项目静态核实）

**更新器 initrd 已提取**：`/root/fw/_archive/dirs/fw_analysis/updater_initrd`（版本 `100.110.035`）

**只有【一个】库含 `verifyKey`**（A7M4 需要改四个）：

| 项 | 值 |
|---|---|
| 库 | `usr/lib/libsdfileinputupdaterapi.so` |
| 架构 | ELF32 ARM EABI5, CPU "7-A", Thumb-2 |
| 符号 | `_ZN7Updater21SignatureVerification9verifyKeyEPKc` |
| 符号值 | `0x0000DBCD`（奇数 ⇒ **Thumb**，实际入口 `0xDBCC`）|
| 大小 | 416 字节 |
| `.text` 映射 | vaddr `0xB718` == file offset `0xB718`（**1:1**）|
| **补丁文件偏移** | **`0xDBCC`** |
| 原始首字节 | `e9 2d 43 f0 b0 a7 46 08 ...` |
| **替换为（Thumb）** | **`00 20 70 47`** = `movs r0,#0; bx lr` |

**公钥**：`etc/VerificationKey.pem`，**RSA-2048**
DER SHA-256 = `00479aeb65d9b333e3fcf874172fc4d072cb83131292f859b7918dadaeea1e67`
（用于识别基线；A7M4 的指纹是 `be79b332…`，不同机型不同）

**其它 updater 库均不含 `verifyKey`**：
`libsenupdater.so`、`libupdater0604.so`、`libupdatercommon.so`、
`libupdatertalk.so`、`libupdaterufunc.so`

## 三、可移植的配方（未验证，需自行承担风险）

```
① 解包更新器 initrd（SquashFS）
② 补丁 usr/lib/libsdfileinputupdaterapi.so @ 0xDBCC → 00 20 70 47
③ 用自家 RSA-2048 公钥替换 etc/VerificationKey.pem
④ 用对应私钥对自制 DAT 签名（PKCS#1 v1.5 / SHA-256）
⑤ 重新打包 initrd，按原厂 SD 更新流程安装
```

**关键点：步骤 ② 只关闭"公钥真实性检查"，包本身的签名仍会验证 ——
所以 ④ 必须真做，不是可选。**

## 四、为什么这条对本项目重要

原目标是把 ZV-1F 的新菜单移植到 ZV-E10 —— 这需要安装**修改过的固件**，
而所有原厂更新路径都过同一道 RSA 验签（a7c2-re 与 A7M4 项目均确认）。
本方法提供了一条**不依赖硬件 fastboot 线**的可行路径。

## 五、A7M4 项目强调的安全前提（必须遵守）

- **任何刷写前必须完整备份**：全部 eMMC 分区、boot0/boot1、分区表、SHA-256，另存离线副本
- **固件更新必须使用原厂电池**，禁止副厂电池（即使已打副厂电量补丁）
- 保持稳定供电与 USB 连接，写入中不拔线/断电
- **不要把另一台相机的完整设置/校准/eMMC 镜像用于恢复**
- 版本号相近不代表二进制兼容 —— 不能把固定地址补丁套到别的机型或版本
- 有效签名、脚本成功、菜单出现，都**不等于**刷写安全或功能完整
