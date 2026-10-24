## 〇、2026-10-24 21:40 最新（覆盖下方历史内容）

### 当前最稳定组合（新菜单）
```
镜像   /mnt/build/tmp/hb/v52.img 或 v53.img
cmdline /root/fw/real_cmdline_lowlog.txt（run_quiet.sh）或 loglevel=7 版（run_verbose.sh）
preload libmmapad.so（匿名 mmap +0x2000）+ libmprotsh.so（忽略 PROT_NONE）
        [+ libgpushim.so / libinputinj.so 可选]
kmod   真 backup/grm_ma/dmm + fake_gles（忠实 128KB 环契约，ioctl 未实现→-EINVAL，事件值 0）
       + stub upm/accel/mag/compass
```
- 状态：36 模块、无 oops、无 data abort、无 SIGABRT（连续 190s，两轮复现）
- App：主循环在跑（futex ×44、GLESKICK ×4、反复读 /proc/self/status 与 /proc/1/cmdline）
- **尚未**：打开 /dev/stream、/dev/dmpgles2；未调用 GraphicsImage/gperm；屏幕全黑

### 关键调试钩子（内核，全部已生效）
```
ZZREG      寄存器
ZZMOVMAP   用户映射（600 段）
ZZR7       r7 所属 vma 及大小
ZZSIG      信号投递（含真实 si_code/kpid/kuid）
ZZTGKILL   tgkill 调用
ZZINSN     故障指令原始编码
ZZMWF      libMWF 的 mp_mon
```

### 老菜单（保持基线，勿动）
```
/mnt/build/ab_v603.img —— 启动链稳定、backup 同步队列已实现、initializeConfig 仍未返回
```

---


## 〇、2026-10-24 最新状态（本段为最新，覆盖下面的历史内容）

### 最可用的两个组合（都可复现）

| 组合 | 镜像 | 状态 |
|---|---|---|
| **老菜单**（ZV-E10 原 /usr） | `/mnt/build/ab_v603.img` | 启动链稳定、bootin→sen.elf→im.elf；backup 同步队列已注册并应答（无崩、无分配风暴）；initializeConfig 仍未返回 |
| **新菜单**（ZV-1F 混合 /usr） | `/mnt/build/tmp/hb/v11.img` | 35~36 模块、MonDrv 成功、fake_gles 大窗口映射、进入 App 线程；但崩溃非确定性（分配器/映射语义不对） |

### 内核（必须是这一颗）
```
/mnt/build/src/a7c-kernel/linux/arch/arm/boot/zImage
= 3.0.27_nl-rt106+  + 调试钩子：
  ZZREG（寄存器） / ZZMOVMAP（600 段用户映射） / ZZMWF（libMWF mp_mon）
  ZZTGKILL（信号投递） / ZZEXCSTACK（96 字用户栈，不过滤）
```

### 关键结论（本轮新增）
```
1) 原厂 backup.ko 必须用真的（stub 会让 MonDrv 的 /dev/mem mmap 失败 → MonQueue NULL → 崩）
2) 原厂 grm_gles.ko 在 QEMU 必崩（sugilite_open 访问硬件）⇒ 必须用 fake_gles
3) 原厂 grm_ma.ko 是 0x61003C 服务端（DMP 显存分配应答）⇒ 必须是真的
4) 只有 upm / accel / mag / compass 需要 stub
5) fake_gles 必须实现"恒等/大窗口"mmap 才能让 App 拿到的地址可用
6) 崩溃点会随轮次变化（appFw guard page / libc / 稳态）⇒ 缺的是分配器语义
```

### 常用命令
```sh
# 运行（注意 pkill 用 -x，避免杀掉自己的 shell）
EXTRA='-global driver=bionz-streamfb,property=blank-pattern,value=off' \
INITRD=/root/fw/handoff_p0/initrd_q4.img OUT=/mnt/build/tmp/runX \
/mnt/build/run_quiet.sh /mnt/build/tmp/hb/v11.img 170
pkill -x qemu-system-arm
```

# ZV-E10 QEMU 当前状态速查（2026-10-23 22:30）

> 目的：**上下文丢失也能在 5 分钟内接手**。只写"现在成立的事实"和"下一步做什么"。
> 逐轮细节见 `ZVE10_PORT_ROADMAP.md`；协议数值见 `ZVE10_QEMU_UI_HANDOFF.md`。

## 一、一句话状态

