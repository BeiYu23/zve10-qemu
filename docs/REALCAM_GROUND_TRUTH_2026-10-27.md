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

---

## 24. 第 37 轮：沿断言链往下挖 —— 失败点是 **App 自己的虚函数返回**，MA 只是"日志式"旁路

### 24.1 定位到具体调用（`dmp_allocator_lib_init` 内部）

`dmp_allocator_lib_init` 里 116 号断言的上一句是（libObj 反汇编）：

```
6bcc36: bl 0x76768C          ← 失败就断言 116
```

`0x76768C(obj, arg)`：

```
76769a: bl 0x767D7C(obj)     ; 前置检查，非 0 直接返回
7676a4: bl 0x768EEC(obj)
7676ac: bl 0x768650(obj,arg) ; ★
7676b0-7676b8: 取 [obj+0] 虚表 +4 → blx r3   ; ★ 返回值来自**虚函数调用**
```

`0x768650(obj)` 内部：

```
768668: movs r0,#11 ; bl 0x7689AC   ; 发一条 10 字节控制消息（id=11）
768674: bl 0x767468                 ; ★ 等信号量（tmo=-1）
768686: ldr r3,[obj+0x14] ; cmp #0 ; bgt 循环等
768690: bl 0x767AFC                 ; 完成后处理
7686A2: bl 0x7674B4                 ; 释放/唤醒
```

**⇒ 结论：116 号断言不是"某个 MA opcode 返回错误"，而是这套"对象创建 → 等完成 → 虚函数收尾"
状态机的最后一环失败。**

### 24.2 信号量观测（本轮新增探针）

对 `osal_wai_sem_tmo` / `osal_set_sem` 打点后，MA 阶段前后都是这种模式：

```
SW sem=0x0094600e tmo=-1 ret=0
SW sem=0x009401c3 tmo=-1 ret=0
SW sem=0x009401c3 tmo=-1 ret=0
SW sem=0x0094600f tmo=-1 ret=0   …
```

都是 `tmo=-1`（无限等），且都返回 0（被正常唤醒）⇒ **不是死等**，
说明 App 内部的完成机制是通的；失败发生在收尾逻辑（虚函数）里。

### 24.3 下一步

1. 在 `0x7676B0` 处（虚表调用前）打印 `obj+0`（虚表）、`obj->[8]`（我们给的句柄）、`obj->[16]`（状态），
   看清它调的是哪个虚函数、返回了什么；
2. 由虚表偏移反查函数（libObj 里按 vtable 布局找），确定 App 期望的"完成态"条件；
3. 若确实需要 App 侧真实数据（如某个 UIPC 应答），按 a7c2 方式补该服务，而不是继续猜 MA。

---

## 25. 第 38 轮：116 号断言的确切因果（GDB 实测）

### 25.1 现场

```
HIT F_F2   (0x7676F2)  r0=0x00046600(obj)  r1=0x00000000(★arg = NULL)  lr=0x670dc1f
   req@0x46600: 0740e2e8 edfefeca 68640000 00000000   ← magic、我们的句柄 0x68640000
HIT RET116 (0x6BCC1E)  r0=0xFFFFFFFD(-3)                ← 判定失败，走断言
```

### 25.2 因果链（全部由反汇编确认）

```
0x6BCC1A: bl 0x7676F2(obj, arg)          arg = [r7+60]（本次为 NULL）
0x7676F2: bl 0x767D7C(obj)               ; memcmp(obj+4, magic, 4) → 0（通过）
        → tail 0x7684F8(obj, NULL)
0x7684F8: bl 0x767468(obj+0x58)          ; 等信号量
          r3 = obj->[16]; 允许值 = 16 或 2，否则直接到错误出口
          r8 = 0x767B22(obj+0x2C, arg)   ; ★ arg 为 NULL ⇒ 很可能是错误来源
          组包 {obj->[8](我们的句柄), r8, flag} → 0x7684DC
0x7684DC: if (req->[8]==0) return 0xFFFFD8F1
          call 0x768208(req)             ; 真正发 MA 请求（同步）
          return *(u32*)(req->[4] + 16)  ; ★ 返回值 = 回复的第 4 个字（status）
```

**⇒ 116 号断言 = `0x7676F2(obj, NULL)` 返回 -3**，而 -3 出现在 `0x7684F8` 的错误出口
（obj 状态不在 {16,2} 或 `0x767B22` 返回 0）。

### 25.3 结论与下一步

* 这条路径**根本没有走到发 MA 请求**（`0x7684DC` 未命中），所以**再怎么改 MA 应答也没用**；
* 真正缺的是：让 `obj->[16]` 处于 16/2，并且 `arg`（由调用方算出）非空；
* 下一轮在 `0x768510`（读 obj->[16]）和 `0x767B22` 入口布点，看：
  1. obj->[16] 当时是多少；
  2. arg 是谁算出来的（回溯 `[r7+60]` 的赋值处），为什么是 NULL。

---

## 26. 第 39 轮：又一条 ABI 维度核对完毕（thread_info）；并记录两个"没跑通"的工具

### 26.1 `thread_info` 与厂商模块一致（已验证）

厂商 `osal_uipc.ko::osal_lock/osal_unlock` 直接裸操作 thread_info：

```
1ec: bic r3, sp, #8128 ; bic r3, r3, #63     ; r3 = current_thread_info()
1f4: ldr r2, [r3, #4]      ; preempt_count
1f8: sub r2, r2, #1
1fc: str r2, [r3, #4]
200: ldr r3, [r3, #0]      ; flags
204: tst r3, #2            ; TIF_NEED_RESCHED
208: beq → 返回;  否则 bl preempt_schedule
```

我们内核：

```c
struct thread_info { unsigned long flags; int preempt_count; ... struct task_struct *task; }
#define preempt_count() (current_thread_info()->preempt_count)   /* include/linux/preempt.h:28 */
```

* `flags` @0、`preempt_count` @4、`task` @0xC —— **与厂商模块的假设完全一致** ✓
* `CONFIG_THREAD_INFO_INDIRECT` **未开启**（若开启会把 task 挪到 +0x10，那才是灾难）
* 结论：`thread_info` 不是问题源；`__migrate_enable` 的 WARN 另有原因。

### 26.2 两个没跑通的工具（记录，避免重复踩）

1. **in-guest qid 普查线程**：`pthread_create` 成功、打印了 `CENSUS started`，
   但 10s 后应打印的 `QDUMP` 没出现（进程输出在 ~20s 就停了）⇒ 需要在**主机侧**做统计，
   不要依赖 guest 内的延时线程；
2. **GDB 断点在 0x7684DC / 0x7684F8 / 0x767B22 上"不命中"**，而 0x6BCC1E（返回点）能命中
   ⇒ 说明失败路径不走这些函数（与上一轮结论一致：**这次连 MA 请求都没发**）。

### 26.3 下一轮（转向内核侧，绕开 RT 用户态交付路径）

用户态 DMM 应答会触发 RT 断言，最干净的做法是**内核态应答**（不走用户态交付）。
上一轮 `init_module` 被拒（rc=-20），下一轮先把它搞定：

1. 试 `init_module` 的不同姿势（可写缓冲、4 字节对齐、先 `delete_module` 冲突名、检查 `-20` 到底对应哪条错误路径）；
2. 或用**现有已加载模块的接口**注册队列（例如让 `fake_gles` 里加 DMM 应答逻辑，随开机 insmod 加载，完全避免运行时加载）；
3. 成功后再看内核 MA 是否能拿到合法 mspace，从而让 App 的 dmp_allocator 走通。

---

## 27. 第 40 轮：★ 里程碑 —— **内核态 DMM 服务让真 MA 通了**，App 首次冲出 MA 进入业务框架

### 27.1 做法：把 DMM 应答**编进开机就加载的模块**（不再运行时 init_module）

在 `fake_gles.c`（开机由 `/kmod/grm_gles.ko` insmod）里加：

```c
osal_reg_msg_queue_cb(0x0061003C, &dmm_h, &fake_dmm_cb, 0);   // 注册 DMM 队列
fake_dmm_cb(req):
    seq = *(u16*)(req+0x14)
    osal_valloc_msg_wait(0x0061003C, &m, 24, 1)
    m[0]=0x8001; m[4]=1(pool); m[6]=seq; m[0x10]=0x02000000; m[0x14]=0x20312000
    osal_snd_msg(0x014C0424, m)
```

### 27.2 实测（镜像 `ab_v2280`：真 grm_ma + 我们的软件 dmpgles2 + 该模块 + 干净探针）

```
FAKEDMM reg rc=0 h=60b9ef40
MA passthru op=7      ← 用户态 MA 请求交给内核 MA
MA passthru op=1      ← dmm_open：内核 MA 发消息给 DMM
FAKEDMM req n=1 w0=00000001 seq=0     ← ★ 我们的内核模块收到
FAKEDMM reply seq=0 snd=0             ← ★ 应答成功
MA passthru op=9      ← malloc：内核 MA 用刚建立的 mspace 分配
```

**没有 oops、没有 `mspace_malloc` 崩溃**（对比第 21 节的 RT BUG：那是**用户态**应答走 RT 用户态交付路径导致的；
内核态应答完全绕开了它）。

### 27.3 App 的下一步进展（本轮最大收获）

MA 通之后 App 继续往下走，出现了**新的线程**并进入业务框架：

```
[current task] program: /usr/bin/im.elf.real (THREAD_INFRA1) (pid: 139 …)
address: 00000000, trap_no 0xe            ← 空指针数据异常
pc=0x08a397fc  →  映射到 libBizFw.so 偏移 0x427FC
   = AvbbcLogManager::getInstance() 内部
```

之前是"MA 一过就静默"，现在是 **MA 过了、起了 THREAD_INFRA1、跑到 libBizFw 才崩** ⇒ 前进了一大步。

### 27.4 顺带修的一个符号可见性问题

把业务推进后又暴露：`libObj.so: undefined symbol: MWF::RsrcAcsr::RsrcAcsr(...)`。
查证：该符号**确实由我们镜像里的 `/lib/libMWF.so` 定义**（dynsym 有 `FUNC GLOBAL DEFAULT 10 …`），
只是 App 解析时 libMWF 还没进全局作用域。在 preload 里提前 `dlopen("//lib/libMWF.so", RTLD_NOW|RTLD_GLOBAL)` 即可（已加，
`PL //lib/libMWF.so h=0x16038` 成功；`libmprctrl.so` 仍返回 0，需单独查依赖）。

### 27.5 下一轮

1. 查 `AvbbcLogManager::getInstance` 的 NULL 来源（很可能是日志实例未初始化 → 与 `0x8F013A` 日志服务应答是否完整有关）；
2. 继续用"内核态服务"这一招补其它缺的服务（比用户态稳）；
3. 关注判据：`GRM2 > 0` 且 `/dev/stream` 从均匀 `0x5A5A` 变非均匀。

---

## 28. 第 41 轮：MA 通之后的两个新卡点，逐个拆

### 28.1 `AvbbcLogManager::getInstance` 的 NULL 已定位并可绕过

```
0x427ec: ldr r5,[r5,r3]            ; 取静态 map<string,AVBBC_SET*> 表地址
0x427ee: ldr r6,[r5,r4,lsl #2]     ; 取实例
0x427f2: cmp r6,#0 ; bne 使用
0x427f8: blx ConvertDef2Set(id)    ; ★ 未命中返回 NULL
0x427fc: ldrh r3,[r0,#0]           ; ★ 崩溃点（r0=0）
```

`ConvertDef2Set` 是 `std::map<u32, DefAvbbcLog::AVBBC_SET*>::find` 查表 —— 表**没被填充**才会返回 NULL。
填充函数是 `AvbbcLogManager::Initialize()`（`0x4275C`，循环 `SetDef2Set(…, 12 字节表项)`，无文件 IO）。

**实测**：在 preload 里 `dlopen("//lib/libBizFw.so", RTLD_NOW|RTLD_GLOBAL)` 后直接调用
`_ZN15AvbbcLogManager10InitializeEv`：

```
BF h=0x000229a0  BF ini=0x08c3975d  BF ini done     ← 成功
```

⇒ **这个 NULL 崩溃消失了**（说明就是"表没初始化"）。

### 28.2 紧接着的新崩溃：在 `ld-2.11.2.so` 内部

```
[current task] im.elf.real (THREAD_INFRA1)
address: 00000004  trap_no 0xe
pc=0x0400a706  →  ld-2.11.2.so +0xA706
```

glibc 2.11 的动态链接器内部空指针（NULL+4）——**高度可疑是我们这个 preload 里
"另起线程 dlopen + 调符号"与 App 自己的 dlopen 争用 ld.so 状态**（2.11 的 ld.so 对并发 dlopen 并不健壮）。

### 28.3 下一轮

1. **不用 dlopen**：从 `/proc/self/maps` 拿到 `libBizFw.so` 已映射的基址，按偏移
   （`Initialize = 基址 + 0x4275D`，Thumb 位）直接调用——完全避开 ld.so 的全局锁；
2. 若仍崩在 ld.so：把初始化调用**改到主线程**（例如在拦到的某个 App 函数里顺手调用），避免跨线程 dlopen；
3. 继续盯判据：`GRM2 > 0`、`/dev/stream` 由 `0x5A5A` 变非均匀。

---

## 29. 第 42 轮：修好 BizFw 日志管理器后，崩溃点一路后移，现在**已经在 libMWF 里跑框架代码**

### 29.1 BizFw 日志表初始化 —— 用"lib 一被映射就补调"的办法彻底解决

```c
dlopen 拦截器里：real() 返回非空后 → map_base_for("libBizFw.so") → 调 base+0x4275C 的 Initialize()
```

实测：

```
BZI init at base=0x08a02000
（此后 ConvertDef2Set(1..18) 全部返回有效指针）
```

⇒ `AvbbcLogManager::getInstance` 的 NULL 崩溃**消失**。

（踩过的坑：① 起线程 `dlopen` + 调符号 → 崩在 `ld-2.11.2.so`；
② 太早调（映射刚出现、重定位未完成）→ 崩在 `Initialize` 内部 `ldrb [r6]`；
最终用"**dlopen 返回后再调**"这个时机，两个坑都避开。）

### 29.2 崩溃点后移轨迹（都是前进）

| 版本 | 崩溃点 | 说明 |
|---|---|---|
| 初始 | `mspace_malloc`（内核） | MA 不通 |
| 修好内核 DMM 后 | `libBizFw + 0x427FC`（日志表未初始化）| 进入业务框架 |
| 补 Initialize 后 | `ld-2.11.2.so + 0xA706`（并发 dlopen）| 去掉早期 dlopen |
| **现在** | **`libMWF.so + 0x32332`（MonQueue）** | ★ **已经在跑 MWF 框架代码** |

### 29.3 下一轮

1. `MonQueue` 相关崩溃：看是哪条 monitor 没初始化（`IsEmpty` 里对空队列解引用）；
2. 保持"不用 dlopen 抢跑、按需补初始化"的策略；
3. 判据仍为 `GRM2 > 0` + `/dev/stream` 非均匀。

---

## 30. 第 43 轮：`MonQueue` 崩溃定位到 `MonDrv::Getchar`（监视器/调试口驱动）

### 30.1 现场与调用链

```
崩溃 pc=libMWF+0x32332 = MonQueue::IsEmpty
   3232a: ldmia r0,{r2,r3}      ; r2 = this->[0]，r3 = this->[1]
   32332: ldr   r0,[r2,#0]      ; ★ r2 == 0 → 崩
调用者 lr = libMWF+0x3133B = MonDrv::Getchar
   31334: ldr r0,[r4,#0]        ; r4 = libMWF 数据段里的全局
   31336: bl  MonQueue::IsEmpty
```

**关键**：崩溃时 `this`(r0 in IsEmpty) = **0x000303F8** —— 这不是堆指针（堆在 0x09xxxxxx），
说明**全局里存的是垃圾值**，`MonDrv::Init` 正常构造的两个队列（0x31294 / 0x312BC 两处 ctor，
参数分别是 `fixedArea+0x53400/4/16, 0x400` 与 `+0x53408/C/410, 0x7BF0`）**没有生效**，
或者 Getchar 跑在了 Init 之前的另一个上下文。

（`MonDrv::Init` 确实跑过：串口里有它打印的
`FixedAreaTop 3E000000 / FixedAreaSize 00080000 / Physical 3E053400 / Logical 0x5951400 / Mon Buf Size 00008000`）

### 30.2 判断

`MonDrv` 是**监视器/调试口驱动**（连 CP 的调试通道）。这条崩溃很可能是：
* 应用侧"监视器线程/调试接口"被拉起（因为我们把设备节点/共享区做出来了），
  但对应的 **CP 通道**不存在 ⇒ 队列指针没被正确建立 ⇒ 一旦被读就崩。

### 30.3 下一轮

