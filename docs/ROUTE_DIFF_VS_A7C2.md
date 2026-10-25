# 路线对照：ZV-E10（我们） vs A7C2（a7c2-re）

**日期**：2026-10-26
**目的**：把两边"让原厂 App 菜单在 QEMU 里真显示"的做法逐层摊开，找出**真分歧**（不是措辞差异），
并给出哪些可搬、哪些不能搬、下一步该改哪一层。
**结论先说**：方法主干我们只对齐了 1/4；真正的分歧有 3 处，其中 1 处（输出面）我们自己的证据已经
证明选错了，1 处（卡点处理方式）正是他们文档里明确列为**禁止捷径**的做法。

---

## 1. 两条路线并排

| 层 | a7c2-re（ILCE-7CM2 2.01，已跑通菜单） | 我们（ZV-E10 V2.03，卡在 init） | 同构? |
|---|---|---|---|
| 机器/板级 | 上游 a7c2-qemu 的 `virt` + DTB 补丁 + ramfb + 自建输入设备；11 文件 QEMU 差分 | 自研 `cxd90045` 机型 + 自研 `bionz-streamfb` 显示模型 | 不同（各自必要） |
| 内核/模块 | 原内核 + 原 DTB + **自建 cpio initramfs**；按原依赖序加载，**跳过**会崩的 USB/MMC/PCIe | 原内核 + 原 initrd + **35 个原模块全上** | **不同**（他们"少加载"，我们"全加载 + 补丁"） |
| 缺失服务的补法 | **`osal_snd_sync_msg` / `osal_snd_msg` / `osal_rcv_msg_tmo` 单点拦截**，按目标 id 自己组装**真实回复报文**发回（约 13 个服务） | **内核侧 stub**（backup BK4 解析、`0x8F0139` ULOGIO）+ `dmac` 补丁 | **不同 ← 分歧 D1** |
| 绘制（GPE） | 内存补丁把 `mpgpe::InitializeDraw` / `mpgpe::Gperm::Init` / fill/blit/flush worker 换成软件光栅器，**写进请求里给的物理位图地址**（`/dev/mem` mmap），再调**原完成回调**让原生完成链自然闭合 | `libgrm2.so` preload：`GRM_bitmapCreate` 把位图**改成固定 VDF 区 `0x20312000`**，CPU 版 fill/rectblit，**无条件 return 0** | 形似神不同（D2/D3） |
| 资源解码 | `/dev/ldec` 缺失 → 原位装 `Lz77Decoder::decodeImpl` vtable 槽，用 `sony_lz77_decode()` **真解出字节**写回原位图；9 样本逐字节对齐 Python 参考 | patch `dmac` 让 `ldec_exit` 返回（**解除失败，未验证解码**） | **不同 ← 他们明确点名"不能把失败改成成功"** |
| 帧完成/输出消费 | 实现缺席的 **AP-only LIRO/ExternalOSD 消费者**：接 `0x8280074d/84d/84e`，按 `1002→1003/1005` 配对，带租期复制，**成功后**才回 `0x6003/0x6005` | 无消费者层；改为**接管内部 grm worker**并**手动 signal 完成 cond** | **不同 ← 分歧 D3（我们踩到他们 §18 的禁路）** |
| 显示桥 | 提交容器 `HOSDAP02`（128B 头+24930B 元数据+614400B 像素）→ `ramfb-viewer` → 标准 fw_cfg/ramfb → GTK | 直接镜像 `0x20312000` 物理区 → streamfb → screendump | 不同；**我们的输出面本身可能是错的**（见 D2） |
| 输入 | 原 OSAL API 发 32B App 消息 + raw→canonical 表 + 按下1/释放0 + 拨盘有符号增量 | 未开始 | 不同（我们缺一层） |
| 判据 | 四层分离：建屏? 画了? 提交了? 消费了?／QMP 截帧与 AP 快照**逐像素**比对 + `pixel_source_seq` 区分"新像素" | 单一判据 `GRM2 > 0`（更早、更弱，但不可伪造） | **不同 ← 判据 D4** |
| 菜单入口 | 原九参数生命周期 `appFw+0x1e78870`（受控入口，明确声明"不是冷启动"） | `viewUnified2` init_array / openView（同类做法） | **同构 ✓** |
| 迭代速度 | cpio 重建 + 串口逐条下发 + bubblewrap；一次实验分钟级 | `run_fast.sh`（去 Xvfb + 提前退出）≈40s + GDB 活体挂接 | **我们更快 ✓** |

