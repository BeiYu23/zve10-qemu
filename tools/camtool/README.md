# 自写服务模式工具（2026-10-28）

配套文档：`../../docs/analysis/deploy/REALDEVICE_SERVICE_MODE_PLAYBOOK.md`

| 脚本 | 用途 |
|---|---|
| `env.sh` | 环境变量（PMCA_ROOT / shim / 交叉编译器） |
| `camctl4.py` | 测 select/write/read 任意路径 |
| `camdiag.py` | 执行通道诊断（进程 / LD_PRELOAD / mounts） |
| `camusb.py` | USB gadget 栈诊断（只读） |
| `camset2.py` | 写 `/setting/mode/preload`（LD_PRELOAD= 格式） |
| `probe.c` | nostdlib syscall-only 探针（可加载到 glibc 2.11.2） |

依赖 `/tmp/camtool`（外部队列 clone）与 `/tmp/camstub/libusb_package.py`。
