# 真机取证（ZV-E10 root shell，2026-10-27）——对我们 QEMU 路线最关键的几条

> 通过 `tools/payload/cam.py`（agent TCP 7777）**只读**采集，未在相机上写任何文件。

## 1. 最重要：App 的绘制目标就是 `/dev/stream` 的 32MB 映射，偏移正是 `0x20312000`

真机 `im.elf`(pid 157) 的 `/proc/157/maps` 里，按大小排序的前几名：

| 大小 | 权限 | 偏移 | 设备 |
|---:|---|---:|---|
| **32.00 MB** | `rw-s` | **0x20312000** | **/dev/stream** |
| 26.09 MB | `r--p` | 0 | /usr/share/app/image_cmn_43.uxc |
| 19.37 MB | `r-xp` | 0 | /usr/lib/libObj.so |
| 15.56 MB | `r-xp` | 0 | /usr/lib/CautionConfig.so |
| 11.47 MB | `r-xp` | 0 | /usr/lib/viewUnified2.so |
| **8.00 MB** | `rw-s` | 0x3f7ed000 | **/dev/stream** |
| 8.00 MB | `-w-s` | 0x80000000 | /dev/mem |
| **4.00 MB** | `rw-s` | 0x22312000 | **/dev/stream** |
| 4.00 MB | `rw-s` | 0x3f3ec000 | /dev/mem |

**⇒ 我们 streamfb 镜像 `0x20312000` 的选择是对的**，而且真机上这块有 **32MB**（不是 1.9MB），
后面紧跟 `0x22312000` 处还有 4MB 的第二个 stream 窗口。QEMU 侧实测 App 也做了同样的映射：

```
QMMAP len=0x2000000 off=0x20312000 prot=0x3 flags=0x1 fd=6 name=stream   (两次，双缓冲?)
```

⇒ 只要 App 开始画，像素就落在我们已经在镜像的地址上；**模型区至少要覆盖 0x20312000 + 32MB**。

## 2. 模块依赖图（真机 `/sys/module/*/holders`、`/proc/modules`）

```
liro  ──uses──> dmm ──uses──> grm_gles          (dmm 与 liro 都是 grm_gles 的 holder)
dmm   ──uses──> grm_gles
grm_ma: refcnt=0, holders 空  (UDIF 驱动：init 只做 udif_driver_register，不靠符号引用)
osal_uipc: 被 38 个模块依赖
```

* `grm_ma` 是 **UDIF 驱动**（`init_module` = `udif_driver_register`），所以 refcnt 0 不代表没被用；
  它导出 `ma_create_mspace_wrapper` / `ma_dmm_open_impl` ⇒ **MA 的 mspace 内存来自 dmm**。
* `dmm` 参数：`DmmConfig=/setting/DmmConfig.bin`（78120 B）——**我们镜像里的一致 ✓**。
* 真机 `/setting` 比我们多：`Backup.bin`(1235836，我们有)、`psd.bin`(4820992)、`facedmy.bin`、
  `facerec0.bin`、`mode/`、`appfw_infra/`、`updater/`、`gps/`。**我们镜像只有一个 DmmConfig + ulogio**。

## 3. 设备节点（真机 `/dev` 共 346 项）

有：`fb0`、`fb1`、`dmpgles2`(char 248:29)、`uipc`、`mem`、`kmem`、`backup`、`dmac`、`dma330`、`ldec`、`dnflasha0..31`、`hdmi*`
**没有**：`/dev/ma`、`/dev/grm_ma`、`/dev/dmm`、`/dev/liro`、`/dev/vdf`（都是内核内部/UDIF）。

* `/dev/dmpgles2` 被 im.elf 映射为 **128KB**：`0b833000-0b853000 rw-s off=0`
  ⇒ 我们 `fake_gles` 的窗口大小（256KB 上限、实际映射按用户请求 128KB）**与真机一致**。

## 4. 其它真机事实

* App 启动方式：`/usr/bin/im.elf debug s g r u l i a lazy-global`（**没有 wrapper、没有 preload**）。
* 内核 cmdline（内存预留）与我们 `real_cmdline_lowlog.txt` **逐项一致**（含 `memrsv=680M@0x15800000:uc`、
  `mem=256M@0x80000000@1`、`memrsv=256M@0x80000000:uc`、8 段 `wbimem`）。
* App 还映射了 25 个 `/dev/mem` 窗口，常被多个线程轮询的是 **`0x1196b000`**（1 页）；
  另有 `0x20098000`(500KB)、`0x20028000`、`0x20145000`、`0x3e753000`、`0x3f3ec000`(4MB)、`0x80000000`(8MB, 只写)。
  读 `0x80000000` 前 16KB = 高熵随机数据（未初始化 scratch，不是图像）。