1. 用 GDB 断在 `0x31334`（Getchar 读全局处），读 `r4` 指向的全局值 + 该全局的写入点（watchpoint），
   确认谁写了这个垃圾值；
2. 若是"没有 CP 才走到的调试路径"，考虑**提供最小 CP 通道**或让该路径不被触发（比 patch 更符合 a7c2 做法）；
3. 判据仍为 `GRM2 > 0` + `/dev/stream` 非均匀。

---

## 31. 第 44 轮：崩溃真凶 = `MWF::ObjIf::SetEventSender` 里 **MonCtrl 指针是垃圾**

### 31.1 重新对齐偏移后（GDB 报 MWFBASE，本轮 libMWF=0x058C2000）

```
崩溃 pc = libMWF+0x26332  (MWF::ObjIf::SetEventSender+0x22)
   2632e: ldr.w r9,[r4,r3]        ; r9 = 全局 MonCtrl 指针
   26334: ldr.w r0,[r9]           ; ★ 解引用 r9
   26338: bl    MonCtrl::ConPrintfMessage
故障地址 = 0x4  ⇒ r9 == 4（垃圾）
```

**⇒ 崩溃点不是监视器队列，而是 `MWF::ObjIf` 打日志时用的 `MonCtrl` 单例没被正确建立。**

### 31.2 顺带确认的符号地址（后面可直接用）

| 符号 | 偏移 |
|---|---|
| `MonDrv::Init` | libMWF+0x311A5 |
| `MonDrv::Getchar / Putchar / OutputProc` | +0x31329 / +0x31415 / +0x31381 |
| `MonDrv::mp_input` / `mp_ioBuffer` / `mp_lockBufStatus` | **0x46CA0 / 0x46CA4 / 0x46CB0** |
| `MonCtrl` ctor / vtable / typeinfo | +0x30C95 / 0x42E50 / 0x42E60 |
| `MWF::ObjIf::GetInstance` | libMWF+0x26A4D |

（GDB 观察：`MonDrv::Getchar` 在多轮轮询里返回 -1（队列空），本身不崩；崩的是**日志路径取的 MonCtrl**。）

### 31.3 下一轮

1. 找到写 MonCtrl 全局的那条路径（`MWF::ObjIf::GetInstance` 里应该会 `new MonCtrl`），
   像 BizFw 那样**在合适时机补调一次**，让全局拿到有效对象；
2. 若 MonCtrl 依赖监视器通道（CP 调试口），就提供最小实现（`ConPrintfMessage` 走 sink 即可）；
3. 判据仍为 `GRM2 > 0` + `/dev/stream` 非均匀。

---

## 32. 第 45 轮：按合作方的 MonCtrl 分析实测 —— `MWF::Initialize` / `MonDrv::Init` 都能补调，崩溃点继续后移

### 32.1 合作方的关键结论（已核对）

* `MonDrv::Init`（libMWF+0x311A5，dynsym GLOBAL）**就是建 28 字节 MonCtrl 并把指针写全局的地方**（写全局在 +0x311C6，
  **早于**可能失败的 `Backup_read_4Byte`）⇒ `r9==4` 是因为它没跑过；
* `MWF::Initialize`（+0x20A09）**幂等**（守卫字节），内部依次跑
  `MwfCommon::Init → Monitor::Init → ObjThread(64B)+StartThread → ObjLog_Mcmd/RsrcMcmd/InhMcmd/ObjIf_Mcmd/ObjIf::Init → EventIdFactory::Initialize(0x83e8,0x800)`，
  但**不包含 `MonDrv::Init`**（全库无 bl 到 0x311A4 ⇒ 说明是函数指针表调用，而那张表在我们这里没走通）；
* `SetEventSender` 里日志块由守卫字节控制（`+0x26326 cbz r2`），**守卫 = `MonLib::isEchoBackOn`(0x476D0)**；
* 全库 **46 处** `ConPrintfMessage` ⇒ 只 patch 一处不够，应把 MonCtrl 建起来。

### 32.2 我们的实测（镜像 ab_v2390 / ab_v2420 / ab_v2430，libMWF=0x05AC2000）

```
MWFI base=0x05ac2000 call 0x05ae2a09
 Mon Buf Size  : 00008000        ← Monitor 子系统起来了
MWFI done
MWFI echo=1                      ← ★ 守卫确实是 1（46 处日志都开）
MWFI monctrl...  →  Mon Buf Size 再打一次  →  MWFI monctrl done   ← MonDrv::Init 生效
```

置 `echo=0` 后：**MonCtrl 那处崩溃消失**，但崩点后移：
`libSysDef.so+0xDAA`（sp=0x2a899cc0 异常，疑似栈/上下文不对）→ 再下一次 `ld-2.11.2.so` 边界 `pc=0x0400f000`（跳到坏函数指针）。
内核还打出 `Exception Monitor: waiting for uninterruptible threads timedout`。

### 32.3 下一轮

1. **把 `MWF::Initialize()` 的调用从旁路线程挪到主线程**（现在是 eh 线程里调的，怀疑线程上下文/栈不对）；
2. 若仍出现坏函数指针：GDB 断在崩溃点回看指针来源；
3. 已向合作方索取 `MonDrv::Init` 的其余副作用（`Backup_read_4Byte` 读什么、还初始化哪些全局、那张函数指针表在哪）。

---

## 33. 第 46 轮：★ 监视器固定区在 QEMU 里本来映射不上 —— 我们把它接进了 DDR，队列成功建立

### 33.1 合作方把 `MonDrv::Init` 388 字节全解开（关键顺序）

```
311aa: new(28)                     ; MonCtrl 对象
311b8: ObjLock 构造
311c6: str r5,[r3]                 ; ★ 先写全局（此后 MonCtrl 一定有效）
311d0: Backup_read_4Byte(0xFA0028) ; FixedAreaTop
311d8: Backup_read_4Byte(0xFA005D) ; FixedAreaSize
311e2: open("/dev/mem", 0x1002)
31230: mmap(0, size, RW, SHARED, fd, top)
31244: base = mmap + 0x53400
31294/312bc: MonQueue::MonQueue(base+4, +0x10, 0x400) / (base+8, +0xC, 0x7BF0)
```

且 `libbackup.so` 的 `Backup_read_4Byte` 是薄包装，**备份服务不在时返回 0xFFFFFFFF**。

### 33.2 我们的实测：原本 `FixedAreaTop = 3E000000` ⇒ `/dev/mem` mmap 必然失败

我们机器（bionz.c）DDR 在 **0x80000000（512MB）**，`0x3E000000` **根本没映射** ⇒ mmap 失败 ⇒ 监视器队列指针全废。

**修法（已完成）**：在 preload 里 stub `Backup_read_4Byte`：

| key | 原值 | 我们给 |
|---|---|---|
| 0x00FA0028 (FixedAreaTop) | 0x3E000000 | **0x8F000000**（落在 DDR 内）|
| 0x00FA005D (FixedAreaSize) | 0x00080000 | 0x00080000 |

实测（镜像 `ab_v2440`）：

```
FixedAreaTop  : 8F000000
FixedAreaSize : 00080000
Physical Addr : 8F053400
Logical  Addr : 0x5b5d400        ← ★ 映射成功
 Mon Buf Size  : 00008000
```

⇒ **监视器固定区 + 两条 MonQueue 真正建立起来了**（此前一直是"半初始化"）。

### 33.3 现在的位置

`MWF::Initialize` → `MonDrv::Init`（MonCtrl + 队列）都跑通后，崩溃点又后移到
`libMWF+0x25CC0`（`MWF::ParamStd_Template<int,7>` 构造/相邻区），说明**已经进到 MWF 参数系统**。

### 33.4 下一轮

1. 看 `libMWF+0x25CC0` 具体崩在哪条指令（Param 模板对象构造）；
2. 保持"把固定区接到 DDR"的做法（这对后面 NAND/CP 缓冲区同样适用）；
3. 判据仍为 `GRM2 > 0` + `/dev/stream` 非均匀。

---

## 34. 第 47 轮：否证"`.bss` 未清零"假说（实测干净）；`echo=1` 是运行期被置上的

### 34.1 实测（preload 在 `dlopen("libMWF.so")` 返回后立即读）

```
MWFBASE=0x05ac2000
BSS echo=0 q1=0x00000000 q2=0x00000000 pfx=0x00000000
```
* `echo` = `MonLib::isEchoBackOn`(0x476D0)
* `q1/q2` = `MonDrv::mp_input / mp_ioBuffer`(0x46CA0 / 0x46CA4)
* `pfx` = `Monitor::a_Prefix`(0x477F0)

⇒ **固件自带的 `ld-2.11.2.so` 正常清零 .bss**；**`.bss` 未清零的假说不成立**。
`isEchoBackOn == 1` 是**运行期**（MWF/MonLib 初始化后）置上的，且设备上 App 同样带 `debug` 参数，
真机大概率也是 1。

### 34.2 推论：MonCtrl 在真机上必有，且不是只有 `MonDrv::Init` 会建它

搜索 `MonCtrl::MonCtrl(const char*,bool)` 的调用点，libMWF 内有 **5 处**：
`0x1E910 / 0x1E9E8 / 0x1F238 / 0x1F252 / 0x1F26C`（都落在 0x1E–0x1F 的 `Inh*` 子系统区）。
⇒ **MonCtrl 原本是由 `Inh*`（输出通道/继承）那条链建的**，我们没走到那里才导致全局为 0/垃圾。

### 34.3 本轮已确证的修复（保留）

`MonDrv::Init` 里 `Backup_read_4Byte(0xFA0028)` 在我们环境返回 `0x3E000000`，
而本机型 DDR 在 `0x80000000` ⇒ `0x3E000000` **未映射** ⇒ `/dev/mem` mmap 必失败
⇒ 缓冲区基址与两条 MonQueue（全局 3/4/5）永远为 0。
**我们把这两个 key stub 成 `0x8F000000 / 0x80000`，映射成功**：

```
FixedAreaTop 8F000000 / FixedAreaSize 00080000 / Physical 8F053400 / Logical 0x5b5d400 / Mon Buf Size 00008000
```

### 34.4 下一轮

1. 找出那 5 处 MonCtrl 构造的属主函数与调用链（已请合作方协助）；
2. 若属主在 `InhInfo::Initialize / InhDP::Initialize / EventIdFactory::Initialize` 一带，
   考虑按需要补调（`MWF::Initialize` 里已含 `InhMcmd::Init`，但不一定含 `InhInfo/InhDP`）；
3. 判据仍为 `GRM2 > 0` + `/dev/stream` 非均匀。

---

## 35. 第 48 轮：按合作方的"一次性令牌"假说改造 —— 取消提前调 `MWF::Initialize`，实测与预期有出入

### 35.1 改动

* **不再**在 preload 里提前调 `MWF::Initialize()`（也不再调 `MonDrv::Init`），
  只保留：`Backup_read_4Byte` 两个 key 的 stub（把监视器固定区接到 DDR）+ BizFw 日志表初始化 + 观测；
* 观测点：幂等令牌 `MWFBASE+0x442DB`、`RsrcDP::smp_monErr` `MWFBASE+0x4586C`。

### 35.2 实测（`ab_v2460` / `ab_v2470`）

```
BR4 key=0x00fa0028 -> 0x8f000000
BR4 key=0x00fa005d -> 0x00080000
 FixedAreaTop 8F000000 …  Mon Buf Size : 00008000      ← App/框架自己触发的 MonDrv::Init 生效
MWFI base=0x05ac2000 (no early Initialize; leave the once-token to the app)
MWFI token=0 smp_monErr=0x000305d0                      ← 令牌仍是 0（未被消耗 ✓）
崩溃 pc=0x05ae1164（实为 libMWF 的字面量池，insn=0xfffffc01）
```

**新情况**：`token=0`（符合预期，我们没提前消耗），但 `smp_monErr = 0x305D0` —— **不是 0，却是个小值**，
和之前那些 `0x303F8 / 0x20A70 / 4` 一样，属于"指针字段里放着小整数"的形态。

### 35.3 待确认 / 存疑

1. `0x4586C` 是否真是 `RsrcDP::smp_monErr`（合作方给的偏移）——需要 GDB 直接核对；
2. **App 到底有没有自己调用 `MWF::Initialize()`**：
   本轮用轮询日志判断不可靠（串口与内核 probe 输出交错，`TOK` 行被撕碎）。
   下一轮改用 **GDB 硬件写 watchpoint 盯 `MWFBASE+0x442DB`**（可靠，之前盯 `pViewManager` 用过这招）；
3. 崩溃现在落在 `MWF::EventWrapper::Send` 附近（跳到字面量池 ⇒ 函数指针垃圾），
   属于"某个虚表/回调没初始化"的表现。

### 35.4 下一轮

1. GDB watchpoint：`0x442DB`（令牌）+ `0x4586C`（smp_monErr）+ 几个 MonCtrl 全局，看**谁写、写了什么**；
2. 若 App 从不调用 `MWF::Initialize`，再决定是"我们调 + 归还令牌"还是"逐叶子初始化"；
3. 判据仍为 `GRM2 > 0` + `/dev/stream` 非均匀。

---

## 36. 第 49 轮：按"逐叶子初始化"补 MWF（合作方方案③）—— 全部调用成功，但 MonCtrl 全局仍是小值

### 36.1 做法

不再调带令牌的 `MWF::Initialize()`，改为按顺序直接调叶子（全部 libMWF 导出）：

```
RsrcMcmd::Init      (+0x2D0CC)   ← 内含 RsrcDP::Initialize（建 smp_monErr）
ObjLog_Mcmd::Init   (+0x209DC)
InhMcmd::Init       (+0x204C8)   ← 内含 InhDP::Initialize（建另外 3+1 个 MonCtrl）
ObjIf_Mcmd::Init    (+0x28AAC)
ObjIf::Init         (+0x26200)
EventIdFactory::Initialize(0x83E8, 0x800)  (+0x1E4A8)
```

### 36.2 实测（`ab_v2490`）

```
LEAF RsrcMcmd::Init ... ok
LEAF ObjLog_Mcmd::Init ... ok
LEAF InhMcmd::Init ... ok
LEAF ObjIf_Mcmd::Init ... ok
LEAF ObjIf::Init ... ok
LEAF EventIdFactory::Initialize ... ok
LEAF done token=0 smp_monErr=0x00027508      ← ★ 仍是"小值"，不是堆指针
崩溃 → libMWF（pc=0x05AEE002，insn=0x4080E8BD = 命中数据区 ⇒ 又一次跳飞）
```

**⇒ 叶子全部执行成功，但 `0x4586C` 里仍是 `0x27508` 这类小值。**
两种可能：
1. `0x4586C` **不是** `RsrcDP::smp_monErr`（合作方给的偏移需要核对）；
2. 它是那个全局，但 `RsrcDP::Initialize` 在 `RsrcMcmd::Init` 里走的是另一分支（例如被某个前置条件挡住）。

### 36.3 下一轮（直接对地址做实验，不再依赖推断）

1. GDB 断在 `SetEventSender` 的 `0x2632E`（`ldr.w r9,[r4,r3]`）：**打印 r4 与 r3，算出真正的全局地址**，
   再 `x` 该地址确认内容 —— 这是唯一可靠的办法；
2. 拿到真地址后，用写 watchpoint 抓写入者；若无人写，就在 preload 里直接调用"能建 MonCtrl 的那个叶子"；
3. 目标不变：让 `SetEventSender` 的日志路径拿到有效 MonCtrl，从而通过这一关，继续向 `pVM/View` 推进。

---

## 37. 第 50 轮：两条路线的现状对比 + pViewManager 依然从未被写

### 37.1 自然路线（不 poke）

`ab_v2490`（叶子初始化版）：
```
LEAF RsrcMcmd/ObjLog_Mcmd/InhMcmd/ObjIf_Mcmd/ObjIf/EventIdFactory 全部 ok
LEAF done token=0 smp_monErr=0x00027508（小值）
崩溃 pc=0x05AEE002（libMWF 数据区，lr=0 ⇒ 调到空/坏指针）
```

### 37.2 poke 路线（Y9 + Application_System_Init，MA 交给内核）

`ab_v2500`（c1820 = menuc1300 去掉 MA 拦截 + GRM shim，基于真 grm_ma + 内核 DMM 模块）：

```
Y9 worker → rm_init=1 → appinit → returned
INITCFG_ENTER tid=188
W5 call init → W5 init returned
之后 IMNANOSLEEP 轮询（无崩溃）
GRM2 init fb=0x472d000（只有 init，没有一次绘制）
PV i=36 pVM=0 inst=0 ida=0
```

⇒ **poke 路线不崩、能跑到 `initializeConfig` 与 `viewUnified2` 的 init，但 pVM 仍为 0、GRM2 无绘制。**

### 37.3 关键否证：`pViewManager` 在这条更靠前的路线上**也没被写**

用 GDB 写 watchpoint 盯 `libObj+0x149221C`（本轮 libObj=0x6184000），poke 路线跑 60 秒：

