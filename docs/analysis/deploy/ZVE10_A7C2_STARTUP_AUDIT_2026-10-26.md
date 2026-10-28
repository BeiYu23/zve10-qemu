# ZV-E10 启动困难审计：对照 a7c2-re

日期：2026-10-26（Asia/Shanghai）。本轮是源码、二进制与既有日志审计，未启动 QEMU、未操作真机、未修改固件镜像。

## 结论

困难有真实的跨代硬件/内核 ABI 成本，但近期反复主要来自：**污染且缺少清单的实验基线、尚未按协议实现的服务、侵入式探针与过度推断**。目前不能把问题定性为“原厂 App/jemalloc 的堆损坏”，更不能说“已排除我们所有改动”。具体崩溃根因仍未定位，是否存在越界写也待证明。

已保留的价值：原内核与厂商模块的 inode 偏移核对、真实 MA/DMM 请求证据、原库加载日志、以及不把彩条冒充菜单的纪律。单一结构字段匹配不等于整个内核 ABI 一致；服务往返也不等于应用框架完成。

## 1. a7c2-re 实际做到什么

核对仓库：chocotan/a7c2-re，本地 `/root/fw/a7c2re`，HEAD `e43c4d0`。不是另一个 `ChambersXDU/sony-a7c2-research` 仓库。

