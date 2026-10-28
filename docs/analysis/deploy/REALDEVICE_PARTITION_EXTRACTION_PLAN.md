# 实机分区提取：准备方案

> 目标：补全 ZV-E10 缺失的 16 个 eMMC 分区备份
> 路径：libJit USB root（安全路径，不写 `/etc/ld.so.preload`）

## 一、已核实的前提（全部来自固件静态分析）

| 项 | 值 | 来源 |
|---|---|---|
| 工具链 | `/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/` | 本机 |
| 固件架构 | ARMv7 Cortex-A5 | libObj / kmod |
| glibc | **2.11.2**（`/lib/libc.so.6`）| `ref_e10/root/lib/` |
| shell 工具 | **busybox 全套**：`dd` `cat` `mount` `sh` `ash` | `ref_e10/root/bin/` |
| eMMC 设备 | `/dev/nflasha<1..31>`、`/dev/nflashaB0/B1`、`/dev/dnflasha*` | `devno.ko` |
| **SD 卡设备** | **`/dev/mmca`（整卡）/ `mmcaa`..`mmcad`（分区）** | `devno.ko` |
| libJit 路径 | `libJiritsu.so` 的 `adj_cmd_set_file2` @0x5B00 → `dlopen("/tmp/jiritsu/libJit.so")` | 反汇编 |
| 载荷要求 | `RTLD_LAZY`、**无 `dlsym`** ⇒ 只需 `_init`（构造函数）能跑 | a7c2-re + 本机核实 |
| 工作分区 | `/tmp/jiritsu` = `nflasha6`（**25 MB，vfat，持久**）| 反汇编 + a7c2 |
| 协议常量 | category `0x1001`、`SET_FILE` `0x4007`、`SET_FILE2` `0x4010`、`file_id=2` | 本机核实命中 |
| AES 密钥 | `md5("jiritsu team" + 4×0x00)`（**固定，非机型专属**）| a6700 源码 + 本机串存在 |


### ★ 文件系统选择（重要修正）

| 文件系统 | 谁提供 | 我们的 payload 能用吗 |
|---|---|---|
| **FAT32 (vfat)** | **内核**（`vmlinux` 里注册了 `vfat`）| **✅ 能** —— `mount -t vfat` |
| **exFAT** | **userspace**（`libInfraFsys.so` / `libObj.so` / `libmpr.so` 里的 **Petit FatFs**：`PFDRV_EXFAT_*`、`PFFAT_EXFAT_*`、`PFENT_EXFAT_*`、`PFVOL_EXFAT_*`）| **❌ 不能** |

**⇒ SD 卡必须格式化为 **FAT32**，不能用 exFAT。**

**★ 为什么可以放心用 FAT32：eMMC 全盘仅 1.85 GB，**任何单个分区都不可能超过 4 GB**
（FAT32 单文件上限），所以不存在必须切分的情况。**

**★ 另一个待验证点：`mount()` 需要 CAP_SYS_ADMIN。**
载荷以 euid=0 运行，但维护进程是否保留完整 capability 未知 ⇒
**Stage 1 必须同时测 `mount`，不只是写文件。**

## 二、要提取的目标（16 个分区）

```
nflasha8, 9, 11, 14, 19, 20, 21, 22, 24, 25, 26, 27, 28, 29, 30, 31
```

**空间估算：**
- eMMC 全盘 ≈ 1.85 GB
- 已有分镜像合计 ≈ 0.84 GB
- **缺失部分 ≈ 1.0 GB** ⇒ **SD 卡需 ≥ 1.5 GB 可用空间**

## 三、⚠️ 风险边界（先读）

**安全的部分：**
- libJit **不写 `/etc/ld.so.preload`** —— 那是上次变砖的路径
- 载荷只写 `/tmp/jiritsu`（25 MB，持久分区）和 SD 卡
- 认证失败 / 载荷跑飞 ⇒ 最多影响维护进程，断电重启恢复

**仍有风险的部分：**
- **读写 `/dev/nflasha*` 本身**：只读 `dd` 是安全的；**任何写操作都危险**
- **SD 卡挂载**：挂错设备（如挂到 `nflasha*`）会破坏数据
- **权限**：载荷以 euid=0 运行，一条错误的 `dd of=` 可能覆盖分区

**硬性约束：**
1. **只读！** 本阶段所有 payload 只允许 `dd if=/dev/nflashaN of=<SD>`
2. **SD 卡先单独插电脑确认格式与剩余空间**
3. **不要用副厂电池**；保持稳定供电
4. **先做 Stage 1（写标记文件），确认载荷真的能跑，再做 Stage 2**

## 四、三阶段计划

### Stage 0：准备（不动相机）