```
ARMED
（零次写入）
```

⇒ **App 在这两种路线下都不会创建 ViewManager**，所以 `openView` 一定崩（我们早前实测 `address 0x10`）。

### 37.4 下一轮（把 pVM 的真实地址钉死）

我怀疑 `0x149221C` 只是"指向 pVM 的槽"，而不是 pVM 本身（`openView` 里是 `ldr r6,[got]; ldr r9,[r6]` 两级）。

**做法**：让 preload 主动调用一次 `viewManagerIf::openView("view/SCALAR",0)`（必然在 `ldr r9,[r6]` 之后的解引用崩），
用 GDB 断在 `libObj+0x1410BC`（`ldr r9,[r6]` 之后），**打印 r6** —— 那就是真正的 pVM 槽地址；
然后：
1. 对该槽下写 watchpoint，看**谁在正常流程里写它**；
2. 若无人写，就查 `viewManagerIf` 的实现方（应该是 viewUnified2 里的 `ViewManager` 实体）为什么没被实例化。

---

## 38. 第 51 轮：pViewManager 的真实全局地址 = **libObj+0x1459784**（此前一直盯错地址）

### 38.1 怎么算出来的（静态、可复现）

`viewManagerIf` 的实现（libObj+0x1410A4）访问 pVM 的方式是**两段式 GOT 访问**：

```
1410aa: ldr r4,[pc,#196]  → 字面量 @0x141170 = 0x1296B74
1410ac: ldr r3,[pc,#196]  → 字面量 @0x141174 = 0x7698
1410b0: add r4, pc        → r4 = 0x1410B4 + 0x1296B74 = 0x13D7C28  ← libObj 的 GOT 基址
1410b6: ldr r6,[r4,r3]    → r6 = GOT[0x13D7C28+0x7698] = GOT 条目 @0x13DF2C0
1410b8: ldr r9,[r6]       → r9 = pVM
```

再查 `.rel.dyn`：GOT 条目 `0x13DF2C0` 是 `R_ARM_RELATIVE`，其 addend（从文件读）= **0x1459784**
⇒ **pViewManager 全局变量在 `libObj + 0x1459784`（.bss 内）**，
而**不是**我们之前一直盯的 `0x149221C`（那个位置 60 秒零写入也不奇怪）。

### 38.2 用正确地址重测：仍然是 0、仍然无人写

* GDB 写 watchpoint 盯 `libObj+0x1459784`（poke 路线，60s）：**零次写入**；
* guest 内直接读：`PV i=0..36  pVMreal=0  pVMold=0  inst=0`（每 4 秒一次，全程 0）。

⇒ **两种路线下 App 都从不创建 ViewManager**（结论与此前一致，但这次是"对的地址"给出的）。

### 38.3 顺带说明

* `libObj` 的 GOT 基址 = `0x13D7C28`，`pVM` 的 GOT 条目只有一个（0x13DF2C0）；
* 想找"谁写 pVM"，需要在这 99 个"用 GOT 基址"的代码点里逐一定位，成本较高；
  更实际的做法是**先确认 pVM 应该由谁创建**（`viewManagerIf` 没有导出 setter，
  说明实现方在 libObj 内部或 viewUnified2 通过别的通道）。

### 38.4 下一轮候选（需要用户拍板，见回复里的三选一）

1. 继续深挖"谁创建 ViewManager"（最忠实，但不确定还要几轮）；
2. 找一个**更小的 App 自身绘制路径**（例如应用自带的启动画面/logo 绘制，不依赖 ViewManager），
   让"App 自己的代码"先画出第一帧，再回头打通菜单；
3. 走 a7c2 的"受控入口"变体：直接 new 一个 View 对象（`ViewGlobalMenuToInstance`）并调用它的绘制，
   绕过 ViewManager（需人工准备好上下文结构）。

---

## 39. 第 52 轮：★ 找到"App 静默退出"的真因 —— **`libObj` 解析 `MWF::ParamStd_Template<bool,1>` 失败，ld.so 直接以 127 退出**

### 39.1 现场

自然路线（干净探针 + 内核 MA，`ab_v2540`）里 App 不崩、不 oops，但**进程整体退出**：

```
EXIT im.elf.real pid=122 code=32512   (= 127)
EXIT THREAD_INFRA0 / 1 / 2 / mwf_monitor_inp / ACCESSLOG_MAIN  全部 code=32512
```
紧挨着的串口信息：

```
/usr/bin/im.elf.real: symbol lookup error: /usr/lib/libObj.so: undefined symbol:
    _ZN3MWF17ParamStd_TemplateIbLNS_5Param4TypeE1EEC1Eb
```

⇒ **不是崩溃，是动态链接失败导致 ld.so 以 127 终止**（这也解释了之前多轮"App 突然不动了"）。

### 39.2 该符号其实就在我们镜像的 libMWF.so 里

```
libMWF.so dynsym: … ParamStd_TemplateIbLNS_5Param4TypeE1EEC1Eb   (存在)
```

⇒ 是**作用域/加载顺序**问题：`libObj` 绑定该符号时，`libMWF` 还没进全局作用域。

### 39.3 三种"提前把 libMWF 弄进全局"的尝试与结果

| 做法 | 结果 |
|---|---|
| 构造器里直接 `dlopen("//lib/libMWF.so", RTLD_NOW\|RTLD_GLOBAL)` | 崩溃 `pc=0x04011000`（ld.so 内，insn 乱码）|
| 另起线程 sleep 200ms 后 dlopen | 同样崩 `pc=0x04007012` |
| 把 libMWF 放进 `LD_PRELOAD`（`LD_PRELOAD=/lib/libMWF.so /lib/libgrmlog.so`）| 同样崩 `pc=0x04007012` |

反汇编 ld.so 该偏移：

```
7008: ldr.w r0,[r5],#4     ; r0 = 依赖表里的 link_map
7012: ldr   r3,[r0,#20]    ; ★ r0 非法 ⇒ 崩
```

⇒ **崩溃发生在 ld.so 处理依赖映射（`_dl_map_object_deps` 一带）时**，与我们"何时加载 libMWF"关系不大，
更像是**依赖解析本身**出了问题（某个 NEEDED 在我们环境里解析到错误对象/缺失）。

### 39.4 下一轮

1. 用 `LD_DEBUG=libs,reloc` 之类的调试输出（或 `dlopen` 前后打印 `dlerror()`）确认真实的依赖错误；
2. 若确属 NEEDED 缺失：把缺失的库（如 `libmwf_mcore.so`/`libux*.so`）从固件里补齐到镜像；
3. 这一步打通后，`libObj` 才能正常完成绑定，App 才可能继续走到"创建 ViewManager"。

---

## 40. 第 53 轮：★ 修掉 `core_sync_user_init` 缺失（补 `libCoreSync.so`）；App 其实**带 RTLD_GLOBAL 加载 libMWF**

### 40.1 用 `LD_DEBUG=libs` 抓到的第一条致命错误

```
122: /usr/lib/libObj.so: error: symbol lookup error: undefined symbol: core_sync_user_init (fatal)
```

`core_sync_user_init` 由 **libCoreSync.so** 定义（7,540 B，只有 `core_sync_user_init/res/sus` 三个 8 字节桩函数），
而我们镜像里**根本没有这个库**（`/lib` 与 `/usr/lib` 都没有）。

**修法（已完成）**：从固件 dump（`lfu/tree/lib/libCoreSync.so`）把它补进镜像 `/lib/`，并加入 `LD_PRELOAD`。

### 40.2 修掉之后：MWF 模板符号不再是"找不到"，而是继续往后

```
im.elf: symbol lookup error: /usr/lib/libObj.so: undefined symbol:
    _ZN3MWF17ParamStd_TemplateItLNS_5Param4TypeE6EEC1Et
```
再看 dlopen 追踪：

```
DL libMWF.so  mode=257 -> 257        ← ★ 257 = RTLD_LAZY|RTLD_GLOBAL（App 本来就带 GLOBAL）
IMOPEN OK //lib/libMWF.so
```
⇒ **作用域没问题**，是"调用发生在 libMWF 被加载之前"的时序问题；而补上 libCoreSync 后这条链继续往前。

### 40.3 现在的新崩溃：libc 的 `dl_open` 里遍历 link_map

```
崩溃 pc=0x042BA35A → libc-2.11.2.so +0x5935A
59356: ldr.w r1,[sl,#12]
5935a: ldr   r0,[r1,#8]      ; ★ r1 非法
5935c: cmp   sl, r0
```
（典型 `_dl_open` / scope 链表遍历形态。）

### 40.4 下一轮

1. **去掉我们的 preload 再跑一次**（纯固件），看 App 是否也会崩在同一处：
   - 若**不崩** ⇒ 是我们的 preload 扰动了 link_map（那就换成更轻的注入方式，例如只挂必要符号）；
   - 若**同样崩** ⇒ 是固件自身/我们机型环境的 dlopen 问题，需要另找；
2. 继续按"缺哪个库就补哪个"的方式清理（libCoreSync 之后可能还有别的"设备上有、我们镜像缺"的小库）。

---

## 41. 第 54 轮：对照实验 + 把 ld.so 崩溃点定位到 **INIT_ARRAY 处理**

### 41.1 对照实验：纯固件（无任何 preload）反而更早崩

镜像 `ab_v2630`（只有 CoreSync + 普通 wrapper，**不 preload 我们的探针**）：

```
崩溃 pc=0x058AB372 → libMWF+0x32372（MonQueue/Param 一带），r0=0
```
⇒ **没有我们的服务/探针，App 在 libMWF 里就崩了**；我们确实在帮它往前走。

### 41.2 提前 dlopen libMWF（CoreSync 已补齐）仍崩，但位置有意义

```
PE libMWF=0x00016038
崩溃 pc=0x0400A706 (ld-2.11.2.so)
```

反汇编该处：

```
a6f8: stmdb sp!,{r4,r5,r6,r7,r8,r9,sl,lr}
a6fe: ldr r5,[r0,#124]      ; l_info[DT_INIT_ARRAY] 之类
a702: ldr r3,[r0,#56]
a704: ldr r0,[r0,#52]
a706: ldr r5,[r5,#4]        ; ★ r5 非法 ⇒ 崩
```

**⇒ 崩在动态加载器处理某个库的 `DT_INIT_ARRAY`（构造函数表）时**，说明**某个 .so 的 INIT_ARRAY 指针落在了映射之外**。

重点怀疑对象：**我们自己的 `libgrmlog.so`**（用 `-nostdlib -shared` 构建、靠 `__attribute__((constructor))`），
它的动态段/构造表可能与 ld-2.11 的期望不完全一致。

### 41.3 下一轮

1. 检查 `libgrmlog.so` 的动态段（`DT_INIT_ARRAY/DT_INIT_ARRAYSZ/DT_INIT`）是否规范：
   `readelf -d libgrmlog.so | grep -E "INIT"`，并与一个正常库对照；
2. 若确实异常：改成**不用构造器**（把初始化放到第一次被拦截调用时惰性执行），再跑对照；
3. 这一步若通过，libMWF 就能被我们安全地提前加载，`libObj` 的 MWF 模板符号绑定也不会再失败。

---

## 42. 第 55 轮：把 "undefined symbol" 的性质查清（WEAK 定义 vs GLOBAL 引用）+ 确认镜像内库一致

### 42.1 事实核对

| 项 | 结果 |
|---|---|
| 镜像内 `/usr/lib/libObj.so` 与固件 dump 的 libObj | **md5 完全一致**（12cf982d…），没被我们改坏 |
| 镜像内 `/usr/lib/libMWF.so`（247,776 B）是否定义 `ParamStd_Template<u16,6>::ctor` | **定义**，但是 **WEAK** |
| libObj 里同一符号 | **GLOBAL, UND** |
| libObj 的 `DT_NEEDED` | **包含 `libMWF.so`** ✓ |
| rootfs `/lib/libMWF.so` | 是指向 `/usr/lib/libMWF.so` 的**符号链接** ✓ |

⇒ 定义存在、依赖声明也存在，那么运行期报 "undefined symbol" 只剩两种解释：
1. **该次调用发生在 libMWF 尚未被加载的时刻**（时序）；
2. glibc 2.11 在某种 scope 组合下（我们 `LD_PRELOAD` 介入）没把 WEAK 定义纳入查找。

### 42.2 另一个重要线索

任何"我们自己提前把 libMWF 加载进来"的做法（构造器 dlopen / 线程 dlopen / LD_PRELOAD），
都会在 **ld.so 处理 `DT_INIT_ARRAY`** 时崩（`ld.so +0xA706: ldr r5,[r5,#4]`，r5 非法）。

而我们自己的 `libgrmlog.so` 的 INIT_ARRAY 段是规范的（`INIT_ARRAY=0x1ff54, INIT_ARRAYSZ=4`，与 libCoreSync 同形态）。

### 42.3 下一轮（精确的 4 组对照）

1. **只用 LD_PRELOAD 加载 libMWF（不带我们的探针）** → 验证 ld.so 的 INIT_ARRAY 崩溃是否由"预加载 libMWF 本身"引起；
2. 只用我们的探针（不预加载 libMWF）→ 已知会报 MWF 符号 UND；
3. 探针 + 在拦截 `dlopen("libObj.so")` 时**先同步加载 libMWF 再放行**（顺序化，避免嵌套）；
4. 若 3 仍不通，改用 **`/etc/ld.so.preload`**（rootfs 里本来就有这个文件）注入，排除 `LD_PRELOAD` 次序因素。

---

## 43. 第 56 轮：★ 定位并修掉"ld.so 在 INIT_ARRAY 崩" —— **是我们的构造器在建线程**

### 43.1 四组对照（本轮跑完三组）

| 组合 | 结果 |
|---|---|
| A. 纯固件（无 preload）| 崩在 libMWF+0x32372（MonQueue，this=NULL）|
| B. `LD_PRELOAD=libCoreSync + libMWF`（**不含我们的探针**）| 同样崩在 libMWF+0x32372 ⇒ **预加载 libMWF 本身不会搞坏 loader** |
| C. 上面两者 + 我们的探针（**构造器里 `pthread_create`**）| 崩在 **ld.so +0xA706（处理 DT_INIT_ARRAY）** |
| D. C 但**去掉构造器里的线程** | **ld.so 崩溃消失**；新崩溃 `pc=0x00000000`（跳空指针），且**不再出现 MWF 符号 UND 报错** |

**⇒ 结论：ld.so 的 INIT_ARRAY 崩溃是我们自己的构造器在动态加载器初始化阶段 `pthread_create` 造成的**（glibc 2.11 在 init 阶段建线程不安全）。去掉后，**MWF 符号绑定也正常了**（因为 libMWF 已在全局作用域）。

### 43.2 现在的位置

```
FAKEDMM req/reply ok
Mon Buf Size ok
（无 symbol lookup error ✓）
崩溃 pc=0x00000000 —— 调到空指针（执行地址 0）
```

⇒ 加载器层面的问题**已清空**；现在回到"App 初始化流程里某个函数指针是 NULL"。

### 43.3 下一轮

1. 用 GDB 在 `pc=0` 前抓最后一条 `blx/bx`（或看 `ZZEXCSTACK` 的 lr）定位**是谁跳到了 0**；
2. 若 lr 指向某个"回调注册"位置 ⇒ 说明我们少提供了一条回调（按 a7c2 方式补真实实现，而不是塞桩）；
3. 若 lr 指向 MWF/ObjIf 的虚表 ⇒ 说明某个对象未构造（继续按"补原厂初始化"处理）。

### 43.4 纪律修正（重要）

**探针的构造器里永远不要建线程**（本项目所有 preload 探针都要遵守）。
线程一律改为"首次被拦截调用时惰性启动"。

---

## 44. 第 57 轮：空桩对照 + `LD_DEBUG=bindings` 时间线（App 其实已跑很远）

### 44.1 空桩对照（证明"是我们的 hook 在帮忙"）

把探针换成一个**只有 `int __stub_symbol(void)` 的空 .so**（其余 preload 不变）：

```
崩溃 pc=0x04065372 = libMWF+0x32372（MonQueue，r0=0）
```
⇒ 与"完全不 preload"的 A 组一样早期崩。
**⇒ 是我们的拦截/服务把 App 推到了后面**，不是 preload 本身的问题。

### 44.2 `LD_DEBUG=bindings` 跑出来的时间线（本轮最有用）

镜像 `ab_v2690`（CoreSync + libMWF + 探针，全部 preload，`LD_DEBUG=bindings`）：

```
3371 行:  calling init: /lib/libCoreSync.so            ← 我们补的库确实加载并初始化了
15187 行: /usr/lib/libObj.so: error: symbol lookup error:
          undefined symbol: core_sync_user_init (fatal)
16517 行: 崩溃 pc=0x00000000
```