```text
目标（App 画面显示+可交互）：未达成，屏幕仍黑、view 从未创建。
但底座已从"脏镜像+自制 stub 库"换成"纯净真机 /usr + 零售 initrd + 与真机一致的内核"，
并在 c419 首次跑通 retail init → chroot → bootin → im.elf（随后 im.elf 用户态 NULL 解引用）。
```

## 二、当前使用的镜像与组件（一律用这套，别再回旧的 ab_v291 基线）

| 组件 | 来源 / 状态 |
|---|---|
| `/usr` 分区 | **纯净真机 dump** `/root/fw/zve10_delivery/nflasha15.img`（sha256 `d9ad9954…`，207 项；**没有** libBizInitializer.so） |
| initrd | 零售 `nflasha3/INITRD.IMG`（2,183,168 B ext2，`/sbin/init` 是 28,580 B 二进制，内含 35 条 insmod 表） |
| 内核 | 自建 `a7c-kernel`，**release = `3.0.27_nl-rt106+`（与真机 vermagic 一致）**，已打 `MODLOAD` 日志补丁 |
| 内核配置 | `CONFIG_LOCALVERSION="+"`、`LOCALVERSION_AUTO=n`（`localversion-rt` 提供 `-rt106`） |
| 探针 | `/lib/libgrmlog.so`（放 n7 分区）；**纯净 /usr 下尚未成功注入**（probe=0） |

**关键构建命令（缺一不可）**

```bash
export PATH=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin:$PATH
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/lib
export ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
cd /mnt/build/src/a7c-kernel/linux && make -j8 zImage
```

## 三、已替换成 stub 的模块（都在 `/usr/kmod/`，逐个因崩溃而换）

| 模块 | 原因 |
|---|---|
| `udate.ko` | 内核线程 `udm_udate` trap |
| `upm.ko` | 装载后卡住（其后无任何 MODLOAD） |
| `accel.ko` / `mag.ko` / `compass.ko` | `udm_mag` 内核线程崩 |
| `backup.ko` | 内核 oops：`backup_suspend+0x828` NULL 解引用 |
| `grm_gles.ko` | 换成我们的 `fake_gles.ko`（提供 `/dev/dmpgles2`） |

stub 模板：`/mnt/build/tmp/udate_stub/`（复制改名即可，`module_init` 直接 return 0）。
名字↔尺寸对照表：`/mnt/build/tmp/kmodlist.txt`（103 个模块）。

## 四、装载/崩溃演进（用来判断是否退化）

```text
c412 (udate)                  → 22 模块，停在 upm
c417 (+upm)                   → 26 模块，udm_mag 崩
c418 (+accel/mag/compass)     → 32 模块，**im.elf 启动**，用户态 trap 0x0e
c419 (+backup)                → 32 模块，im.elf 启动，用户态 NULL 解引用；内核 oops 消失
```

## 五、下一步（正在做 ①）

```text
① 给 backup 提供"最小可用 /dev/backup 语义"（而不是纯 stub）：
   libbackup.so 的 Backup_read 需要能读到数据，否则 im.elf NULL 解引用
② 把探针装回去（验证 /setting/mode/preload 是否被 bootin 使用；否则退回包壳注入）
③ 回到 UI 主线：资源已就绪却不建 view → 查 UIPC 等待（0x13A 积压 609 / 0x3E014D 重发 17493）
```

## 六、已知的"假线索"（别再走）

- **BBC / libBizFw / SeqController 与 UI 无关**（相机音视频控制层），`CreateAndAddSequence` 不是绘制入口
- 旧镜像里的 `libBizInitializer.so` 是**我们自己写的 stub**，不是索尼的；旧基线 `kicks=11` 不可信
- `mounting1 /dev/dnflasha* failed errno(6)` 在历史成功 run 里也有，**不是**故障原因
- `/usr/lib/appFw.so`、`gui.so`、`libNVM.so`、`libSaveLoadSettings.so` 在本机**根本不存在**，不要等它们
- `libObj.so` 不经 IMDB，im.elf 里是**硬编码路径** `/usr/lib/libObj.so`

## 七、跑机命令

```bash
cd /root/fw/oss
EXTRA='-global driver=bionz-streamfb,property=blank-pattern,value=off -global driver=bionz-streamfb,property=format,value=1' \
INITRD=/root/fw/handoff_p0/initrd_q4.img OUT=/mnt/build/tmp/cXXX \
  /mnt/build/run_cap.sh /mnt/build/ab_v419.img 150
pkill -f qemu-system-arm
```
判读：`serial.log` 看 `MODLOAD read ok` 序列 / `program:`+`address:` / `PV i=`；`qemu.out` 看 `ZZGPEREG`/`ZZGPECMD`。

