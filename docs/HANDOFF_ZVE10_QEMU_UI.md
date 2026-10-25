# ZV-E10 老菜单 App UI in QEMU —— 交接文档

> 生成时间：2026-10-26 01:00（Asia/Shanghai）
> 目标：让 ZV-E10 原厂 `/usr` 的 App 菜单在 QEMU 里**真正显示**（不是我们伪造的画面）
> 状态：**未达成**。显示链路与工具链已就绪，App 初始化链未走通。

---

## 0. 一句话现状

**显示链路通、绘制 shim 就绪、backup/日志服务通；App 卡在 `initializeConfig()` 不返回**，
根因收敛到：`grm` 请求队列的"完成通知"在 App 的 worker 路径上从未发出，
而请求方在下游还要检查一个结果字段。**App 至今没有调用过任何 GRM 绘制接口。**

---

## 1. 绝对不能再犯的判据错误（最重要，先看这条）

```
屏幕上出现颜色 / 整屏纯色  ⇏  进展
```

* QEMU 模型 `hw/display/bionz_streamfb.c:180-184` 自己写着：
  *stream buffer is memset() to 0x5a by the VDF init code and stays like that
  until the App actually renders, so a uniform buffer means "no frame yet"*
* `(90,73,214)` 反推 RGB565 = `0x5A5A`，**零误差** ⇒ 就是 VDF init 的填充值。
* 模型里有现成函数 `streamfb_buffer_uniform()`，**"整屏同色" 是它判定"还没渲染"的条件**。

**唯一可信的进展判据：**

```
GRM2 日志 > 0（App 真的调用了 GRM 绘制接口）
且我们的 shim 至少成功执行了一次 Fill / Rectblit
```

---

## 2. 已确认的底座（这些可以放心用）

| 项 | 状态 | 位置/证据 |
|---|---|---|
| QEMU 机型 | ✅ | `hw/arm/bionz.c`（machine `cxd90045`） |
| 显示模型 | ✅ | `hw/display/bionz_streamfb.c`，镜像物理地址 `0x20312000`，RGB565 |
| 正确几何参数 | ✅ | `width=640 height=480 stride=1280 format=1 blank-pattern=off` |
| 满屏彩条自测 | ✅ | `ab_v651`（证明"能显示"） |
| 绘制 shim | ✅ | `libgrm2.so`：`GRM_bitmapCreate` → `[bmp+0x08]`=VDF 缓冲；CPU 版 `gpermFillrect/Rectblit` |
| backup 真实数据 | ✅ | 内核 stub 解析 BK4（页表 @0x10C、槽表 @0x143E、数据区 @0x31E40）；**但实验证明删掉注册 boot 表现不变 ⇒ 非必需** |
| 日志控制队列 | ✅ | `0x8F0139` 由用户态应答（`ULOGIOREG rc=0`） |
| 内核探针 | ✅ | `fake_gles.ko`：MAPD（vma）/THR（pc,sp）/STK（用户栈 dump）等 |
| 用户态探针 | ✅ | `/lib/libgrmlog.so`（LD_PRELOAD）：锁/cond/sem/ioctl/GETS/SETS/线程创建 |
| 镜像 | ✅ | `n7`=rootfs（offset 168MB, 8MB）；`n15`=/usr（offset 732MB, 300MB） |

---

## 3. 卡点：`initializeConfig()` 永不返回

### 3.1 调用链
```
initializeConfig (libObj+0x14BE84)
  └ new(24) + ctor(libObj+0x14BD58)
      ├ load libObj.initializeModelConfig        → config+0x08
      ├ load viewUnified2.initializeViewConfig   → config+0x04
      ├ (fallback) load libObj.<...>             → 失败则 exit(0)
      └ load libObj.initializeModelTree          → config+0x0C
```
* **config 对象 24 字节**：`+0x00` vtable / `+0x04` viewConfig / `+0x08` modelConfig / `+0x0C` modelTree / `+0x14` byte。
* 只有 4 次 load 全成功才返回；任一失败 → 清理后 **`exit(0)`**（这就是"隐藏 viewUnified2.so 后 EX=0"的原因，不是新故障）。