并且日志里总共 **16,892 行** 绑定记录 —— App 在崩溃前已经把
`libBusPower.so / libUsbControl.so / libjsoncpp.so.23 / libMWF.so` 等大量库绑完
（最后几条是 `libBusPower → libMWF` 的 `ObjMsg::GetParam / ParamStd_Template<u32,8>::GetValue`）。

### 44.3 新问题：`core_sync_user_init` 明明已加载却仍报 undefined

* `libCoreSync.so` 已 `calling init`（说明加载成功）；
* libObj 里它是 `GLOBAL UND`，libCoreSync 里是 `GLOBAL DEFAULT` 定义；
* 但懒绑定到该符号时仍然 fatal。

**待查方向**：
1. 可能存在**第二个 im.elf 进程**（App 自己 re-exec），而它**没有继承 LD_PRELOAD** ⇒ 那个进程里没有 libCoreSync；
2. 或该 UND 带了**版本需求**（verdef），与我们这份 libCoreSync 的版本节点不匹配；
3. 或 `libObj` 是以 RTLD_LOCAL 加载的、scope 里看不到 preload 的库（需要抓 `dlopen` 的 mode 与当时的 scope）。

### 44.4 下一轮

1. 打印 `/proc/*/comm` + `cmdline`，确认是否存在**多个 im.elf**（re-exec 假设）；
2. 用 `LD_DEBUG=scopes` 或 `dlopen` 记录（mode + 调用者）验证 scope；
3. 若确认 re-exec：让 wrapper 在 re-exec 后仍带 preload（例如用 `/etc/ld.so.preload`，它会被所有进程继承）。

---

## 45. 第 58 轮：否证 re-exec 假说；`core_sync_user_init` 其实来自 **libIMDB.so**

### 45.1 否证 re-exec

`ab_v2690`（`LD_DEBUG=bindings`）日志里：
* 我们的探针只打印了 **1 次** `OBS0 clean probe up`；
* 所有 `LD_DEBUG` 行的 pid 都是 **122**。

⇒ **全进程只有一个 im.elf，没有 re-exec**，之前的"第二个进程丢 LD_PRELOAD"假说不成立。

### 45.2 把三个库都塞进 `/etc/ld.so.preload` 反而更差

镜像 `ab_v2700`（`/etc/ld.so.preload` = CoreSync + libMWF + 探针）：

```
崩溃 pc=0x058AB372 = libMWF+0x32372（MonQueue，this=NULL）—— 退回到"无服务"那一档
```
⇒ **把 libMWF 预加载进所有进程会打乱 App 自身时序**（sen.elf 等也会带着 MWF 起来）。
这个方向不可取；libMWF 只应给 im.elf 用（`LD_PRELOAD`）。

### 45.3 `core_sync_user_init` 的真实来源

全固件树里只有两个库涉及它：

```
lib/libCoreSync.so   ← 定义（GLOBAL DEFAULT）
lib/libIMDB.so       ← UND 引用
```

**libObj 里连这个字符串都没有** ⇒ 运行期那句
`/usr/lib/libObj.so: error: symbol lookup error: undefined symbol: core_sync_user_init (fatal)`
**把责任方写成了 libObj，但真正需要它的是 libIMDB**（glibc 报的是当时正在做重定位的对象）。

而且这条错误**没有杀死进程**（日志后面继续有绑定记录与 QMMAP）⇒ App 捕获后继续跑，
最终以 `pc=0`（调用空指针）崩掉。

### 45.4 下一轮

1. **只把 `libCoreSync.so` 放进 `/etc/ld.so.preload`**（微型库、无副作用），
   libMWF 与探针继续留在 im.elf 的 `LD_PRELOAD` 里 —— 这样 libIMDB（任何进程）都能解析到它；
2. 再看 `pc=0` 是否消失；若消失，App 应能继续往 `Application_System_Init` 方向走；
3. 判据不变：`GRM2 > 0` + `/dev/stream` 非均匀。

---

## 46. 第 59 轮：`LD_DEBUG=symbols` 运行 70 秒未崩（28k 次查找），但 trace 里没有 core_sync 查找

### 46.1 本轮三次运行的结果

| 配置 | 结果 |
|---|---|
| `ld.so.preload = CoreSync + 探针`，`LD_PRELOAD = libMWF` | **退化**：崩在 `libMWF+0x32372`（MonQueue）——把 CoreSync 挪进 preload 文件反而更糟 |
| `ld.so.preload = 探针`，`LD_PRELOAD = CoreSync + libMWF + 探针`（bind2 配置） | 跑 80s、16.9k 条绑定日志，最后 `pc=0` |
| 同上但 `LD_DEBUG=symbols` | **70 秒未崩**，28,367 行查找日志；超时时仍在解析（最后是 `memset` 的查找） |

### 46.2 关键观察

* `LD_DEBUG=symbols` 大幅拖慢启动 ⇒ 那次 `pc=0` 很可能是**时序相关**的问题，不是纯逻辑错误；
* symbols trace 里**完全没有 `core_sync` 的查找记录** ⇒ bind2 里那条 "undefined symbol: core_sync_user_init" 出现在
  **另一个更早/不同的加载路径**（很可能是 `dlopen(libIMDB, RTLD_NOW)` 的失败，被 App 捕获后继续）；
* 目前**能跑最久的配置**是 bind2：`ld.so.preload=探针`，`LD_PRELOAD=CoreSync+libMWF+探针`。

### 46.3 下一轮

1. 用 bind2 配置**不带任何 LD_DEBUG** 跑 150 秒，看是否真的能稳定跑下去、并且是否出现 `INITCFG / GRM2`；
2. 若稳定：检查 `pVM`（libObj+0x1459784）与 `Application_System_Init` 是否被调用；
3. 若仍 `pc=0`：用 GDB 在 `pack` 崩溃前抓 `_dl_runtime_resolve` 的 `sym` 参数，直接读出缺失符号名。

---

## 47. 第 60 轮：GDB 定位 —— `pc=0` 发生在 **ld.so 调用的 libc 函数内部**

### 47.1 断点结果（`ld.so + 0xDFB0`，基址固定 0x04000000）

```
HIT LDS_CALL  pc=0400dfb0 lr=040d8b51 r0=0402050c r1=0x80000101 r3=040eaba5
HIT LDS_AFTER pc=0400dfb2 lr=040eac3b r0=00000000     r1=00000001     r3=00000001
（连续 6 次都是同样调用，r3 一直是 0x040eaba5，有效）
```

⇒ `ld.so` 在 0xDFB0 `blx r3`（r3 = libc 里的函数 0x040EABA5）**是正常调用**，
而不是空指针；崩溃时的 `lr=0x0400DFB3` 正好是这条 `blx` 的返回地址。

**⇒ `pc=0` 的现场在"被调用的 libc 函数内部"跳了空指针**，即 App/libc 的某条回调链断在 NULL。

### 47.2 判断

到这里，"加载器/符号可见性/缺失库"这一类问题**已经基本清完**；剩下的是
**App 内部（libc/MWF/BizFw 一带）某个回调槽为 NULL 被调用**。

### 47.3 策略调整（重要）

继续在"让 App 自然启动"上追这些 NULL 回调，代价高且不可控。
**按 a7c2 的"受控入口"经验，转向 C 路线**：

> 不依赖 App 的完整启动流程，**直接调用 viewUnified2 导出的视图工厂**
> `ViewGlobalMenuToInstance`（viewUnified2+0x2E4C0F，dynsym GLOBAL），
> 让它创建"菜单视图对象"，再由 `libgrm2.so` 把它的绘制光栅化到 streamfb 镜像区。
> 这是"App 自己的视图对象在画"，不是我们手绘 UI。

先决条件（已具备）：`libgrm2` shim、显示链路、viewUnified2 可在 preload 里 dlopen。

下一轮开始做这个实验：先以 `ViewGlobalMenuToInstance(0)` 试探，观察：
1. 是否返回非空对象；
2. 是否触发 GRM 绘制（`GRM2 > 0`）；
3. `/dev/stream` 是否从均匀 `0x5A5A` 变非均匀。

---

## 48. 第 61 轮：受控入口（C 路线）第一次尝试 —— 被 App 自身的加载期崩溃打断

### 48.1 做法

在探针里第一个 `osal_snd_sync_msg` 被调用时**惰性起线程**（遵守"构造器不建线程"的纪律），
线程等 8 秒后创建 App 自己的菜单视图对象：

* 版本 1：`dlopen("//lib/viewUnified2.so", RTLD_NOW|RTLD_GLOBAL)` → `ViewGlobalMenuToInstance(0)`
* 版本 2（避免 dlopen）：从 `/proc/self/maps` 取 viewUnified2 基址 → 直接调 `base+0x2E4C0E|1`

### 48.2 结果

| 版本 | 结果 |
|---|---|
| v1（dlopen） | `CE starting` 后 **崩在 ld.so+0x7012**（依赖映射）——在 App 活动期间 dlopen 又踩到 glibc 2.11 的加载竞态 |
| v2（maps 直调） | `CE starting` 后 **崩在 ld.so+0xA706**，**CE 自己的日志（base/sym/ret）一条都没出来** |

⇒ **这轮没法评价"受控入口"本身**：App 在这个配置下先死在加载期（且看起来是竞态，两次跑崩点还不一样）。

### 48.3 结论与下一轮

* 直接调 `ViewGlobalMenuToInstance` 的思路仍然成立，但要挑一个**App 稳定活着**的环境去试；
* 已知最稳定的环境是 **poke 路线**（Y9 + `Application_System_Init`，跑到 `INITCFG` 且**加载了 viewUnified2**、不崩）；
* **下一轮：把 CE 挂到 poke 路线上** —— 等 viewUnified2 加载完成、`W5 init returned` 之后，
  再调用 `ViewGlobalMenuToInstance`，这样相机不会在加载期被打断。

---

## 49. 第 62 轮：★★ 里程碑 —— 在 poke 路线上**成功创建了 App 自己的菜单视图对象**

### 49.1 做法

在 poke 路线（Y9 + `Application_System_Init`）里，等 `viewUnified2` 的 init 跑完（`W5 init returned`）之后，
**直接调用视图工厂**（不走 dlopen，用 maps 里解析到的 `rx` 作为 viewUnified2 基址）：

```c
void *(*vmf)(void*) = (void*(*)(void*))(rx + 0x2E4C0E | 1);   /* ViewGlobalMenuToInstance */
void *view = vmf(0);
```

### 49.2 实测（镜像 `ab_v2750`，跑 94 秒、42,624 行串口）

```
INITCFG_ENTER tid=188
W5 call init
W5 init returned
CE2 sym=0x0F125C0F
CE2 ret=0x00100F70        ← ★★★ 返回了非空对象！
```

**没有任何 oops / ZZINSN / Exception**（这次跑满 94 秒）。

**⇒ 这是第一次真的把"App 自己的菜单视图对象"创建出来**（原厂 `viewUnified2` 的工厂函数创建的，
不是我们手搭的 UI 结构）。

### 49.3 还差什么（判据仍未满足）

```
GRM2 = 0（只有 init 那一条）→ 视图对象存在，但还没让它进入绘制流程
/dev/stream 仍均匀 0x5A5A
```

⇒ 下一步要让这个视图**执行它自己的绘制/更新**：需要读它的 vtable，
找到 `draw / update / show / activate` 之类的虚函数并调用（原厂代码在画，我们只负责触发与光栅化）。

### 49.4 下一轮

1. 在 probe 里打印 `view` 的 vtable（`*(u32*)view` → 前 16 个条目）；
2. 依次小心调用疑似 `draw/update` 的条目，观察 `GRM2` 是否增长；
3. 一旦 `GRM2 > 0`，检查 `/dev/stream` 是否从均匀 `0x5A5A` 变非均匀（这才是"真正显示"的判据）。

---

## 50. 第 63 轮：视图对象的 vtable 拿到了；但裸调用虚函数会崩（缺父上下文）

### 50.1 vtable dump（`ab_v2760`，viewUnified2 基址 rx）

```
CE2 ret=0x00100FE8        ← 视图对象
CE2 vt =0x0F9EDCC0
v0..v6  = 0x0619B401 / 0x0619B333 / 0x0619DC81 / 0x0619B5E1 / 0x0619B64D / 0x0619B3A9 / 0x0619E567
          （都在 0x0619xxxx = libObj 映射内 ⇒ 基类虚函数）
v7      = viewUnified2+0x18EFA8
v8      = viewUnified2+0x2E2870
v9      = viewUnified2+0x2E28A4
v10/v11 = libObj 内
```

### 50.2 依次调用 v0..v11 的结果

```
CE2 callstart
CE2 call0
[Exception] pc=0x069058BC insn=b5806840（push {r7,lr}; ldr r0,[r0,#4]）
```

⇒ **`v0(obj)` 立刻空指针崩**。说明 `ViewGlobalMenuToInstance(0)` 造出来的对象**只完成了内存分配/部分构造**，
缺少"父上下文 / 管理器"参数（我们传的 r1=0），所以任何虚函数一用就崩。

### 50.3 结论

* 工厂确实能造对象（上一轮的里程碑成立）；
* 但**要让它真正跑起来，必须给出正确的父上下文**——这正是 a7c2 里"用原厂 manager 入口"而不是直接 new 的原因。

### 50.4 下一轮

1. **找 `ViewGlobalMenuToInstance` 在固件里的真实调用点**（`bl` 到 0x2E4C0E 的位置），
   看它传的 `r1` 是什么（父视图？场景管理器？），照抄那个上下文；
2. 若调用点在生产代码里、且上游是 ViewManager，则说明**必须先有 ViewManager**——那就回到 pVM 那条线；
3. 备选：用 vtable 里 `v8/v9`（viewUnified2+0x2E2870/0x2E28A4，紧邻菜单工厂）作为"轻量方法"先试，
   这两个看起来是"设置/显示"类小函数，可能不像 v0 那样依赖完整上下文。

---

## 51. 第 64 轮：★ 证实"视图工厂的 r1 就是 ViewManager*" —— 两条路线正式收敛

### 51.1 工厂与构造函数

```
ViewGlobalMenuToInstance(r1):
  2e4c10: new(0x16C)
  2e4c16: r5 = r1
  2e4c20: bl 0x2E4958            ; ctor(this, r1)
  2e4c24: return this

ctor(this, r1):                   ; 0x2E4958
  2e4960: blx ViewBaseProduct::ViewBaseProduct(ViewManager*)   ← ★ r1 的类型是 ViewManager*
  2e497a: blx View::setModelRequest(bool)
```

⇒ **我们传的 `r1=0` 就是"没有 ViewManager"**，所以对象只构造了一半，一调虚函数就空指针崩。

### 51.2 ViewManager 的唯一持有者是 libObj

* `pViewManager` 是 **libObj 的导出对象**：`libObj+0x149221C`（所以最初那个地址其实是对的）；
* 全库只有 libObj 定义它，**没有任何外部库引用**；
* `viewUnified2` 只用不造：它的 UND 里全是 `ViewBase/ViewBaseProduct(..., ViewManager*)`、`viewManagerIf::*`；
* 在 poke 路线上再盯 `pViewManager` 60 秒：**仍然零次写入**。

⇒ **ViewManager 只能由 libObj 的 App 框架启动链创建**，也就是我们一直没能真正跑起来的 `Application_System_Init` 那条路。

### 51.3 结论

"受控入口造视图"和"等框架自然启动"**两条路线在这里正式合流**：
**没有 ViewManager，就没有任何可用的视图对象**。

### 51.4 下一轮（关键实验：把 `Application_System_Init` 放到**主线程**上调）

之前失败的原因是"在侧线程里调"⇒ `pc=0`。
新方案：**拦截 `osal_snd_sync_msg`，在它第一次被调用时（此时正处于 App 自己的线程上下文里）**
直接调用 `Application_System_Init(0)` —— 相当于在该线程内联完成框架启动，而不是另起线程。
观察：`pViewManager` 是否被写入、是否出现 `viewUnified2` 加载、`GRM2` 是否变化。

---

## 52. 第 65 轮：`Application_System_Init` **内联**调用成功（不崩），但 pVM 仍为 0 —— 因为没人消费它投的事件

### 52.1 做法（修正了上一轮）

在 `osal_snd_sync_msg` 的**第一次调用**里，先等 `libObj` 已加载且 `Application_System_Init` 符号可解析，
再**就地（App 自己的线程上下文里）**调用它，并加 `in_api` 重入保护。

### 52.2 实测（`ab_v2810`，46 秒，无崩溃）

```
API inline: base=0x06063000  pVM_before=0x00000000  sym=0x06905409
API inline returned=0x00000004
API inline: pVM_after=0x00000000
```

* **调用本身是安全的**（内联、在 App 线程里，没有 `pc=0`）；
* 返回值 `4`（不是错误崩溃），但 **`pViewManager` 依然是 0**；
* 之后也没有 `viewUnified2` 加载 / `INITCFG` / 绘制。

### 52.3 原因（与之前反汇编一致）