* 真机 kernel `3.0.27_nl-rt106+ #1`（我们是重建的 `#142`）。

## 5. 对当前卡点的直接含义

我们的崩溃是内核 `grm_ma::mspace_memalign`（调用者：`osal_uipc.ko`），mspace 内容是 `0x5A5A5A5A`。
真机依赖图说明 **MA 的池由 `dmm` 提供，而 `dmm` 依赖 `grm_gles`**。
我们在 QEMU 里用 `fake_gles` 顶替了 `grm_gles`（不导出内核接口），
`dmm` 拿不到真正的显存池 ⇒ MA 的 mspace 是垃圾 ⇒ uipc 大块分配时炸。

⇒ 两条候选路线（下一轮实验）：
1. **真 `grm_gles` + 只把 `sugilite_open` 打成 `return 0`**（避免碰硬件寄存器），让 dmm 拿到依赖；
2. 或者干脆**绕开 MA**：把 uipc 大块分配那条路在用户态接管（仍需先坐实是哪个调用触发）。

## 6. 还想从真机拿的东西（都只读）

* `/setting/psd.bin`、`/setting/appfw_infra/`、`/setting/mode/` 的文件清单（可能影响 App 初始化分支）；
* `dmesg` 里 **module init 阶段**（前 200 行）与 `liro`/`ldec`/`stream` 相关的行（可能有缓冲地址打印）；
* 若安全：用只读 payload 把 `/dev/dmpgles2` 的 128KB 窗口整块 dump 出来（对比我们 fake_gles 的初始化内容）。

---

## 7. 重要修正（合作方实测 + 我们 QEMU 侧验证）：0x20312000 开头是**描述符/参数区**，不是像素面

### 7.1 合作方在真机上的观测（只读 mmap）

对 `/dev/stream` 偏移 `0x20312000`、长度 32MB 做**只读** mmap，菜单打开并停住时采样 10 次：

* 内容 **完全不变**（非零 153/8192 = 1.87%）；
* 前 256 字节里是大量 **自指指针**：`30203180 → 0x80312030`、`38203180 → 0x80312038` …（= `0x20312xxx + 0x60000000` 内核 VA 偏移），
  外加 `0xFFFFFFFF`/`0xFFFFE86B` sentinel 和成对重复指针 ⇒ **典型描述符数组/命令表**。

⇒ 这块 **至少开头不是像素**，App 往这里写的是"命令/描述符"。

### 7.2 我们 QEMU 侧的独立验证（`tb_dumpstream.sh`，monitor `xp` 读物理内存）

同一镜像（real grm_ma + fake_gles）跑到 ~20s 时：

```
0x20312000 : 5a5a5a5a × 32 words      ← 还是 VDF init 填充，App 一个字节都没写
0x22312000 : 00000000 × 32 words
0x3f7ed000 : 00000000 × 32 words
```

⇒ **我们的 App 根本没走到写这块缓冲**（卡在更早的 MA/init），而不是"写了但画面不对"。

### 7.3 结论收窄（避免又一次假里程碑）

| 说法 | 现在的判断 |
|---|---|
| `0x20312000` 是像素缓冲 | ❌ 修正为：至少开头是**描述符/参数区** |
| App 开始画 ⇒ 像素落在 0x20312000 | ⚠️ 未证实；真机在这块写的是描述符 |
| 我们镜像这块就能看到画面 | ⚠️ 不成立——要看到像素得先知道像素面在哪个窗口 |
| 我们 QEMU 里 0x5A5A | ✅ 仍是"App 未写"的证据（真机这块内容已非 0x5A，我们的还是） |

### 7.4 待确认：真正的像素面在哪

真机上 App 还有这些 `/dev/stream`、`/dev/mem` 窗口（我们已在 QEMU 侧确认 App 也映射同样的 stream 窗口）：

| 来源 | 偏移 | 大小 |
|---|---:|---:|
| /dev/stream | 0x22312000 | 4 MB |
| /dev/stream | 0x3f7ed000 | 8 MB |
| /dev/mem | 0x3f3ec000 | 4 MB |
| /dev/mem | 0x80000000 | 8 MB（App 只写） |

判据（已请合作方在真机执行）：菜单按 DOWN 后采样——
**某字段递增 ⇒ 描述符/命令环；出现 640×480×2 级别的整块变化 ⇒ 像素面**。

### 7.5 对我们路线的影响

