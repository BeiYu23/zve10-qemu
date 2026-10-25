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

---

## 10. 第 23 轮：`grm_ma` 服务应答语义仍未对上（但拿到关键线索）

### 10.1 `ma_findbyname_impl` / `ma_dmm_open_impl` 的**出参布局**（反汇编确认）

`ma_findbyname_impl(req, out)`（0x704），出参从 `req+0x14` 起：

```
成功： out[0] = entry->[8]   out[4] = entry 指针   out[8] = entry 指针   out[12] = entry 指针
失败： out[0] = 0            out[4] = -8(-0x8)     …
```

`ma_dmm_open_impl(req, out)`（0x348），出参从 `req+0x1C` 起：成功时 `out[0]=0`、`out[0xC]=新 entry`。

### 10.2 修正后的应答实验（`menuc1180`，镜像 `ab_v1750`）

* **好消息**：App 接受了我们给的 mspace 句柄——后续 `op9 malloc` 里 `msp=0x68640000`
  **正是我们返回的值**（说明握手字段位置对了）；
* **坏消息**：紧接着 malloc 的 `size` 变成 `0x06e56323`（明显是**指针字节**）、`align` 同值，
  我们按 size 分配失败 → App 重试 **10,075 次**（跑到 70s 超时）。

⇒ 说明 `findbyname` 的返回里**还有一两个字段**（很可能是 msp 的 size/align 描述）被 App 当作
结构体读走了；我们填的是猜测值，于是 App 把指针当 size 用。

### 10.3 名字串的真相（排除一个方向）

`op7` 入参的字符串按字节解是 **`gl_common`（很可能完整是 `gcl_common`）**，
但我们在 `libObj.so / libmpr.so / libMWF.so / libgrm.so` 里**搜不到 `gcl_` 这个串**（连 `common` 都只有少量命中）。

⇒ 这个名字**不是静态字面量**，是运行期拼出来的（或来自 `viewUnified2/CautionConfig` 之类的大配置库）。
**所以"搜字符串找客户端"这条路走不通**，下一轮改用"按 opcode 常量在 .text 里找客户端封装"。

### 10.4 下一轮的更优切入点

`ma_malloc_impl` 的入参是 `{msp, size, align}`（req+4 起），出参 `{status, ptr}`（req+0x10 起）——
这部分我们已经对了。真正没对的是 `findbyname/dmm_open` 返回的 **entry 结构**。
既然 `entry->[8]` 被返回给用户态，下一步应当在
`ma_list_findbyname` / `ma_list_add`（0x10A4 / 0xE54）里看清 **entry 结构的完整字段**，
然后在 preload 里返回**自洽的完整 entry**（而不是只填几个字段）。

---

## 11. 第 24 轮：`grm_ma` 客户端的返回值语义 = **必须是本进程可解引用的用户态地址**

### 11.1 客户端定位（用了 qid 常量做锚点）

`0x014C0424` 这个 qid 在 `libObj.so` 里**只出现一次**（文件偏移 `0x7680D0`，字面池），
据此定位到 **用户态 MA 客户端就在 `libObj` 的 `0x7680xx–0x7689xx` 一带**：

```
768098: ldr r1,[pc,#48]      ; r1 = 0x014C0424  (qid)
7680a6: bl  768054           ; 构造 44 字节请求 (osal_valloc_msg_wait(...,0x2c,1))
7680ca: strh r6,[r3,#0]      ; ★ 把 opcode(u16) 写进 msg[0]
...
-- 发送/取回复 (在 0x768208 里) --
76859a: ldr r3,[r4,#4]       ; msg
76859c: ldr r0,[r3,#16]      ; ★ 客户端读回复的 msg+0x10
```

⇒ 与内核侧完全对上：`msg+0x10 = status`、`msg+0x14 = 返回值(指针)`。

### 11.2 两次崩溃证明"返回值必须是用户态 VA"

| 我们回的 `msg+0x14` | 结果 |
|---|---|
| `0x20312000`（物理地址/池基址） | App `memset(0x20312000)` ⇒ **trap `address: 20312000`**（该地址在进程里没映射） |
| mmap 1MB 后的 VA `0x09af2000` | App 仍 `memset` 到 `0x09bf2000`（= VA+0x100000）⇒ **又一次 fault**，说明它一次写 ≥1MB |
| **mmap 整个 32MB stream 窗口**后返回该 VA | **不再崩溃**（本轮 `ab_v1800`），但 App 随后**静默停住**（无异常、无后续请求） |

⇒ 结论：**MA 返回的必须是"这个进程里能直接写的 VA"**（真机上是内核把它映射进调用进程的）。
我们在 preload 里用 `mmap(/dev/stream, 0x2000000, off=0)` 自造这个 VA，方向是对的。

### 11.3 本轮的方法学收获

* `qid 常量在 .lib 里唯一出现` ⇒ 是定位客户端最有效的锚点（比 `__builtin_return_address` 可靠得多；
  后者被 PLT/尾调用干扰，落在无关的小 getter 上）；
* 我们先前"把返回当物理地址"的假设被两次 fault 否证——**不要用地址"看起来合理"来当证据**。

### 11.4 下一轮入口

