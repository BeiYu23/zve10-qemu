# ZV-E10 老菜单 UI in QEMU —— 完整接手 / 最佳实践文档

**版本**：2026-10-26
**目标**：让 ZV-E10 原厂 `/usr` 的 App 菜单在 QEMU 中**真正显示**（App 自己渲染，不是我们伪造画面）
**当前状态**：**未达成**。显示链路、绘制 shim、服务应答、探针体系全部就绪；App 初始化链未走通。

> 路线对照（与 a7c2-re 的方法差异、该改哪一层）：见 `docs/ROUTE_DIFF_VS_A7C2.md`。
> 卡点调用链（GDB 实证 + 0x3E014D 服务应答）：见 `docs/TRACE_GRM_INIT_CHAIN.md`。

---

# 第一部分：先读这一页（纪律）

## 0.1 唯一可信的进展判据

```
屏幕上出现颜色 / 整屏纯色            ⇏  进展
GRM2 日志 > 0（App 真的调用了 GRM）   ⇒  进展
```

**为什么**：`hw/display/bionz_streamfb.c:180-184` 原文写着
*stream buffer is memset() to 0x5a by the VDF init code and stays like that until the App
actually renders, so a uniform buffer means "no frame yet"*。
`(90,73,214)` 反推 RGB565 = `0x5A5A`（零误差）⇒ 就是填充值。模型里 `streamfb_buffer_uniform()`
**用"整屏同色"来判定"还没渲染"**。

## 0.2 四条反复踩到的坑

| 坑 | 正确做法 |
|---|---|
| "没观察到"当成"没发生" | LD_PRELOAD 只能看见**跨模块 PLT 调用**；同模块内部 `bl` 看不见（MWF handler 就是内部创建）。换观察层：hook libc（cond/sem 是最终出口），或看**效果**而非调用 |
| 线程 id 当固定锚点 | tid 会漂移（187 → 176/189）。用 `comm + 栈特征` 或每轮重新取 |
| 判据可被伪造 | 用"不可伪造"的判据（GRM 调用命中），别用屏幕颜色 |
| 先叠结论再验证 | 顺序必须是：**先证伪自己的里程碑**，再往上叠 |

## 0.3 单次实验成本（很重要）

```
改探针 → 编译（秒级）→ 打镜像（秒级）→ 跑 QEMU（40s，提前退出）→ 看结果
结论不确定时不要改探针：直接开 GDB 实例，用断点/读内存回答（见 §3）
```

---

# 第二部分：系统现状

## 1. 已确认的底座（可放心使用）

| 组件 | 状态 | 关键信息 |
|---|---|---|
| QEMU 机型 | ✅ | `hw/arm/bionz.c`，machine `cxd90045` |
| 显示模型 | ✅ | `hw/display/bionz_streamfb.c`，镜像物理地址 **`0x20312000`**，RGB565 |
| 正确几何 | ✅ | `width=640 height=480 stride=1280 format=1 blank-pattern=off` |
| 满屏彩条自测 | ✅ | `ab_v651`（证明"能显示"，与 App 无关） |
| 绘制 shim | ✅ | `libgrm2.so`：`GRM_bitmapCreate` → `[bmp+0x08]`=VDF 缓冲；CPU 版 `gpermFillrect/Rectblit` |
| backup 数据服务 | ✅ | 内核 stub 解析真实 BK4（页表@0x10C / 槽表@0x143E / 数据区@0x31E40）。**但删掉注册 boot 表现不变 ⇒ 非必需** |
| 日志控制队列 | ✅ | `0x8F0139` 用户态应答（`ULOGIOREG rc=0`） |
| 内核探针 | ✅ | `fake_gles.ko`：MAPD(vma) / THR(pc,sp) / STK(用户栈) 等 |
| 用户态探针 | ✅ | `/lib/libgrmlog.so`（LD_PRELOAD）：锁 / cond / sem / ioctl / 线程 / MWF 信号 |
| 内核 & 模块 | ✅ | 3.0.27_nl-rt106+，35 个模块；bootin → sen/ash → `im.elf` |