---

## 2. 已经同构的部分（可以放心继续）

1. **"用原厂生命周期，不手搓 UI"** — 他们九参数入口，我们 init_array/openView；都不碰 presenter/controller 裸构造。
2. **"在导出/入口边界替换硬件，而不是让 App 改道"** — 我们 `libgrm2.so` 与他们 `mpgpe` 软件光栅器是同一类手法
   （实测确认可行：`libObj.so` 的 `.rel.plt` 里有 `GRM_gpermFillrect @ 0x683f4d` / `GRM_gpermRectblit @ 0x684087`
   的 `R_ARM_JUMP_SLOT`，即**这两条绘制入口是走 PLT 的，LD_PRELOAD 能赢**）。
3. **显示最终交给 QEMU 真窗口 + 截帧取证** — 他们 ramfb/QMP，我们 streamfb/QMP，等价。

---

## 3. 三处真分歧 + 一处判据差异

### D1 服务伪造放在**哪一层**：OSAL 消息层（他们） vs 内核/设备层（我们）

- 他们：只有一个 choke point `osal_snd_sync_msg(dest, msg)`。缺什么服务就按 `dest` 拦下来，
  自己 `osal_valloc_msg` 组**同格式回复**、`osal_snd_msg` 回给请求方；文档里逐条写明
  "backup_sync_msg 只读回复 `+4` 的 status 字"、DMM 回复要带 handle/address/size 等。
  **好处**：不改内核、不动真机驱动、能看到每一条请求的真实字节，出错只影响一个服务。
- 我们：把补法沉到内核（BK4 页表解析、`0x8F0139` 应答、`dmac` 补丁）。代价是
  **不可观测**（要看效果，不能看调用）、**一次补错牵连全局**（`dmac` 就是这么埋下的）。
- **可复用性已实测**：`libosal_uipc.so` 导出面与他们完全同名（`osal_snd_sync_msg` / `osal_snd_msg` /
  `osal_rcv_msg_tmo` / `osal_reg_msg_queue_cb` / `osal_reg_sync_msg_queue_cb` / `osal_valloc_msg_wait` /
  `osal_free_msg` / `osal_msg_size` / `osal_dynamic_id` / `osal_snd_sync_direct` …共 129 个动态符号），
  且 `libObj.so` 通过 PLT 导入了其中 **21 个 `osal_*`**。⇒ 这条 choke point 在我们这里**同样存在且可用**。

### D2 输出面：**App 真正提交的位图/容器**（他们） vs **我们猜的固定物理区**（我们）

- 他们从**申请绘制时参数里给出的物理地址**写像素（`address = *(u64*)parameters`，`/dev/mem` mmap），
  帧提交再从 Bitmap 带租期复制成自校验容器。也就是说：**像素在哪他们的软件 GPE 就画在哪**，
  不再引入第二个"约定地址"。
- 我们把 `GRM_bitmapCreate` 的像素指针**改成** `0x20312000`，等于强制所有绘制落到一块我们挑的区域。
  而我们自己的调查结论是：**真机显示是 descriptor-driven，`0x20312000` 98.7% 是零，不是扫描源**
  （`FALSE_MILESTONE_5A_FILL.md`、`bionz_streamfb.c:180-184` 的注释）。
  ⇒ 我们等于把"输出面"押在一个**没被证明是渲染目标**的地址上；目前没暴露问题只是因为 App 还没画。
- 正确姿势（他们的）：软件 GPE 尊重请求地址；镜像区只做**最后一段搬运**（copy 到 `0x20312000` 或 ramfb）。

