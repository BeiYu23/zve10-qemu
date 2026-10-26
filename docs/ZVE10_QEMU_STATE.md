# ZV-E10 QEMU 端口现状速查（2026-10-27 / 最近更新：第 67 轮）

## 判据（唯一有效）
* `GRM2 > 0`（App 真的发绘制命令）
* `/dev/stream@0x20312000` 从均匀 `0x5A5A` 变非均匀
**当前：两条都不满足。**

## 已打通（有实测证据）
| 项 | 证据 |
|---|---|
| 机型/内核/35 个厂商 .ko | 内核 `3.0.27_nl-rt106+ #144`；模块全加载 |
| 内核 ABI 对齐 | `CONFIG_QUOTA=y` ⇒ `offsetof(inode,i_cdev)=0x198` 与厂商 `grm_gles` 一致；真 `grm_gles` 打开 `/dev/dmpgles2` 不再崩 |
| `thread_info` ABI | `flags@0 / preempt_count@4 / task@0xC`，与厂商 `osal_uipc` 裸操作一致 |
| MA/DMP 通路 | 内核态 DMM 服务（`0x0061003C`，编进开机模块）⇒ 真 MA 走通 `op7→op1→op9`，无 oops |
| 监视器固定区 | `Backup_read_4Byte` stub 到 DDR ⇒ `FixedAreaTop 8F000000`、两条 MonQueue 真正建立 |
| 显示链路 | `streamfb` 镜像物理 `0x20312000`；`libgrm2.so` 光栅化 shim 就绪（彩条已验证） |
| 缺失库 | 补 `libCoreSync.so`（`core_sync_user_init` 等桩） |
| **UIPC 共享区** | `mmap(/dev/mem, 0x00900000, 1MB)` ⇒ `0x00946xxx` 动态 id 变成**合法指针**，App 真正读写队列对象 |
| **backup 应答语义** | 值走**返回值字段 `m[1]`**（`MonDrv` 打印 `FixedAreaTop 8F000000 / Size 00080000` 证实） |
| 菜单视图对象 | poke 路线上 `ViewGlobalMenuToInstance(0)` **返回非空对象**（0x100F70/0x100FE8） |

## ★ 仪器纠错（2026-10-27，第 89 轮）
`osal_dynamic_id` 真实签名是 **`(flags, id, unsigned *out)` 三个参数**；
我们第 82–88 轮的拦截器写成了两个参数，**把 out 指针吃掉** ⇒
"分配永远失败（-1024）"全是**我们自己的 bug**。修好后分配 100% 成功。

## 当前卡点（按因果顺序）
1. **App 不启动框架**：主线程停在 libc 等待里，线程数极少；`pViewManager`（`libObj+0x149221C`）**从未被写**；
2. `Application_System_Init` **内联调用安全**（返回 4），但**只投事件不处理**；
3. 事件泵（`MWF::ObjThread`）从未启动 ⇒ 事件无人消费；
4. 视图工厂的 `r1` 已证实是 **`ViewManager*`** ⇒ 没有 ViewManager，视图对象只能"半构造"，一调虚函数就空指针。

## 已排除的方向（不要再走）
* 屏幕颜色/`0x5A5A` 当进展 ✗
* 用户态应答 MA（会踩 RT `__migrate_enable`）✗（改用内核态）
* preload 构造器里建线程 ✗（会搞坏 `DT_INIT_ARRAY` 处理）
* 把 `libMWF.so` 塞进 `/etc/ld.so.preload` ✗（会打乱 App 时序）
* 未清零 `.bss` 假说 ✗（实测加载后 .bss 全 0）
* re-exec 假说 ✗（全程单进程 pid=122）

## 下一步（唯一主线）
**向 App 队列投递它愿意处理的事件**（真实构造事件消息，不伪造函数返回）：
先拦 `osal_rcv_msg_tmo` 找到主线程真正的等待点，再按 `requestModelExecute` 的格式投递，
目标是让 App 自己 `创建 ViewManager → 开视图 → 发绘制命令`。