App 拿到有效 VA 后**静默停住**（无异常、无新请求）。下一步用 GDB 看它此刻停在哪个调用
（大概率是在等我们"另一个"字段/另一个 opcode 的回复），或者在 `0x768xxx` 客户端里
逐函数读清 `dmm_open`(op1) 返回结构该填什么。

---

## 12. 第 25 轮：MA 应答后 App 不再"卡在同步发送"，而是**在 libc 里空等**

### 12.1 全量同步发送打点（`menuc1240`，镜像 `ab_v1810`）

给**每一条** `osal_snd_sync_msg` 都加 `SS/SR`（进入/返回）日志后复跑：

```
… SS/SR qid=0x008f013a ret=-1020（日志服务，正常）
MA op=7 findbyname
MA op=1 dmm_open
MA op=9 malloc  → MAMAP32 va=0x09af2000   ← 我们回了合法 VA
（此后：没有任何新的 SS，也没有 Exception）
```

⇒ **App 不是卡在某个同步发送上**（否则会看到只有 SS 没有 SR 的那一条）。

### 12.2 进程内线程普查（`menuc1250-1262`，自建 in-guest census）

在 preload 里起一个线程，12s/24s 各读一次 `/proc/self/task/*/{comm,stat}`：

```
CEN T12
  tid=123 st=R sp=0x5efff4c0 pc=0x04284534   ← 主线程，pc 在 libc（0x0424f000+ 区间）
  tid=128 st=R sp=0x04921b74 pc=0x04021344   （uipc_proxy，正常）
CEN T24   （完全相同，两个时刻都不变）
```

**两条硬结论**：
1. **主线程停在 libc**（`pc` 落在 libc 映射内）⇒ 它在 futex/poll 一类原语里等，不是在我们 hook 里；
2. **此刻进程只有 2 个线程**——正常 App 在后期有 40+ 线程，说明它**很早就停了**，
   也就是"打通 MA 之后仍然走不下去"的下一个原因在**更早的初始化**里，不是线程太多卡住。

### 12.3 工具状态

新增可复用的 in-guest 诊断：线程普查（comm/state/sp/pc）+ 栈扫描（找 libObj 返回地址）。
本轮 maps 解析还有 bug（`libObj_base()` 返回 0，导致栈扫描没输出），下一轮先修它再取主线程调用链。

### 12.4 下一轮

1. 修 `libObj_base()`（打印 maps 读取长度做自检），拿到主线程在 libc 里等**之前**的那条 libObj 调用链；
2. 同时对照 `0x768xxx` 客户端：`op1 dmm_open` 的返回结构我们只填了 status，
   很可能 App 需要它返回的"池/句柄"才能继续 → 用同样的方法（歧义就做实验否证）。

---

## 13. 第 26 轮：MA 回复里**必须带一个用户态"名字指针"**（客户端会 memcpy 它）

### 13.1 客户端对象构造（`libObj 0x7687D0` 一带，反汇编）

```
7687d0: push {r4,r7,lr}; sub sp,#16; r4=r0; r7=sp
7687d6: movs r0,#5 ; r5=r1 ; ldr r1,[r1,#4] ; bl 76898e    ; 发请求
7687e0: ldr r3,[r4,#16]; cmp #1 ; bne 768852               ; 等 reply 状态
7687e6..7687f6: 取 reply 里的三个值
7687fa: ldr r3,[r5,#4]  ; str r3,[r4,#40] ; str r3,[r8]     ; reply+4  → 对象字段
768802: ldr r3,[r5,#12] ; strb r3,[r8,#4]                   ; reply+12 → flags 字节
768808: ldr r1,[r5,#0]                                     ; ★ reply+0 = 名字指针
76880e: memcpy(r8+5,  r1, 16)                              ; ★ 从该指针拷 16 字节名字
768818: memcpy(r4+24, r1, 16)
```

⇒ **`dmm_open / create_msp` 的回复结构里，`+0` 字段是一个"本进程可读的 16 字节名字指针"**，
`+4` 是一个整数（大小/句柄），`+12` 是 flags 字节。**只填 status 是不够的**（我们前两轮就是这么干的）。

### 13.2 按此实现的两版对照（镜像 `ab_v1890` / `ab_v1900`）

| 回复方式 | 结果 |
|---|---|
| 只填 status + 句柄（`ab_v1890`） | App 走到 `op9 malloc`，拿到 VA 后**静默停住**（无异常、无新请求、只有 2 个线程） |
| 补上"名字指针 + size + flags"（`ab_v1900`，名字页用 `/dev/stream` 最后一页映射） | App **只到 op1 就停**了（连 op9 都没发）⇒ 说明 op1 的字段位置/顺序**还不完全对** |

### 13.3 工具现状

* in-guest 线程普查可用（`tid/state/sp/pc`），但 **`libObj` 基址在 preload 里解析失败**
  （`dlsym("initializeConfig")`+`dladdr` 返回 0；读 maps 也因长度/解析问题失败）⇒ 栈扫描暂时没输出；
* 内核 `[THR]/[STK]` dump 只在**长跑**（>60s）出现；短跑（40s idle 就退出）看不到。

### 13.4 下一轮

1. **把 `dmm_open`(op1) 的字段位置定死**：用 `ab_v1890`（能走到 op9 的那版）做基线，
   一次只改一个字段（+0 / +4 / +12）做 A/B，观察 App 是否继续发 op9/op3；
