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