### 3.2 卡在 grm 请求队列的完成通知
```
grm_init → libObj+0x7D7BB0 → libObj+0x7D7D30（★此函数负责"按需创建两个队列+worker"，不是纯等待）
  0x7D7D30 内：入队(0x7D7840) → signal(q2+0x20) → cond_wait(q2+0x68, q2+0x50)   ← 卡在这
  醒来后：ldr r3,[r7,#20] ; cmp r3,#0 ; bne 0x7D7E74(重试) ; 否则置完成标志返回
```
**队列对象（地址跨运行稳定）**
```
q  = 0x072E5CD0      q2 = 0x072E5C28
布局： +0x04 队列mutex  +0x20 队列cond  +0x50 完成mutex  +0x68 完成cond
       +0x98 {count, head} 链表     +0xA4 回调指针
worker 入口 = libObj+0x7D7C11（由 libObj+0x7D7780 里 pthread_create 创建，构造时传 cb）
请求方等的是 q2+0x68 = 0x072E5C90
```
**libc 层面统计（决定性证据）**
```
整轮只有 7 次 pthread_cond_signal；目标：0x5DF78×2 / 0x5E7D8×2 / 0x86AD0 / 0x86C88 / 0x72E5C48
★ 0x072E5C90 从未出现在 signal 里 ⇒ 完成通知从未发出
```
**已排除**
```
· 不是 osal 信号量：所有 tmo=-1 的 osal_wai_sem_tmo 都有返回（S=81 / E=81）
· 不是 MWF 事件缺失：libMWF 内部确实会 SetSignal（lr 落在 libMWF 内）；flag(+0x3C) 能读到 1
· 0x7D7D30 不能补成 return 0（那样两个队列根本不会被创建）
```

---

## 4. 已经做到哪一步（可复现）

### 4.1 接管 worker（**已成功**，镜像 `ab_v1531`）
```
取项（count--, head=head->next）→ 在【独立线程】里调用原回调（避免阻塞 worker）
→ 等 300ms → trylock(q+0x50) → signal(q+0x68) → unlock → free(item-8)
实测日志：
  W5 q=0x072e5cd0 / W5 q=0x072e5c28
  W5D t=0x00008001          （取到同步请求）
  CBX enter / CBX exit      （回调正常返回）
  W5S q68=0x072e5c90        ★ 精确命中 q2 的完成 cond
```
⇒ **"发不出通知"这一层已解决。**

### 4.2 还没解决的最后一环：结果字段
```
回调参数 arg = 0x09458C68（主线程栈上）  arg+16 原值 = 1
请求方醒来检查 [r7+20]，=0 才算成功；非 0 → 重试(re-enqueue)
实验：把 arg+16 写 0（一次 / 连续 200 次）都**没有**让请求方放行
     （连续写那版连循环都没走完，怀疑 arg 指向的栈帧已失效）
```
**⇒ 说明 `arg+16` 很可能不是请求方真正读的那个字段，或对象身份不对。**
**下一步必须先确认对象身份：在 `0x7D7E06`（读之前）抓当次 `r7`，与回调 `arg` 比对。**

---

## 5. 已验证无效/危险的路线（别再走）

| 做法 | 结果 |
|---|---|
| 把 `0x7D7D30` 补成 `return 0` | ❌ 跳过队列创建，无法接管 |
| 把 `0x14BD58` 整段 `bx lr` | ⚠️ `initializeConfig` 返回了(EX=1)，但配置为空 → 崩在 `0x50` |
| 只 nop ctor 里 4 次 load（`0x14BD90/A8/C0/DA`） | ⚠️ 同上，返回后崩在 `0x06CCF3D7` |
| 隐藏 `viewUnified2.so` | ❌ 走 fallback → 必失败 → `exit(0)` |
| 强制 `MWF::ObjSignal::GetSignal` 返回 0 | ❌ 醒来查状态又回去等 |
| 看门狗定时 `SetSignal(obj)` | ❌ 同上（信号是消费型，且不改变业务状态） |
| 连续写回回调 arg 的 +16 为 0 | ❌ 不放行，且循环卡住 |
| 二进制补丁 `dmp_allocator_lib_init` / interpose `dmp_allocator_mmap_init` | ❌ 无关（已证伪） |

---

## 6. 环境与复现步骤