2. 基址解析换一条路：直接在 preload 里读 `/proc/self/maps` 但先把长度打印出来自检（上次失败原因未定）。

---

## 14. 第 27 轮：按 a7c2 的做法补"输入注入"这一层

### 14.1 a7c2 到底怎么做的（对照我们的路线）

他们**不是**只靠生命周期入口就出画面，而是两层配合：

1. **用固件自己的生命周期入口**把菜单 controller 建起来（我们对应 `viewManagerIf::openView(...)`）；
2. **用原 OSAL API 注入按键**（32B App 消息，raw 键码 + `press=1/release=0`，guest 侧 50ms 短按；
   拨盘用有符号增量 `±1024` 而不是 press/release）。
   键码表：MENU `0x1ab`、确认 `0x1ac`、上 `0x3`、下 `0x30`、左 `0x15`、右 `0x1af`、删除/C2 `0x1f8`、右后拨盘 `0x10000013`。

⇒ **"菜单键是否要按下去"——要，这是他们验证过的必经一步。**

### 14.2 ZV-E10 这边的等价物（我们早就反汇编出来了）

```
InputService::createEvent(unsigned long key, uchar press)   libObj+0x11C89A（导出）
InputService::pushFilterEvent(Event*)                       libObj+0x11E5D4（导出）
InputService::callback_panel → snd_InfraInputEvent_to_InputServant → osal_snd_msg(qid=0x14A0429)
_InfraInputEvent = 24B；qid 0x14A0429 只被 libObj 引用 ⇒ InputServant 在本进程内
m_pMyInstance = libObj+0x1459548
```

**并且当年"注入无效"的根因已经消失**：那时 `viewUnified2.so` 根本没被加载 ⇒ `m_pMyInstance == NULL`；
本轮实测 App **已经加载 viewUnified2.so 并跑完它自己的 init**。

### 14.3 本轮实验状态

* 组合探针（`menuc1320` = poke + 备份服务 + MA 服务 + 输入注入 + GRM shim）已编好（镜像 `ab_v1920`）；
* 但注入点被我写在了 worker 的第 30 次迭代（1s/次），本轮 48s 就 idle 退出，**只跑到 `Y9 i=24`**，
  ⇒ **注入还没执行到**（这也解释了日志里没有 `INJ ...`）；
* 日志里那段 `Exception stack` 是**我们内核探针自己的 printk 回溯**（`fake_gles maps_thread`），不是 guest 崩溃。

### 14.4 下一轮（明确的 a7c2 步骤）

1. 把注入点提前到**初始化一完成就执行**（不依赖 worker 迭代计数），并在注入前后各打一次
   `m_pMyInstance`、`GRM2` 计数；
2. 注入序列：`createEvent(0x1ab,1)+push` → 80ms → `createEvent(0x1ab,0)+push`；
3. 若 MENU 无反应，再依次试 `view/HARDKEYMENU_DISP` 等 name-based `openView`（已在 `menuc1310` 里实现，
   只差把它放进正确时机）；
4. 判据：`GRM2 > 0` 且 `/dev/stream@0x20312000` 由均匀 `0x5A5A` 变非均匀。

---

## 15. 第 28 轮：**UI/视图系统从未初始化**（pVM 与 InputService 实例全程为 0）

### 15.1 运行时证据（GDB 写watch + 探针轮询）

在 `ab_v1930`（poke+备份+MA+注入+GRM shim 的组合镜像）上：

* **GDB 对两个全局变量下写 watchpoint**（`tb_watchpv.sh`，地址按当轮 QMMAP 动态解析，本轮 libObj=0x6262000）：
  `libObj+0x149221C`(pViewManager) 与 `libObj+0x1459548`(InputService::m_pMyInstance)
  ⇒ **45 秒内零次写入**（`wp.log` 只有 ARMED）。
* **探针侧轮询**同样从头到尾：
  ```
  IJ wait i=100 inst=0x00000000
  IJ wait i=180 inst=0x00000000
  INITCFG_ENTER tid=189
  PV i=36 pVM=0 inst=0 ida=0
  ```

⇒ **App 的视图管理器与输入服务从未被创建**。这也解释了上一轮"注入无效"：
不是注入方式错，是**注入对象还不存在**。

### 15.2 这与 a7c2 的经验一致

他们的原话是：*"View 打不开 ⇒ `InputService::m_pMyInstance = NULL` ⇒ 注入无效"*，
而他们的解法是**先用原厂生命周期入口把 controller/视图建起来**，再注入按键。
我们现在正处在这一步之前：**连 view 系统本身都没起来**。

### 15.3 已确认可用的"受控入口"素材（下一轮直接用）

| 符号 | 位置 | 说明 |
|---|---|---|
| `ViewGlobalMenuToInstance` | **viewUnified2.so +0x2E4C0F（导出）** | 全局菜单视图构造器（`new(0x16c)` + ctor）|
| `ViewBaseMenuToInstance` | viewUnified2.so +0x2DE0F6 | 基础菜单视图 |
| `ViewSettingMenuToInstance` | viewUnified2.so +0x2F36F4 | 设置菜单视图 |
| `viewManagerIf::openView(const char*,int)` | libObj+0x14117C（导出） | 名字式开视图 |
| `InputService::createEvent/pushFilterEvent` | libObj+0x11C89A / 0x11E5D4（导出） | 按键注入 |