## 八、2026-10-24 更新（第 6–9 轮后现状）

### 8.1 已达到
- 启动链稳定：纯净 /usr + 零售 initrd + `3.0.27_nl-rt106+` 内核 + 最小 stub
  （udate/upm 保留原厂；accel/mag/compass/backup/grm_ma 用 stub；grm_gles=fake_gles）
  → 35 模块、无内核 oops、可跑 330s 稳定。
- 探针可在纯净 /usr 下注入（替换 `/usr/bin/im.elf` 为 LD_PRELOAD 包壳）。

### 8.2 关键地址（可直接用）
```
pViewManager 变量        libObj + 0x149221C
GraphicsEngine 单例变量  libObj + 0x145CAB8   （运行时实测恒为 0）
  访问路径：sys_getInstance 读 GOT 槽 libObj+0x13DCB78 → 变量地址 → *(var)
Appframework 线程入口    libObj + 0x8A2398（首调用 0x6271D4，uxengine 初始化路径）
线程描述符               libObj + 0x1455F24（.data，+0x08 = 0x008A2399）
osal_valloc_msg_wait     实测 ret=0 且 out 指针有效 ⇒ 分配成功不阻塞（"池耗尽"已证伪）
```

### 8.3 当前唯一卡点
```
App 框架从不自行初始化；我们调 Application_System_Init 只起线程；
该线程阻塞在 futex，而 GraphicsEngine 单例始终为 0
⇒ 引擎从未创建 ⇒ ViewManager/WidgetSystem 永不出现 ⇒ 没有 view*.uxc、没有画面。
直接调 uxengine_init(libObj+0x6271D4) 会崩（它假定引擎已存在）⇒ 引擎创建在更早的位置。
```

### 8.4 下一步
1. 找 GraphicsEngine 单例的真正创建者（.init_array 或更早的 ux::core 初始化），确认它依赖什么；
2. 必要时"手工构造引擎并写入单例槽"（类似早期对 BBC 对象的做法）。

## 九、2026-10-24 02:15 现状（第 6–14 轮总结）

### 9.1 已达成的（可复现）
```
① 启动链稳定：纯净真机 /usr + 零售 initrd + 3.0.27_nl-rt106+ 内核 + 最小 stub
   → 35 个模块装载、无内核 oops、可稳定运行 330s
   stub 清单：udate/upm 保留原厂；accel/mag/compass/backup/grm_ma 用 stub；grm_gles=fake_gles
   backup stub 必须导出原始 145 个符号并接受模块参数（否则 osal_ulogio 等装载失败）
② 探针可注入纯净 /usr（把 /usr/bin/im.elf 换成 LD_PRELOAD 包壳 + 真身 im.elf.real）
③ 可观测：dlopen 序列、osal_valloc_msg_wait、/proc/osal/uipc、线程表、GPE/VDF
④ GraphicsEngine 人造成功：calloc(0x400) 写 libObj+0x145CAB8，再填 +0x220/+0x234
   与 libObj+0x145CBC4（可选回调表，零块=全部跳过），然后调 uxengine_init(0x6271D4)
   → 跑通、不崩、gfx 非 0
```

### 9.2 卡点（UI 出不来）
```
ViewManager 构造(libObj+0x8A4E28) 需要 (this, a, b, c, 配置对象*) 共 5+ 参数，
其中配置对象带 vtable（vtable[+8] 是工厂）。零对象调用会崩在 libObj+0x8A4E42。
调用者 = libObj+0x8A2FA6（一个应用引导函数：先 new EventManager/ModelManager 等，再构造 ViewManager）。
⇒ 要出 UI，等于重建"应用引导函数 + 其依赖的带 vtable 配置对象"这一层。
```

### 9.3 关键地址表（新增）
```
ViewManager 构造       libObj + 0x8A4E28   （5+ 参数，需要带 vtable 的配置对象）
ViewManager 构造调用者  libObj + 0x8A2FA6   （应用引导函数内部）
主循环                 libObj + 0x8A3104   （线程体 0x8A2398 末尾进入）
可选回调表全局         libObj + 0x145CBC4
GraphicsEngine 单例    libObj + 0x145CAB8
pViewManager           libObj + 0x149221C
```

### 9.4 下一步两个选择
- A：继续逆"应用引导函数(0x8A2FA6 所在)"，把它依赖的 EventManager/ModelManager/配置对象逐一造出来
      —— 路径明确但工作量相当于重写 App 的一层初始化；
- B：把目标收窄为"底座 + 引擎层在 QEMU 跑通"（已达成），UI 部分等有应用库/真机界面时再做。
