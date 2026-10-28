# ZV-E10 QEMU 端口现状速查（审计更新：2026-10-26 Asia/Shanghai）

> **先读 [启动审计](ZVE10_A7C2_STARTUP_AUDIT_2026-10-26.md)。** 下述旧轮次条目保留作历史，不自动继承其因果判断。13 个“补回库”实际为生成桩；ab_v3050 非原厂干净对照；ld.so+0xA706 属 PLT 符号解析而非 init_array；尚未定位真正致错写者。pViewManager 地址及“从不初始化”等陈述须结合具体库摘要、装载偏移与足够观测窗口重验。

## 判据（活性门槛，不是完整菜单证明）
* 区分 `GRM2 init` 与 App 的真实绘制请求；日志存在不等于绘制。
* `/dev/stream@0x20312000` 非均匀最多证明该区被写；开头已知是描述符，不等于像素。
* 完成还需：原绘图参数 → 正确位图/格式/stride → 有所有权保证的像素提交 → 显示端一致 → 原菜单输入引发因果变化。
**当前：没有 App 菜单绘制/显示的完成证据。**

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

## 下一步（审计后收敛）
1. 固定 kernel/config/initrd/rootfs/usr/preload/argv 的资产摘要，分开原厂、生成桩和观测器。
2. 先闭合 Backup/初始化的最早失败契约，不随机填表、不反复手调 init。
3. 按实际异常与 PLT 解析现场定位 loader 问题，确认坏字段后再设 watchpoint。
4. 原管理器/事件泵成立后，才在原 UI 线程走一次性受控菜单生命周期；软件绘制保留原位图隔离与锁，再连接已确认的像素提交。
详见 [完整审计与证据](ZVE10_A7C2_STARTUP_AUDIT_2026-10-26.md)。
