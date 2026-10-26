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
