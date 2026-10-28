# 真机服务模式调试通道 Playbook（2026-10-28 夜实测）

> 相机：ZV-E10，固件 **`30110_06.2026060501`**（注意：不是常见的 `..._05.2025031501`）
> 本文每条都有当晚实测输出支撑。工具：`/tmp/camtool`（dino2048 风格）+ 自写脚本。

## 0. 一句话总结

**不需要 Pico、不需要 fastboot、不需要拔插，就能：读写相机任意文件、执行任意 shell 命令、
remount rw `/usr`。** 前提是相机**正常启动到能枚举 `054c:0d95`**。

## 1. 环境（主机侧）

```bash
export PMCA_ROOT=/mnt/build/src/Sony-PMCA-RE     # 本地已有
export PYTHONPATH=/tmp/camstub                   # libusb_package shim（见下）
```

`libusb_package` 只是 `find_library`，系统有 libusb 时用 shim 即可：

```python
# /tmp/camstub/libusb_package.py
import ctypes.util
def find_library(name='usb-1.0'): return ctypes.util.find_library(name)
```

**Linux 特有坑**：`usb-storage` 会绑定相机的 Bulk-Only 接口。
`detach_kernel_driver(0)` 或系统级 `unbind` 都行，但 PMCA 的 `UsbBackend.reset()` 自己会 detach，
**不要重复拆**。

## 2. 认证流程（唯一正确姿势）

```python
from jiritsu_lib import connect_hardened
j = connect_hardened(attempts=1, timeout=60, verbose=True, verify=False)
# verify=False 必须！否则 health() 读 /version.txt 失败会判整次连接失败
```

实测流程：`0x0d95` → MSC auth → 重枚举 `0x0336` → service auth → **OK**。

### ⚠️ 三条铁律

1. **不要调 `j.stop()`** —— `STOP_SENSER` 会摘掉 USB 设备
2. **不要用 `cam.py`** —— 它的 `probe_usb_state()` 会 `ctrl_transfer` 后判 `hung` 并
   `reset_devices()`，实测直接把设备搞掉线
3. **一次会话把所有事做完** —— 会话结束设备卡在 `0336`，再连会 `GenericUsbException`

### 恢复手段：软件 reset（不用拔插）

```python
import usb.core, usb.backend.libusb1, ctypes.util
b = usb.backend.libusb1.get_backend(find_library=ctypes.util.find_library)
for d in usb.core.find(find_all=True, idVendor=0x054c, backend=b):
    try: d.reset()
    except Exception: pass          # 'Entity not found' 属正常
# 约 1 秒后设备自动回到 0x0d95
```

## 3. 文件读写（Jiritsu 通道）

```python
def wf(path, data):          # 写任意路径（数据前加 b'\0\xff\0'）
    j.select(path)
    return j.jiritsu(16, b'\0\xff\0' + data)

def rf(path):                # 读
    j.select(path)
    r = j.jiritsu(17, b'\0\xff\0')
    return r[1] if (isinstance(r[0], int) and r[0] == 0) else None
```

**实测能力**：

| 项 | 结果 |
|---|---|
| 写 `/tmp/jiritsu/` `/log/` `/setting/` | ✅ 返回 `(0, b'')` |
| 单次写入大小 | 4 K / 16 K / 64 K / **256 K** 全部 OK 且读回 md5 一致（0.3 s/256K） |
| 读回（本会话或以往会话写的文件） | ✅ |
| 读**开机就存在**的系统文件（`dmode`、`ld.so.preload`） | ❌ 返回 `0x10000006` |
| FileControl `0xff01`（cmd 0/1/2/3 全试） | ❌ 一律返回 `128`，此固件未实现 |
| `GET_VERSION(0x400d)` | ❌ `0x10000006`（06 版固件可能改了命令码） |

可用命令码只有 **`0x4001` / `0x4002`(MOUNT) / `0x4003`**（返回 0），其余全错。

## 4. ★ 执行任意 shell 命令

原理（来自 camtool）：