### 6.1 运行（务必用 systemd scope，避免把内存记到 dsh-web 上）
```bash
systemd-run --scope --collect \
  -p MemoryMax=3G -p MemorySwapMax=1G -p KillMode=control-group \
  --unit=qemu-zve10-$(date +%s) \
  -- bash -lc 'cd /root/fw/oss && \
     EXTRA="-global driver=bionz-streamfb,property=blank-pattern,value=off \
            -global driver=bionz-streamfb,property=width,value=640 \
            -global driver=bionz-streamfb,property=height,value=480 \
            -global driver=bionz-streamfb,property=stride,value=1280 \
            -global driver=bionz-streamfb,property=format,value=1" \
     INITRD=/root/fw/handoff_p0/initrd_q4.img OUT=/mnt/build/tmp/xx \
     timeout 320 /mnt/build/run_lowlog.sh /mnt/build/<IMG> 120'
pkill -x qemu-system-arm     # 结束时务必清理
```
输出：`/mnt/build/tmp/xx/{serial.log, screen.png}`

### 6.2 关键镜像
| 镜像 | 说明 |
|---|---|
| `ab_v1132` | **干净基线**（无二进制补丁），排障从这里开始 |
| `ab_v1290` | 打了 `0x7D7E0A`（重试分支→nop）的版本 |
| `ab_v1380` | ctor 整段 `bx lr`（会崩，仅供对照） |
| `ab_v1400` | 只 nop 4 次 load（会崩，仅供对照） |
| `ab_v1531` | **worker 接管 + 回调在独立线程 + 精确 signal（当前最完整）** |
| `ab_v1550/1560` | 结果字段写入实验 |

### 6.3 探针怎么挂
```
/bin/im.elf 是 shell wrapper：
  #!/bin/sh
  LD_PRELOAD="/lib/libgrmlog.so"  exec /usr/bin/im.elf.real "$@"
换成自己的 .so 即可（探针源码在 /mnt/build/menuc*.c，编译用 toolchain armv7-eabihf）
```

### 6.4 镜像怎么改
```bash
# n7 = rootfs（8MB @ 168MB）；n15 = /usr（300MB @ 732MB）
dd if=IMG of=slice.img bs=1M skip=732 count=300 status=none
debugfs -w -R 'rm /lib/libgrmlog.so' slice.img
debugfs -w -R 'write /mnt/build/libgrmlog-xxx.so /lib/libgrmlog.so' slice.img
dd if=slice.img of=IMG bs=1M seek=732 conv=notrunc status=none
```

---

## 7. 下一步建议（按优先级）

1. **先确认对象身份**：在 `libObj+0x7D7E06`（读 `[r7,#20]` 之前）抓当次 `r7`，
   与回调 `arg`（`0x09458C68`）比对；若不等，说明结果字段在别处，需顺着指针找，**不要再猜偏移**。
2. **只接管 q2，不动 q**：q 很可能承载渲染路径；全接管后屏幕从纯色退化为噪声就是副作用证据。
3. **把 GRM2 命中做成硬判据**：在 `GRM_gpermFillrect` 的 interpose 里**无条件**打第一条日志，
   把它当作"App 开始绘制"的唯一标志。
4. 若上述收敛失败，再考虑 a7c2 的另一条路：**不依赖 App 自己的帧输出**，
   而是从拦截到的 GRM 绘制请求重建帧（plan → rows/copy，含边界检查），
   投递到 `0x20312000`。此时"画面"是我们软件执行 App 绘制指令的结果，而不是 App 原生帧。

---

## 8. 方法论教训（都踩过）

```
① "没观察到" ≠ "没发生"：LD_PRELOAD 只看得到跨模块 PLT 调用，
   同模块内部直接 bl 看不见（MWF 的 handler 就是内部创建的）。
   要换观察层：hook libc（cond/sem 是最终出口），或看效果而不是看调用。
② 线程 id 会漂移（187 → 176/189），跨轮结论必须用 comm+栈重新锚定。
③ 判据要选"不可伪造"的：屏幕颜色可被 memset 伪造，GRM 调用命中不能。
④ 顺序：先证伪自己的里程碑，再往上叠结论。
```

---

## 9. 相关文档
* `docs/ZVE10_PORT_ROADMAP.md` —— 逐轮详细日志（含每次实验的原始输出摘要）
* `docs/ZVE10_QEMU_STATE.md` —— 速查
* `docs/HANDOFF_2026-10-25.md` —— 更早一版交接（部分结论已被本文件修正）
* `docs/artifacts/` —— 截图等产物（注意：`2026-10-25_oldmenu_app_fullscreen_fill.png` 已被证伪，是 0x5A 填充）