### D3 卡点处理：**实现缺失的消费者**（他们） vs **接管 worker + 强制 signal**（我们）

- 他们卡住时的现象和我们几乎一样：*"像素已经画好，第二帧却一直等待"*。根因拆解是
  "ready 信号量 `0x02886047` 已返回，等的是**另一把** `ExternalOSDIF` gate `0x02886044`"，
  解法是**把那个缺席的消费者任务实现出来**（按协议 1002/1003/1005 配对，锁内复制，成功后回 6003/6005）。
  **不是**去 signal 内部 cond。
- 我们做的是 `ab_v1531`：接管 `q/q2` 的 worker，自己调 cb，然后 `signal(q2+0x68)` 精确命中 `0x072E5C90`
  —— 结果请求方醒来发现 `[r7,#20] != 0` 又重试（业务上"失败"）。
  他们文档 §18 把这类做法逐条列为**被否证的捷径**：
  > "给未知队列空 ACK / 随意 signal / 重复 BB ready —— 能破坏所有权、时序或 promise，只产生假进展或新崩溃"
  而且他们的 `{0x2003,0x8201}` 早年被当作"建屏开关"重复兑现，后来纠正为录像编码器 promise，
  重复兑现会触发 `future_error(2)`。**我们的 worker 接管属于同一谱系**。
- 判别问题（决定该补谁）：`[r7,#20]` 这个结果字**本来应该由谁写**？
  - 若由 **cb 内部的硬件/驱动调用**写 → 那是"设备缺失"，应补设备/驱动（D1 的服务或器件）。
  - 若由 **另一个用户态消费者任务**写 → 应实现消费者（他们的路）。
  我们的 `[arg+16]` 手写 0 无效，说明**我们连字段身份都还没定**，现在加大补丁力度是盲飞。

### D4 判据强度：单层硬判据（我们） vs 四层逐像素（他们）

- 我们：`GRM2 > 0`。不可伪造，但**只能证明"开始画"，不能证明"画完/提交/被消费"**，
  而且踩过 `0x5A5A` 填充的假里程碑（两面都吃过这个亏：他们 §2 的"三张菜单图代表三层证据"）。
- 他们：建屏记录 / notifyDraw+drawFramebuffer 成对 / 容器提交（`pixel_source_seq` 才代表新像素）/
  QMP 截帧与 AP 快照**逐像素一致**。四层各要自己的证据，且明确"send 返回 0 ≠ 接收者存在"。
- 建议我们改成双层：`GRM2 > 0`（App 开始画）+ **streamfb 内容 ≠ 0x5A5A 且与 shim 记录一致**（真画完）。

---

## 4. 他们踩过并纠正的坑，与我们正在踩的

| 他们的纠正（BOOT-TROUBLESHOOTING §18 / §4） | 我们对应现状 |
|---|---|
| 把失败改成功（LZ77/`/dev/ldec`）不算修复 | **我们在做**：`dmac` 补丁让 `ldec_exit` 返回 |
| 给未知队列空 ACK / 随意 signal | **我们在做**：worker 接管 + 强制 signal 完成 cond |
| 重复兑现 BB ready 推动建屏 | 我们曾想"伪造 config/直接放行 init" |
| "没观察到"= "没发生" | 我们已记为纪律（同模块 `bl` 不可见） |
| send 返回 0 即有效 | 我们 `GRM2` 判据正是为反驳这个 |
| 日志里的字段含义要逐字核对（他们 `{0x2003,0x8201}` 误判、我们 `UIPCARG f0=0x20 实际是 msg[0]=长度`） | 已经踩过一次，同类 |
| 黑屏先分类（没建屏/只清屏/没提交/没接显示桥） | 我们的 `0x5A5A` 事件等价 |
| 不要先改内核 | **我们相反**：先动内核 stub/dmac |

---

## 5. 可迁移 / 不可迁移