```python
write('/tmp/jiritsu/run.sh', '#!/bin/sh\n' + cmd)   # ① 写脚本
write('/tmp/jiritsu/libJit.so', PAYLOAD)            # ② 写载荷 → 触发 dlopen
#   载荷 ctor: fork → close(3..255) → execve("/bin/sh", run.sh)
time.sleep(2)
read('/tmp/jiritsu/out.txt')                        # ③ 读输出
```

- `PAYLOAD` = `/tmp/camtool/payload/libJit_exec.so`（**440 B**，纯汇编构建，不依赖 glibc）
- 实测：`REBOOT_NOW` 出现在 `out.txt`，`reboot` 成功执行并重启相机 ✅
- **dlopen 按名缓存**：同一 im.elf 生命周期内只执行一次，要重新执行必须重启相机

**busybox 实测可用**：`sh ash cat ls echo mount dd tr grep sed cp mv rm sync kill ps uname`
**不可用**：`head tail uniq id find wc awk vi`（无 /bin 软链，可试 `busybox <applet>`）

## 5. ★ 改 `/usr`（不用 fastboot）

```sh
mount -o remount,rw /usr                    # → REMOUNT_OK
echo test > /usr/tool/wtest.txt             # → 写入成功
mount -o remount,ro /usr                    # → 恢复
```

实测全部成功。**但注意空间限制**：

```
原厂 /usr 已用 238 MB，free 10 MB
换成 M2 库净增 ≈ +47 MB（libObj 省 17 MB，appFw 增 64 MB）  → 装不下
```

⇒ **"只替换几个库"不可行，必须整片刷 v9b**。

## 6. 启动链与 preload（已证伪）

```
init(initramfs) → 挂 setting/system/tmp/usr → bootin.elf → im.elf s g r u l i a
bootin.elf 字符串:
  /setting/mode/preload
  /usr/tool/LeakTracer.so|libefence.so|libjemalloc_tsh.so|libmcheckb.so   ← 白名单
  strncmp(whitelist, buf+11, ...)   ← buf+11 = 跳过 "LD_PRELOAD=" 11 个字符
```

**试过两种格式，都不生效**：

```
① /usr/tool/LeakTracer.so:/log/probe.so          （无前缀）
② LD_PRELOAD=/usr/tool/LeakTracer.so:/log/probe.so（有前缀，buf+11 正好命中白名单）
```

**结果**：相机正常启动，但探针没跑；直接查所有进程 environ：

```sh
for d in /proc/[0-9]*; do tr '\000' '\n' < $d/environ | grep -a LD_PRELOAD; done
# → 完全为空
```

**⇒ 此固件的 bootin 不把 `/setting/mode/preload` 传给任何进程。这条路彻底关闭。**

（副产品：`/etc/ld.so.preload` 依然是最危险的雷区，glibc 2.11.2 加载失败即致命，**永远不要碰**。）

## 7. USB gadget 现状

```
UDC: dwc3.0, state=configured        ← 已被 Sony gadget 占用
已加载: usbg_storage(53969) usbg_sen usbg_stillimage usb_extcmd usb_portMonitor usb_darwin ...
/sys/kernel/config                   ← 未挂载，configfs 不可用
/dev/usb/ 节点(全 777): massstorage gadgetcore scsi sicd_core/data/event sen usb_extcmd kikilog ...
```

- **`/dev/usb/adb` 节点存在，但没有 adbd 二进制** ⇒ adb 不可用
- Sony 的 `usbg_storage` 就是当前 MSC 模式的实现，要让主机看到 `/log` 需要逆向它的 ioctl
- **自己的 `g_mass_storage` 抢不到 UDC**（已被占用），除非先 rmmod Sony 的 gadget（会让 USB 断开）

**v9b 下 USB 起不来的对策**：在 v9b 镜像里改启动脚本预置 gadget insmod，
而不是运行中抢 UDC。

## 8. 已知不可用的命令（省得再试）

| 命令 | 返回 | 结论 |
|---|---|---|
| `0x400d` GET_VERSION | `0x10000006` | 06 版固件不支持 |
| `0xff01` FileControl | `128` | 未实现 |
| `0xff03` MemoryDump | `128` | 未实现 |
| `read_file` 读系统已有文件 | `0x10000006` | 只认本会话创建的文件 |
| `/dev/usb/adb` | — | 无 adbd |
| preload 任意格式 | — | environ 里始终为空 |
