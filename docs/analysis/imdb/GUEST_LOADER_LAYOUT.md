# 客机加载器布局与探针注入的正确姿势

> QEMU 侧注入 `libcrashprobe.so` 时报：
> `error while loading shared libraries: ld-linux-armhf.so.3: cannot open shared`
> 本轮把布局查清，结论是**探针的 ABI 不对**，不是路径问题。

---

## 一、`/lib` 与 `/usr/lib` 是两个不同分区

| 路径 | 来源 | 内容 |
|---|---|---|
| **`/lib`** | **rootfs（nflasha7）** | **glibc 加载器与 C 库**：`ld-linux.so.3`（符号链接，12 B）→ `ld-2.11.2.so`（**101,736 B**） |
| **`/usr/lib`** | `/usr`（nflasha15，混合镜像） | 新 UI / 中间件库（`libObj.so` / `appFw.so` …）**不含任何加载器** |

⇒ **`/lib` 不是 `/usr/lib` 的软链**，两者是独立挂载。

## 二、`im.elf` 要的加载器是 `//lib/ld-linux.so.3`

```
$ readelf -l ref_zv1f/usr/bin/im.elf | grep INTERP
  [Requesting program interpreter: //lib/ld-linux.so.3]
（ZV-E10 的 im.elf 完全相同）
```

**注意开头是双斜杠 `//lib`**（Sony 的写法，等价 `/lib`）。

## 三、★ `ld-linux-armhf.so.3` 全设备不存在

| 分区 | `ld-linux-armhf.so.3` |
|---|---:|
| nflasha7（rootfs） | **0** |
| nflasha15（usr） | **0** |
| nflasha10（etc/tmp/var） | **0** |
| nflasha3（system） | **0** |

**`ld-linux-armhf.so.3` 是 buildroot 交叉工具链的加载器名**，相机用的是 Sony 自己的
`ld-linux.so.3 → ld-2.11.2.so`（glibc **2.11.2**）。

⇒ 探针报那个错，说明 **`libcrashprobe.so` / 包壳是用 buildroot 工具链编的**（glibc 2.26），
被 glibc 2.11 的进程加载时找不到它期望的加载器。**与 `LD_PRELOAD` 路径无关。**

## 四、要注入用户态探针，必须满足 glibc 2.11

我们没有 Sony 那套 gcc 4.5.1 / glibc 2.11.2 工具链，所以：

1. **`.so` 必须不依赖 libc**：`-nostdlib -shared -fPIC`，只用裸 syscall
   （`write` / `open` / `mmap`），这样任何 glibc 版本都能预加载
2. **包壳不能用 buildroot 编的 ELF**（那会引入 `ld-linux-armhf.so.3` 依赖）；
   用 **`#!/bin/sh` 脚本**，或**静态链接**的 ARM 可执行文件
3. 历史上我们部署过的 `payload.so`（7,992 B）就是按"不用 libc"的路子做的

## 五、⚠️ 顺带发现：ZV-1F 的 rootfs 缺加载器符号链接

```
ZV-E10 rootfs/lib : ld-linux.so.3 -> ld-2.11.2.so   ✓
ZV-1F  rootfs/lib : ld-2.11.2.so                    （★ 没有 ld-linux.so.3）
```

⇒ **若用 ZV-1F 的 rootfs，`im.elf` 的 `//lib/ld-linux.so.3` 会直接找不到而无法启动。**
⇒ 进一步印证移植方案"**保留 ZV-E10 的 rootfs、只补 3 个目录**"是正确的。
（v5 镜像里确认 `/lib/ld-linux.so.3` 存在，属主 57285:1000。）

## 六、建议：走内核异常钩子（方案 3）

**推荐直接做方案 3**，理由：

1. 彻底绕开 glibc 2.11 / buildroot 2.26 的 ABI 问题
2. QEMU 侧已有 `ZZEXCSTACK` 钩子，加一段 `r-xp` maps dump 成本很低
3. 不用改客机任何文件 → 交付镜像保持干净、可复现

具体做法：异常处理里读 `/proc/<pid>/maps`，把所有 `r-xp` 行（start-end + 路径）打到串口，
就能把 `pc=0x05B4F3BE` 映射到具体库。