```
① 准备 SD 卡
   · 格式化为 **FAT32**（exFAT 内核不支持，只能靠 Sony 的 userspace 库，payload 用不了）
   · 可用空间 ≥ 1.5 GB
   · 在电脑上建目录 dump/

② 编译最小验证载荷（ARMv7）
   见 §五 payload-A

③ 准备发送工具
   · 移植 a6700-unlock（改 PID 0x0D95）或 a7c2-re 的 usb_service_probe.py
   · 先只做到"能枚举 + 认证通过"，不发任何文件
```

### Stage 1：最小验证（关键闸门）

```
④ 发送 payload-A（只写一个标记文件）
⑤ 验证：/tmp/jiritsu/jitran.txt 存在且内容正确
   ★ 判据只看内容，不看"USB 请求被接受"
⑥ 若 Stage 1 失败 ⇒ 停下，不要继续
```

### Stage 2：分区提取

```
⑦ 发送 payload-B（挂 SD 卡 + 循环 dd）
⑧ 验证：SD 卡 dump/ 目录里 16 个文件都在，且非全零
⑨ 回到电脑核对 SHA-256，与全盘镜像中对应区域比对
```

## 五、载荷设计（极简，因为 busybox 什么都有）

### payload-A：标记文件（验证代码真的跑了）

```c
/* payload_a.c — 只写标记文件，最保守
 * 编译：
 *   arm-buildroot-linux-gnueabihf-gcc -shared -fPIC -nostdlib \
 *       -Wl,-soname,libJit.so -o libJit.so payload_a.c
 */
#define SYS_fork   2
#define SYS_exit   1
#define SYS_execve 11

static int sc3(int n, int a, int b, int c)
{
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2) : "memory");
    return r0;
}

__attribute__((constructor)) static void init(void)
{
    int pid = sc3(SYS_fork, 0, 0, 0);
    if (pid == 0) {
        char *argv[] = { "/bin/sh", "-c",
            "echo JITRAN_OK_$(date +%s) > /tmp/jiritsu/jitran.txt", 0 };
        sc3(SYS_execve, (int)argv[0], (int)argv, 0);
        sc3(SYS_exit, 1, 0, 0);
    }
    /* 父进程（维护进程）立即返回，不阻塞 */
}
```

**★ 用直接 syscall 而不是 `system()`：避免依赖 libc 符号解析，载荷更稳。**

### payload-B：分区提取

```c
/* payload_b.c — 挂 SD 卡并逐分区 dd（只读）
 * 脚本逻辑：
 *   mkdir -p /tmp/sd
 *   mount -t vfat /dev/mmcaa /tmp/sd
 *   mkdir -p /tmp/sd/dump
 *   for n in 8 9 11 14 19 20 21 22 24 25 26 27 28 29 30 31; do
 *       dd if=/dev/nflasha$n of=/tmp/sd/dump/nflasha$n.img bs=64k 2>/dev/null
 *       sync
 *   done
 *   echo DONE > /tmp/sd/dump/_DONE
 */
```

**执行方式：fork 后在子进程里跑，父进程立即返回**（避免拖住维护进程）。

## 六、验证判据（分层）

| 层 | 判据 | 不能据此声称 |
|---|---|---|
| 认证 | 设备枚举为 `054C:0336` 且二次认证通过 | 不能只看 PID 变了 |
| 载荷送达 | `SET_FILE2` 返回成功 | 不能当"代码跑了" |
| **代码真的跑** | **`/tmp/jiritsu/jitran.txt` 内容正确** | — |
| 提取成功 | SD 卡 16 个文件存在、非全零、大小合理 | 不能只看文件存在 |
| 备份可信 | 与本机全盘镜像对应区域 SHA-256 一致 | — |

## 七、应急

| 情况 | 处理 |
|---|---|
| 载荷没跑 | 检查 `dlopen` 是否成功（`adj_cmd_set_file2` 失败会返回 `0x10000000`）|
| 相机无响应 | 断电重启（libJit 不改持久状态，重启即恢复）|
| SD 卡挂载失败 | 确认设备名 `mmcaa` vs `mmca`，确认文件系统类型 |
| 提取速度极慢 | eMMC 读取本身慢；分批做，每批写完 `sync` |
| **发现任何写操作** | **立即停止** —— 本阶段不允许任何写 flash |

## 八、待确认（进 Stage 0 前）

1. **ZV-E10 的初始 USB PID 是否确为 `0x0D95`**（插上相机 `lsusb` 确认）
2. **SD 卡设备是 `mmcaa` 还是 `mmca`**（挂载后 `mount` 观察）
3. **`/tmp/jiritsu` 分区是否在启动时自动挂载**（`nflasha6`）
