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

---

## 9. ★★★ `/setting/env.txt` —— 真正的注入通道（2026-10-29 反汇编确认）

### 9.1 证据链

`/sbin/init`（initramfs，带调试符号）中：

```
字符串:  parse_and_set_env / set_oreore_env / /setting/env.txt
导入:    putenv@GLIBC_2.4  unsetenv@GLIBC_2.4  __environ
符号:    parse_and_set_env = FUNC @0x97cc (228 B)
         set_oreore_env    = FUNC @0x98b1 (152 B)
         is_oreore_env     = OBJECT(4B) @0x14868   ← 只是标志位，本函数不引用
```

`parse_and_set_env` 反汇编（关键 5 条）：

```asm
9850:  movs r1, #61        ; '='
9854:  blx  memchr         ; 在行里找 '='
9858:  cbnz r0, putenv     ; 找到 → putenv(整行)
985a:  blx  unsetenv       ; 没找到 → unsetenv(整行)
9862:  blx  putenv
9882:  bl  profile_entry
```

### 9.2 格式（实测+反汇编）

| 写法 | 行为 |
|---|---|
| `KEY=VALUE` | `putenv("KEY=VALUE")` |
| `SOMEKEY`（不含 `=`） | `unsetenv("SOMEKEY")` ← 可用来取消变量 |
| `# 注释` / 前导空白 | `skip_comment_and_space()` 跳过 |
| 单行长度 | 上限 1024 字节（超出截断） |
| 变量名过滤 | **无**（`is_oreore_env` 在本函数里未被引用） |

**调用时机**：init 的 `main` 内、`execve(bootin)` **之前** ⇒ 设入的环境变量
**会被 bootin 及其 fork 的所有子进程（含 im.elf）继承。**

### 9.3 为什么这是最佳注入点

| 对比项 | `/setting/mode/preload` | `/setting/env.txt` |
|---|---|---|
| 实测生效 | ❌ environ 里始终没有 LD_PRELOAD | ✅ 代码级确认 |
| 格式约束 | 白名单 + `strncmp(entry, buf+11, …)` | 标准 KEY=VALUE，无过滤 |
| 影响范围 | 仅 im.elf | 所有 exec 的进程 |
| 失败恢复 | 改文件内容 | **删文件即可** |

### 9.4 用法

```bash
# 写（注意：Jiritsu 会沙盒化路径，必须用 shell cp 到真实 /setting）
# 沙盒: Jiritsu write "/env.txt"
cat > /setting/env.txt <<'EOT'
# 注入探针
LD_PRELOAD=/log/probe.so
EOT

# 恢复：删掉即可
rm -f /setting/env.txt
```

### 9.5 待验证

- [ ] 原厂环境下写 `LD_PRELOAD=/log/probe.so` → 重启 → 查 `/proc/*/environ` 是否出现
- [ ] 探针 .so 必须 **nostdlib 纯 syscall**（相机 glibc 2.11.2，2018 工具链编的动态库加载不了）
- [ ] 确认探针在 im.elf 里能正常执行 constructor（不被 dlopen 缓存影响）

---

## 10. env.txt 实验的三个注意点（2026-10-29 另一对话补充）

### 10.1 ⚠️ 探针路径别放 `/log`

init 字符串出现次数：`/usr` 46 次、`/setting` 7 次、**`/log` 只有 1 次**。

| 挂载点 | 是否在 init 早期挂载 |
|---|---|
| `/usr` | ✅ 必然（bootin 就在 /usr/bin） |
| `/setting` | ✅ 明确在挂载列表里 |
| `/log` | ⚠️ **可能晚于 bootin 启动** |

**若 `/log` 尚未挂载，`LD_PRELOAD=/log/probe.so` 会静默失败**（ld.so 只告警后继续），
很容易误判为"env.txt 不生效"。

**⇒ 探针路径优先级：`/setting/probe.so` > `/usr/lib/probe.so` > `/log/probe.so`**

### 10.2 ⭐⭐ `LD_DEBUG`：零代码实验，可能不需要写探针

env.txt 能设任意环境变量 ⇒ 可直接让 glibc 自己汇报加载过程：

```
LD_DEBUG=libs                    # 所有库加载
LD_DEBUG=files                   # 文件查找过程
LD_DEBUG=bindings                # ★ 符号绑定（能看出哪个符号解析失败）
LD_BIND_NOW=1                    # 立即绑定全部符号（缺失即暴露）
LD_DEBUG_OUTPUT=/setting/lddebug # 输出到文件，避免刷屏
```

**输出走 stderr**；配 `LD_DEBUG_OUTPUT` 会写成 `/setting/lddebug.<pid>`。

**⇒ 一次启动就能看到 appFw.so 有没有被 dlopen、卡在哪个符号 —— 不用写任何探针。**

⚠️ 前提：glibc 2.11.2 是否支持这些变量需实测（`strings /lib/ld-linux.so.3 | grep LD_DEBUG`）。

### 10.3 ⚠️ 先确认 env.txt 不会被 init 重写

init **读** `/setting/env.txt`（parse_and_set_env），但需确认它**不写**。
验证方法：写一个标记 → 重启 → 检查内容是否还在。若被覆盖则该机制不可用。

### 10.4 nostdlib 探针的能力边界

纯 syscall 探针**不能 `dlsym`/`dlopen`**（那些在 libc/libdl 里）。
但做以下事情完全够用：
- 读 `/proc/self/maps` → 写 `/setting/` 或 `/log/`
- 读 `/proc/self/environ`、`/proc/self/cmdline`
- `mount()` / `insmod`（init_module）等 syscall

已有可用探针：`/tmp/probe/probe.so` 5,508 B，无 NEEDED、无未定义符号，
constructor 写 `/log/BOOTPROBE.txt`（含 maps）。

## 11. 已排除的候选（省得重做）

| 候选 | 实验 | 结论 |
|---|---|---|
| M2 版 `libInfraMscServer.so` 导致 USB 消失 | 单变量换入原厂 `/usr`，重启 | **USB 完全正常** ⇒ 无关，已恢复原状 |
| `/setting/mode/preload` 通道 | 两种格式（裸路径 / `LD_PRELOAD=` 前缀）+ 真实写入 `/setting` | environ 里始终无 `LD_PRELOAD` ⇒ 此固件 bootin 不传 |
| FileControl `0xff01` | cmd 0/1/2/3 全试 | 一律 `128` ⇒ 未实现 |