## 2. 卡点：`initializeConfig()` 不返回（**2026-10-26 已定位并部分解决，见 §2.0**）

### 2.0 更新：真正的卡点 = Backup 同步服务 `0x3E014D` 无人应答

用 GDB 逐层下降（每轮 2.5 分钟）得到的完整链：

```
libObj+0x7D7D30 请求等待 → q2 worker(0x7D7C10) → cb2(0x7D7870, P[0]=1)
→ 0x7D7940 → 0x7D5200 → 0x7D8560 → 0x78C560 → 0x790B30 → 0x7DCF70
→ 0x6F925E → vtbl+8=0x6F534E (port init) → 0x6F4C74 → blx Backup_nread@plt
→ libbackup.so:Backup_nread → backup_sync_msg(qid=0x003E014D) → 永不返回
```

* 在 preload 里按 `(qid, msg, …)` 正确应答 `0x003E014D` 后，App **越过该卡点**
  （镜像 `ab_v1610/ab_v1620`，连续 118s 无异常，应答 5000+ 条）；
* 但 **`GRM2` 仍为 0**：App 还没走到绘制，截图噪声不算进展；
* ⚠️ 早期探针把 `osal_snd_sync_msg` 参数写成 `(msg, qid)`（顺序反了），
  所以历史上"应答过 0x3E014D 没变化"的结论**作废**。

**完整证据、协议布局、复现脚本见 `docs/TRACE_GRM_INIT_CHAIN.md`。**

### 2.1 静态结构（历史记录）

### 2.1 静态结构
```
initializeConfig (libObj+0x14BE84)
  └ new(24) + ctor(libObj+0x14BD58)
      ├ load libObj.initializeModelConfig      → config+0x08
      ├ load viewUnified2.initializeViewConfig → config+0x04
      ├ (fallback) load libObj.<…>             → 必失败 ⇒ exit(0)
      └ load libObj.initializeModelTree        → config+0x0C
```
* config 对象 24 字节：`+0x00` vtable / `+0x04` viewConfig / `+0x08` modelConfig / `+0x0C` modelTree / `+0x14` byte
* **只有 4 次 load 全成功才返回**；任一失败 → 清理后 `exit(0)`（"隐藏 viewUnified2.so 后 EX=0"不是新故障）

### 2.2 运行时链条
```
grm_init → libObj+0x7D7BB0 → libObj+0x7D7D30
   0x7D7D30 职责 =【按需创建两个队列（各带 worker）】+ 发起请求并等待（不是纯等待！）
   内部：入队(0x7D7840) → signal(q2+0x20) → cond_wait(q2+0x68, q2+0x50)   ← 卡这里
   醒来后：ldr r3,[r7,#20] ; cmp r3,#0 ; bne 0x7D7E74(重试) ; 否则置完成标志并返回
```

### 2.3 队列对象（地址跨运行稳定）
```
q  = 0x072E5CD0        q2 = 0x072E5C28
布局： +0x04 队列mutex   +0x20 队列cond   +0x50 完成mutex   +0x68 完成cond
       +0x98 {count, head} 链表         +0xA4 回调指针
worker 入口 = libObj+0x7D7C11（在 libObj+0x7D7780 里 pthread_create，构造时传入 cb）
请求方等待的完成 cond = q2+0x68 = 0x072E5C90
```

### 2.4 决定性证据（libc 层面统计）
```
整轮只有 7 次 pthread_cond_signal，目标：0x5DF78×2 / 0x5E7D8×2 / 0x86AD0 / 0x86C88 / 0x72E5C48
★ 0x072E5C90 从未出现在 signal 里 ⇒ 完成通知从未发出
```