**问题**：这些入口都依赖 view 框架已初始化（pVM/pMyInstance），而它们为 0。
⇒ 下一轮要么找到**创建 ViewManager 的那条原厂路径**（谁在正常启动里 new ViewManager？），
要么按 a7c2 的做法**直接把视图对象构造出来**再交给绘制（需要弄清 ctor 的父上下文参数）。

### 15.4 本轮实验记录（避免重复）

* `menuc1330`（注入线程轮询 inst）：inst 恒 0，200 次后放弃；
* `menuc1340`（在 inst==0 的第 40 次迭代尝试 `ViewGlobalMenuToInstance(0)`）：
  该分支日志**未出现**（疑被编译器/时序吞掉），需下一轮改用独立线程+显式日志重试。

---

## 16. 第 29 轮：`openView` 内部其实是"投一个 Event"，但 pVM==0 时调用会崩

### 16.1 `viewManagerIf::openView(const char*,int)` 的真实实现（反汇编）

```
1410a4: ... ldr r6,[got pVM槽] ; ldr r9,[r6]        ; r9 = pViewManager
1410bc: cmp r9,#0 ; beq 141102                       ; 若 pVM==0 跳过使用
1410ca: … 用 r9 调 0x12CED4（按名字取 view 描述）
141102: r0 = r5 (名字) ; bl IdGenerator::Get         ; ★ 名字 → view id
14110c: new(16) ; Event ctor(r1 = 0x11012001, r2=4, r3=0)
14111c: new(20) ; ParamBase(IdGenerator::Get(name)) ; Event::addParameter(6, …)
141134: new(20) ; ParamBase(int 参数)              ; Event::addParameter(0x1A(26), …)
14114c: 取全局对象 → bl 0x8A3B6C                    ; ★ 投递事件（app 自己的事件泵）
```

**要点**：
* `openView("view/XXX", n)` = **构造 Event(0x11012001)**，参数 `6`=view id、`26`=int 参数，然后**投进 App 自己的事件泵**——
  这正是 a7c2 说的"用原厂生命周期/事件入口"，不是手工建 UI；
* 但实现里在 pVM 上取 view 描述（0x1410CA），**pVM==0 时会走空指针路径**。

### 16.2 实验：直接调 openView ⇒ 崩（已实测）

`menuc1360`（符号用 `dlopen("libObj.so")` 句柄解析后调用）：
```
TG handle=0x000222c8
TG ov=0x063ab17d ce=0x0638689b pf=0x063885d5
[Exception] address: 00000010  pc=libObj内  lr≈0x63ab159(回到我们的 trigger)
```
⇒ **`address 0x10` = pVM(NULL)+0x10**，与 16.1 的静态分析完全一致：
**视图管理器没建起来之前，openView 不能调。**

### 16.3 顺带两条重要工程结论

1. **preload 里解析 libObj 符号必须用 `dlopen("libObj.so")` 的句柄**：
   `dlsym(RTLD_DEFAULT, "_ZN13viewManagerIf8openViewEPKci")` 返回 **0**（libObj 是 dlopen 进来的，不在全局作用域）；
2. **对照实验（重要）**：把 `/bin/im.elf` 换回**原厂 23240B 二进制、完全不 preload** 跑，
   App **崩溃得更早**（串口仅 2959 行就进 `Exception>`）⇒ 我们改名成 `im.elf.real` 不是主因，
   **真正卡住它的是缺失的服务应答**，不是名字。

### 16.4 现在的确切位置（一句话）

> App 能跑到 `initializeConfig` 并加载 `viewUnified2/CautionConfig`，但 **ViewManager / InputService 从未被创建**
> （GDB 写 watchpoint 45s 零写入），因此既不能开视图也不能注入按键；
> App 全进程**只创建过 2 个线程**（两个 grm worker），说明它的框架/UI 启动链根本没跑起来。

### 16.5 下一轮

1. 找 **谁创建 ViewManager**：`View::View(ViewManager*)`（libObj+0x8A0014）的调用者，或
   `Application_System_Init` 里 `createRequestModelExecuteEvent` 之后应由谁消费；
2. 先确认 App 的事件泵是否在跑（若没人消费事件，投 Event 也没用）；
3. 若框架确实起不来，就按 a7c2 的"受控入口"：**直接 new 一个 ViewManager + View**（需要弄清 ctor 的父上下文），
   而不是调 openView。

---

## 17. 第 30 轮：App **从不自己调用 `Application_System_Init`**（框架启动链根本没开跑）

### 17.1 实验（干净探针 + 只观测 `Application_System_Init`）

探针 `menuc1400`（`ab_v1990`）= 干净观测 + 备份服务 + MA 服务 + **interpose `Application_System_Init`（只记录参数）**，
**不含任何 Y9/MWF poke**。跑 40s 结果：

```
（没有任何 APPINIT 行）
MA op=7 findbyname  → MA op=1 dmm_open → MA op=9 malloc → MAMAP32 va=0x096e5000
之后静默（无异常）
```

⇒ **App 自己的启动链在调用 `Application_System_Init` 之前就停住了**；
此前能看到 `INITCFG_ENTER`/`viewUnified2` 加载，**全靠我们 Y9 线程手动调它**——那是绕过 App 自身启动的。

### 17.2 排除"启动参数/名字"这条假设（对照实验）

