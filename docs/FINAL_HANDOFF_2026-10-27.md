# ZV-E10 QEMU 菜单移植 —— 结项交接（2026-10-27）

**结论先说：目标未达成。** 原厂菜单从未在 QEMU 中显示过；`GRM2` 无绘制记录，
`/dev/dmpgles2` 从未被打开，`/dev/stream` 内容从未由 App 写入。
本文记录的是**已经证实的东西、已经排除的东西、以及为什么走不下去**，用于交接或重启。

---

## 1. 目标与判据

目标：让 ZV-E10 原厂 `/usr` 的 App 老菜单在 QEMU 中**由 App 自己绘制**显示。

不可伪造的完成判据（本阶段一条都没满足）：

1. App 真的调用自己的绘制入口（`libObj` 的 `GRM_bitmapCreate` / `GRM_gpermFillrect`）；
2. 图形设备 `/dev/dmpgles2` 被打开并产生 ioctl；
3. `/dev/stream@0x20312000` 从均匀 `0x5A5A` 变为非均匀；
4. 显示端呈现像素，且一次真实输入引起画面变化。

---

## 2. 已证实可用（可直接复用）

| 项 | 证据 | 位置 |
|---|---|---|
| 自定义 `cxd90045` 机型 + DDR 布局与 cmdline 一致 | `mem=1G@0` / `mem=256M@0x80000000@1` 对应 DDR0/DDR1 | `src/qemu-sony/hw/arm/bionz.c` |
| `bionz-streamfb` 显示设备（镜像物理 `0x20312000`） | QMP screendump 可截帧 | `src/qemu-sony/hw/display/bionz_streamfb.c` |
| **DMM 协议实现（open/close）** | 反汇编得出：`reply+0x10`=物理地址、`+0x14`=长度、`+0x04`=单元索引、`+0x06`=seq、`reply[0]`=`0x8000|op` | `emulator/menu/dmm_shim/fake_gles2.c` |
| DMM 修复后的实际效果 | `QMMAP len=0x2000000 off=0x20312000`（正确的 32MB 图形池） | §90 |
| MA 服务确实工作 | GDB 断 `ma_user_sync_cb` 抓到 op=7/1/9/10/2/1/9×4 序列 | §91.7 |
| 原厂启动路径在修正 DMM 后**稳定 180 秒零崩溃** | 此前同路径会在 ~60 秒随机崩在 ld.so | §91.2 |
| 菜单专用宿主（绕过 IMDB 硬件链） | 加载原厂 UIPC/UTM/Backup/MWF/Obj，调用 `Application_System_Init` | `emulator/menu/menu_host.c` |
| 可审计的镜像构造 | 每个替换文件读回校验 SHA-256、分区 `e2fsck -n` | `emulator/menu/make_image.py` |
| 固定时长运行器（不用 idle 判稳） | `SECONDS_MAX` 到期才结束 | `emulator/menu/run.sh` |
| 原厂线程体全文解出 | `ux 初始化 → 栈上构造 App → 投事件 → 主循环 → 析构` | §98 |

---

## 3. 已排除（有实测证据，不必重复）

| 假设 | 结论 | 依据 |
|---|---|---|
| 13 个"补回的库"是原厂文件 | **否**，全是本机 GCC 6.4 生成的桩 | §89 |
| `ld.so+0xA706` 是 `DT_INIT_ARRAY` 处理 | **否**，是 PLT 符号解析路径 | §89 |
| 加载器崩溃源于固件堆损坏 | **未证实**；修正 DMM 后同路径不再崩 | §91.2 |
| 缺 33 个 `MISSSO` 库 | **不需要**，真机 `nflasha15` 里同样没有这些库 | §91.3 |
| 崩在 `osal_rcv_msg_tmo` 用户态入口 | **否**，命中的是内核同名函数 | §92.3 |
| `p4`（`0x145CBC4`）有写者 | **无**：字面量池/movw-movt/基址+偏移三种写法都扫过，且预置写监视从未命中 | §100–101 |
| 单例从未创建（全部） | **部分错**：`GraphicsEngine` 是静态对象，一直存在 | §104 |
| 一个字节就能修好 | **否**：单例之间存在环形依赖，手动创建会互相踩空 | §103 |
| 我们自己的信号量拦截导致崩溃 | **否**：去掉后崩溃完全相同 | §112 |

---

