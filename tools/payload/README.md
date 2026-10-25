# libJit 载荷（ZV-E10 实机）

## 背景

ZV-E10 的 `libJiritsu.so` 在 `adj_cmd_set_file2` 里会把自己写进来的文件 `dlopen`，
路径固定 `/tmp/jiritsu/libJit.so`。因为用 `RTLD_LAZY` 且**不 `dlsym`**，
**载荷只需要一个能跑起来的构造函数 `_init`，不需要导出任何符号。**

⇒ 这是**安全**的 root 执行路径：**不写 `/etc/ld.so.preload`**（那是上次变砖的路径）。

## 工具链陷阱（必须先知道）

```bash
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/lib
```
**不加这行会报：`cc1: error while loading shared libraries: libmpfr.so.4`**
（系统只有 `libmpfr.so.6`，工具链需要 `.so.4`；`compat/` 里存了一份）

## 设计原则

| 原则 | 原因 |
|---|---|
| **`-nostdlib` + 纯直接 syscall** | 目标机 glibc 是 **2.11.2**；带 libc 会撞符号版本 |
| **构造函数里 `fork`，父进程立即返回** | 不阻塞维护进程 |
| **子进程做完就 `_exit`** | 不留残留 |
| **只读 / 只写标记文件** | Stage 1 不碰 flash |

## 载荷清单

| 文件 | 阶段 | 作用 |
|---|---|---|
| `payload_probe.c` | **Stage 1** | 写标记文件 + 试 `mount` + dump `/proc/mounts` |
| `payload_agent.c` | Stage 2 | TCP 命令服务（备份/读取）|

## 构建

```bash
./build.sh payload_probe.c libJit.so
```

## 验证清单（上传前）

```bash
readelf -SW libJit.so | grep init_array     # 必须有条目
readelf -lW libJit.so | grep LOAD           # p_offset 与 p_vaddr 模页同余
readelf -sW --dyn-syms libJit.so | grep UND # 不应有真实未定义符号
```

## Stage 1 判据

上传后读 `/tmp/jiritsu/probe.txt`，内容应包含：

```
PROBE_OK
MKDIR_RC=0
MOUNT_VFAT_MMCAA_RC=<0 或负数>
MOUNT_VFAT_MMCA_RC=<0 或负数>
MOUNTS_FD=<fd>
--- MOUNTS BEGIN ---
...
--- MOUNTS END ---
PROBE_DONE
```

**这是最有价值的一段：`MOUNTS` 会告诉我们相机**自己**有没有把 SD 卡挂在某处 ——
如果有，后续备份就不需要 `mount` 权限，直接往已有挂载点写。**

---

## 发送工具 `send_payload.py`

### 为什么这样设计

**两个来源拼起来用，各取所长：**

| 环节 | 来源 | 原因 |
|---|---|---|
| **两阶段认证** | `a6700-unlock`（`PYTHONPATH` 引用，不复制代码）| 已实测、pyusb 实现、平台无关 |
| **文件帧格式** | `a7c2-re` 的记录 | **ZV-E10/A7C2 用 `file_id=0xFF` 约定，整文件一帧** |

### ⚠️ 为什么不用 a6700 的发文件函数

a6700 工具自己的代码里写着：

```python
if len(frame) - 12 - 4 > PAYLOAD_LIMIT:      # PAYLOAD_LIMIT = 0x124 = 292
    print("warning: payload exceeds 0x124; needs multi-frame/DMA continuation")
```

**它只支持 ≤292 字节，大文件需要"multi-frame/DMA continuation"，而它没实现。**

**而 a7c2 在 ZV-E10/A7C2 上的实测格式是：**

```python
SET_FILE_PATH = pack("<HH", 0x1001, 0x0f) + b"\0" + path + b"\0"
SET_FILE2     = pack("<HH", 0x1001, 0x10) + b"\x00\xff\x00" + <整个文件>
GET_FILE      = pack("<HH", 0x1001, 0x11) + b"\x00\xff\x00"
# file_id = 0xFF 表示"用已选定的路径"
# 上限 ARTIFACT_MAX_BYTES = 0x500000 (5 MB)
```

**⇒ 我们的载荷只有 5,568 B，一帧就够。**

### 用法

```bash
export A6700_SRC=/tmp/r3/应用/src

# 先干跑（不碰 USB）
./send_payload.py libJit.so --dry-run

# 真发
./send_payload.py libJit.so --pid 0x0D95 --read-result
```

### 参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `--pid` | `0x0D95` | **初始（海量存储）PID —— ZV-E10 需实测确认** |
| `--service-pid` | `0x0336` | Senser 服务 PID（协议级，跨机型一致）|
| `--path` | `/tmp/jiritsu/libJit.so` | 远端目标路径（固件会自己 dlopen 它）|
| `--read-result` | 关 | 上传后用 `GET_FILE` 读回 `tmmc.jit` |
| `--dry-run` | 关 | 只构帧，不碰 USB |

### 本机依赖（已核实齐全）

```
pyusb    ✓   pyserial ✓   libusb ✓   Python 3.14
```