设备上 App 的启动参数是 `/usr/bin/im.elf debug s g r u l i a lazy-global`。
我们把 wrapper 改成**完全照抄这份 argv**（镜像 `ab_v2000`）跑：**行为一模一样**
（无 APPINIT、MA 三步、随后静默）⇒ **argv 不是判别条件**。

### 17.3 现在最大的未知

App 在 `MA malloc` 之后做什么、期待什么——它没有报错也没有继续。
下一步不再猜：用我们已有的**内存 inline hook**（`install_hook5`，之前给 `libObj 0x69CA08` 用过）
去挂 `libObj 0x7684DC / 0x76858E / 0x7687D0` 这几个 **MA 客户端小函数**（已用 qid 常量定位到 0x768xxx 区），
打印它们的入参/返回，**看清 App 拿到我们回复后到底判断了什么**。

### 17.4 备选（若客户端确实要求"内核态语义"）

那就要么：
* 给 libObj 的 MA 客户端喂**与真机一致的结构**（需要真机上把这块内存 dump 下来对照）；
* 要么放弃"让它自然启动"，改为 **a7c2 的受控入口**：由我们构造 ViewManager/View 并驱动，
  同时用 `libgrm2.so` 软件光栅化——这条不依赖 App 自身启动链，但需要弄清 View 构造的上下文参数。

---

## 18. 第 31 轮：发现一个**可能解释一切怪象**的底层问题：我们内核的 `struct inode` 布局与厂商内核不一致

### 18.1 线索来源：真 `grm_gles` 打开 `/dev/dmpgles2` 时崩在 `[inode+0x198] == 0`

早期 oops：

```
PC is at sugilite_open+0x28/0x1f8 [grm_gles]
 434: ldr r4,[r0,#408]     ; r4 = *(u32*)(inode + 0x198)   ← 应为 inode->i_cdev
 45c: ldrex r2,[r3]        ; r3 = r4+0x40  ⇒ r4=0 ⇒ 访问 0x40 崩
r4 = 00000000
```

`chrdev_open()` 在调用 `ops->open` 之前一定已经设置 `inode->i_cdev`；
读到 0 说明 **模块编译时的 `i_cdev` 偏移（0x198）和我们内核的实际布局不一致**。

### 18.2 我们内核的 config 与"厂商内核"差在哪

```
我们的 .config:
  # CONFIG_FS_POSIX_ACL is not set
  # CONFIG_SECURITY is not set
  # CONFIG_SECURITYFS is not set
  # CONFIG_IMA ... (未开启)
```

而 `struct inode` 在 3.0.27 里正是这些宏**夹在 `i_flags` 与 `i_cdev` 之间**：

```c
struct inode {
    umode_t i_mode; unsigned short i_opflags; kuid_t i_uid; kgid_t i_gid;
    unsigned int i_flags;
#ifdef CONFIG_FS_POSIX_ACL          ← 两个指针（i_acl / i_default_acl）
#endif
    const struct inode_operations *i_op;
    struct super_block *i_sb;
    struct address_space *i_mapping;
#ifdef CONFIG_SECURITY             ← 一个指针（i_security）
#endif
    …  dev_t i_rdev; …  struct cdev *i_cdev;
```

**每开一个这样的选项，后面所有字段（含 `i_cdev`）整体后移 4 字节。**
厂商内核（跑着 ext2/私有驱动，且有 Sony 安全/ACL 相关代码）几乎肯定开了其中若干项
⇒ **同一份 `.ko` 在我们内核里读 `inode->i_cdev` 会读到"别的字段"**（实测读到 0）。

### 18.3 为什么这条很重要

我们的内核是**自己重建**的（`3.0.27_nl-rt106+ #142`，业务模块全是**厂商原版 .ko**）。
如果结构布局不一致，**所有厂商模块都可能在各处读错偏移**，表现为：
* `grm_gles` open 崩（已见）；
* `grm_ma` 的 mspace/池指针取到垃圾（`0x5A5A5A5A`，已见）；
* 各种"服务明明应答了但 App 不前进"的古怪行为（这一整轮的谜题）。

### 18.4 下一轮：把偏移量**量出来**并按需校正内核

1. 在我们自己的 `fake_gles.ko`（已能自由重建）里加一行 printk：
   `offsetof(struct inode,i_cdev)`、`offsetof(struct file,private_data)`、`sizeof(struct inode)`、`sizeof(struct file)`；
2. 与厂商模块的期望值对照（已知 `grm_gles` 期望 `i_cdev == 0x198`）；
3. 若不符：按候选 config（`FS_POSIX_ACL=y` / `SECURITY=y` / `IMA=y` 组合）重编内核，
   直到 `offsetof(i_cdev)==0x198` —— **这一步可能一次性解开后面所有"服务应答了却不前进"的问题**。

---

## 19. 第 32 轮：**实测内核 struct 偏移 = 0x190，厂商模块期望 0x198（差 8 字节）**

### 19.1 怎么量的（可复现）

给我们的 `fake_gles.ko`（内核探针模块，源码在 `/mnt/build/fg_tb2/fake_gles.c`）加一行：

```c
printk(KERN_INFO "INOFF inode_i_cdev=%u inode_size=%u file_private_data=%u file_size=%u inode_i_private=%u\n",
       offsetof(struct inode,i_cdev), sizeof(struct inode),
       offsetof(struct file,private_data), sizeof(struct file), offsetof(struct inode,i_private));
```