### 2.5 已排除
```
· 不是 osal 信号量：所有 tmo=-1 的 osal_wai_sem_tmo 都返回了（S=81 / E=81）
· 不是"MWF 没初始化"：MWF::Initialize 被调用；libMWF 内部确实会 SetSignal（lr 落在 libMWF）
· 0x7D7D30 不能补成 return 0（那样队列根本不会被创建）
```

## 3. 已经做到哪一步

### 3.1 接管 worker —— **已成功**（镜像 `ab_v1531`）
```c
loop:
  lock(q+0x04); 等 count>0; 取项(count--, head=head->next); unlock
  cb = *(void**)(q+0xA4);  arg = *(void**)(item+4)
  在【独立线程】里调用 cb(arg)          // 避免阻塞 worker
  usleep(300ms)
  trylock(q+0x50); signal(q+0x68); unlock
  free(item-8)
```
实测：
```
W5 q=0x072e5cd0 / W5 q=0x072e5c28
W5D t=0x00008001           取到同步请求
CBX enter / CBX exit       回调正常返回
W5S q68=0x072e5c90         ★ 精确命中 q2 的完成 cond
```
⇒ **"发不出通知"这一层已解决。**

### 3.2 没解决的最后一环：结果字段
```
回调参数 arg = 0x09458C68（主线程栈上，主线程 sp≈0x09458BC8）；arg+16 原值 = 1
请求方醒来检查 [r7,#20]：=0 成功；≠0 → 重试(re-enqueue)
实验：arg+16 写 0（一次 / 连续 200 次）都 没 让请求方放行
      （连续写那版连循环都没走完，疑 arg 指向的栈帧已失效）
⇒ arg+16 很可能不是请求方真正读的那个字段，或对象身份不对
```

---

# 第三部分：工作流与最佳实践

## 4. 快速迭代（实测 110~150s → 40s）

### 4.1 `run_fast.sh`（已就绪）
```bash
OUT=/mnt/build/tmp/xx \
EXTRA='-global driver=bionz-streamfb,property=blank-pattern,value=off \
       -global driver=bionz-streamfb,property=width,value=640 \
       -global driver=bionz-streamfb,property=height,value=480 \
       -global driver=bionz-streamfb,property=stride,value=1280 \
       -global driver=bionz-streamfb,property=format,value=1' \
./run_fast.sh /mnt/build/<IMG> 120 12 INITCFG_EXIT
# 参数：IMG [MAXSEC] [IDLE秒] [marker]
# 环境：MINRUN（默认40）、INT（"有意义日志"正则，可覆盖）
```
它做四件事：
1. **去掉 Xvfb**：`-display none` + QMP `screendump`（实测照样出 640×480 ppm）
2. **提前退出**：命中 marker / 连续 IDLE 秒无"有意义新日志" / 超时
3. **MINRUN=40s**：避免把启动空窗误判为卡死
4. 结束自动 kill + 转 png

⚠️ 坑：**不要用"日志文件大小不变"判断空闲** —— 串口一直在刷 UIPC 计数。
必须统计"有意义日志行数"（`INITCFG_|GRM2|CBX|W[0-9] |CS[0-9]? |…`）。

### 4.2 GDB 直连：一次启动做多次实验（强烈推荐）
**libObj 基址跨运行稳定在 `0x05E82000`**（`x/4xw 0x5e82000` 读到 ELF 魔数）。
```bash
# 启动时加 -gdb tcp::1235（其余参数同 run_fast.sh）
gdb-multiarch -q -batch -ex 'target remote :1235' \
  -ex 'info registers pc sp' \
  -ex 'x/8xw 0x05E82000+0x7D7E00'
```
能做什么：
* 断点停在关键点（如 `0x05E82000+0x7D7E06`，读 `[r7,#20]` 前那条）→ **一次钉死 r7 与 arg 是否同一对象**
* 直接改内存/寄存器验证假设（把结果字段写 0 再 continue）
* 失败只需 `continue`，**不用重启**

⚠️ `savevm/loadvm` 不可用（`bionz-streamfb` 等自研设备未注册 vmstate）⇒ **GDB 直连是省时正解**。

