# zvegisk / ZV-1M2 开机部署审计：NO-GO

> 2026-10-26。结论：**当前版本不得设置为永久开机 preload；备用 libIMDB 补丁禁止使用。**
> 本报告取代旧文档中的“零风险”“绝不会砖”“拔卡必回原厂”“补丁全部验证通过”等安全断言。
> 本次仅离线读代码和反汇编；没有连接相机、上传库、修改启动配置或重启。

## 1. 审计范围与限制

已完整阅读：
- [zvegisk 主源码](../tools/zvegisk/zvegisk.c)，1104 行。
- [构建脚本](../tools/zvegisk/build_zvegisk.sh)。
- [测试加载器](../tools/zvegisk/zvegisk_testload.c)。
- [agent 参考源码](../../_archive/dirs/agent/agent_v14.c)，252 行。
- [Sony 模式脚本](../../ref_e10/usr/bin/change_mode.sh)。
- [libIMDB 蹦床源码](</mnt/build/fw/imdbpatch/bind.S>)与补丁 ELF、bootin/init 的相关分支。

没有完成：64 MB appFw 全部控制流、所有 Sony RPC/驱动/外设固件的持久化写入闭包证明。因此本报告不声称已证明整个供体应用安全；以下缺陷已足以拒绝部署。
两项独立只读复审返回了关键发现，但均未完成最终回合；下列条目以主会话已读取源码与复核的地址为依据，而非假称两份审计均已完成。

审计对象指纹：

| 对象 | 大小 | SHA-256 |
|---|---:|---|
| zvegisk.c | 41366 B | f87610eebd0fa376f13f5e2017f7f827a392455dab1bfcb9220905d5c18318a7 |
| zvegisk.so | 40348 B | 4971853d996765aa5e839b87df6688c5000e2e9c2fec491aeba38e484fee36ce |
| libIMDB_patched.so | 40596 B | 1c6a30590c763428e20090c18ea1570a6815e1f12a614fb426bb1ea2e1b2408f |

## 2. dmode=3p 到底改变什么

