# Sony 服务模式（Senser）与 root 终端

> 参考来源：`dino2048/zv-e10-toolkit` 的 `docs/service-mode.zh-CN.md`、
> `docs/root-terminal.zh-CN.md`（MIT）。我们自己的实测记录见
> `docs/JIRITSU_CHANNEL.md`、`docs/JIRITSU_USB_ROOT.md`、`tools/payload/`。

## 1. 这是什么

相机在 **USB 海量存储模式**（`054c:0d95`）下接受 Sony 私有的 "Senser" 服务请求。
认证通过后可以：
- 调 `SetFile2` 之类接口往相机写**配置类文件**（我们的 `admin`/agent 注入就是走这条）；
- **`setTerminalEnable(True)` 打开一个调试终端：`Uid/Gid = 0 0 0 0` → root shell**。

## 2. 流程（每一步都可能因为固件版本不同而失败）

```
① 相机切到 USB 海量存储模式（不是 Auto / 不是 MTP）
      枚举为 054c:0d95
② StartSenser          发 Sony 私有 vendor request
③ 重新枚举              设备变成 054c:0336（Senser）
④ 认证                  challenge/response（PMCA-RE 的 SonySenserAuthDevice）
⑤ 业务请求              SetFile2 / readTerminal / writeTerminal …
⑥ StopSenser           相机回海量存储模式；没回就拔插一次 USB
```

我们自己的实现在 `tools/payload/`（`send_payload.py` + `env.sh` + `vendor/agent_v14.so`），
完整步骤见 `tools/payload/INJECT_QUICKSTART.md`：

```bash
cd /root/fw/oss/tools/payload
source env.sh                                     # 必须，设置 A6700_SRC
python3 send_payload.py vendor/agent_v14.so --dry-run   # 先干跑
python3 send_payload.py vendor/agent_v14.so --pid 0x0D95 # 真发
```

成功标志：`SET_FILE2 ... status=0x00000000 (OK)`。
**之后相机会在几秒内从 USB 消失**（`STOP_SENSER` 的效果，SD 卡在电脑上也不见了），
这是正常的；拔线后 WiFi 会恢复。

## 3. root 终端能做什么（这才是它对我们最大的价值）

| 项 | 能力 |
|---|---|
| 身份 | `/proc/$$/status` = `Uid: 0 0 0 0`、`Gid: 0 0 0 0` |
| 默认（只读）白名单 | `cat ls find ps du df grep head tail wc sha256sum md5sum stat readlink pwd uptime date uname free echo id dmesg`（可带 `busybox` 前缀） |
| 只读模式额外拒绝 | 单引号外的 shell 元字符；`/dev/*`；`find` 的 `-exec/-delete/-fprint` |
| 命令限制 | 单行 ASCII、≤200 字符；空闲 10 分钟自动结束 |
| 完整 shell | 需要在会话里开 `--owner-authorized-write`；破坏性命令（分区/块设备/递归删除/挂载/模块/重启）仍在黑名单 |
| 模块 | `--allow-module-swap` 额外放开 `insmod`/`rmmod`（必须先开写模式） |

有 root 终端之后，**一条命令就能修掉本次这类砖**：

```sh
cp /etc/ld.so.preload.orig /etc/ld.so.preload
```

（`disable_preload.py` 的文件头里也是这么写的恢复方案。）

## 4. 对我们当前这台砖机为什么用不上

前提是**固件能启动到接受服务会话的程度**。我们现在是 3 秒重启，Linux 用户态根本没起来
⇒ `054c:0d95` 不会出现 ⇒ 这条路暂时不可用。

**能用它的时机**：相机恢复正常启动后，以后再做任何 `/etc`、`/setting` 的实验，
优先用它（可逆、不碰 eMMC 裸块），而不是去改 `/etc/ld.so.preload`。

## 5. 边界与副作用

- 服务认证本身可能更新相机上的 `/setting/.etools` 和 `/setting/.hasp`（工具不会主动写这些）。
- 协议未公开、依赖固件版本；只在 ZV-E10 固件 `30110_05.2025031501` 上实测过。
- 不是 BootROM 路径：**不能用于救 3 秒重启这种早期故障**。
- STOP_SENSER 后如果设备没重新出现，拔插 USB 一次。

## 6. 与 BootROM 路线的关系

| | BootROM（Pico 注入） | 服务模式（Senser） |
|---|---|---|
| 需要相机能启动 | 不需要（BootROM 阶段） | **需要** |
| 权限 | 直接读写 eMMC 块 | 文件级 + root shell |
| 风险 | 写错块 = 变砖 | 主要是 `/setting` 配置 |
| 适用 | 救砖、刷分区、取证 | 日常实验、快速验证 |

**救砖只走 BootROM；日常实验优先走服务模式的 root 终端。**