1. **判据不变**：`GRM2 > 0`（App 真调绘制 API）仍是第一道硬判据；之后还要加一条
   "Stream 窗口内容从 `0x5A5A` 变成非均匀"；
2. **建模目标可能要挪**：如果像素面在 `0x22312000`，streamfb 应改镜像那里（或同时镜像两块）；
3. **当前阻塞点不变**：仍是内核 `grm_ma::mspace_memalign`（MA 池来自 dmm，dmm 依赖 grm_gles）。

---

## 8. 决定性发现：App 的图形请求走 UIPC 到 `0x014C0424`（grm_ma 服务）

用 preload hook `osal_snd_sync_msg` 记录**大消息**（≥512B）后，抓到 App 的真实服务调用面：

```
SV qid=0x014c0424  w0=0x0092000a w1=0x6864fb20 w2=0x20312000
SV qid=0x014c0424  w0=0x00920009 w1=0x6864faa0 w2=0x00258000
SV qid=0x014c0424  w0=0x00920009 w1=0x6864faa0 w2=0x00002000
SV qid=0x014c0424  w0=0x00920009 w1=0x6864faa0 w2=0x00001000
SV qid=0x014c0424  w0=0x00920009 w1=0x6864faa0 w2=0x00000200
SV qid=0x00924127  w0=0x00920111 w1=0x6864faa0 w2=0x00000200
SV qid=0x0094016a / 0x012182ed / 0x00ca8192 / 0x00c881bb …
```

* `0x014C0424` 正是我们旧文档里记的 **"grm_ma 服务"**（当年 QSYNC 统计 69 次 ret0）。
* 消息里 `w2 = 0x20312000`（stream 基址）、`0x258000 / 0x2000 / 0x1000 / 0x200`（大小）——
  这就是 **App 通过 UIPC 向内核 grm_ma 申请图形内存**的证据。
* 我们 QEMU 里因此走到 `grm_ma::mspace_memalign` 并崩溃（池是 `0x5A5A` 垃圾）。

### 8.1 这与"真机 grm_ma refcnt=0"并不矛盾

`grm_ma` 的 `init_module` 只做 `udif_driver_register`；**UDIF 是按名字查表调用，不 module_get**，
所以 refcnt 永远是 0（真机 `liro`/`upm` 的 refcnt 也都是 0）。**不能用 refcnt 判断"没走 MA"。**

### 8.2 修正后的路线（a7c2 式：服务在用户态实现，而不是修内核池）

```
App ──UIPC sync──> 0x014C0424 (grm_ma 服务) ──> 内核 mspace_memalign（QEMU 里坏）
```

⇒ 有两条路：
1. **让内核侧 MA 有合法池**（要 dmm/grm_gles 全真工作；`sugilite_open` 因 `inode->i_cdev==0` 崩，
   我们试过把它打成 `return 0`，结果触发 RT `__migrate_enable` 的 BUG ⇒ 这条路不是打一枪能解决的）；
2. **在 preload 里实现 `0x014C0424` 服务**（完全照 `0x003E014D` 备份服务的做法），
   用普通 `mmap`（`/dev/mem` 固定池，如 stream 窗口）应答分配请求 ⇒ **App 拿到"图形内存"，
   内核 MA 根本不会被调用**。

**⇒ 下一步就是做 2**：先反汇编 `grm_ma.ko`（19KB，带符号）搞清 UDIF 请求/应答格式，
再在 preload 里实现该 qid 的服务端。

### 8.3 真机取证的其它结论（合作方，方法论已修正）

按 DOWN 期间 100 轮采样（100ms）：

| 面 | 结论 |
|---|---|
| `stream@0x20312000`(32MB) | **唯一变化的面**，尾指纹恒定 ⇒ **活跃命令/描述符环** |
| `stream@0x22312000`(4MB) | 静态（4 色） |
| `stream@0x3f7ed000`(8MB) | wbi_cmpr **压缩流**（RGB565 解码呈重复竖带） |
| `mem@0x3f3ec000`(4MB) | 0.4% 非零，近乎空 |
| `mem@0x80000000`(8MB, wo) | 噪声 |

⇒ **CPU 侧没有可用的像素面**：菜单像素由 RTOS/硬件解描述符后产生。
我们 QEMU 里 `0x20312000` 仍是全 `0x5A5A` ⇒ **App 连描述符都没写过**，卡在 MA 服务。

### 8.4 `grm_ma.ko` 服务端接口（已从 .ko 符号表整理，带偏移，供下一轮实现用）

`grm_ma.ko`（19,628 B，**符号完整**）里的关键实现函数：