[Sony 脚本](../../ref_e10/usr/bin/change_mode.sh#L105-L116)同时将两种用途写为 `3p`：jemalloc 调试（参数 f）和指定 preload（参数 p）。所以 **p 是 preload 标志，不是“新菜单模式”**；具体加载什么由另一文件决定。

离线反汇编核对：
- bootin +0x949c..0x94a8：字符 `3` 选择启动阶段，`p/P` 将标志 OR 0x40。
- +0x9990：检查 0x40；+0x99a4 打开 `/setting/mode/preload`，+0x99b8 最多读取 498 字节。
- +0x99d0..0x99ec：比较四个调试库路径的前缀。
- **未匹配**：+0x99fa 记录 OSAL 日志，+0x99fe 分支到 +0x9a1c，跳过 putenv。
- **匹配**：+0x9a00..0x9a10 加 NUL，+0x9a18 调 putenv。

结论：
1. 两个配置文件在内部持久分区，设置后每次开机都会重新尝试，不是断电即清的补丁。
2. 这条入口比全局 loader preload 范围小，但不等于只影响一个永不派生子进程的目标。环境可能被其子进程继承；挂载与设备副作用也可影响其他进程。
3. 单纯写 `3p` 不等于刷写内核/MCU；**被加载的代码**仍可以经系统调用或 Sony 服务产生持久化副作用。
4. 不存在/某些无法映射的 preload 条目常可忽略；但成功映射后的重定位、ABI、构造函数异常仍可终止或卡住目标进程。错误字符串里出现 ignored 不能证明所有损坏情况都安全。
5. glibc 支持空格/冒号分隔，并不意味着 preload 列表顺序可以作为构造函数依赖顺序契约。
6. 三段 preload 提前放入 M2 libIMDB 时，**M2 库在我们的 safemode/SD 检查之前已被动态加载**。拔卡、old 模式不撤销该事实。
7. 该分支只加 NUL，未见去除结尾换行；不能把带换行的配置内容默认当成同一路径。

## 3. 必须停止部署的严重缺陷

### P0-1：挂载与应用启动无同步，可能混用新旧引擎

位置：[构造函数](../tools/zvegisk/zvegisk.c#L1093-L1104)、[模块先行与配置读取](../tools/zvegisk/zvegisk.c#L1012-L1047)。

父进程 fork 后立刻继续；子进程才做配置读取和挂载。父进程何时 dlopen appFw/libObj 与子进程何时 bind 没有握手。尤其先加载代码模块还可能消耗数秒。相同的启动基础库只说明已加载部分相同，不能证明后续 dlopen 一定晚于 bind。

当前代码也没有“同步安装 M2 两个子树并确认后才继续”的实现。不得把 loop mount 成功等同于可启动 M2 引擎。

### P0-2：safemode 与 old 模式不构成前置隔离

位置：[自检先于 safemode](../tools/zvegisk/zvegisk.c#L975-L989)、[代码模块先于 SD safemode](../tools/zvegisk/zvegisk.c#L1016-L1045)。

即使内部 safemode 存在，仍先执行 tmpfs/mknod/loop ioctl/ext2 mount；SD safemode 要在代码模块之后才被读取。外层 work 还在 safemode 判断前写 phase。再加上 M2 libIMDB 已在 preload 中，不能宣称安全模式保证纯原厂启动。

### P0-3：备用 libIMDB 蹦床语义错误，禁止使用

源码：[bind.S](</mnt/build/fw/imdbpatch/bind.S>)。

- 行47–58：裸 mount syscall 第五参数应在 r4；源码将零压栈，r4 仍保存 image fd。ext2 挂载因此收到错误 data 指针，可能失败。bind 的 data 可能被忽略，但这不修复首个 mount 参数错误。
- 行97–100：ET_DYN 库尾调用写成 `bx 0x36dd`，没有加运行时 load bias，目标不是原构造函数的实际地址，可直接崩溃。
- 行60–80：两次 bind 没有返回值检查、没有部分成功后的事务回滚。
- 没有创建挂载所需目录或保证设备节点父目录是可 mknod 的文件系统；忽略 mknod 错误。
- 失败路径 unlink preload 不检查返回值、不持久化确认、不解除已关联 loop/已挂载树；随后仍继续 M2 libIMDB 当前启动。

核验：init_array 的 R_ARM_RELATIVE（offset 0xf000）仍存在，指向蹦床本身的重定位不是此次已证实错误；错误在蹦床内部的绝对尾跳转。字节边界干净、Thumb 位正确，不代表逻辑正确。

供体会话已承认错误并撤回该补丁。

### P0-4：没有隔离供体的持久化与硬件写入

只读 ext2 只保护镜像本身。root 进程仍能：
- 直接写可写挂载或原始块设备；
- 通过 backup/UIPC/UDIF 请求其他进程、RTOS、内核或 MCU 写配置/固件；
- 触发驱动错误导致内核重启。

fork 不建立新的挂载命名空间、不降权、不限制设备权限，更不是内核故障隔离。即使另建挂载命名空间，也不自动隔离既有服务、设备 ioctl 或其他进程持有的 fd。

“libbackup 相同”只证明客户端实现相同，**不证明 BKID、配置值、迁移行为、服务端格式语义兼容**。相同更新字符串不能证明不会自动升级/迁移；没有 libgrm 文件也不能消除 libObj 内部 GPU 客户端与内核 ABI 耦合。原更新程序相同也不能证明 M2 调用流程相同。

因此供体运行后的持久化写入仍是未消除的阻断项。备份只能提高恢复机会，不防止事故，更未覆盖所有外设 NVM。

## 4. zvegisk 代码中的高风险与功能缺陷

### H-1：ARM open 标志写错，模块枚举可能完全没执行

位置：[scan_dir](../tools/zvegisk/zvegisk.c#L651-L675)、[overlay 目录打开](../tools/zvegisk/zvegisk.c#L801-L802)。

代码将 O_DIRECTORY 写成 0x10000；目标工具链 ARM asm/fcntl.h 定义 O_DIRECTORY=040000（0x4000），O_DIRECT=0200000（0x10000）。该常量错误已确定，具体设备返回还未测；常规目录 O_DIRECT 路径可返回 EINVAL。之前空目录/不存在目录的测试没有验证真实模块加载。

### H-2：watchdog 不能判断 UI 健康

位置：[ui_running/watchdog](../tools/zvegisk/zvegisk.c#L906-L956)。

只扫 PID 1..3999，每个 maps 只读一次前8191字节，并查库名字。库映射不代表已绘制、事件泵工作或 UI 未死锁；其他进程的映射也可误判成功。误判失败则90秒后主动 reboot，正是此前事故类型。没有真实健康握手、单实例/启动代际绑定、可靠失败隔离。

### H-3：快速循环计数仍不可靠

位置：[同步写](../tools/zvegisk/zvegisk.c#L231-L243)、[phase/self_disable](../tools/zvegisk/zvegisk.c#L858-L895)、[work](../tools/zvegisk/zvegisk.c#L1067-L1090)。

- open/write/fsync/unlink 失败均未可靠处理；目录可能在首次 phase 写入时尚未创建。
- O_TRUNC 先清空旧值；崩溃/断电窗口内可丢计数。
- phase DONE 表示 worker 返回，不表示新菜单健康；SD缺失/safemode/部分失败等早退也被标DONE并清 rapid_fail。
- 未 armed 或 SD挂载失败时不启动watchdog/不清正常 boot_count；普通成功加载也可能累计到4次并删除 preload。
- 自删 preload 不校验文件归属、不恢复原配置、无验证删除后启动行为。
- 无法保护构造函数之前、loader阶段、fork失败、内核早崩的情况。

此前“0.5秒以上大概率可救”“0.3秒以下UART”等具体时限无实测依据，撤回。

### H-4：挂载账本不能保证完整回滚

位置：[load_modules计数重置](../tools/zvegisk/zvegisk.c#L688-L689)、[记录/回滚](../tools/zvegisk/zvegisk.c#L731-L774)、[实际bind](../tools/zvegisk/zvegisk.c#L820-L834)。

最多记录32条但未限制真实挂载数；写记录失败仍继续；记录无启动ID、旧记录不清理；回滚缓冲仅1536B，容纳不了最大32条记录。截断行仍会被解析，umount返回不检查却删除账本。bind部分失败不回滚，overlay失败不累计fails；phase两趟还重置挂载计数。不能称为精确恢复。

### H-5：模块超时与成功检测不可信

位置：[wait_child](../tools/zvegisk/zvegisk.c#L395-L413)、[port_listening](../tools/zvegisk/zvegisk.c#L418-L460)、[carrier](../tools/zvegisk/zvegisk.c#L507-L553)。

只杀直接子PID，双fork daemon能逃逸；SIGKILL后blocking wait4仍可能无限等D-state，8秒不是硬上限。EINTR未重试/收尾。LD_PRELOAD被忽略时busybox true可能exit0而误判模块成功；端口检查不验TCP state=LISTEN、不验进程归属。无digest/兼容性校验、无并发/常驻资源约束。

### H-6：overlay 输入缺乏边界和类型约束

目录解析不校验reclen范围/NUL终止，路径截断不报错；d_type未知值当文件处理，symlink未做真实路径边界验证。未实现敏感路径allowlist、模块type强校验、冲突检测或只读目标约束。不能靠module.prop声明限制其效果。

### H-7：agent 不是无副作用的“安全模块”

参考源码：[agent_v14.c](../../_archive/dirs/agent/agent_v14.c)。

行185–237存在旧bind清单自动应用/90秒重启逻辑；不受zvegisk armed控制。行166–181监听所有网卡、每连接fork，未见认证/并发限制；行112–117允许最多16MiB分配；行84使用8MiB整文件读且可能截断。行95自动cp原路径到.orig，会造成额外存储写入；路径被插入shell命令未转义。

源码与已部署二进制尚需可重复构建对应确认，所以这里是“参考源码中确定存在的风险”，不能不经验证就默认作为开机救援agent。曾运行成功不等于适合永久开机。

## 5. 诊断与测试结论需要降级

- [延迟快照](../tools/zvegisk/zvegisk.c#L381-L388)在fork子进程中读self/maps，只反映自己的地址空间；父进程后续dlopen不会出现。
- [maps](../tools/zvegisk/zvegisk.c#L287-L300)/[mounts](../tools/zvegisk/zvegisk.c#L357-L370)搜索未限定单行，可能后行命中却打印首行。日志里打印rootfs不是正确定位证据。
- bind 后maps路径仍可能显示原调用路径。不能只凭 `/usr/lib/libObj.so` 判断bind失败，应关联目标进程map的device/inode、目标mount ID/根路径与已验证文件。
- [testload](../tools/zvegisk/zvegisk_testload.c#L25-L39)只wait直接dlopen子进程。构造内部fork的worker/延迟快照/watchdog不被这个exit0证明完成；父加载器不转发子失败状态（自身仍return0）。不是完整监督测试。
- 最近一次运行只覆盖old模式、没有实际模块的分支；不能称为模块框架/overlay/boot回退全通。
- loop自检会改共享挂载状态，固定loop0不做所有权确认，不自动解绑/卸载；tmpfs重复挂可能堆叠，不一定失败。不能将它当无副作用检查。
- 无未定义符号、编译无告警、ELF对齐正确都不是防砖证明。

## 6. 已有证据仍保留的价值

此前输出支持：300MiB镜像上传后MD5匹配；只读loop挂载成功、关键文件可读；在临时目录的bind/umount成功；部分settings文件复制并有4个文件MD5校验。

但这些不等于供体可安全启动：
- 备份是运行中文件拷贝，不是冻结服务后取得的一致性整分区快照；etc复制曾因symlink失败，权限/FAT属性未完整保存。
- 旧全分区备份不是本轮最新设备状态，也未覆盖外设MCU内部flash。
- 先前上传测试过程中曾误删原厂日志/历史文件、传输脚本曾漏检查shell重定向失败；这些不能因最终MD5匹配被忽略。
- 当前相机实时状态本次未重新读取，不能承诺“现在一定原厂/正常”。最后已知未设置开机dmode/preload。本轮不改变这一状态。

## 7. 恢复边界

1. 用户已说明Linux UART未引出，不能作为可用恢复通道。
2. BootROM/Pico→USB fastboot先前已救过全局preload，是有价值的独立后路。
3. 旧救援脚本定位nflasha10的全局preload，**不是**本次nflasha2的mode/preload；不能拿旧LBA直接写新目标。
4. 必须从当前媒体动态解析分区/BPB/FAT目录，找到精确文件、保存扇区ground truth、限定最小变更、验证读回。旧镜像里MODE为空不说明当前相机为空。
5. 该救援能力仍不保证能恢复M2误写的MCU/镜头/其他外设内部NVM。
6. 子进程不能在内核panic后继续运行；快速循环可能在读SD和持久化计数之前发生。无时间保证。

## 8. 下一步安全准入要求（尚未实施）

- 禁止使用当前补丁；禁止将当前zvegisk永久放入开机链；不运行新供体app，不做live /usr/lib替换。
- 先修正安全模式前置、ARM常量、解析边界、真实健康握手、模块监督、事务回滚；所有关键持久化结果必须检查。
- **启动架构需要重审**：必须在决定stock/M2之前完成安全判定，在可验证的exec边界选择完整依赖集；不能依赖异步fork抢在dlopen前完成，也不能在gate之前preload供体。具体入口尚待离线验证，不能立即改造成品。
- 先在本地ARM仿真/隔离环境做故障注入：坏ELF、缺镜像、超时、磁盘满、fsync失败、部分bind失败、>32文件、重复加载、模块双fork、正常启动与断电。
- 对M2通过备份服务/UIPC/驱动进行的配置迁移和硬件写入继续建立证据。单纯覆盖setting目录不能阻断其他服务已打开fd/内核侧写入，不能当完整sandbox。
- 在任何永久启动试验前，先验证针对setting/mode/preload的独立救援定位；保存最新一致性备份并离机存储。
- 修复后只申请范围明确的一次性测试；任何新so上传/覆盖、启动配置写入、重启，仍需用户单独明确同意。

**总结：传输成功与loop成功成立；当前永久启动与自动恢复安全性不成立。现在停止在部署之前是必要措施，不是保守措辞。**