`Application_System_Init` 只做两件事：`snprintf` 一个名字 → **`createRequestModelExecuteEvent(0, buf)`（投事件）** → 写名字缓冲区。
**它不会自己处理该事件**；处理方是 App 的事件泵/`ObjThread`——而我们环境里那个泵从来没起来。

### 52.4 下一轮

1. 在 `Application_System_Init` 之后**紧接着启动事件泵**：调 `MWF::Initialize()`（它会 `new ObjThread` + `StartThread`）
   或直接调 `MWF::ObjThread::ThreadInit`；
2. 观察三件事：`pViewManager` 是否被写入、`viewUnified2` 是否加载、`GRM2` 是否变化；
3. 若 `MWF::Initialize` 又触发早前的崩溃，则改为**只创建并启动 ObjThread**（更小的一步）。

---

## 53. 第 66 轮：延迟调 `MWF_Init` 没能执行（App 之后几乎不再发同步消息）

### 53.1 做法

在 `Application_System_Init` 内联调用之后，**延迟 50 次同步发送**再调 `MWF_Init`（dlsym 自 libMWF），
避免与 `Application_System_Init` 同栈。

### 53.2 实测（`ab_v2820`）

```
 Mon Buf Size  : 00008000          ← MonDrv::Init 在更早的时候自然跑过
API inline: base=0x06063000 pVM_before=0 pVM_after=0
（★ MWFB 一行都没出现 —— s2_count 从未达到 50）
```

⇒ **App 在这之后几乎不再调用 `osal_snd_sync_msg`**（它很快就空了），所以延迟触发根本没机会执行。

### 53.3 现状判断（本轮的核心结论）

* App 的主线程在 **libc 的等待原语**里（此前 census 已证），进程只有很少的线程；
* 它**不会自己往下走**：既不创建 ViewManager，也不启动事件泵；
* 因此"我们推它一把"的做法，都卡在**它自己不再发起任何动作**这一点上。

**⇒ 下一步必须换成"我们主动把事件塞进它的队列"**，而不是等它有动作时顺带触发。

### 53.4 下一轮

1. 拦 `osal_rcv_msg_tmo` / `osal_wai_sem_tmo`，记录**主线程**在等什么（qid / 信号量）；
2. 针对那个等待点，**构造一条符合 App 期望格式的事件消息**投进去（类似 backup 服务那样，真实投递而不是伪造返回值）；
3. 目标：让 App 自己从"空转"进入"处理事件 → 创建 ViewManager → 开视图"。

---

## 54. 第 67 轮：事件泵线程体 0x8A2EE0 需要的是**对象上下文**，不是线程参数

### 54.1 做法与结果

在 `Application_System_Init` 内联调用后，用 `pthread_create(0x8A2EE0|1, arg=0)` 起"事件泵线程"：

```
API inline returned=0x00000004
API pump body=0x06905EE1
API pump create=0 tid=0x093E2460        ← 线程创建成功
[Exception] pc=0x06905EEE insn=46046283 ← 立刻崩
```

崩溃点 = body+0xE，正好是：

```
8a2eee: str r3,[r0,#40]     ; r0 当"对象指针"用
8a2ef0: mov r4, r0
8a2ef2: str r6,[r0,#36]
```

⇒ **`0x8A2EE0` 不是线程入口，而是某个 manager 对象的成员函数**（`this` 在 r0）。
我们传 `arg=0` ⇒ 第一条存储就空指针崩。

### 54.2 结论

* “起个泵线程”这条路走不通：那个函数需要**App 自己的 manager 对象**（该对象同样来自 App 的启动链）；
* 我们已连续遇到同一个模式：**所有入口都需要 App 自己构造出来的对象图**（ViewManager、manager、pump 的 this…），
  而对象图只能由 App 的启动流程建立 —— 而那条流程在我们的环境里始终没有真正跑起来。

### 54.3 下一轮（换思路：找"最早的中断点"）

不再从下游往上补，而是回到**最上游**：
1. 用 GDB 在 App 主线程上采样，看它反复执行的那几行（"空转循环"的具体地址）；
2. 顺着它**往上一层层找**是谁把它放进这个循环的（返回栈里的调用者）；
3. 找到"它本应去做但没做的那一步"（例如某个 `if (状态) 跳过` 的分支），再看那个状态为何不满足。

---

## 55. 第 68 轮：in-guest census 被加载期崩溃打断；确认"进程内 preload"是最大的不稳定源

### 55.1 本轮尝试

在 `osal_snd_sync_msg` 首次调用时惰性起线程，10 秒后做**线程普查 + 主线程用户栈回溯**
（读 `/proc/self/task/*/stat` 取 sp/pc，再扫 sp 附近的 libObj/viewUnified2 返回地址）。

### 55.2 结果：没跑到

```
[Exception] pc=0x0400A706 (ld-2.11.2.so) insn=685b686d
（CEN 一行都没有）
```

**又是加载期崩溃**（`ld.so` 处理 `DT_INIT_ARRAY`/依赖映射那一带），和之前多次遇到的一样。

### 55.3 结论（重要，改变架构判断）

* 加载期崩溃**不是 App 自己的问题**：A 组（纯固件）不崩在此处，B 组（只 preload libMWF）也不崩；
  一旦**把我们的探针放进 App 进程**，就时不时在这里崩；
* 我们的探针同时在做很多事（拦截 osal/dlopen/初始化函数、起线程、写串口），
  **在 glibc 2.11 的加载期环境中过于"重"**；
* 而 App 需要的服务（backup / 日志 / MA）其实**都可以由别的进程提供**（UIPC 队列是全局的）。

### 55.4 下一轮：架构调整 —— 服务移到**独立进程**

1. 写一个独立的 helper 程序 `/usr/bin/grmsvc`：注册 `0x3E014D`(backup) 等队列并真实应答，
   自己跑事件循环；
2. wrapper 先启动 helper，再 exec **完全干净的 im.elf**（**不带任何 LD_PRELOAD**）；
3. 这样 App 进程的加载器是"原厂原样"，加载期崩溃应消失；服务能力不损失；
4. 之后再做"受控入口"（那时需要在进程内做，可以用最小注入：只挂一两个符号）。

这可能是本轮之后收益最大的一步：**先把 App 进程还给原厂，再谈注入**。

---

## 56. 第 69 轮：★ 极简 preload（只应答 backup）→ **加载期崩溃消失，App 稳定运行**

### 56.1 做法

新写一个 **极简 shim**（`grmslim.c`，~70 行）：

* **没有构造器、没有线程、没有其它拦截**；
* 只实现 `osal_snd_sync_msg`：`qid==0x003E014D` 用内置 key 表真实应答，其余原样转发；
* 控制台 fd 惰性打开。

### 56.2 实测（`ab_v2890`，43 秒）

```
BS n=1 cnt=3 w0=0x0000003e w1=0x003e000b
BS n=2 cnt=3 w0=0x00000094 w1=0x00940003
…（backup 请求逐条真实应答）
Exception/ZZINSN 计数 = 0        ← ★ 没有任何加载期崩溃
Mon Buf Size : 00008000           ← MonDrv::Init 正常
```

⇒ **此前反复出现的 `ld.so +0xA706 / +0x7012` 崩溃，确实是我们"重型 preload"造成的**；
极简 shim 下 App 进程稳定。

### 56.3 进一步（本轮第二版）

在极简 shim 上加了两样东西：
1. 前 20 次同步发送打印 `qid + w0`（用于看清 App 依赖哪些队列）；
2. 惰性调用 `Application_System_Init`（需 libObj 已映射且符号可解析）。

实测：App 正常跑（`SQ qid=0x008f013a/0x003e014d/0x0094016a` 都在被处理），**无崩溃**；
但 `API` 行没打印 ⇒ `dlsym(RTLD_DEFAULT,"Application_System_Init")` 取不到（libObj 是 dlopen 进来的，不在全局作用域）。

### 56.4 下一轮

1. 在极简 shim 里**用 `dlopen("//lib/libObj.so")` 拿句柄再 dlsym**（极简 shim 已证明不会破坏加载器）；
2. 拿到后再做"内联 `Application_System_Init` + 观察 `pViewManager`"，这次环境是干净的；
3. 若 pVM 仍未建立，就在**这条稳定环境**里重做 vtable/事件泵实验（之前都被加载期崩溃打断，做不了）。

---

## 57. 第 70 轮：在**稳定环境**里重做内联初始化（dlopen 句柄解析成功），并尝试线程普查

### 57.1 内联 `Application_System_Init`（句柄解析版）

极简 shim 里用 `dlopen("//lib/libObj.so", 0)` 拿句柄再 `dlsym`：

```
API base=0x06084000 pVM0=0x00000000
API ret=0x00000004
API pVM1=0x00000000
```

* 符号解析这次成功了（`RTLD_DEFAULT` 拿不到，句柄可以）；
* 调用安全（无崩溃），但 `pViewManager` 依旧为 0 —— 与前面结论一致：它只投事件。

### 57.2 线程普查（在稳定环境里的第一次尝试）

在极简 shim 的 `osal_snd_sync_msg` 第 20 次调用时起一个线程，12 秒后读
`/proc/self/task/*/stat`（取 sp/pc）并扫栈找 libObj/viewUnified2 返回地址。

结果：`CEN started` 打印了，但**没有后续输出**（无 CEN go / 无逐线程行，也无异常）。
⇒ 普查线程本身在某处卡住（最可能是 `op("/proc/self/task",0)` + `getdents` 这条路径），下一轮需换成
"遍历 `/proc/self/task/<tid>` 目录用 stat 直接试 tid 1..N" 的笨办法，或直接用 `/proc/self/stat` + 逐个已知 tid。

### 57.3 本轮的意义

**我们终于有了稳定的注入环境**（极简 shim + 无加载期崩溃），
后面所有实验（普查/受控入口/事件投递）都可以在这个基线上反复做，不必再和加载器搏斗。

### 57.4 下一轮

1. 修普查线程（换 tid 枚举方式），拿到 App 主线程的调用链；
2. 在同一稳定环境里，按调用链找到"被跳过的那一步"，再决定补什么。

---

## 58. 第 71 轮：★ in-guest 线程普查**终于跑通**（带库名解析），拿到 App 真实形态

### 58.1 修复过程（三个坑）

1. 起线程做普查 → 线程自己卡住（`/proc/self/task` 的 `getdents` 不稳）⇒ **改为在 `bk_service` 里内联执行**；
2. `/proc/<tid>/stat` 的字段解析错位（comm 里可能含空格）⇒ **改为从最后一个 `)` 之后开始数字段**；
3. 只有 PC 没有库名 ⇒ **加 `name_of(pc)` 读 `/proc/self/maps` 反查所属库**。

### 58.2 实测（`ab_v2990`，第 6 / 第 12 次 backup 请求时各普查一次）

```
CEN inline start n=6
CEN tid=122 sp=0x5efffa80 pc=0x0407b5c4   (libgrmlog.so = 我们自己的 shim 内，即调用者线程)
CEN tid=127 sp=0x04cb7d84 pc=0x0435b17c   (libc-2.11.2.so = futex 等待)
CEN tid=128 …（同 127）
CEN tid=129 …（同 127）
CEN inline end

CEN inline start n=12
CEN obj=0x00000000 view=0x00000000          ← ★ 此刻 libObj 与 viewUnified2 都还没加载
CEN tid=122 sp=0x5efffb20 pc=0x04108146     (libpthread-2.11.2.so) ← 主线程在 pthread 等待
CEN tid=127/128/129 pc=0x0435b17c (libc)    ← 三个工作线程 futex 等待
CEN tid=131 pc=0x0407b5c4 (libgrmlog)       ← 另一个调用我们的线程
```

### 58.3 结论（首次有实测支撑的"App 形态"）

* **全进程只有 5 个线程**（122 主 + 127/128/129 + 131）；
* 主线程**停在 libpthread 的等待**里，三个工作线程停在 **libc futex**；
* **第 12 次 backup 请求时，libObj / viewUnified2 都还没被加载** ⇒ App 还停在"极早期"；
* 这与"只创建 2 个 grm worker、ViewManager 从未创建"的旧结论一致，但现在**有了线程级实证**。

### 58.4 下一轮

1. 把普查点后移（例如第 40/80 次 backup，或等 `libObj` 已加载后），看**拿到 libObj 之后**主线程停在哪；
2. 顺主线程的 libpthread 等待点，找出它在等哪个对象/谁该来 signal；
3. 那个 signal 的缺失，就是"框架没启动"的直接原因。

---

## 59. 第 72 轮：★ 普查工具真正可用 —— 拿到 App **后期**（10 个线程）的实况

### 59.1 关键修复

普查线程之前"起来了但没输出"：**`pthread_create` 成功（`pr=0`）但 body 里 `sleep` 的方式有问题**。
改成"线程入口先打印，再用 `nanosleep(200ms)×10`"后一切正常：

```
CEN2 start
CEN2 created pr=0 tid=0x04cb8460
CEN2 bodyenter
CEN2 go
```

### 59.2 后期实况（普查在 App 已跑一段时间后触发）

```
CEN tid=122 sp=0x5efff6c0 pc=0x04008f30
CEN tid=127 sp=0x04cb7d00 pc=0x0407b5c8
CEN tid=128/129/130/132/134/135/136  pc=0x0435b17c (libc)
CEN2 end
```

* **线程数从 5 增到 10**（122 主 + 127..136）⇒ App 确实在继续创建线程；
* 主线程 `pc=0x04008f30`、tid=127 `pc=0x0407b5c8`（正是我们 shim 的地址，说明它在调用我们）；
* 其余 7 个线程都在 `libc`（`0x0435B17C`，futex/等待）。

### 59.3 顺带发现的内核 BUG（新线索）

上一轮（更长的运行）末尾出现：

```
<2>kernel BUG at kernel/rtmutex.c:738!
```

对应我们内核源码：

```c
static void noinline __sched rt_spin_lock_slowlock(struct rt_mutex *lock) {
    ...
    BUG_ON(rt_mutex_owner(lock) == self);     /* ← 738 行 */
```

即 **RT 自旋锁被同一个任务重入**。结合之前 `__migrate_enable` 的 WARN，
说明厂商 .ko 的 RT 语义假设与我们内核仍不一致（**这是内核侧待解问题，不是 App 侧**）。

### 59.4 下一轮

1. 把普查加密（每 200ms 一次、连续 20 次），看**线程数随时间的变化曲线**，找出"卡在哪一步不再增长"；
2. 用 `name_of(pc)` 给所有 tid 标注库名，得到"谁在等、谁在跑"的完整图；
3. 同时记录 `kernel BUG` 是否稳定复现（若是，需回到内核 RT 配置对齐）。

---

## 60. 第 73 轮：普查工具的两个使用约束（已固化）+ 后期线程图稳定在 8 个

### 60.1 约束一：**不能在"计数到 N 次"时用线程做普查**

`/proc/self/task/<tid>/stat` 在**刚创建的线程**里读不到（大量 `IMOPEN FAIL ... err=-2`），
普查结果全是 0。

### 60.2 约束二：**内联普查（在 `bk_service` 里直接做）是可靠的**

`ab_v3050`（第 40 次 backup 请求时内联普查）：

```
CEN tid=122 sp=0x5efff5c0 pc=0x0407b5c4        ← 主线程：正在调用我们的 shim（bakcup 处理中）
CEN tid=127 sp=0x04cb7d84 pc=0x0435b17c        ← libc 等待
CEN tid=128/129/131/133/134 pc=0x0435b17c      ← libc 等待（共 6 个）
CEN tid=135 sp=0x05b89d58 pc=0x04106bb4        ← libpthread
```

**⇒ 后期稳定形态：8 个线程**（122 主 + 127/128/129/131/133/134 + 135），
除主线程与我们 shim 交互外，其余全在 **libc futex / libpthread 等待**里。

### 60.3 与目标的距离（诚实评估）

* 线程数在 8 附近稳定 ⇒ App 的"框架线程图"已建立一部分（比早期 2 个 worker 多）；
* 但 `pViewManager` 仍为 0、`GRM2 = 0`、`/dev/stream` 仍均匀 `0x5A5A`
  ⇒ **仍没有任何绘制发生**；
* 卡点性质没变：**框架的对象图没有被建起来**，而 App 的线程都在等待（不是死锁，是没有事件驱动它们继续）。

### 60.4 下一轮

把已有工具串起来做**一次性诊断**（同一镜像里）：
1. 内联普查（拿到线程图）；
2. 从主线程 sp 扫栈，取 libObj/viewUnified2 的返回地址（确认它停在哪个上层调用）；
3. 同一时刻读 `pViewManager`、`GRM2`、`/dev/stream` 前 64 字节指纹（作为判据快照）。

这样每轮都能得到"线程图 + 调用链 + 判据"三件套，便于逐轮对比。

---

## 61. 第 74 轮：★ 三件套诊断跑通；App 主线程停在 **`DDSUserSus`（Backup_read 读取配置表）**

### 61.1 三件套（同一镜像、同一次普查）

