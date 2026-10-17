# NOTICE

本仓库仅包含**原创**的补丁、脚本、文档与派生技术数据。

**不包含**任何 Sony 固件、二进制、证书或它们的副本。
使用者需要**自行从自己的设备导出固件**，并自行承担合规责任。

> This repository contains only **original** patches, scripts, documentation and
> derived technical data. It contains **NO** Sony firmware, binaries or
> certificates. Users must **dump firmware from their own devices** and are
> responsible for their own compliance.

---

## 相关说明

- 本项目目的是**互操作性研究与个人设备改造**
- 请遵守你所在地区的法律，以及 Sony 的软件许可条款
- 文档中出现的商标、产品名归其各自所有者所有
- 本项目与 Sony 无关联

## 第三方参考与致谢

本项目的许多思路来自公开研究，包括但不限于：

- **Lost-week-1025 / CXD90045-FPSU**
- **DavidBuchanan314** —— *ILCE-7M4-RE*、*Sony-ILCE-7M4-Linux*
- **ma1co** —— *Sony-PMCA-RE*、PMCA-QEMU、`fwtool.py`
- **chocotan/a7c2-re** —— a7C II 原生 QEMU 相机实验
- **YuuLuo/a7c2-service-preload** —— 服务模式 / `ld.so.preload` 先行工作

上述项目**不随本仓库分发**，各自遵循其自身许可。
完整清单与逐项说明见 [`docs/ACKNOWLEDGEMENTS.md`](docs/ACKNOWLEDGEMENTS.md)。

## Licensing

- 代码（`scripts/`、`patches/`、`rescue/`、`display/`、`tools/`）：**MIT**（见 [LICENSE](LICENSE)）
- 文档（`docs/`、`hardware/`、`rescue/docs/`）：**CC BY 4.0**
