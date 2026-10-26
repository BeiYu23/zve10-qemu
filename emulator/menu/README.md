# 菜单专用宿主（emulator/menu）

**结论：本目录的代码没有让菜单显示出来。** 它是本阶段所有实验的可复现载体，
以及一份"已经做到哪一步、卡在哪"的现场。完整结论见
[`docs/FINAL_HANDOFF_2026-10-27.md`](../../docs/FINAL_HANDOFF_2026-10-27.md)。

## 这是什么

不启动原厂 `im.elf` 的 main（因而跳过整套 IMDB 硬件初始化链），
改为直接加载原厂动态库并调用原厂生命周期入口：

```
libosal_uipc → libosal_utm → libbackup → libMWF(MWF::Initialize) → libObj
  → Application_System_Init(NULL)
  → libObj+0x8a2398   （原厂线程体：ux 初始化 → 栈上构造 App → 投事件 → 主循环）
```

所有被调用的都是**固件自己的函数**，没有伪造菜单、没有伪造绘制结果。
唯一"替代"的是 Backup 同步队列（只应答 4 个已核实的 key，其余显式返回失败）。

## 目录内容

| 文件 | 作用 |
|---|---|
| `menu_host.c` | 宿主本体；含线程/信号量/物理地址等观测钩子（可按需删除） |
| `start.S` | ARM EABI `_start`，调固件 libc 的 `__libc_start_main` |
| `build.sh` | 交叉编译（`-marm`、`-mfloat-abi=softfp`、链接固件 `libc-2.11.2`/`libdl`） |
| `make_image.py` | 生成实验镜像：替换文件逐个**读回校验 SHA-256**，分区 `e2fsck -n` |
| `run.sh` | 固定时长运行（`SECONDS_MAX`），不使用 idle 判稳 |
| `dmm_shim/` | 内核态 DMM 应答 + 伪造 `dmpgles2` 设备（协议按反汇编实现） |

## 复现

```bash
# 1) 工具链需要自身运行库
export LD_LIBRARY_PATH=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/lib

# 2) 构建宿主
bash build.sh

# 3) 构建内核模块
export CROSS_COMPILE=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin/arm-buildroot-linux-gnueabihf-
make -C /mnt/build/src/a7c-kernel/linux M="$PWD/dmm_shim" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules

# 4) 生成镜像（base 用已验证的基线，输出必须是新文件）
python3 make_image.py --base /mnt/build/ab_v3050.img \
    --output /mnt/build/menu-only-new.img --work /mnt/build/tmp/menu-new \
    --host ./menu-host --module ./dmm_shim/fake_gles2.ko

# 5) 运行（一次只允许一台 QEMU）
SECONDS_MAX=60 bash run.sh /mnt/build/menu-only-new.img /mnt/build/tmp/run-new
```

## 现在卡在哪（一句话）

原厂 UX 初始化的第一次执行，在 libc 的 Thumb syscall 蹦床上以 **ARM 态**取指
（`trap 6 = 非法指令`，故障地址为偶数）；其后的 App 主体初始化还需要一个
`libObj+0x145CBC4` 指针，而该指针在本镜像中**不存在任何写者**。

## 不要做的事

- 不要把"崩溃点后移"当成进展；
- 不要用 idle 退出判断稳定（必须固定时长）；
- 不要在宿主里裸调 `osal_stat_msgq`（会崩 guest）；
- 不要在 syscall 热点蹦床上下 GDB 断点（会把 guest 拖死）；
- 不要给 `MISSSO` 的可选库造桩（真机也没有这些库）；
- 不要把 `dlopen` 返回的 handle 当模块基址（用 `/proc/self/maps` 里 `r-xp` 的起点）。