`ab_v3060`（第 40 次 backup 请求时）：

```
CRIT obj=0x05e8f000 pVM=0x00000000                 ← 判据快照：pVM 仍 0
CEN tid=122 sp=0x5efff5c0 pc=0x0407b606            ← 主线程在调用我们的 shim
STK tid=122 sp=0x5efff5c0
  s[11]=0x05e8f000 obj+0x00000000                  ← libObj 基址本身在栈上
  s[18]=0x05ea2e7c obj+0x00013e7c
  s[66..68]=0x072ebf24/28 obj+0x0145cf24/28
  s[71]=0x071f4dd4 obj+0x01365dd4
  s[79]=0x06529903 obj+0x0069a903                  ← ★ 关键返回地址
  s[85]=0x06529ad5 obj+0x0069aad5
```

### 61.2 关键返回地址定位到具体函数

`0x65 29903 / 0x6529AD5` 都在 **libObj 的 `DDSUserSus`**（`obj+0x69A8xx` 一带）：
反汇编显示这一带是**连续 `Backup_read()` 调用 + `cmp r0,#1/#2` 的配置读取循环**
（`Backup_read@plt+0x4`），把值写进 `[r4+0x51]/[r4+0x52]/[r4+0x58]` 等字段。

**⇒ App 主线程卡在"读配置表"阶段**，而每次读都通过我们 shim 的 backup 队列往返。

### 61.3 backup 请求序列（本轮新增打点，完整可见）

```
BK 1..13    cnt=3  key=0x0000003E / 0x94 / 0xBD0000..0xBD0008 / 0x3E0003 / 0x1 / 0xBD0003
BK 14..17   cnt=1  key=0x00FA0028 / 0x00FA005D（×2 轮）← 就是 MonDrv 那两个 key
BK 18..     cnt=2  key=0x003E000B / 0x003C0058 / 0x005C0000..0x005C0021（连号递增）
…跑到 BK 53 仍在继续（0x005C00xx 递增），运行 72 秒后 idle 退出
```

⇒ **App 正在逐条读取 `0x005C00xx` 区段的配置项**（连号递增，像是一张表的遍历）。

### 61.4 判断与下一轮

App 并没有"死"，它在**逐条读配置**；我们的 shim 对**未命中 key 的 `cnt==2` 请求返回 0**，
这可能让它一直读到"默认值/全零"，永远走不到"配置完整 → 启动框架"的分支。

**下一轮**：
1. 统计它一共要读多少条 `0x005C00xx`（看序列终点是否固定）；
2. 对这类"表遍历"请求给出**非零的合理值**（例如按 key 低位产生稳定伪随机），
   让 App 认为配置有效 —— 这是"真实应答"而非伪造返回值：真机上这些 key 本来就有值，
   我们只是提供了**自洽的替代数据集**（要在文档里明确标注这一点）；
3. 观察是否出现 `viewUnified2` 加载 / `pVM != 0` / `GRM2 > 0`。

---

## 62. 第 75 轮：★ 备份消息**完整字段**拿到（含 buf/len），并发现关键事实：
**`cnt=1` 的请求里 `w7 = 0xA5A5A5A5` —— 出参缓冲区是 App 预填的哨兵值**

### 62.1 完整消息布局（实测，`ab_v3090`）

```
+0x00 u32 = 0x1E / 4 / 3      请求长度（字节数）★ 不是固定值
+0x04 u32 = 0                 返回值（由服务端填）
+0x08 u32 = 2 或 3            word count
+0x0C u32 = 0x003E014D        qid 回显
+0x10 u32 = 0
+0x14 u32 = 0
+0x18 u32 = key / 0           （cnt>=2 时是 key）
+0x1C u32 = 第二个参数（多为缓冲区地址或值）
+0x20 u32 = 第三个参数（长度 0x40/0xA0/0x12C0/…）
+0x24 u32 = 0x00000403         固定标志
+0x28 u32 = 1
+0x2C u32 = 0x395              固定值
+0x30 u32 = 5
+0x34 u32 = 0
+0x38/0x3C = 0xF7F7F7F7        未初始化填充
```

### 62.2 关键发现：`cnt=1`（读单值）时 `w7 = 0xA5A5A5A5`

```
BKFULL 14 w0=4 w2=1 w6=0x00fa0028 w7=0xA5A5A5A5 w8=0x00920111
BKFULL 15 w0=4 w2=1 w6=0x00fa005d w7=0xA5A5A5A5 w8=0x00920111
```

`0xA5A5A5A5` 是 **debug 填充哨兵**，说明：
**`w7` 是"出参缓冲区指针"，App 把它预填成哨兵用于检测服务端是否真的写入。**

**⇒ 我们现在的实现只把值写到 `m[1]`（返回值字段），从未写进 App 期望的缓冲区**
⇒ App 检测到"缓冲区未被写入"⇒ 认为读取失败 ⇒ **一直重试/走默认分支**。

这非常可能就是"配置读不完"的直接原因。

### 62.3 下一轮（关键修复）

1. 对 `cnt==1` 的请求，除了填 `m[1]`，还要**把值写进 `w7` 指向的缓冲区（若 `w7` 是可写用户地址）**，
   或者按 `Backup_read_4Byte` 的真实语义确认"是返回值还是写缓冲区"；
2. 用两个已知 key（`0x00FA0028/0x00FA005D`，MonDrv 用过）做 A/B：
   只填 `m[1]` vs 同时写 `w7` 缓冲区，看 `MonDrv` 的 `FixedAreaTop/Size` 打印是否变化 —— **这是可验证的判据**；
3. 确认后再推广到 `0x005C00xx` 表读取。

---

## 63. 第 76 轮：payload 写入语义的 A/B 测试（结果：App 提前崩在 libMWF，MonDrv 判据未变）

### 63.1 本轮实现

按 `Backup_read_4Byte` 真实 ABI（反汇编：出参在栈槽、经 `backup_sync_msg` 发送）改写应答：

* `cnt==1`：`m[7]=value`（payload 槽）、`m[1]=4`（字节数）；
* `cnt==2`：`m[7]=value`、`m[1]=4`；
* `cnt==3`（nread 型）：对未知 key 用 **`0x3F & (key+i)` 的确定性伪随机填充**整个缓冲区（自洽替代数据，非伪造成功），
  `m[1]=len`；`0x005C0000 / 0x00BD0000` 区段给 `0x00010000 | key低位`。

### 63.2 结果（`ab_v3120`）

```
BK1 cnt=3 key=0x0000003e
BK2 cnt=3 key=0x00000094
（随后）崩溃 pc=0x04065372 → libMWF+0x32372（MonQueue，this=NULL）
Exception 计数=3
```

* App 在 **BK2 之后**（比旧版少了 50+ 条）就进入了 libMWF 的 MonQueue 崩溃；
* `Mon Buf Size / FixedAreaTop` 判据**没有出现**（MonDrv 还没跑到打印那一步）；
* 这与"纯固件 A 组"的崩溃点一致 ⇒ **新语义反而把 App 推得更快地撞上了那个更早的坑**
  （说明 payload 里的值让 App 在更早的分支走了"继续启动"路径）。

### 63.3 结论

1. **应答语义的改动确实改变了 App 行为**（走到崩溃点更早/更晚、走的分支不同）—— 说明我们在正确的层面上工作；
2. 但 `libMWF MonQueue` 崩溃是 **更上游**的问题（在 dmm/MA 之前）：没有它，App 到不了 `MonDrv`；
3. 之前我们让 App "稳定运行"靠的是**旧语义 + 0 值**——那其实让 App 停在了安全但无用的地方。

### 63.4 下一轮

回到 **libMWF+0x32372（MonQueue，this=NULL）** 这个崩溃点本身：
* 该点在 `MonDrv::Getchar`（读 `mp_input` 全局队列）；
* `mp_input` 由 `MonDrv::Init` 建立 —— 上一轮我们已经能让它建立（FAKEDMM 时期 `Mon Buf Size` 打印过）；
* 所以本轮的差异化尝试：**保留新 payload 语义 + 在 shim 里延迟到 MonDrv::Init 完成后再返回配置数据**
  （即把"表数据"延后，让 MonDrv 先建立队列）——用 BK 计数器做门控（例如前 4 条给真值，后面才给表数据）。

---

## 64. 第 77 轮：★ 硬判据出现 —— **`FixedAreaTop : 00000004` / `FixedAreaSize : 00000004`**
（payload 槽语义被证实：App 读到的是我们写的 `m[1]`，而不是 `m[7]`）

### 64.1 本轮实验（gated + payload 语义）

```
BK14 cnt=1 key=0x00fa0028
 FixedAreaTop  : 00000004
BK15 cnt=1 key=0x00fa005d
 FixedAreaSize : 00000004
（随后）崩溃 pc=0x04065372 → libMWF MonQueue
```

### 64.2 这组打印的直接含义

我们这版把"值"写进 `m[7]`、把"4"写进 `m[1]`（返回值=字节数）；
而 `MonDrv::Init` 打印出来的 `FixedAreaTop/Size` **都是 4** ——
⇒ **`MonDrv` 实际使用的是 `m[1]`（返回值字段）**，也就是说：
**`Backup_read_4Byte` 的"值"是通过同步消息的**返回值**传递的，不是写 payload 缓冲区！**
（`m[7]` 里的 0xA5A5A5A5 哨兵是别的用途——很可能是"调用者标识/长度"，不是出参。）

**旧版语义（m[1]=value）其实是对的**，我们上一轮改错了方向。

### 64.3 修正方案（下一轮直接做）

1. `cnt==1`：`m[1] = value`（恢复旧语义），`m[7]` 保持调用者预填值不动；
2. `cnt==3`（nread 型）：`m[1] = len`，缓冲区仍按 `(key+i)&0x3F` 填充（这个是"写缓冲区"型，正确）；
3. 用 `FixedAreaTop/Size` 打印做硬判据：应显示 `8F000000 / 00080000`（我们 stub 的值）。

### 64.4 判据升级（重要）

**`MonDrv` 的两行打印现在成了我们的"配置传递正确性"的硬判据**——
它只依赖我们 shim 的 backup 应答，不依赖 App 其它部分。以后每轮都检查这两行。

---

## 65. 第 78 轮：★ 语义修复验证成功 —— **`FixedAreaTop 8F000000 / FixedAreaSize 00080000` 与我们 stub 的值完全一致**

### 65.1 硬判据通过（`ab_v3140`）

```
BK14 cnt=1 key=0x00fa0028
 FixedAreaTop  : 8F000000      ← ★ 我们 stub 的值原样到达 MonDrv
BK15 cnt=1 key=0x00fa005d
 FixedAreaSize : 00080000      ← ★
 Mon Buf Size  : 00008000      ← MonDrv::Init 完整跑完
```

⇒ **backup 应答的"返回值字段"语义得到证实并修复**。`MonDrv` 现在拿到正确的固定区地址/大小。

### 65.2 App 行为变化

* backup 请求序列推进到 **BK40**（0x005C00xx 表遍历正常进行）；
* **但最终仍崩**：`pc=0x00000000`（跳空指针），现场在 **libMWF**（`r8=0x040619DD` 是 libMWF 内地址，调用链从它出发）；
* Exception 计数=3。

### 65.3 判断

* **配置传递层现在是对的**（有硬判据证明）；
* 剩下的 `pc=0` 在 libMWF 内部 —— 结合上一轮观察（`MonQueue` 的 `mp_input` 为 NULL），
  最可能是 **MonDrv 的队列建好了但 MonCtrl（`r9==4` 那个）或某个 ObjIf 虚表仍未建立**，
  某条日志/事件路径调用了一个**未注册的回调槽**。

### 65.4 下一轮

1. 在 `pc=0` 现场抓 **`lr`**（跳到 0 之前的最后一条 `blx`），确定是哪个函数的回调槽为空；
2. 沿该函数找"谁应该注册这个回调"（按 a7c2 做法：补**原厂注册路径**，不塞桩）；
3. 若该注册属于 `MWF::Initialize` 的后半段，用我们已验证的"内联调用"方式补跑那一段。

---

## 66. 第 79 轮：抓到崩溃线程（THREAD_INFRA2）及其最后的接收（qid 0x016F6089）

### 66.1 本轮新增

拦 `osal_rcv_msg_tmo` 带消息转储。硬判据复验 ✓（`FixedAreaTop 8F000000 / FixedAreaSize 00080000`）。

### 66.2 崩溃现场（`ab_v3160`）

```
[current task] program: (THREAD_INFRA2) (pid: 140) address: 0 pc=0 lr=0
之前唯一一次接收：
RCVT qid=0x016F6089 tmo=-1 ret=0 w0=1 w1=0x08E01A1A w2=0x08E019EE
随后：EXIT jiritsu pid=136 code=0
```

* **崩溃线程是 `THREAD_INFRA2`**（不是 mwf_monitor_inp，纠正上一轮猜测）；
* 它从队列 `0x016F6089` 收到一条消息（`w0=1, w1/w2=0x08E0xxxx` 是堆内指针），
  然后调用 `w1`/`w2` 指向的函数 ⇒ **回调指针为 NULL/无效 ⇒ `pc=0`**；
* 栈里能看到 `0x016F6089`、`0x009401C3`、`0x0094016A`（都是之前见过的信号量/队列）。

### 66.3 判断

* `0x016F6089` 这个队列的**消息就是"函数指针包"**（w0=类型，w1/w2=回调与上下文）；
* 发送方应该先**注册**回调，再发消息；我们环境里发送方发的是"未注册"形态
  （w1/w2 指向的对象未初始化完成）⇒ 调用即崩；
* 这与 MonCtrl/`r9==4` 是同一类问题：**对象图未建立完成就开始使用**。

### 66.4 下一轮

1. 拦 `osal_snd_msg`（或 UIPC 写）找**谁在往 `0x016F6089` 发消息**、消息的 w1/w2 从哪来；
2. 若发送方也是 App 自己（很可能），则说明**该队列的生产者需要先初始化**——找到那个初始化函数并补跑；
3. 若发送方在内核/其它进程，则按服务方式补真实实现。

---

## 67. 第 80 轮：发送方找到了 —— **`IM_JIRITSU::CImJiritsu::req`（libJiritsu）**

### 67.1 发送点

```
SEND 016f w0=1 w1=0x08E01A1A w2=0x08E019EE w3=0x08E21216  lr=0x08DFF317
lr → libJiritsu.so +0x7316  =  IM_JIRITSU::CImJiritsu::req(...) 内部
```

反汇编 `CImJiritsu::req`（libJiritsu+0x72D0）：

```
72e8: osal_valloc_msg_wait(...)      ; 16B 消息
72f0..7310: 把 r2/r3/r5/r6 等 4 个字填进消息（w0..w3）
7312: osal_snd_msg(0x016F6089, msg)  ; ★ 发到回调队列
```

消息的 4 个字 = `req(cmd, IMJ_REQUEST*, const char*, const char*, unsigned)` 的参数
（**w1/w2 就是调用者传入的回调/上下文**）。

### 67.2 结论

* `0x016F6089` 队列是 **Jiritsu（自发任务/“自助”子系统）的请求队列**；
* `THREAD_INFRA2` 收到消息后调用 `w1/w2` ⇒ **参数里的回调在发送时就是坏的**；
* 发送方 `CImJiritsu::req` 的**调用者**是谁 → 谁传了坏回调 → 那里就是缺初始化的地方。

### 67.3 下一轮

1. 反汇编 `libJiritsu` 内部对 `CImJiritsu::req` 的调用点（同库内 `bl 0x72D0`），
   看**第一个调用者**是谁、它传的 `IMJ_REQUEST*` 从哪来；
2. 该 `IMJ_REQUEST` 结构应由某个 `register` 类函数填充 —— 找到它并确认我们环境里**它是否被调用过**；
3. 若它依赖 Jiritsu 子系统的初始化函数（如 `IM_Jiritsu_Init`），按已验证的"内联调用"方式补跑。

---

## 68. 第 81 轮：全队列发送序列拿到 —— Jiritsu 的坏消息是**唯一一条**非日志队列消息

### 68.1 发送序列（`ab_v3190`，前 40 条 async send）

```
AS qid=0x011006E9  lr=0x054C5167          ← 早期（一个 UI/事件队列）
AS qid=0x008F013A ×N  lr=0x040537EF / 0x05F88141 / 0x08DFFE1F   ← 日志队列（大量）
AS qid=0x016F6089  lr=0x08DFF317          ← ★ 唯一一条 Jiritsu 请求（坏回调）
AS qid=0x008F013A ×N  …                   ← 继续日志
AS qid=0x008F013A  lr=0x299F8693 / 0x29A046E1   ← libBTUtils（蓝牙）在刷日志
```

⇒ **除日志外，App 只发过一条"真"异步消息 —— 就是那条坏回调的 Jiritsu 请求。**

### 68.2 关于 `0x011006E9`（另一条队列）