**构建工具链必须用 Buildroot 6.4**（宿主 gcc 15 编不了 3.0 内核）：

```bash
export PATH=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin:$PATH
export LD_LIBRARY_PATH=/mnt/build/toolchain/compat:/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/lib
make -C /mnt/build/src/a7c-kernel/linux M=/mnt/build/fg_tb2 ARCH=arm \
     CROSS_COMPILE=arm-buildroot-linux-gnueabihf- modules
# 模块安装位置：/usr 分区 /kmod/grm_gles.ko（我们的 probe 模块内部名是 fake_gles）
```

### 19.2 实测结果（当前内核）

```
INOFF inode_i_cdev=400(0x190)  inode_size=424  file_private_data=168(0xA8)  file_size=192  inode_i_private=420
```

* **`struct file` 完全匹配**（`private_data=0xA8`，厂商 `sugilite_open` 正是写 `[filp+0xA8]`）；
* **`struct inode` 差 8 字节**：我们 `i_cdev=0x190`，`grm_gles` 期望 **0x198** ⇒ 它读到的是别的字段
  （实测为 0）⇒ 打开 `/dev/dmpgles2` 时 `ldrex` 访问 0x40 崩溃。

### 19.3 为什么是 8 字节：3.0.27 的 `struct inode` 布局

`i_cdev` 之前只有两个 config 相关字段：

```c
#ifdef CONFIG_SECURITY
	void *i_security;            /* +4 */
#endif
	...
#ifdef CONFIG_QUOTA
	struct dquot *i_dquot[MAXQUOTAS];   /* MAXQUOTAS=2 ⇒ +8 */
#endif
	union { ... struct cdev *i_cdev; };
```

（`CONFIG_FS_POSIX_ACL` 的 `i_acl/i_default_acl` 在 `i_cdev` **之后**，改它不影响——我们试过，偏移不变 ✓ 证实）

⇒ **厂商内核几乎肯定开了 `CONFIG_QUOTA`（MAXQUOTAS=2）**，正好 +8 字节 = 0x198。
**正在按这个改内核并重编验证**（`CONFIG_QUOTA=y` + `CONFIG_QUOTACTL=y`）。

### 19.4 这为什么可能是"总闸"

App 整条链上的怪象（`grm_gles` open 崩、`grm_ma` 取到 0x5A5A 垃圾池、服务都应答了却不前进）
**都可能是同一个原因**：我们内核与厂商内核的结构布局不一致，**厂商原版 .ko 到处读错字段**。
若对齐成功，很多"补服务也不动"的问题可能一次性消失。

### 19.5 关于"要不要换成 virt 现代机型"（回答用户的疑问）

**换不了**：ZV-E10 是 Sony CXD90045（ARMv7 + 私有中断控制器/定时器/内存映射），
厂商 3.0.27 内核**只能在 cxd90045 机型上启动**；a7c2 能上 `virt` 是因为它那颗是 AArch64 + 接近标准 ARM64 内存模型。

真正的差别不在"机型新老"，而在**我们把替换点放在哪一层**：
* a7c2：真固件用户态 + 只替换**用户态 API**（mpgpe/GPE/OSD 消费者）+ 标准 ramfb ⇒ 完全不用碰内核 ABI；
* 我们：跑**真内核 + 35 个厂商 .ko** ⇒ 必须同时满足**内核 ABI 与设备语义**（本轮就是这块的账）。

⇒ 结论：机型保留（被迫），但要做两件事减轻内核负担：
1. **对齐内核 config**（正在做；目标是让厂商 .ko 的偏移假设成立）；
2. **砍掉不需要的厂商模块**（wlan/bt/usb 等，a7c2 就是这么干的），只留 App 菜单所需的最小集。

---

## 20. 第 33 轮：**找到了 MA 的 DMM 服务（qid 0x0061003C），并从用户态成功应答**；剩下的是 RT 内核 BUG

### 20.1 从 `grm_ma.ko` 反汇编出的 DMM 往返协议（关键）

`ma_dmm_open_send_recv`（0x1FC）：

```
osal_valloc_msg_wait(0x61003C, &msg, 24, 1)     ← 24 字节消息
msg[0x00] u16 = 1（操作码）  msg[0x04]=ctx  msg[0x08]=0
msg[0x0C] = 0xFFFFFFFF      msg[0x10]=source  msg[0x14] u16=序列号  msg[0x16] u16=1
osal_snd_msg(0x61003C, msg)                      ← ★ 不是 snd_sync！
osal_rcv_msg_tmo(0x014C0424, &msg, 30000)        ← 30 秒超时，在 MA 自己的队列上收应答
校验：reply[0x00]==0x8001 且 u16@reply[6]==序列号
取值：*out1 = reply[0x10]  *out2 = reply[0x14]  *out3(u16) = reply[0x04]
```

* 字面量取自 `grm_ma.ko` .text 0x33C = **0x0061003C**（DMM 队列）、0x340 = 0x014C0424（MA 队列）、
  0x344 = 序列号全局（模块 .data +0x34）；
* `ma_init` 里 `osal_reg_sync_msg_queue_cb(0x014C0424, ctx, 0, 0)` ⇒ **MA 的用户侧队列确实是 sync 队列**（我们的 hook 没错），
  但**它跟 DMM 的往返是 snd_msg+rcv_tmo**（合作方指出的这点是对的）。