**可迁移（方法级，已在本文档验证 API 存在）**
1. `osal_snd_sync_msg`/`osal_snd_msg`/`osal_rcv_msg_tmo` 单点服务伪造（含"回复报文只读某几个字段"的取证方式）。
2. 软件 GPE：**尊重请求给的地址**做真事 + 调**原完成回调**；不支持的模式返回错误码（5），不假成功。
3. 输出消费者按协议真回包（我们需先确认 ZV-E10 上对应的消费者是谁）。
4. 四层判据 + 容器/序列号区分"元数据更新"与"新像素"。
5. 内存补丁（vtable/函数指针表）而非只靠 LD_PRELOAD —— 我们已用 GDB 验证基址稳定，具备条件。
6. "先证伪里程碑"的文档纪律（两边都栽过）。

**不可迁移（逐项作废，别抄地址）**
- 一切地址/屏 ID/队列 id/键码（`0x346`、`0x2ca`、`0x8280074d`、`0x1ab`…）——代际不同。
- 框架命名：他们 `mpgpe::Gperm/fill_worker`，我们 `GRM_gpermFillrect/Rectblit` + `grm::CmdRectDraw::stage1`。
- ABI：他们是 AArch64/glibc2.25（`pthread_mutex_t` 48B、`x0..x8/x30` 跳板）；我们 ARMv7（`r0..r3/lr`、24B mutex）。
- 固件形态：他们单体 `appFw`，我们模块化 `libObj.so` 等 —— 所以他们的"PLT 不可拦截"盲区与我们相反：
  **我们这边 C 接口导出是走 PLT 的，反而更好拦**（见 §2 第 2 条实测）。
- 他们的"少加载模块"策略：我们已跑通 35 模块，不要回退。

---

## 6. 建议：把方法主干换成他们的，把我们的设备保留为"显示桥"

1. **停掉"强制完成"实验线**（worker 接管 / 手写结果字段 / nop 分支）。理由：D3，且他们没有一例成功来自伪造完成。
2. **一层一层往上找"谁该回答"**：用 GDB 在 `0x7D7E06` 钉死 `r7` 与 cb `arg` 是否同一对象，
   顺 cb 内部调用链找出**返回失败的那个调用**（ioctl/驱动/服务），分类到 D1（服务）或器件。
3. **补齐服务层**：把我们目前内核侧的那些应答搬（或复制）到 `osal_snd_sync_msg` 拦截层，
   先只做**观测版**（记录 dest/code/bytes/caller，原样放行），拿到"App 到底在等谁"的完整清单
   —— 这正是他们 `SND_SYNC_OTHER` 的做法，一次运行就能出全表。
4. **把 `libgrm2.so` 改成"尊重地址"版**：不再强改 bitmap 像素指针，按请求里的地址画；
   不支持的模式返回 5；加"完成回调"。它一旦真被调用，`0x20312000` 只做最后 copy。
5. **判据升级**：`GRM2>0` + "非 `0x5A5A` 且与 shim 绘制记录自洽"，再往上加"提交/消费"层。

---

## 7. 附：证据索引

- 他们：`/root/fw/a7c2re/report/2026-10-17-qemu-native-camera/{STATUS,DESIGN,BOOT-TROUBLESHOOTING}.md`；
  `runtime/analysis/menu-observe.c`（`osal_snd_sync_msg` 拦截 1938+、`software_fill_worker` 2962+、
  `software_gperm_initialize` 2956、`observe_lz_decode` 2786、函数指针表补丁 367/370）；
  上游 `docs/tech/{04,05}.md`。
- 我们：`docs/HANDOFF_ZVE10_MASTER.md` §2（卡点）、§3（worker 接管）、§0（判据）；
  `docs/FALSE_MILESTONE_5A_FILL.md`；`/mnt/build/tmp/grm2.c`（现行 shim）。
- 本地实测（本轮）：`readelf --dyn-syms libosal_uipc.so` = 129 项，含全部 `osal_*` 同名 API；
  `readelf -r libObj.so` 有 21 个 `osal_*` + `GRM_gpermFillrect/Rectblit` 的 `R_ARM_JUMP_SLOT`。