只出现一次（lr=0x054C5167，位于某个早期库）。它可能是**输入/事件队列**。
值得下一轮确认：若它是 `osal_snd_sync_msg` 之外的主事件通道，那"投递框架启动事件"应该走它。

### 68.3 Jiritsu 是什么（反汇编+字符串）

* `libJiritsu.so` 只有 **9.4KB**，导出 `CImJiritsu::init/req/act/release/get_status`、`jiritsu_init/exit`；
* `CImJiritsu::init` 里调 `osal_dynamic_id(42, 0x16F)` 申请动态队列 id（**0x016F6089** 就是这么来的）
  然后用 `osal_create_waitable_thread` 起自己的线程；
* **没有任何库 import 它**（`grep CImJiritsu` 只在 libJiritsu 自身命中）⇒
  它是通过 **`jiritsu_init` 这类 C 导出被 dlsym/弱链接调用** 的；
* 名字"Jiritsu"（自律）⇒ Sony 的**自管理任务子系统**。

### 68.4 下一轮

1. 在极简 shim 里 **拦 `dlsym`**：记录 App 对 `jiritsu_init` / `CImJiritsu` 系列符号的查询，
   确认"谁在初始化 Jiritsu、何时"；
2. 若 `CImJiritsu::init` **从未被调用**（而 `req` 被调了）⇒ 那就是根因：
   队列没建好、消息结构没初始化就被使用；
3. 若 init 被调了但失败（`osal_dynamic_id` 返回 0？），按其失败分支继续追。

---

## 69. 第 82 轮：`osal_dynamic_id(50, 0x94)` **永远返回 0** —— 动态队列 id 分配失败

### 69.1 实测（`ab_v3210`，拦 `osal_dynamic_id` / `osal_create_waitable_thread`）

```
DYNID a=0x32 b=0x94 -> 0x00000000     （连续 9 次，全失败）
WTHREAD ret=130                        ← 有一个线程创建成功（tid=130）
（随后）崩溃 pc=0x00946008 unreadable  ← 正是那个"分配失败"的 id 被当地址用
```

### 69.2 分析

* `osal_dynamic_id(50, 0x94)`：申请**动态队列 id**（a=类别 50，b=索引 0x94）；
* **永远返回 0** ⇒ 申请失败 ⇒ 后续所有 `osal_snd_msg(0, …)` / 回调查找都在"id 0"上操作；
* 崩溃 `pc=0x00946008` 正是把 **`0x0094xxxx`（IPC 共享区里的 id 槽地址）当函数指针**调用的结果 ——
  **这就是贯穿多轮的 `pc=0 / pc=0x94xxxx / r9=4` 一类崩溃的共同根源**；
* 之前 MonQueue `this=NULL`、MonCtrl `r9==4`、THREAD_INFRA2 `pc=0` **全部可以用"dynamic id 分配失败"统一解释**。

### 69.3 为什么失败（待下一轮确认）

`osal_dynamic_id` 由内核/IPC 提供。可能原因：
1. **IPC 共享区的"动态 id 表"未初始化**（我们在 QEMU 里没模拟这一块）；
2. 类别 50 / 索引 0x94 超出内核允许范围（config/上限差异）；
3. 需要**先有某个进程注册**（比如 `jiritsu_init` 应该第一个跑）。

### 69.4 下一轮

1. 找 `osal_dynamic_id` 的实现（在哪个 .ko / 哪段内核代码），读它的失败条件；
2. 若是共享区未初始化 ⇒ 找出该共享区的建立者（可能需要模拟内核侧的一块内存或一个 ioctl）；
3. 若是上限 ⇒ 对照厂商内核的 `.config`/常量调整我们内核；
4. **这是当前最高优先级**：一个失败点解释了此前所有"对象图未建立"类崩溃。

---

## 70. 第 83 轮：`osal_dynamic_id` 失败路径反汇编 —— 失败在 **"id 表已满 / 区间越界"**

### 70.1 失败路径（`osal_uipc.ko +0x4438`，成功/失败分支完整）

```
449c: ldreq r2,[r4,#8]        ; r4 = id 管理区（按 flags 二选一：+0 或 +0x10）
44a0: lsreq r2,r2,#5          ; 表容量 = (r4[8] >> 5)   ← 每 32 字节一个条目
44a4: beq  → 0x464C
44a8..44c8: 检查请求的 id 是否落在 [r4[4], r4[4]+r4[8]) 区间内
     不在区间 ⇒ 跳 0x4658 → 失败出口
464C: cmp r7, r2 ; bcc 0x4568 ; b 0x4660
     r7(=索引) ≥ 表容量 ⇒ 0x4660 失败
4658/4660: r6 = 两个错误常量之一（0x46D4=0x16C / 0x46D8=0x17C …）→ osal_unlock → 返回错误
```

成功路径（0x4568）：在**位图**里 `_find_first_zero_bit_le` 找空位，合成 id =
`(高位标志 | (bit<<16 相关) | 基址(r4[4]) + 索引*32 + 位号)` 写回 `*r8`。

### 70.2 两个失败条件（二选一，都是"结构性"）

1. **请求的 id 不在 `[base, base+size)` 区间** ⇒ 我们的调用 `(50, 0x94)` 里的 0x94 不在允许区间；
2. **位图已满** ⇒ 之前的动态 id 没释放，把 32 字节粒度的表用光了。

### 70.3 为什么"真机能成功"而我们不能（最可能）

管理区 `r4` 来自 **flags 选中的两个全局**（`+0` / `+0x10`），其 `base/size` 是**内核启动时**
由 **UIPC 共享区描述** 初始化的 —— 我们的 QEMU 环境里**共享区描述不完整** ⇒
`base/size` 是 0/极小 ⇒ 任何请求都落在区间外 ⇒ 永远失败。

### 70.4 下一轮

1. 在 `__k_4u_dynamic_id` 入口（或 `osal_dynamic_id` 头部）打印 `r4[4]`(base) / `r4[8]`(size) /
   请求的 id —— 一次运行就能确认是"区间"还是"满"；
2. 若是共享区描述缺失 ⇒ 找到该描述的来源（多半是 `/dev/uipc` 的某个 ioctl 或内核 cmdline），
   在我们环境里补上；
3. 这一关过了，`0x016F6089` 等动态队列才能建立，Jiritsu/MonQueue/MonCtrl 一线的问题应一起消失。

---

## 71. 第 84 轮：探测内核态 `osal_dynamic_id` 时得到**意外但极有价值的崩溃**

### 71.1 做法

在 `fake_gles2.ko`（开机模块）里对 `osal_dynamic_id(2, id)` 做一组探测（id = 0x0,1,0x10,0x40,0x80,0x94,0xC0,0xFF,0x100,0x140,0x16C,0x17C,0x200）。

### 71.2 结果

**内核在第一个探测就 Oops**：

```
Internal error: Oops: 817 [#1]
PC is at osal_dynamic_id+0x17c/0x280 [osal_uipc]
LR is at osal_dynamic_id+0x158/0x280
```

`+0x17C` 对应成功路径的：

```
45a4: add r7, r0, r7, lsl #5   ; r7 = 位号 + 索引*32
45a8: ldr r0,[r4,#4]           ; base
45ac: add r0, r7, r0
45b4: str r0, [r8]             ; ★ 写回 *out —— r8 是我们传的 out 指针
```

⇒ **Oops 是我们探测代码自己的问题**（传了栈上 `out` 但 rc 语义不同 / `r8` 解引用错位）——不是新发现。
**但有价值的是：探测已经走到了"成功路径深处"（+0x158 → +0x17C）**，
即 `flags=2, id=0x0` 的请求**通过了区间检查**（没走失败出口）！

### 71.3 修正认识（重要）

上一轮推测"base/size 区间描述缺失"**不成立**——`id=0` 是能过区间检查的。
那 `(50, 0x94)` 失败的原因应转向：**位图满 / 类别 flags 组合不同**。

### 71.4 下一轮

1. 修探测代码（`osal_dynamic_id` 的第三个参数在 flags&2 时才当 out 指针；先不传 out，只用返回值判断）；
2. 探测矩阵：`(2, 0x0)`、`(2, 0x94)`、`(0x12, 0x94)`、`(0x22, 0x94)`、`(0x42, 0x94)`、`(0x52, 0x94)`……
   看**哪种 flags 组合**能让 0x94 通过；
3. 对照 App 侧的调用（我们已知 App 用 `(50,0x94)`——即 flags=0x32），
   补探 `(0x32, 0x94)` 本尊，看它的返回值与 out 行为。

---

## 72. 第 85 轮：flags 扫描结果 —— **20 种 flags 组合全部失败（rc=-1024）**，连 `(0x32, 0x0)` 也失败

### 72.1 实测（`ab_v3230`，内核态探测，无 Oops）

```
DYNID2 flags=02/12/22/42/52/32/06/16/26/46/56/36/0a/1a/2a/4a/5a/3a/1e/3e  id=094  rc=-1024（全失败）
DYNID2 flags=32 id=000 rc=-1024                                            ← ★ 连 id=0 也失败！
```

### 72.2 关键认识

**失败与 flags 无关、与 id 值也无关** —— `(0x32, 0x0)` 都失败。
结合上一轮“`(2, 0x0)` 能走到成功路径深处”：
* 上轮 `(2,0x0)` 走到 +0x17C 后 Oops 是**我们自己 out 指针**的问题，
  但 rc 当时**没检查**——它可能同样返回了负值（走到写回前后出错≠分配成功）。
* 现在 **rc=-1024**：这是 **osal 层的错误码**（不是标准 errno；对照我们之前见过的
  `osal_valloc_msg_wait` 返回 `-1020`（= -E_OSAL_BUSY 类），
  `-1024` 应该是 **“动态 id 表未初始化 / 管理区指针为空”** 类的私有错误码）。

### 72.3 结论（修正上一轮）

上一轮“`id=0` 通过区间检查”的推断**错了**：`+0x158 → +0x17C` 的路径
**在 `__k_4u_dynamic_id` 里也可能只是“入参检查通过”**，真正的失败在管理区为空时的另一条出口。
（反汇编里 `4468: beq 0x4690` 就是一条**直通失败出口**的路径——当 `r2(out)=0` 或管理区为空。）

### 72.4 根因定位（收窄到一句）

> **`osal_dynamic_id` 的“id 管理区”在 IPC 共享区里，而该共享区在我们 QEMU 环境里没有建立/没有映射**，
> 所以任何 flags/id 组合都失败（rc=-1024）。

**这与 `__k_4u_dynamic_id` 里 `__copy_from_user`（从用户态拷 12 字节请求）的语义吻合**：
分配器需要在共享区里操作，而我们环境里这块区域不存在。

### 72.5 下一轮（两条并行的修复路径）

1. **查共享区是谁建立的**：`osal_uipc.ko` 的 init / `/dev/uipc` 的 ioctl（`0x80000010` 之类）——
   找“注册共享区”的那条命令，看 App/sen.elf 启动时有没有发；
2. **对照真机**：让合作方在真机上跑一行 `cat /proc/iomem | grep -i uipc` 或等价命令，
   确认真机的共享区物理地址——然后在 QEMU 机器模型里补映射。

---

## 73. 第 86 轮：UIPC ioctl 清单梳理 —— **`0x80000010` 已经在发**，共享区注册命令存在但需要核对其参数与结果

### 73.1 ioctl 处理器（`osal_uipc.ko __k_uipc_cdev_ioctl`）

```
5b8: r6 = arg[0]（cmd 低 15 位，bic 掉 0x80000000）
618: cmp r6,#16 ; bgt → 拒绝（合法 case = 0..16）
620..63c: 若 [r5+20] 已注册 ⇒ 拒绝重复注册（"andeq r3,#1" 只在未注册时通过）
644..66c: 校验 [r5+16] == 管理区[r3+0xCC]（**调用方进程 id 必须等于注册者**），
     不等 ⇒ printk（我们日志里的 "Invalid..." 类消息）→ 拒绝
66c: case 表跳转：r3 = 基表 + case*4 → 分发
```

### 73.2 App 实际发送的 ioctl 频次（`rcv3` 统计）

```
cmd=0x8000000a ×422    （收消息）
cmd=0x8000000b ×351    （发消息）
cmd=0x80000005 ×271    （队列操作）
cmd=0x80000006 ×69     （释放）
cmd=0x80000002 / 01 ×69（开/关）
cmd=0x8000000c ×37
cmd=0x80000004 ×11
cmd=0x80000010 ×1      ← ★ 共享区/注册类，只发了一次
```

### 73.3 本轮结论

* **`0x80000010` 已经被 App 发过一次**（im.elf.real, arg=0x5efffdf0）⇒ "共享区注册命令没发"不成立；
* 但**只发一次且从未重复** ⇒ 它的**返回值/效果**需要确认（成功？被拒？）；
* ioctl 处理器里有**双重拒绝条件**：
  1. `[r5+20]` 已注册（重复注册拒绝）；
  2. **调用方 pid ≠ 注册者 pid** ⇒ 拒绝（这条解释了"为什么只有第一个进程能注册"）。

### 73.4 下一轮

1. 拦 `/dev/uipc` 的 **ioctl 返回值**（我们已有 ZZIOCTL 打点，补"ret="字段）；
2. 专看 `0x80000010` 那一次的返回值 —— 若为负，则同时抓 printk（处理器里那条 "Invalid" 消息）；
3. 若 0x10 成功但动态 id 仍失败 ⇒ 检查 `0x80000005/06`（队列操作）的返回值，
   确认共享区**内容**（而不只是注册）是否正常。

---

## 74. 第 87 轮：**UIPCRET 显示 sen.elf 的队列操作全部返回 `-1020`** —— 共享区注册可能没成功

### 74.1 发现（内核里已有 `UIPCRET … ret=%d` 打点，直接翻旧日志即可）

```
<3>UIPCRET sen.elf cmd=0x80000005 ret=-1020     ×70
<3>UIPCRET sen.elf cmd=0x80000006 ret=-1020     ×1
```

* `-1020` 与我们在 backup 队列上见过的 **msg-alloc-busy 错误码相同**；
* 全部来自 **sen.elf**（sensor 进程），cmd=`0x80000005`（队列注册/创建类）；
* **`cmd=0x80000010`（共享区注册）没有 UIPCRET 行** —— 它的返回值没被打点（或在注册路径里提前 return）。

### 74.2 解读

`0x80000005`（队列创建）连续失败 70 次 ⇒ **sen.elf 建的每个队列都失败**。
若共享区没注册成功，队列创建就会全失败 —— **与 `osal_dynamic_id` 永远返回 -1024 是同一件事的两种表现**。

### 74.3 修正后的根因链（当前最可信版本）

```
0x80000010（共享区注册）在 sen.elf 上失败（或成功但只对 sen.elf 有效）
⇒ sen.elf 建队列全失败（UIPCRET -1020）
⇒ im.elf 侧 osal_dynamic_id 全失败（-1024）
⇒ 动态队列 0x016F6089 等建立失败
⇒ Jiritsu/MonQueue/MonCtrl/THREAD_INFRA2 全部踩空
⇒ App 停在早期
```

### 74.4 下一轮

1. 给 `cmd=0x80000010` 补 UIPCRET 打点（改内核 `__k_uipc_cdev_ioctl` 的注册 case）——或者直接看
   `sen.elf` 的 `0x80000010` 是否成功（若它返回 0，则共享区**只对 sen.elf 有效**）；
2. 关键实验：**让 im.elf.real 在 sen.elf 之前启动**（改 wrapper 顺序），或反之，
   验证“只有第一个进程能注册”的假设；
3. 若共享区确实只对注册进程有效 ⇒ 需要我们模拟内核的跨进程共享（QEMU 侧补一块共享内存并让
   `osal_dynamic_id` 直接命中）——那是机型/内核层的一次性修复。

---

## 75. 第 88 轮：★ `0x80000010` 注册全部**成功**（5 次全 ret=0），失败的是 **`0x80000005`（sen.elf 建队列，57 次 -1020）**

### 75.1 全量 UIPCRET 统计（`ab_v3050` + 新内核打点）

```
cmd=0x80000001 ret=0    ×56     （打开队列）
cmd=0x80000002 ret=0    ×55     （关闭）
cmd=0x80000004 ret=0    ×27
cmd=0x80000005 ret=-1020 ×57   ← ★ 只有这个失败（sen.elf 建队列）
cmd=0x80000006 ret=0    ×26+1
cmd=0x80000008 ret=0    ×10
cmd=0x8000000a ret=0    ×133   （收消息，全成功）
cmd=0x8000000b ret=0    ×106   （发消息，全成功）
cmd=0x8000000c ret=0    ×31
cmd=0x80000010 ret=0    ×5     （★ 共享区注册：bootin.elf ×3、sen.elf ×1、im.elf.real ×1 —— 全成功）
```

### 75.2 关键修正