## 4. 当前确切阻塞点（按因果顺序）

1. **UX 初始化的第一次执行中途崩溃，且崩溃前已把状态字 `0x145ca6c` 置为 1。**
   该字由初始化自身在入口写入（`0x610f9c`，写监视已抓到），**并非被污染**；
   后续调用看到 1 就自增返回，故表现为"被跳过"。（§113 已撤回早先"守卫被污染"的解释。）
2. **崩点在 libc 的 Thumb syscall 蹦床**：`trap 6 = 非法指令`，
   故障地址 `0x04042796` 为**偶数**，该处字节 `80 bd` 是 Thumb 的 `pop {r7,pc}`
   ⇒ 某个间接调用/返回**丢了 Thumb 位**，Thumb 代码被以 ARM 态执行。
   已核对 `CONFIG_ARM_THUMB=y`、hwcap 正常，排除"内核不支持 Thumb"。
3. **`p4`（`libObj+0x145CBC4`）无人创建**：它是 `0x627164` 这一步的必需对象，
   而在整份 libObj 中既无写者、也无指针槽指向；运行期（含原厂路径 110 秒）从未被写。

⇒ 三者构成一个闭环：要让 App 走到绘制，必须让 UX 初始化完整跑完；
要让初始化跑完，必须解决 Thumb 位丢失；而 `p4` 的产生就在初始化后半段——
目前初始化在到达那里之前就崩了。

---

## 5. 下一步（若重启此课题）

按信息增益排序：

1. **定位 Thumb 位丢失点**（最关键）。建议内核侧记录 `regs->ARM_cpsr & PSR_T_BIT` 在
   `ret_to_user` / 信号返回 / `blx` 目标处是否翻转；或在可疑 `blx` 前用写监视保护返回地址。
   注意：GDB 断点不能下在 syscall 热点蹦床上（会把 guest 拖死，§107 已记录）。
2. **查清 `0x145ca6c` 状态字为何为 1**：它由 `libObj+0x610f40` 从某个全局拷贝而来
   （GOT 槽指向 `ux::core::ID::ZERO`），需要确认宿主进程里谁把它置成了 1。
3. **`p4` 的生产者**：在初始化后半段（`create_mspace` / `resmgr` 之后）继续单步，
   或用写监视覆盖整个 `.bss` 段一次性定位。
4. 上述任一项突破后，才轮到绘制侧（`GRM_*` → `/dev/dmpgles2` → streamfb 提交）。

---

## 6. 不要做的事（血泪教训）

- **不要**再把"崩溃点后移"当作进展；必须先确认前一处失败真正消失。
- **不要**用 idle 退出判断稳定；固定时长。
- **不要**在宿主里裸调 `osal_stat_msgq`（会把 guest 崩掉，§91.6）。
- **不要**在 syscall 热点蹦床下 GDB 断点（§107）。
- **不要**给 `MISSSO` 的可选库造桩（真机也没有，§91.3）。
- **不要**把 `dlopen` 返回的 handle 当作模块基址（要用 `/proc/self/maps` 的 `r-xp` 起点，§94.3）。
- **不要**相信我们自制内核里 `ZZSIG` 日志的 `kpid/kuid` 字段（取错 siginfo 联合体，§108.2）。

---

## 7. 资产清单

| 路径 | 说明 |
|---|---|
| `emulator/menu/menu_host.c` | 菜单专用宿主（含各类观测钩子，可按需裁剪） |
| `emulator/menu/dmm_shim/fake_gles2.c` | 内核态 DMM 应答 + 伪造 `dmpgles2` 设备（协议已按反汇编实现） |
| `emulator/menu/make_image.py` | 生成实验镜像，替换文件逐一读回校验、`e2fsck -n` |
| `emulator/menu/build.sh` / `run.sh` | 交叉编译（softfp、固件 libc）与固定时长运行 |
| `/mnt/build/tb_*.sh`、`gdb_*.py` | GDB 取证脚本（写监视、断点、线程 PC 采样） |
| `docs/REALCAM_GROUND_TRUTH_2026-10-27.md` | 逐轮证据（§1–§112） |
| `docs/ZVE10_A7C2_STARTUP_AUDIT_2026-10-26.md` | 对 a7c2-re 的架构对照与早期结论审计 |

实验镜像：`/mnt/build/menu-only-v*.img`（每版对应文档中的一节）。