### 4.3 其它可叠加优化
| 优化 | 说明 |
|---|---|
| 串口降噪 | guest 每秒刷几千行 `ZZIOCTL …0x8000000a/0b/05`（单轮 15k+ 行） |
| 一次跑全量 | 与其多轮验证，不如一轮 dump 所有相关点（探针多加几行日志几乎零成本） |
| 并行准备镜像 | `dd/debugfs` 与上一次运行重叠 |

## 5. 复现步骤

### 5.1 运行（务必放 systemd scope，别把内存记到 dsh-web 上）
```bash
systemd-run --scope --collect \
  -p MemoryMax=3G -p MemorySwapMax=1G -p KillMode=control-group \
  --unit=qemu-zve10-$(date +%s) -- bash -lc '<run_fast.sh 或 run_lowlog.sh 命令>'
pkill -x qemu-system-arm     # 结束必清理
```

### 5.2 输出
```
$OUT/serial.log   串口
$OUT/screen.png   截图（注意 §0.1：纯色 = 没渲染）
$OUT/qmp          QMP socket
```

### 5.3 改镜像（`n7`=rootfs 8MB@168MB；`n15`=/usr 300MB@732MB）
```bash
dd if=IMG of=slice.img bs=1M skip=732 count=300 status=none
debugfs -w -R 'rm /lib/libgrmlog.so' slice.img
debugfs -w -R 'write /mnt/build/libgrmlog-xxx.so /lib/libgrmlog.so' slice.img
dd if=slice.img of=IMG bs=1M seek=732 conv=notrunc status=none
```

### 5.4 探针挂载
```
/bin/im.elf 是 shell wrapper：
  #!/bin/sh
  LD_PRELOAD="/lib/libgrmlog.so" exec /usr/bin/im.elf.real "$@"
换成自己的 .so 即可；探针源码在 /mnt/build/menuc*.c，
编译：arm-linux-gnueabihf-gcc -O2 -shared -fPIC -nostdlib -Wl,-soname,libgrmlog.so …
```

## 6. 关键镜像
| 镜像 | 说明 |
|---|---|
| `ab_v1132` | **干净基线**（无二进制补丁），排障从这里开始 |
| `ab_v1290` | 打了 `0x7D7E0A`（重试分支→nop） |
| `ab_v1380` / `ab_v1400` | ctor 整段 `bx lr` / 只 nop 4 次 load（都会崩，仅供对照） |
| `ab_v1531` | **worker 接管 + 回调独立线程 + 精确 signal（当前最完整）** |
| `ab_v1550/1560` | 结果字段写入实验 |

---

# 第四部分：技术附录

## 7. 关键结构与语义（已核实）

### 7.1 grm 队列
```
+q0x04 mutex   +q0x20 cond(队列空)   +q0x50 mutex(完成)   +q0x68 cond(完成)
+q0x98 {u32 count, void* head}        +q0xA4 回调
item 布局： item-8 = type ; item+4 = cb 参数
worker 分支：type==2 → 只调 cb，不 signal；type==0x8001 / 其它 → cb 后 signal 完成
```
### 7.2 MWF::ObjSignal（libMWF.so）
```
+0x08 cond ; +0x38 ObjLock* ; +0x3C flag(u8)
GetSignal(u32 tmo)：tmo==0xFFFFFFFF ⇒ 无限 cond_wait；tmo==0 ⇒ 立即超时
  返回码：0=成功 / 12→6 / 110→5 / 其它→0xFFFF；无论成功或超时都清 flag
SetSignal()：置 flag + cond_signal（只有 libObj 里 2 个调用点，且都在错误/中止路径）
⇒ 正常置位走 EventHandlerUseSignal（事件驱动），伪造 GetSignal 只会让它空转
```
### 7.3 显示与 VDF
```
framebuffer 物理 0x20312000，RGB565，640x480，stride 1280
VDF init 会 memset 0x5A ⇒ 纯屏 = 未渲染
```