| 偏移 | 名字 | 推测职责 |
|---:|---|---|
| 0x005c | `ma_init` | 模块初始化 |
| 0x00d0 | `ma_user_sync_cb` | **UDIF 用户请求同步回调（服务入口）** |
| 0x01fc | `ma_dmm_open_send_recv` | 向 dmm 发请求并收应答 |
| 0x0348 | `ma_dmm_open_impl` | 打开 dmm 连接（拿池） |
| 0x03b8 | `ma_dmm_close_send_recv` | 关闭 dmm |
| 0x0588 | `ma_cache_ctl` | cache 维护（flush/invalidate） |
| 0x0680 | `ma_create_msp_impl` | 创建 memspace |
| 0x0704 | `ma_findbyname_impl` / 0x0750 `ma_findbyphys_impl` | 按名/按物理地址查 |
| 0x07bc | `ma_mspace_memalign_wrapper` | **memalign（我们崩在这里的上一层）** |
| 0x07ec | `ma_malloc_impl` / 0x08e0 `ma_free_impl` | 分配/释放 |
| 0x097c | `ma_create_mspace_wrapper` / 0x09c8 `ma_destroy_mspace_wrapper` | mspace 生命周期 |
| 0x0ab4 | `ma_compact_init_impl` / 0x0bc0 `ma_compact_impl` | 内存整理 |

配合 App 侧的请求（`w0=0x00920009` / `0x0092000a` 这类"服务类+opcode"），
下一轮要做的是：**对 `ma_user_sync_cb` 的 opcode 分派表和 `ma_malloc_impl` 的返回格式做 RE**，
然后在 preload 里实现 `0x014C0424` 服务端（分配用 `/dev/mem` 固定池，回复真实句柄/物理地址）。

---

## 9. 第 22 轮：在 preload 里实现 grm_ma 服务（`0x014C0424`）——**部分成功，协议还差一层**

### 9.1 从 `grm_ma.ko::ma_user_sync_cb` 反汇编出的分派表（已确认）

`u16 *(u32*)req` = opcode（高 16 位 `0x0092` 是服务标签），opcode 1..13：

| op | 处理 | 入参 | 出参（**在同一个消息缓冲里**） |
|---:|---|---|---|
| 1 | `ma_dmm_open_impl` | req+4 | req+0x1C |
| 2 | `ma_dmm_close_impl` | req+4 | req+8 |
| 3 | `ma_create_msp_impl` | req+4{name,size,flags} | req+0x20 |
| 4 | （默认/空） | — | — |
| 5 | `ma_cache_flush_impl` | req+8, out req+4 | |
| 6 | `ma_cache_invalidate_impl` | req+8, out req+4 | |
| 7 | `ma_findbyname_impl` | req+4 | req+0x14 |
| 8 | `ma_findbyphys_impl` | req+4 | req+8 |
| **9** | **`ma_malloc_impl`** | req+4{msp,size,align} | **req+0x10{status,ptr}** |
| 10 | `ma_free_impl` | req+4{msp,ptr} | req+0x10 |
| 11 | `ma_compact_init_impl` | req+4 | req+8 |
| 12 | `ma_compact_impl` | req+4 | req+0x14 |
| 13 | `ma_compact_fin_impl` | req+4 | req+8 |

`ma_malloc_impl` 内部：`msp=[r5]`，`size=[r5+4]`，`align=[r5+8]`；成功时 `out[0]=0, out[4]=ptr`，
失败 `out[0]=-1`；找不到 msp ⇒ `-1018`。

### 9.2 尝试：在 preload 里应答（`menuc1170/1171`，镜像 `ab_v1730/ab_v1740`）

* 首次尝试（**回错字段**：op7 的 out 应在 req+0x14，我写到了 req+4）⇒ App 只发出 **1 条** 请求就不再动。
* 修正字段后：**MA 请求量 12,788 条**，但内容出现大量 `0xf7f7f7f7` 填充与荒谬尺寸
  （`size=0x06e56323`），我们的回复让 App 进入**重试风暴**（run 直接跑到 80s 超时）。

⇒ 说明"只把 op 分派和 status 填 0"不够：**App 对 findbyname / dmm_open 的返回值有具体期待**
（很可能是 msp 的真实句柄/指针，后续 malloc 会带回来做比较），我们必须先 RE
**用户态客户端**（libObj 里的 `ma_*` 封装）才能实现正确应答。

### 9.3 本轮得到的另一个硬结论

`op=7 findbyname` 的入参就是字符串 `"gcl_common..."`（w1..w4 连起来是 ASCII），
说明 **App 用名字查 mspace**；这给了下一轮 RE 的抓手：
在 libObj 里搜 `"gcl_common"` 就能找到客户端代码。