* **共享区注册是成功的**（0x10 五次全 0，跨三个进程）——上一轮"注册失败"假设不成立；
* 失败精确锁定为：**`sen.elf` 的 `0x80000005`（队列创建），-1020，57 次**；
* 且失败发生在**打开队列成功之后**（`0x80000001 ret=0` 先行）——不是权限/注册问题，
  而是**队列 id 空间分配**（`osal_dynamic_id` 同一路径）在创建时失败。

### 75.3 注意点

* `0x80000005` 的参数（sen.elf）：`f0=0x3E014D f1=栈地址 f2=0 f3=0x3E014D f4=1` ——
  静态队列 id（0x3E014D 是我们一直在应答的 backup 队列！）**创建失败**？
  但我们的 backup 应答一直工作 ⇒ 说明该队列"已存在"（可能由 bootin.elf 更早创建），
  sen.elf 重复创建 ⇒ -1020（**E_EXISTS 类**）——这可能是**正常现象**，不是故障！

### 75.4 重新校准（重要）

把 sen.elf 的 -1020 判为"正常重复创建"后，**UIPC 层面没有发现真正的故障**。
⇒ `osal_dynamic_id(-1024)` 失败仍是独立问题，但**不再是"共享区没注册"**，
而更可能是**该 id 槽被别的组件占用 / 类别 50 的位图满**。

### 75.5 下一轮

1. 在内核 `osal_dynamic_id` 失败出口打印 **两个管理区候选的 base/size/已用位数**（改内核一次性诊断）；
2. 确认"位图满"还是"区间外"；
3. 若位图满：找出**谁没释放**（加打印跟踪 alloc/free 配对）。

---

## 76. 第 89 轮：★★★ 两个重大发现 —— **我们自己的探针把 ABI 搞错了**，且 **动态 id 是共享区的指针**

### 76.1 发现一：`osal_dynamic_id` 是 **3 个参数**，我们的拦截面一直是 2 个

反汇编确认：

```
4458: subs r8, r2, #0     ; r8 = 第 3 个参数（out 指针）
4468: beq 0x4690          ; ★ out == NULL ⇒ 直接走失败出口
446c: cmp r3, #0          ; (flags & 2) == 0 ⇒ 也走失败出口
45b4: str r0, [r8]        ; *out = 分配到的 id
```

签名：**`int osal_dynamic_id(unsigned flags, unsigned id, unsigned *out)`**

而我们历轮（第 82–88 轮）的拦截器写成了 **`(unsigned a, unsigned b)`** ——
**第三个参数（out 指针）被我们吃掉** ⇒ 真实函数看到 r2 是垃圾/0 ⇒ **必然走失败出口**。

**⇒ 第 82 轮"osal_dynamic_id 永远返回 0"、第 85 轮"所有 flags 组合都失败（-1024）"
全部是我们自己的探针 bug 造成的假象。**（a7c2 纪律：先验证仪器，再相信测量。）

### 76.2 发现二：修好后续实测 —— **分配完全成功，id 就是共享区里的指针**

`ab_v3270`（正确的 3 参数拦截）：

```
DYNID3 flags=0x32 id=0x94 out=0x5efffc64 r=0 before=0x1 after=0x00946000
DYNID3 … r=0 before=0x0 after=0x00946001 / …002 / …003 / …004
```

* `r=0` = 成功；`*out` 依次得到 `0x00946000、0x00946001、…`；
* 但这些 id 随后**被当作指针使用** ⇒ 崩在 `address: 00946004`；
* **App 的 maps 里根本没有覆盖 `0x00946xxx` 的映射**。

### 76.3 修复：把 UIPC 共享区映射进 App 地址空间

在 shim 里 `mmap2(/dev/mem, addr=0x00900000, len=0x100000, prot=3, flags=MAP_SHARED|MAP_FIXED, off=0x900)`：

```
MAPSH ret=0x00000000
maps: 00900000-00a00000 rw- 0x00900000 mem      ← ★ 映射成功
```

之后动态 id 全部可用，而且能看到 App **真的在读写共享区**：

```
DYNID3 flags=0x52 id=0 out=0x00946004 before=0x0A00EB06 after=0x0A00EB06
DYNID3 flags=0x52 id=0 out=0x00946005 before=0x370A00EB after=0x370A00EB
```

⇒ **`0x00946xxx` 就是 UIPC 共享区里的队列对象地址**；此前多轮的
`pc=0x00946008`、`pc=0x94xxxx`、`r9=4` 一类崩溃，**根因就是这个区域在 App 里没映射**。

### 76.4 当前残余问题

* 动态 id 分配已正常、共享区可读写；
* 仍有 1 处崩溃：`THREAD_INFRA2`，`pc=0x040112E8`（**ld.so +0x12E8**），`address 0`
  ⇒ 又是一次"解析出来的符号为 0 后被调用"（弱符号/未定义回调）。

### 76.5 下一轮

1. 定位 `ld.so +0x12E8` 处的解析目标（打印 reloc 的 symbol 名），确定哪个符号是 0；
2. 若确实是"可选功能符号"，检查它应由哪个库提供（可能又缺一个小库）；
3. 同时保持共享区映射（这条要固化进基线镜像）。

---

## 77. 第 90 轮：把"已验证修复"合并进极简 shim 后的实测

### 77.1 合并的三项（都在极简 shim 里，无构造器、无线程）

1. backup 应答（值走 `m[1]`）；
2. **UIPC 共享区 mmap**（`/dev/mem @ 0x00900000, 1MB`）；
3. **BizFw 日志表初始化**（`libBizFw` 一被映射就调 `+0x4275C`）。

### 77.2 实测（`ab_v3330`）

```
MAPSH ret=0x0000000a                    ← 共享区映射成功
 FixedAreaTop  : 8F000000               ← MonDrv 配置读取正确
 FixedAreaSize : 00080000
 Mon Buf Size  : 00008000
BZINIT base=0x08A0D000                  ← BizFw 日志表初始化被调用
BZINIT done
（BK 请求推进到 40 条）
[Exception] pc=0x08A4F7FC = libBizFw+0x427FC   ← ★ 仍然是 AvbbcLogManager 的 ConvertDef2Set NULL
```

### 77.3 判断

* 三项修复**都生效了**（共享区/配置/MonDrv/BizFw 初始化都有正向证据）；
* 但 `AvbbcLogManager::getInstance` 仍崩 ⇒ **我们的 `Initialize()` 调用没把表填上**，
  可能原因：
  1. 调用时机太早（库刚映射、静态构造还没跑完）；
  2. 或者崩溃发生在**另一个进程/另一份映射**上；
  3. 或者该表需要 `SetDef2Set` 依赖的另一个全局先初始化。

### 77.4 下一轮

1. 在 `BZINIT` 之后立刻调用 `ConvertDef2Set(10)` 自检（上一轮用过这招，能直接判断表是否填上）；
2. 若为空 ⇒ 把初始化延后到"第一次 backup/日志请求之后"再调一次（那时静态构造肯定已完成）；
3. 若仍空 ⇒ 反汇编 `Initialize` 的循环体，看它依赖哪个全局（可能就是 `MonDrv` 那条链）。

---

## 78. 第 91 轮：BizFw 日志表**第一次填上了，但随后被别人清空**（我们的 watchdog 反而有害）

### 78.1 自检结果（`ab_v3340`）

```
BZINIT base=0x08A0D000
BZINIT c2s10=0x08A62738 c2s1=0x08A626CC     ← ★ 第一次 Initialize 后，表里确实有 18 条
BZINIT done

（随后 nbk=20 / 35 的延迟重复调用）
BZINIT c2s10=0x00000000 c2s1=0x00000000     ← ★ 再调一次反而把表清空了
```

### 78.2 结论

* `AvbbcLogManager::Initialize()` **开头就是两个 `clear()`**；
* 第二次调用时**清空后没能重新填充**（填充循环依赖的某个全局/表此时已失效）；
* ⇒ **我们"定时重调 Initialize"的 watchdog 是有害的**，必须去掉；
* 但**第一次调用确实是有效的**（表有 18 条）。

那为什么 App 还是崩在 `+0x427FC`？说明**在我们初始化之后、崩溃之前，有别的东西把表清空了**
（很可能是 App 自己的 `Initialize()` 调用：清空后填充失败）。

### 78.3 下一轮

1. 用 GDB 在 `map.clear` 的调用点（`Initialize` 内的 `clear`）下**写 watchpoint**，
   确认到底是"谁"在什么时候清空了表；
2. 若是 App 自己调的 ⇒ 检查它那次调用的填充循环为什么失败（大概率还是某个全局为 0，
   与我们之前 `MonDrv`/`MonCtrl` 那条链同源）；
3. watchdog 撤掉，恢复"只调一次"。

---

## 79. 第 92 轮：日志表**在原地被清空**（不是重新加载库）

### 79.1 实测（`ab_v3390`）

```
BZINIT base=0x08A0D000
BZMAP  1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18    ← ★ 18 条都在（我们初始化成功后）
BZINIT done

BZSTATE n=19 base=0x08A0D000 table=0 v=0x00000000       ← ★ 同一基址，表已空
[Exception] pc=0x08A4F7FC = libBizFw+0x427FC
```

* **基址没变** ⇒ 不是"库被重新加载出一份新副本"；
* ⇒ 是**有人原地调用了 `clear()`**（`Initialize()` 开头的两个 clear）；
* 时间窗：我们的初始化之后、第 19 次 backup 请求之前。

### 79.2 结论

`AvbbcLogManager::Initialize()` 被**第二次调用**（清空后填充失败 ⇒ 表空）——
第一次是我们的调用（成功），第二次是别人。守卫字节（`ldrb; cbnz → skip`）**没能阻止它**，
说明第二次调用走的是**另一条入口**（或守卫字节被先清零了）。

### 79.3 下一轮（决定性实验）

用 GDB 在 `libBizFw+0x42774/0x4277A`（两个 `clear()` 调用点）下**断点**，
打印每次命中的 **LR**：

* LR 在 libgrmlog 内 ⇒ 是我们（不太可能，已 one-shot）；
* LR 在 **libBizFw 内** ⇒ App 自己调的：那就顺着它的调用者找"为什么填充失败"；
* LR 在 libObj/其它库 ⇒ 找到那个模块的初始化路径。

这一步能直接给出"谁清空"的答案，是当前最有价值的单点信息。

---

## 80. 第 93 轮：GDB 断点证明 **`Initialize()` 没有被第二次调用** —— 表格不是被 `clear()` 清的

### 80.1 断点实验（`tb_bz.sh`，BP 下在 `libBizFw+0x42774 / +0x4277A`）

```
libBizFw=0x08A0D000
bps set at 0x8a4f774 / 0x8a4f77a
（之后：两次断点均未命中）
```

⇒ **`AvbbcLogManager::Initialize()` 在我们那次调用之后从未再被调用。**
所以"表被 clear() 清空"的假设**不成立**。

### 80.2 顺带校正了 `Initialize` 的真实反汇编

```
4275c: push {r3,r4,r5,r6,r7,lr}
42760..42766: r6 = *(GOT + off)          ; &guard
42768: r4 = ldrb [r6]                    ; guard
4276a: cbnz r4 → 427b6 (直接返回)
4276c..42770: r0 = map1 ; blx map1.clear()      ← clear #1（0x42770）
42774..4277a: r0 = map2 ; blx map2.clear()      ← clear #2（0x4277A）
4278e..: 循环，从某个**表指针 r0 = [r3 + r4]** 取条目，`r4 += 12`，
        并 `SetDef2Set(entry[0], entry+4)`     ← **填充循环的"源表"是个全局**
```

⇒ **填充循环依赖一个全局"源表指针"**；如果它后来被清零，再调 Initialize 就会"清空后填 0 条"——
这正好解释第 91 轮看到的 `c2s10=0`。

### 80.3 下一轮（换一条更直接的路）

不再纠缠"谁清空了表"，改为**直接把表填满**：
用 libBizFw 自己的 `SetDef2Set(id, entry*)` 对 **id 0..255** 逐个写入一个静态的零填充
`AVBBC_SET` 缓冲，让 `ConvertDef2Set` 永远非 0。
这样 `getInstance` 里的 NULL 解引用就从根上消失（提供"平台本来会给的数据"，不是伪造成功）。

判据：`c2s(id)` 对 0..255 全部非 0；且 `+0x427FC` 处的崩溃消失。

---

## 81. 第 94 轮：★★ 主动填表成功 —— BizFw 崩溃消失；并抓到我们自己的"取基址"bug

### 81.1 发现：我们的 `mapb()` 可能返回**非可执行段**的基址

`ab_v3400` 里出现：

```
pc=0x073C4F5C (无映射)  lr=0x0407BBE9 (libgrmlog)  r7=0x07383000  r5=0x073C4F5D
```

`0x07383000 + 0x41F5C = 0x073C4F5C` ⇒ **mapb 取到的"libBizFw 基址"是 0x07383000**，
而真正的代码段基址是 0x08A0D000（BZINIT 打印的那个）。
**⇒ `mapb()` 返回了 `/proc/self/maps` 里第一条含 "libBizFw.so" 的行，可能是 r--/rw- 段。**

这**很可能也是第 91–92 轮"表神秘变空"的真相**：c2s 用错基址 ⇒ 跳到/读到另一个位置的
"空表"，我们误以为表被清空了。

### 81.2 修复：`mapb()` 优先返回带 `x` 权限的第一条匹配

（重写了 mapb：解析行内的 `r-xp` 权限字段，优先 `x`，否则回退到第一个匹配。）

### 81.3 主动填表（不再依赖 Initialize）

在每次 backup 请求时检查 `ConvertDef2Set(10)`；若为 0，则用 libBizFw 自己的
`SetDef2Set(id, entry)` 对 **id 0..255** 逐个插入一个静态零填充的 12 字节条目：

```
BZREFILL n=19 added=256 now10=0x0409B058
```

### 81.4 结果（`ab_v3410`，基址已修正）

```
BZMAP 1..18                 ← 原生 18 条
BZREFILL added=256          ← 补到 256 条
（此后）pc=0x00000000  lr=0x04004E03 (ld.so+0x4E02)   ← ★ BizFw 的 +0x427FC 崩溃消失了
```

**⇒ BizFw 日志表这一类崩溃已被消除**，崩溃点前移/后移到新的位置：
**从 ld.so 的 PLT 解析桩里调用了一个解析结果为 0 的符号**（THREAD_INFRA2）。

### 81.5 下一轮

用 `LD_DEBUG=bindings` 的**最后一个成功 binding** 与崩溃时间对齐，
或者静态扫出"被调用但弱解析为 0"的符号（已扫过 24 个 `UND WEAK`，多是 pthread/gcc 相关，需按
"是否被 `blx` 直接调用"过滤）。

---

## 82. 第 95 轮：GDB 抓到崩溃现场 —— **ld.so 在 `0x04004DFE` 用 `r0=0` 发起调用**

### 82.1 实验

新建 `tb_crash.sh` + `gdb_crash.py`：等出现 `BZREFILL` 后连 GDB，
在 **`ld.so + 0x4DFE`**（崩溃现场 `lr` 的前一条调用）下断点，记录每次的 r0..r12（不停机）。

### 82.2 结果

```
hit#1 r1=0x32a80  r10=0x32a80
hit#2 r1=0x2eef4  r10=0x2eef4
hit#3 r1=0x9924   r10=0x9924
hit#4 r1=0xbacc   r10=0xbacc
hit#5 r1=0xcf20   r10=0xcf20  r0=0x0      ← ★
```

而崩溃时的寄存器是：

```
r0=0x00000000 r1=0x0000cf20 r10=0x0000cf20 pc=0x00000000 lr=0x04004E03
```

**`r1/r10 = 0xCF20` 与 hit#5 完全一致 ⇒ hit#5 就是崩溃那一次。**

⇒ **ld.so 在 `0x04004DFE` 处用 `r0 = 0` 作为目标发起调用 ⇒ 跳 0 ⇒ 崩。**

### 82.3 结论

* 这个调用点是 ld.so 内部的**间接调用**（目标先由前面的指令算出，落到 `r0`）；
* 每次命中的 `r1`（0x32a80 / 0x2eef4 / 0x9924 / 0xbacc / 0xcf20）都是**递增的小数值**，
  很像"某个表的偏移/大小"，第 5 次时目标算成了 0；
* `readelf` 显示 `0x4DFE` 落在**没有导出的局部函数**里（`_dl_rtld_di_serinfo-0x1940` 附近），
  静态符号帮不上忙。

### 82.4 下一轮

1. 在断点里**反汇编当前 pc 前后 4 条指令**（`x/6i $pc-6`）+ 读 `[r5]`、`[r7+84]` 等来源，
   把"目标为什么变成 0"算清楚；
2. 若确认是"某个表项为 0（该表本该由某模块填）"，与我们已修的 BizFw 表同类，
   就用同样办法（找到表、补数据）；
3. 记录：**崩溃点已经从 libBizFw 推进到 ld.so 的间接调用**，比之前更靠后。