## 8. 已验证无效 / 危险的路线（别再走）

| 做法 | 结果 |
|---|---|
| `0x7D7D30` 补成 `return 0` | ❌ 跳过队列创建，之后无法接管 |
| ctor `0x14BD58` 整段 `bx lr` | ⚠️ `initializeConfig` 返回(EX=1)，配置为空 → 崩在 `0x50` |
| 只 nop ctor 的 4 次 load（`0x14BD90/A8/C0/DA`） | ⚠️ 同上，返回后崩在 `0x06CCF3D7` |
| 隐藏 `viewUnified2.so` | ❌ 走 fallback → 必失败 → `exit(0)` |
| 强返 `MWF::ObjSignal::GetSignal` = 0 | ❌ 醒来查状态又回去等 |
| 看门狗定时 `SetSignal(obj)` | ❌ 同上（信号是消费型，不改变业务状态） |
| 连续写回调 arg+16 = 0 | ❌ 不放行，且循环卡住 |
| 补 `dmp_allocator_lib_init` / interpose `dmp_allocator_mmap_init` | ❌ 无关（已证伪） |
| 全局 interpose `pthread_mutex_lock` | ❌ 早期启动即失败（EN=0），只能在构造/预解析后小心使用 |
| 全局无锁 `pthread_cond_broadcast` | ❌ 丢唤醒 + 破坏固件锁协议 |

## 9. 下一步（按优先级，精确到地址）

1. **确认对象身份（最高优先）**
   在 `0x05E82000+0x7D7E06`（读 `[r7,#20]` 之前）用 GDB 抓当次 `r7`，
   与回调 `arg`（`0x09458C68`）比对：
   * 相等 ⇒ 结果字段确实是 `arg+16`，那问题是"写入时机/被覆盖"→ 在 `cond_wait` 返回后立刻写
   * 不等 ⇒ 结果字段在别处，顺指针找，**不要再猜偏移**
2. **只接管 q2，不动 q**：q 很可能承载渲染路径（全接管后屏幕从纯色退化为噪声 = 副作用证据）
3. **把 GRM2 命中做成硬判据**：在 `GRM_gpermFillrect` interpose 里**无条件**打第一条日志
4. 若以上收敛失败，转向 a7c2 思路：**不依赖 App 自己的帧输出**，
   从拦截到的 GRM 绘制请求重建帧（plan → rows/copy + 边界检查），投递到 `0x20312000`

## 10. 环境速查

```
工程仓库        /root/fw/oss （文档在 docs/，git 远端 github.com/cokeenet/zve10-qemu）
QEMU 源码       /mnt/build/src/qemu-sony  （arm-softmmu/qemu-system-arm）
内核            /mnt/build/src/a7c-kernel/linux/arch/arm/boot/zImage
initrd          /root/fw/handoff_p0/initrd_q4.img
fake_gles.ko    /root/fw/fake_gles/  （make -C /mnt/build/src/a7c-kernel/linux M=$PWD ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- modules）
探针源码        /mnt/build/menuc*.c → libgrmlog-*.so
运行脚本        /mnt/build/run_fast.sh（快）  /mnt/build/run_lowlog.sh（原版）
镜像目录        /mnt/build/ab_v*.img
内核命令行      /root/fw/real_cmdline_lowlog.txt
gdb             gdb-multiarch（libObj base = 0x05E82000）
```

## 11. 相关文档
* `docs/ZVE10_PORT_ROADMAP.md` —— 逐轮详细日志（含每次实验原始输出摘要）
* `docs/HANDOFF_ZVE10_QEMU_UI.md` —— 上一版交接（本文件是其超集）
* `docs/FAST_ITERATION.md` —— 快速迭代细节
* `docs/ZVE10_QEMU_STATE.md` —— 速查
* `docs/artifacts/` —— 截图（注意：`…oldmenu_app_fullscreen_fill.png` 已被证伪，是 0x5A 填充）