### 20.2 我们在 preload 里实现 DMM 服务 —— **成功**

```
DMMREG sym=0x04073301  rc=0  h=0x60b9f340        ← osal_reg_msg_queue_cb(0x61003C, ...) 成功
DMMW  valloc=0x04072fed snd=0x04072c35           ← dlopen("libosal_uipc.so") 解析到真符号
DMMREQ n=1 w0=0x04040004 seq=1                   ← ★ 内核 MA 真的把请求送来了
DMMREP seq=1 n=1                                 ← ★ 我们的应答已发回 0x014C0424
```

**要点**：
1. **DMM 服务是用户态的**（0x61003C 未被内核占，我们注册成功并收到内核发来的消息）；
2. 应答必须**在独立线程里发**（在回调里直接发会让接收线程重入）；
3. 解析 `osal_*` 必须用 `dlopen("libosal_uipc.so")` 句柄（`dlsym(RTLD_DEFAULT)` 会给 0x90c0 这类假地址）。

### 20.3 现在的卡点：收到/发出应答后内核 BUG（RT 断言）

```
Internal error: Oops: 817  PC is at __bug+0x20  LR is at __migrate_enable+0x10c
r0=0x00000083(131) 进程 THREAD_INFRA0
```

* 这是 PREEMPT_RT 的 **`__migrate_enable` 断言**（migrate 计数不平衡）；
* 触发点在**厂商模块（grm_ma/osal_uipc）处理应答的路径**上；
* 30 秒超时未应答时也会走到同一断言（说明这条 RT 语义在我们内核里和厂商编译时假设不一致）。

⇒ **又一次指向"我们重建的内核与厂商内核不一致"**（与第 32 轮的 struct 偏移问题同源）：
厂商 `.ko` 假设的 RT 语义 / 内核版本内部实现与我们 `#144` 不完全一致。

### 20.4 下一轮

1. 对比 RT 配置：`CONFIG_PREEMPT_RT_FULL` / `CONFIG_PREEMPT_RT_BASE` / `CONFIG_MIGRATE_DISABLE` 等，
   看厂商内核可能的组合（我们当前是 `PREEMPT_RT_BASE`）；
2. 或者绕开内核 MA：既然 **DMM 请求是内核→用户态** 的，我们也可以**让 MA 的 mspace 永远不被使用**
   （在用户态 MA 服务里把 malloc 应答做得完全自洽），同时在自定义入口路线上继续；
3. 记录结论：**"补服务"这条路已经打通到"内核真的来问我们了"**，剩下的是 RT/ABI 级别的对齐。

---

## 21. 第 34 轮：**ABI 修复验证成功（i_cdev 0x198 ✓）**；新卡点是 `task_struct`/RT 内联不一致

### 21.1 `CONFIG_QUOTA=y` 让结构布局对上厂商模块（已验证）

重建内核 `#144`（`CONFIG_QUOTA=y, CONFIG_QUOTACTL=y`），再用**新 config 重编** `fake_gles.ko` 打印：

```
INOFF inode_i_cdev=408(0x198)  inode_size=432  file_private_data=168(0xA8)  file_size=192
```

**`0x198` = 厂商 `grm_gles` 期望的偏移** ✓
（此前 `#142` 无 QUOTA 时是 `0x190`，差 8 字节 = `i_dquot[MAXQUOTAS=2]`）

**实测效果**：真 `grm_gles` 打开 `/dev/dmpgles2` **不再崩在 `sugilite_open`**，
现在只剩 `mspace_malloc`（MA 池）这一个崩溃点。

### 21.2 DMM 服务：用户态实现"能收能回"，但内核侧照样 BUG

* 用户态注册 `0x0061003C` 成功、能收到内核 MA 的请求、能把 24B 应答发回 `0x014C0424`；
* 但在内核处理应答（或 30s 超时）时触发：

```
WARN_ON_ONCE(p->migrate_disable <= 0)   ← kernel/sched.c 的 __migrate_enable
Internal error: Oops: 817  PC=__bug+0x20  LR=__migrate_enable+0x10c
进程 THREAD_INFRA0；调用链里有 0x5f3c9xxx [grm_ma] / 0x5f047xxx [osal_uipc]
```

**⇒ `migrate_disable/migrate_enable` 在 RT 内核里是头文件内联的**（直接读写
`current->migrate_disable`），厂商 .ko 用的是**厂商内核的头文件常量偏移**；
我们内核的 `struct task_struct` 布局与之不同 ⇒ 内联代码把计数写到了错字段 ⇒ 我们这边看到 `<=0` 就 BUG。

**这与第 32/34 轮的 `struct inode` 问题是同一类**：**重建内核的 struct 布局必须与厂商内核一致**。

### 21.3 内核态 fake_dmm.ko 尝试失败

把 `fake_dmm.ko`（注册 0x61003C、内核里直接回应答）**编进 preload**、运行时用 `init_module(128)` 加载：

```
MODLOAD fake_dmm rc=20
```

⇒ 被拒（`-20`）。可能原因：Sony 内核有模块签名/白名单，或我们的 `init_module` 调用方式不对。
（注：vendor .ko 都是签名的，我们自己编的没有签名。）

### 21.4 下一轮（两条并行）