- [启动故障记录](../../a7c2re/report/2026-10-17-qemu-native-camera/BOOT-TROUBLESHOOTING.md#L79-L88)：原 AP 内核、原 bootin/im/appFw、限定的软件服务；随后原控件发出真实绘图，CPU 软件模型处理 GPE/LZ77，消费者真实提交像素后才完成。
- [受控入口](../../a7c2re/report/2026-10-17-qemu-native-camera/runtime/analysis/menu-observe.c#L2643-L2684)：要求管理器非空、屏已创建且已返回、当前在原 UI 线程、vtable/controller/状态均符合预期，才一次性调用原九参数生命周期入口。不是从任意线程手工构造一个 ViewManager，也不是反复补 init。
- [明确边界](../../a7c2re/report/2026-10-17-qemu-native-camera/BOOT-TROUBLESHOOTING.md#L141-L147)：受控菜单不等于正常 power-on 全链恢复。退出菜单后的 shooting/boot 仍未完成。
- [DMM 实现](../../a7c2re/report/2026-10-17-qemu-native-camera/runtime/analysis/dmm-service.c#L10-L73)：请求长度、opcode、代际 handle、分配/打开/关闭状态与失败码都是模型的一部分；不是统一回零。
- [IMDB 实现](../../a7c2re/report/2026-10-17-qemu-native-camera/runtime/analysis/prepare-assets.py#L153-L169)：校验输入摘要、精确清除十个服务的掩码，并核对输出摘要；不是把多条硬件初始化记录替换为同一个 CoreSync 入口。

因此可借用的是分层方法和服务语义，不能照抄 ARM64 地址、参数布局、glibc 符号版本、板级映射。a7c2 用原 4.9 内核；我们用重建 3.0 RT 内核承载私有二进制模块，本身就增加了 ABI 验证成本。

## 2. 按严重性排序的发现

### P0：所谓“补回原厂库”实际是生成桩

[旧记录 §84](REALCAM_GROUND_TRUTH_2026-10-27.md#L3452-L3467) 将 13 个约 7.5KB 库描述为固件树原有文件、只是镜像漏拷。

本轮逐个 readelf 核验：Atn、bkdmn、I2CRight、i2cuipc、InfraCrypt、InfraError、InfraInterchipDatatrans、InfraSetupFile、InfraSystemAdjust、InfraSystemProperty、NVM、read_refresh、SaveLoadSettings，以及 CoreSync，均含 Buildroot GCC 6.4.0、本机交叉工具链 CRT 路径和对应的 `*.c` 源文件名。CoreSync 的 init/sus/res 反汇编均为 `mov r0,#0; bx lr`。

[桩生成器](../scripts/gen_all_stubs.py#L15-L32) 会从字符串猜入口，为缺失库统一生成 `int entry(void) { return 0; }`。不能据此证明服务已初始化、输出参数已写入或依赖已满足。目录叫 `lfu/tree` 不构成原厂来源证明。

**影响**：补桩确实可消除 dlopen 缺文件，但不构成原厂功能恢复。旧“0 missing NEEDED”也不覆盖 dlopen/dlsym 的运行期契约。

### P0：共同带有干预的三组对照，不能排除全部自有改动

[旧 §88](REALCAM_GROUND_TRUTH_2026-10-27.md#L3634-L3670) 的三组都带 Backup shim，且共享重建内核、开机模块、rootfs、加载器和运行环境。

本轮从 ab_v3050 **只读抽取** usr，确认 `/bin/im.elf` 是本机 `imwrap4.c` 编译的 ELF launcher，而非原厂入口；rodata 明确包含 `LD_PRELOAD=/lib/libCoreSync.so /lib/libMWF.so /lib/libgrmlog.so` 和人为指定的 `debug/s/r/u/l/i/a/lazy-global` 参数。所以“原始基线”只表示较早实验版本。

[最小 shim](/mnt/build/grmslim.c#L22-L33) 仍截获 Backup、给四个 key 写自定义值，并将一定地址范围内的未知请求输出清零后报长度成功；[当前 shim](/mnt/build/grmslim47.c#L12-L22) 还为整类 key 编造非零值，[第 85–88 行](/mnt/build/grmslim47.c#L75-L91) 对未知表请求填 `(key+i)&0x3F`。这是主动配置替换，不是被动观察。

**能得出的结论**：三种变体均可崩溃。**不能得出的结论**：所有改动无关、固件自身有 bug。崩溃提前到另一位置，也不代表旧故障被否证。

### P1：把 PLT 解析误写为 DT_INIT_ARRAY

直接反汇编参考原 loader：`ld-2.11.2.so +0xA6F8` 开始读取 `r0+0x7C/+0x38/+0x34`；`+0xA706` 是 `ldr r5,[r5,#4]`。后续从 relocation 读 type/symbol，按 16 字节 Elf32_Sym 索引，读取版本表。按该 ARM32 link_map 布局，对应 `l_info[DT_JMPREL] / [DT_SYMTAB] / [DT_STRTAB]`，属于 PLT fixup/动态符号解析路径，不是初始化数组遍历。

因此旧 §41/§43/§84–88 关于这个地址的层级定位需撤回。“构造器建线程导致 glibc 2.11 的 init array 坏掉”不是现有证据能证明的普遍机制；确实需要审计构造期重入/锁/跳板，但不能把线程与 dlopen 一概说成不受支持。

不同地址崩溃只支持“多个异常现场，需查共同状态”，不是越界写的充分证明。

### P1：上一轮 GDB 实验没有捕获到它声称要找的 fatal strcmp

[探针](/mnt/build/gdb_str.py#L19-L29) 在 strcmp **逐字节循环内部** `+0x112E8` 下断点，且只保留前 400 次，再也不记录。日志很快耗尽于合法字符串的逐字符比较，不等于 400 次 dlopen。

[实际串口末尾](/mnt/build/tmp/st2/serial.log#L4810-L4819) 是 `THREAD_INFRA2`、`pc=0x04011000`、非法指令异常，非 `pc=0x040112E8` 的 NULL 比较。`st.log` 的正常前缀不能解释这次致命现场。批处理也未保存异常前的完整寄存器/调用栈快照。

### P1：“图形 shim 就绪”和显示判据过强

[现有 grm2 原型](/mnt/build/tmp/grm2.c#L24-L55) 将每个新建 bitmap 的像素指针都改成同一 fb，强写 stride/format，锁解锁无真实所有权；[Rectblit](/mnt/build/tmp/grm2.c#L90-L99) 只记日志即返回成功。不能作为完整菜单渲染后端，且若启用会破坏源/目标位图隔离。

[既有真机取证](REALCAM_GROUND_TRUTH_2026-10-27.md#L82-L113) 已说明 0x20312000 开头是描述符，然而 [streamfb 注释](/mnt/build/src/qemu-sony/hw/display/bionz_streamfb.c#L10-L20) 仍当作像素 scanout。

`GRM2>0 + 0x5A5A 变非均匀` 最多是候选活性门槛，不是不可伪造的“菜单完成证明”：init 日志、描述符/分配器写入和 shim 自己写都可能满足。真正完成还需原绘图参数来源、真实目标位图与格式/stride、消费所有权、提交帧、显示端逐像素一致与一次菜单操作后的因果变化。

### P2：实验基础设施会制造假稳定与假前进

- [runner](/mnt/build/run_fast.sh#L31-L38) 按日志 idle 结束，40 秒最小时间不足以排除 60 秒后的故障；此前已承认这一误判。
- `grep -c ... || echo 0` 零命中时会得到两个 0，导致数字比较错误；不是总能当成无害提示。
- [镜像修改器](/mnt/build/mk_libset.sh#L6-L13) 使用共享 `/tmp/ls.tmp`、吞 debugfs 错误、未逐文件读回/摘要校验。读整盘不报 I/O 错，只说明可读取，不等于内容/文件系统正确。
- [速查](ZVE10_QEMU_STATE.md) 的“唯一下一步”、pViewManager 偏移和轮次已与逐轮日志脱节。不要继续按陈旧目标文本重复做已完成的 inode 修复。

## 3. 本轮阳性核验，避免审计变成全盘否定

从 ab_v3050 抽出的 usr：`e2fsck -fn` 返回 0。libObj、libMWF、libosal_uipc、libIMDB 四库与现有 ref_e10 对应文件 SHA-256 一致。说明这四个 usr 文件在该基线没有字节差异；不证明 ref_e10 的完整官方来源，也不涵盖 rootfs、模块及所有库。

审计副本与 fsck 日志位于 `/mnt/build/tmp/audit-20261026-gpt61/`，只增加一份 300MiB usr 副本；未改任何输入镜像。

## 4. 收敛顺序

1. **先建立可审计底座**：固定原始资产来源与摘要；原厂、派生补丁、生成桩、观测器分别列清单；记录实际 kernel/config/initrd/rootfs/usr/preload/argv。保留旧镜像作证据，不再把任一 ab_vXXXX 当“原厂对照”。
2. **先修最前面的契约失败**：从原 im.elf → IMDB 初始化顺序追踪首次错误；Backup 未识别请求不得随机填表；确认请求长度、出参、返回语义。原有初始化函数不要靠强制重入/清表来维持。
3. **按正确位置抓 loader 现场**：在 PLT resolver/实际异常入口记录 link_map、relocation、符号名、原始栈、线程和映射；保存正常到首次异常的变化。只有确认被破坏字段的地址和值之后，再设 write watchpoint。
4. **菜单与完整冷启动拆成两条验收线**：菜单线先保证原管理器和事件泵成立，再在原 UI 线程使用经过 ABI/生命周期验证的一次性受控菜单入口。蓝牙/USB 按依赖证据隔离，不把基础配置/共享服务一并当硬件删掉。
5. **随后实现真实软件绘制与输出契约**：每张原位图保留独立像素与锁；支持 Fill/Copy/字体/资源解码；未知操作报不支持；真实消费后才完成。显示桥只读取已确认的已提交像素，不强占描述符池。

这不是建议换架构或重写整个项目。可以复用目前已证实有效的 QEMU/内核/IPC 工作，但要先止住基线和探针自身制造的不确定性。当前菜单仍未显示，目标没有完成。