1. **继续对齐 struct 布局**：量 `offsetof(struct task_struct, migrate_disable)` 等，
   并从**厂商 .ko 的反汇编里读出它假设的偏移**（内联代码里有 `ldr/str [current, #off]`）——
   用"厂商模块的常量"反推内核 config，比猜更快；
2. **或绕开内核 MA**：既然我们已能在用户态完整应答 MA（op7/op1/op9），
   就继续走"用户态 MA + libgrm2 软件光栅化"这条路，不去触碰内核 MA/DMM。

---

## 22. 第 35 轮：找到 App 在 MA 之后的**断言点**，并发现 MA 客户端有**两条通道**

### 22.1 我们自己的 assert hook 吞掉了 App 的断言（重要教训）

`menuc1009` 系探针里 `__assert_fail` 被我们 hook 成 **打印后死循环**。
于是 App 一旦断言，**表面上就是"静默停止"**——之前多轮"App 突然不动了"其实是被我们自己的 hook 挂住了。

修好观测后立刻看到：

```
AAS e == MA_ErrNone @dmp_allocator.c:116  lr=0x…（libObj+0x6BCBD0）
```

### 22.2 断言点定位（libObj 反汇编）

`dmp_allocator_lib_init`（libObj+0x6BCB58）流程：

```
6bcb58: osal_wai_sem_tmo(…, -1)                    ; 进临界区
6bcb86: 构造请求 {name, 640(0x280), 0,0,0,0, -1, 0}  on stack
6bcbb6: bl 0x7675B4                                 ; ★ 创建 dmm 连接对象
6bcbba: cbz r0 → 成功; 否则 → __assert_fail("e == MA_ErrNone", 105/116)
```

`0x7675B4` → `new(0xB8)` + ctor `0x7687CC(obj, req)`：

```
7687d6: movs r0,#5 ; ldr r1,[req+4] ; bl 0x76898E   ; 发请求（长度 6）
7687e0: ldr r3,[obj+16] ; cmp #1 ; bne 失败          ; ★ 要求 obj->[16] == 1
7687fa: 读取 reply: [req+4] → obj+40 ; [req+12] 字节 ; [req+0] = 16 字节名字指针
```

⇒ **我们 op1 的应答（status=0）让 `obj->[16]` 不是 1 ⇒ 直接判失败 ⇒ 断言。**

### 22.3 MA 客户端有**两条通道**（此前只 hook 了一条）

`0x7688D0`（真正的发送函数）：

```
osal_valloc_msg_wait(qid_lit, &msg, len+32, 0)     ; 32 字节头 + 负载
msg[0] u16 = 2 ; msg[2] u16 = 0x80 ; msg[4]=0 ; msg[8]=arg ; msg+16 构造 ; msg+24 负载
osal_snd_msg(qid_lit, msg)                          ; ★ 异步发送（不是 sync！）
```

而之前我们 hook 的 `osal_snd_sync_msg(0x014C0424)` 只覆盖了**同步**那一类请求（op7/op1/op9）。
⇒ **还有一整条异步 MA 通道我们完全没应答**，这解释了"应答了还是不走"。

### 22.4 下一轮

1. hook `osal_snd_msg`（qid 0x014C0424）把**异步 MA 请求**也接住，打印 opcode/长度/负载，
   看清这条通道的请求与“应答该发到哪里”；
2. 同时把 op1 的应答改成让 `obj->[16]` 变成 1（先按 `reply[4]=1` 之类做 A/B 试）；
3. 记录：**assert hook 必须保留"打印+记录"，但不能让 App 卡死**——改成打印后调用原 `abort()`。

---

## 23. 第 36 轮：GDB 现场确认 —— **我们的 MA 应答确实被 App 吃进去了**，失败发生在"第二次创建"

### 23.1 现场（GDB 断点，libObj=0x6051000）

```
HIT CHECK16 (0x7687E0)   obj@0x042b68: 0740e2e8 edfefeca 00000000 00000000 00000001 …
                                    ↑ obj->[16] == 1  ⇒ 这一关过了

HIT SET16   (0x768846)   obj@0x042b68: 0740e2e8 edfefeca 68640000 00000000 00000001 00000000 635f6c67 6f6d6d6f
                                    ↑ obj->[8] = 0x68640000 = 我们回的句柄
                                      obj+24 处是 "gcl_common"（名字被 memcpy 进来）

HIT ASSERT116(0x6BCBD0)  out 结构里指向 obj=0x042b68；r0=0
```

**⇒ 结论：我们的 op1 应答格式是被正确消费的**（句柄进 `obj->[8]`，名字进 `obj+24`），
第一轮 dmm 连接创建**成功**；随后 App 又做了**第二次**创建/初始化，那次失败了才触发 116 号断言。

### 23.2 下一步（下一轮直接做）

1. 在 `0x6BCBCC`（真正 `bl __assert_fail` 处）布点，**向上遍历栈**（ARM 有 fp）找到是哪个 `bl` 返回了非 0；
2. 同时把我们所有 MA 应答的**完整 24 字**打出来（现在只打前几个字），确认第二次请求的 opcode/字段；
3. 把 `__assert_fail` hook 改成"打印 + 主动 SIGSEGV"，避免再出现"静默卡死"（本轮已改，效果良好：断言立刻变成可见崩溃）。
