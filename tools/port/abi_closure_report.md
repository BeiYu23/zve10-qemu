# ZV-1F 新菜单 → ZV-E10 移植：用户态 ABI 闭包可行性报告

> 本报告所有数字均由脚本 `abi_closure.py` 实际执行 `readelf` / `grep` / `strings` 类命令得到，
> 未做任何推测性外推。标注 **[推测]** 的条目为推断，非实测。

## 0. 结论速览（VERDICT）

**结论：闭包可实现（YES）。** 存在一个有限的库文件清单，使 `appFw.so` 的未定义符号
**无一成为加载期阻塞项**：其中 714 个可由合并后的符号域直接解析，其余 3 个全部是
WEAK 未定义或仅经 PLT 惰性引用（详见下方加载期语义判定）。

严格口径下的实测结果：

- `appFw.so` 未定义符号 **717** 个（`readelf` 输出中的 UND 行数为 718，其中第 0 号是 LOCAL 且无名的空符号，不计入）
- 仅靠 ZV-E10 现有库可解析 **568（79.2%）** → 缺口 **149** 个
- 合并两树后（MERGED）可解析 **714（99.6%）** → 残余仅 **3** 个
- `appFw.so` 是否 `BIND_NOW`：**否**（实测无 `DF_BIND_NOW` / `DF_1_NOW` / `GNU_RELRO`）

残余 3 个符号的加载期语义（实测 `readelf -W -r` 的重定位类型）：

| 类别 | 个数 | 说明 |
|------|------|------|
| 惰性绑定(仅首次调用才报错) | 1 | 仅 R_ARM_JUMP_SLOT；无 BIND_NOW → 加载期不报错，调用到才报错 |
| WEAK 未定义(非致命) | 2 | WEAK UND，glibc 允许为 0，永远不报错（旧 gcc 遗留） |

**因此：`appFw.so` 在 `MERGED` 下可以成功加载** —— 残余符号全部是 WEAK 未定义（glibc 视为可空）
或仅由 `R_ARM_JUMP_SLOT`（PLT 惰性绑定）引用。由于 `appFw.so` 未设置 `BIND_NOW`，
这些符号**不会**在加载期报错；只有当对应代码路径真的被调用时才会触发 `symbol lookup error`。

### 必须从 ZV-1F 借入的库清单（贪心集合覆盖，6 个文件）

| # | SONAME | ZV-1F 相对路径 | ZV-1F 大小 | 新覆盖符号数 | 累计覆盖 |
|---|--------|----------------|------------|--------------|----------|
| 1 | `libmpr.so` | `firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libmpr.so` | 3024500 | 71 | 71 |
| 2 | `libObj.so` | `firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libObj.so` | 3929768 | 61 | 132 |
| 3 | `libCertificateManager.so` | `firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libCertificateManager.so` | 189824 | 9 | 141 |
| 4 | `libGOConnectionManager.so` | `firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libGOConnectionManager.so` | 72108 | 2 | 143 |
| 5 | `libInfraCCPFStatic.so` | `firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraCCPFStatic.so` | 143808 | 2 | 145 |
| 6 | `libInfraFsys.so` | `firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraFsys.so` | 549168 | 1 | 146 |

> 注意：贪心覆盖未能覆盖全部 gap，剩余 3 个符号无任何 ZV-1F 库可提供 —— 但这 3 个在两棵树内也都无提供者（含 ZV-E10 侧），即它们不是“借 ZV-1F 的库就能补上”的缺口，而是本来就无人提供的符号；其加载期语义见 §5。

## 1. 数据来源与扫描范围（实测）

| 项 | 值 |
|----|----|
| ZV-E10 动态库(.so) 数 | 351 |
| ZV-1F 动态库(.so) 数 | 286 |
| 扫描到的 ELF 可执行文件数 | 191 |
| 非 ELF 被跳过的文件数 | 296 |
| 内核模块 .ko 数 | 206 |
| `appFw.so` 路径 | `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/appFw.so` |
| `appFw.so` 大小 | 53189440 B |
| 架构 | ELF32 / ARM / EABI5（`readelf -h` 实测） |

符号池规模（去重后的“可提供符号”数量）：

| 池 | 库数 | 定义符号数 |
|----|------|------------|
| E10_CORE | 265 | 201389 |
| E10_ALL | 310 | 202169 |
| F_ALL | 280 | 71620 |
| MERGED | 327 | 188499 |

口径定义（严格对应实际扫描的目录）：

| 池 | 组成 |
|----|------|
| `E10_CORE` | ZV-E10 `nflasha15/lib` + `nflasha7/lib`（根文件系统） |
| `E10_ALL` | `E10_CORE` + initrd 库（`0400_updater/.../initrd.img_unpacked/{lib,usr/lib,usr/local/lib}`、`nflasha3/initrd.img_unpacked/lib`）+ updater 库（`updater.img_unpacked/usr/lib`、`udtrbody.bin_unpacked/usr/lib`）+ `nflasha15/scenario` |
| `F_ALL` | ZV-1F `nflasha15/lib` + `nflasha7/lib`（根文件系统）+ `nflasha3/initrd.img_unpacked/lib` + `updater.img_unpacked/usr/lib` + `nflasha15/scenario` |
| `MERGED` | 上列全部两树库文件的并集，同名 SONAME 按 `--prefer` 规则取其一 |

> `scenario/*.so` 是运行时 `dlopen` 的场景插件，不属于任何库的 DT_NEEDED；为回答“两棵树里究竟有没有提供者”才纳入提供者池。它们**不**参与 DT_NEEDED 闭包计算。

> 注意：`MERGED` 在**符号层面不是并集**。同名 SONAME 只能有一个胜出，被遮蔽一方的导出符号不会进入 `MERGED`。实测 `--prefer F` 会遮蔽多个体积大得多的 ZV-E10 库（如 `libObj.so` 21,305,528 B、`libmpr.so` 13,021,036 B、`libInfraRemote.so` 8,708,516 B），因此 `MERGED` 的定义符号数（188499）反而**少于** `E10_ALL`（202169）。
> 为排除遮蔽造成的假缺口，本报告另行用“两树全部库的符号并集（UNION_ALL）”复核过残余集合，结果一致。

被跳过的非 ELF 文件（前 20，全部为数据/脚本，不参与符号解析）：

- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/githash`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/dhcpd/dhcpd.leases`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/dhcpd.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/p2p_multi_go.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/p2p.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/pump.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/bss.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/pump_dns.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ipsec/socket-default.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ipsec/vici.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ipsec/kernel-netlink.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ipsec/openssl.conf`
- `<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ipsec/nonce.conf`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/dhcpd/dhcpd.leases`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/dhcpd.conf`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/p2p_multi_go.conf`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/dhcpd_no_dns.conf`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/p2p.conf`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/pump.conf`
- `<repo>/unpacked_zv1f/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/netconf/bss.conf`

### 1.1 同名 SONAME 冲突与合并规则

合并规则参数：`--prefer F`（同名 SONAME 时由哪棵树胜出）。
实测冲突条目数：**310**。

| SONAME | 胜出方 | 被遮蔽方 | 两文件是否逐字节相同 | 胜出大小 | 被遮蔽大小 |
|--------|--------|----------|----------------------|----------|------------|
| `libjsoncpp.so.23` | E10:libjsoncpp.so.23 | E10:libjsoncpp.so | 是 | 200444 | 200444 |
| `libjsoncpp.so.23` | E10:libjsoncpp.so.23 | E10:libjsoncpp.so.00.11.0 | 是 | 200444 | 200444 |
| `ld-linux.so.3` | E10:ld-2.11.2.so | E10:ld-2.11.2.so | 是 | 101736 | 101736 |
| `libgcc_s.so.1` | E10:libgcc_s.so.1 | E10:libgcc_s.so.1 | 是 | 252937 | 252937 |
| `libc.so.6` | E10:libc-2.11.2.so | E10:libc-2.11.2.so | 是 | 984292 | 984292 |
| `libpthread.so.0` | E10:libpthread-2.11.2.so | E10:libpthread-2.11.2.so | **否** | 67996 | 67980 |
| `libdl.so.2` | E10:libdl-2.11.2.so | E10:libdl-2.11.2.so | **否** | 13976 | 13968 |
| `ld-linux.so.3` | E10:ld-2.11.2.so | E10:ld-2.11.2.so | **否** | 101736 | 101720 |
| `libuuid.so.1` | E10:libuuid.so.1.3.0 | E10:libuuid.so.1.3.0 | **否** | 36202 | 13480 |
| `libm.so.6` | E10:libm-2.11.2.so | E10:libm-2.11.2.so | **否** | 427692 | 427688 |
| `librt.so.1` | E10:librt-2.11.2.so | E10:librt-2.11.2.so | **否** | 22468 | 22448 |
| `libSTMcmdexecute.so` | E10:libSTMcmdexecute.so | E10:libSTMcmdexecute.so | **否** | 4688 | 4364 |
| `libcrypt.so.1` | E10:libcrypt-2.11.2.so | E10:libcrypt-2.11.2.so | **否** | 38532 | 38524 |
| `libc.so.6` | E10:libc-2.11.2.so | E10:libc-2.11.2.so | **否** | 984292 | 984276 |
| `libUsbBusPower.so` | E10:libUsbBusPower.so | E10:libUsbBusPower.so | **否** | 21616 | 41132 |
| `libtestcmd_scn.so` | E10:libtestcmd_scn.so | E10:libtestcmd_scn.so | **否** | 5044 | 6236 |
| `libosal_utm.so` | E10:libosal_utm.so | E10:libosal_utm.so | **否** | 4900 | 4932 |
| `libupdatertalk.so` | E10:libupdatertalk.so | E10:libupdatertalk.so | **否** | 3452 | 3404 |
| `libupdaterufunc.so` | E10:libupdaterufunc.so | E10:libupdaterufunc.so | **否** | 4372 | 4324 |
| `libUsbControl.so` | E10:libUsbControl.so | E10:libUsbControl.so | **否** | 94388 | 147464 |
| `libstdc++.so.6` | E10:libstdc++.so.6.0.14 | E10:libstdc++.so.6.0.14 | **否** | 669432 | 669432 |
| `libInfraSKD.so` | E10:libInfraSKD.so | E10:libInfraSKD.so | **否** | 10184 | 10160 |
| `libInfraFsys.so` | E10:libInfraFsys.so | E10:libInfraFsys.so | **否** | 428492 | 536832 |
| `libul_xml.so` | E10:libul_xml.so | E10:libul_xml.so | **否** | 56676 | 56632 |
| `libssp.so.0` | E10:libssp.so.0.0.0 | E10:libssp.so.0.0.0 | **否** | 5864 | 5864 |
| `libInfraMediaCommon.so` | E10:libInfraMediaCommon.so | E10:libInfraMediaCommon.so | **否** | 331236 | 397252 |
| `libupdatercommon.so` | E10:libupdatercommon.so | E10:libupdatercommon.so | **否** | 37048 | 37004 |
| `libgcc_s.so.1` | E10:libgcc_s.so.1 | E10:libgcc_s.so.1 | **否** | 252937 | 252717 |
| `libSysDef.so` | E10:libSysDef.so | E10:libSysDef.so | **否** | 3267652 | 1135000 |
| `libMWF.so` | E10:libMWF.so | E10:libMWF.so | **否** | 247776 | 241364 |
| `libusbcmd.so` | E10:libusbcmd.so | E10:libusbcmd.so | **否** | 15996 | 23888 |
| `libptp.so` | E10:libptp.so | E10:libptp.so | **否** | 52860 | 142304 |
| `libsencore.so` | E10:libsencore.so | E10:libsencore.so | **否** | 366412 | 393464 |
| `libpro00.so` | E10:libpro00.so | E10:libpro00.so | **否** | 3692 | 4368 |
| `libtestcmd.so` | E10:libtestcmd.so | E10:libtestcmd.so | **否** | 10128 | 12272 |
| `libul_GpioCtrl.so` | E10:libul_GpioCtrl.so | E10:libul_GpioCtrl.so | **否** | 4560 | 4512 |
| `libosal_ulogio.so` | E10:libosal_ulogio.so | E10:libosal_ulogio.so | **否** | 27712 | 27664 |
| `libbackup.so` | E10:libbackup.so | E10:libbackup.so | **否** | 9384 | 12104 |
| `libosal_uipc.so` | E10:libosal_uipc.so | E10:libosal_uipc.so | **否** | 26692 | 26652 |
| `libupdatercommon.so` | E10:libupdatercommon.so | E10:libupdatercommon.so | **否** | 37048 | 37004 |
| `libupdatercommon.so` | E10:libupdatercommon.so | E10:libupdatercommon.so | **否** | 37048 | 37004 |
| `libUsbBusPower.so` | F:libUsbBusPower.so | E10:libUsbBusPower.so | **否** | 22696 | 21616 |
| `libJiritsu.so` | F:libJiritsu.so | E10:libJiritsu.so | **否** | 178952 | 175092 |
| `libtestcmd_scn.so` | F:libtestcmd_scn.so | E10:libtestcmd_scn.so | 是 | 5044 | 5044 |
| `libmintx.so` | F:libmintx.so | E10:libmintx.so | **否** | 1532236 | 1532236 |
| `libEMG.so` | F:libEMG.so | E10:libEMG.so | **否** | 5872 | 5876 |
| `libInfraDlnaControl.so` | F:libInfraDlnaControl.so | E10:libInfraDlnaControl.so | **否** | 386048 | 365952 |
| `libInfraDevInfoSender.so` | F:libInfraDevInfoSender.so | E10:libInfraDevInfoSender.so | **否** | 13180 | 13184 |
| `libosal_utm.so` | F:libosal_utm.so | E10:libosal_utm.so | 是 | 4900 | 4900 |
| `libGOConnectionManager.so` | F:libGOConnectionManager.so | E10:libGOConnectionManager.so | **否** | 72108 | 70228 |
| `libInfraBluetoothCore.so` | F:libInfraBluetoothCore.so | E10:libInfraBluetoothCore.so | **否** | 217392 | 168992 |
| `libnl-genl-3.so.200` | F:libnl-genl-3.so.200.17.0 | E10:libnl-genl-3.so.200.17.0 | 是 | 13616 | 13616 |
| `libjemalloc_ds.so.1` | F:libjemalloc_ds.so.1 | E10:libjemalloc_ds.so.1 | 是 | 2936 | 2936 |
| `libInfraBluetoothLowEnergy.so` | F:libInfraBluetoothLowEnergy.so | E10:libInfraBluetoothLowEnergy.so | **否** | 541388 | 264596 |
| `libexpat.so.1` | F:libexpat.so.1.6.11 | E10:libexpat.so.1.6.11 | 是 | 107440 | 107440 |
| `libupdatertalk.so` | F:libupdatertalk.so | E10:libupdatertalk.so | 是 | 3452 | 3452 |
| `libInfraMediaMount.so` | F:libInfraMediaMount.so | E10:libInfraMediaMount.so | **否** | 44908 | 36416 |
| `libpopt.so.0` | F:libpopt.so.0.0.0 | E10:libpopt.so.0.0.0 | 是 | 21268 | 21268 |
| `libInfraStreamingUsb.so` | F:libInfraStreamingUsb.so | E10:libInfraStreamingUsb.so | **否** | 52320 | 52524 |
| `libnl-3.so.200` | F:libnl-3.so.200.17.0 | E10:libnl-3.so.200.17.0 | 是 | 78620 | 78620 |
| `CameraOnDemandSaver.so` | F:CameraOnDemandSaver.so | E10:CameraOnDemandSaver.so | **否** | 10544 | 10096 |
| `libObj.so` | F:libObj.so | E10:libObj.so | **否** | 3929768 | 21305528 |
| `libz.so.1` | F:libz.so.1.2.11 | E10:libz.so.1.2.11 | 是 | 77484 | 77484 |
| `libNetService.so` | F:libNetService.so | E10:libNetService.so | **否** | 678984 | 674776 |
| `libupdaterufunc.so` | F:libupdaterufunc.so | E10:libupdaterufunc.so | 是 | 4372 | 4372 |
| `libcom_err.so.2` | F:libcom_err.so.2.1 | E10:libcom_err.so.2.1 | 是 | 7964 | 7964 |
| `libInfraBluetoothWoLEBoot.so` | F:libInfraBluetoothWoLEBoot.so | E10:libInfraBluetoothWoLEBoot.so | **否** | 7904 | 7808 |
| `libutil_date.so` | F:libutil_date.so | E10:libutil_date.so | 是 | 5824 | 5824 |
| `libInfraMediaDetect.so` | F:libInfraMediaDetect.so | E10:libInfraMediaDetect.so | **否** | 66508 | 78248 |
| `libUsbChargingApp.so` | F:libUsbChargingApp.so | E10:libUsbChargingApp.so | **否** | 37596 | 37604 |
| `libInfraIPsec.so` | F:libInfraIPsec.so | E10:libInfraIPsec.so | **否** | 27968 | 29388 |
| `libUsbControl.so` | F:libUsbControl.so | E10:libUsbControl.so | **否** | 98356 | 94388 |
| `libdig_level.so` | F:libdig_level.so | E10:libdig_level.so | **否** | 26732 | 26732 |
| `libAK8975.so` | F:libAK8975.so | E10:libAK8975.so | 是 | 45428 | 45428 |
| `libpcre.so.1` | F:libpcre.so.1.2.12 | E10:libpcre.so.1.2.12 | 是 | 238304 | 238304 |
| `libInfraUsbHostStorage.so` | F:libInfraUsbHostStorage.so | E10:libInfraUsbHostStorage.so | 是 | 19128 | 19128 |
| `libcompassutil.so` | F:libcompassutil.so | E10:libcompassutil.so | 是 | 6128 | 6128 |
| `libopus.so.0` | F:libopus.so.0.8.0 | E10:libopus.so.0.8.0 | 是 | 352924 | 352924 |
| `libstdc++.so.6` | F:libstdc++.so.6.0.14 | E10:libstdc++.so.6.0.14 | 是 | 669432 | 669432 |
| `libNetCommon.so` | F:libNetCommon.so | E10:libNetCommon.so | **否** | 122876 | 122876 |
| `libjsoncpp.so.23` | F:libjsoncpp.so.23 | E10:libjsoncpp.so.23 | 是 | 200444 | 200444 |
| `libnl-cli-3.so.200` | F:libnl-cli-3.so.200.17.0 | E10:libnl-cli-3.so.200.17.0 | 是 | 28892 | 28892 |
| `libInfraSKD.so` | F:libInfraSKD.so | E10:libInfraSKD.so | **否** | 7259708 | 10184 |
| `libInfraFsys.so` | F:libInfraFsys.so | E10:libInfraFsys.so | **否** | 549168 | 428492 |
| `libIMDB.so` | F:libIMDB.so | E10:libIMDB.so | **否** | 40596 | 37204 |
| `libBackupManager.so` | F:libBackupManager.so | E10:libBackupManager.so | **否** | 5836 | 5844 |
| `libbsa.so` | F:libbsa.so | E10:libbsa.so | 是 | 129536 | 129536 |
| `libInfraMscServer.so` | F:libInfraMscServer.so | E10:libInfraMscServer.so | **否** | 45300 | 45300 |
| `libInfraBluetooth.so` | F:libInfraBluetooth.so | E10:libInfraBluetooth.so | **否** | 98952 | 90200 |
| `libupdatermedia.so` | F:libupdatermedia.so | E10:libupdatermedia.so | **否** | 4572 | 4040 |
| `libRsrcHotshoe.so` | F:libRsrcHotshoe.so | E10:libRsrcHotshoe.so | **否** | 22808 | 22428 |
| `libBTAdjust.so` | F:libBTAdjust.so | E10:libBTAdjust.so | **否** | 10912 | 10984 |
| `libprocps.so.6` | F:libprocps.so.6.0.0 | E10:libprocps.so.6.0.0 | 是 | 47820 | 47820 |
| `libSoundTouch.so.1` | F:libSoundTouch.so.1.0.0 | E10:libSoundTouch.so.1.0.0 | 是 | 60348 | 60348 |
| `libInfraNetFunctionManager.so` | F:libInfraNetFunctionManager.so | E10:libInfraNetFunctionManager.so | 是 | 36800 | 36800 |
| `libInfraNetConnectionManager.so` | F:libInfraNetConnectionManager.so | E10:libInfraNetConnectionManager.so | **否** | 25164 | 25116 |
| `libcrypto.so.3` | F:libcrypto.so.3 | E10:libcrypto.so.3 | 是 | 2656568 | 2656568 |
| `libWlanAdjust.so` | F:libWlanAdjust.so | E10:libWlanAdjust.so | **否** | 56308 | 56308 |
| `libip4tc.so.0` | F:libip4tc.so.0.0.0 | E10:libip4tc.so.0.0.0 | 是 | 17512 | 17512 |
| `libInfraMtpServer.so` | F:libInfraMtpServer.so | E10:libInfraMtpServer.so | **否** | 278948 | 171296 |
| `libncurses.so.6` | F:libncurses.so.6.2 | E10:libncurses.so.6.2 | 是 | 259936 | 259936 |
| `libOnDemandLoader.so` | F:libOnDemandLoader.so | E10:libOnDemandLoader.so | **否** | 40500 | 40372 |
| `libnl-nf-3.so.200` | F:libnl-nf-3.so.200.17.0 | E10:libnl-nf-3.so.200.17.0 | 是 | 55664 | 55664 |
| `libExternalCommand.so` | F:libExternalCommand.so | E10:libExternalCommand.so | 是 | 5372 | 5372 |
| `libInfraNetDeviceInfo.so` | F:libInfraNetDeviceInfo.so | E10:libInfraNetDeviceInfo.so | 是 | 7820 | 7820 |
| `libul_xml.so` | F:libul_xml.so | E10:libul_xml.so | 是 | 56676 | 56676 |
| `libssh2.so.1` | F:libssh2.so.1.0.1 | E10:libssh2.so.1.0.1 | 是 | 159952 | 159952 |
| `dataflowInfraFramework.so` | F:dataflowInfraFramework.so | E10:dataflowInfraFramework.so | **否** | 176368 | 172680 |
| `libInfraHttpClientUtil.so` | F:libInfraHttpClientUtil.so | E10:libInfraHttpClientUtil.so | **否** | 15380 | 15936 |
| `libInfraNetworkServiceInfo.so` | F:libInfraNetworkServiceInfo.so | E10:libInfraNetworkServiceInfo.so | **否** | 33696 | 33696 |
| `libssp.so.0` | F:libssp.so.0.0.0 | E10:libssp.so.0.0.0 | 是 | 5864 | 5864 |
| `libInfraAccessLogWrapper.so` | F:libInfraAccessLogWrapper.so | E10:libInfraAccessLogWrapper.so | **否** | 3364 | 3344 |
| `libBTUtils.so` | F:libBTUtils.so | E10:libBTUtils.so | **否** | 18456 | 16472 |
| `libqrencode.so` | F:libqrencode.so | E10:libqrencode.so | 是 | 27228 | 27228 |
| `libInfraPtpExtCmd.so` | F:libInfraPtpExtCmd.so | E10:libInfraPtpExtCmd.so | **否** | 34136 | 34064 |
| `libxtables.so.5` | F:libxtables.so.5.0.0 | E10:libxtables.so.5.0.0 | 是 | 19704 | 19704 |
| `libe2p.so.2` | F:libe2p.so.2.3 | E10:libe2p.so.2.3 | 是 | 20540 | 20540 |
| `libInfraMediaCommon.so` | F:libInfraMediaCommon.so | E10:libInfraMediaCommon.so | **否** | 421776 | 331236 |
| `libnl-route-3.so.200` | F:libnl-route-3.so.200.17.0 | E10:libnl-route-3.so.200.17.0 | 是 | 200084 | 200084 |
| `libWlanResource.so` | F:libWlanResource.so | E10:libWlanResource.so | **否** | 35240 | 35240 |
| `libupdatercommon.so` | F:libupdatercommon.so | E10:libupdatercommon.so | 是 | 37048 | 37048 |
| `libInfraIPTC.so` | F:libInfraIPTC.so | E10:libInfraIPTC.so | **否** | 40696 | 109676 |
| `libupdaterufp.so` | F:libupdaterufp.so | E10:libupdaterufp.so | **否** | 14388 | 13152 |
| `libInfraAirplaneMode.so` | F:libInfraAirplaneMode.so | E10:libInfraAirplaneMode.so | **否** | 8028 | 7912 |
| `libpcap.so.1.0.0` | F:libpcap.so.1.0.0 | E10:libpcap.so.1.0.0 | 是 | 153300 | 153300 |
| `libWlanControlCommon.so` | F:libWlanControlCommon.so | E10:libWlanControlCommon.so | **否** | 14792 | 14804 |
| `libInfraRemote.so` | F:libInfraRemote.so | E10:libInfraRemote.so | **否** | 1273012 | 8708516 |
| `libSysDef.so` | F:libSysDef.so | E10:libSysDef.so | **否** | 6033848 | 3267652 |
| `libBusPower.so` | F:libBusPower.so | E10:libBusPower.so | **否** | 31552 | 31532 |
| `libNetContUtil.so` | F:libNetContUtil.so | E10:libNetContUtil.so | **否** | 74844 | 74844 |
| `libInfraExtBackupCommunicator.so` | F:libInfraExtBackupCommunicator.so | E10:libInfraExtBackupCommunicator.so | **否** | 14244 | 14244 |
| `libjsoncpp.so.23` | F:libjsoncpp.so | F:libjsoncpp.so.23 | 是 | 200444 | 200444 |
| `libInfraKikiLogGenerator.so` | F:libInfraKikiLogGenerator.so | E10:libInfraKikiLogGenerator.so | **否** | 12032 | 12036 |
| `libcurl.so.4` | F:libcurl.so.4.8.0 | E10:libcurl.so.4.8.0 | 是 | 409696 | 409696 |
| `libjsoncpp.so.23` | F:libjsoncpp.so.00.11.0 | F:libjsoncpp.so | 是 | 200444 | 200444 |
| `libInfraAccessLog.so` | F:libInfraAccessLog.so | E10:libInfraAccessLog.so | 是 | 11716 | 11716 |
| `libssl.so.3` | F:libssl.so.3 | E10:libssl.so.3 | 是 | 412204 | 412204 |
| `libgenuinetime.so` | F:libgenuinetime.so | E10:libgenuinetime.so | 是 | 4108 | 4108 |
| `libInfraExtCmdSvc.so` | F:libInfraExtCmdSvc.so | E10:libInfraExtCmdSvc.so | **否** | 16196 | 16196 |
| `libResourceBluetooth.so` | F:libResourceBluetooth.so | E10:libResourceBluetooth.so | **否** | 7804 | 7800 |
| `libMWF.so` | F:libMWF.so | E10:libMWF.so | **否** | 247864 | 247776 |
| `libusbcmd.so` | F:libusbcmd.so | E10:libusbcmd.so | **否** | 16000 | 15996 |
| `libptp.so` | F:libptp.so | E10:libptp.so | **否** | 52860 | 52860 |
| `libsencore.so` | F:libsencore.so | E10:libsencore.so | **否** | 373804 | 366412 |
| `libpro00.so` | F:libpro00.so | E10:libpro00.so | 是 | 3692 | 3692 |
| `libInfraKikiLogSender.so` | F:libInfraKikiLogSender.so | E10:libInfraKikiLogSender.so | **否** | 6748 | 6748 |
| `libtestcmd.so` | F:libtestcmd.so | E10:libtestcmd.so | **否** | 10128 | 10128 |
| `libInfraNetInitializer.so` | F:libInfraNetInitializer.so | E10:libInfraNetInitializer.so | **否** | 21608 | 21132 |
| `libul_GpioCtrl.so` | F:libul_GpioCtrl.so | E10:libul_GpioCtrl.so | 是 | 4560 | 4560 |
| `libSKDWrapperManager.so` | F:libSKDWrapperManager.so | E10:libSKDWrapperManager.so | **否** | 5888 | 6188 |
| `libgps_rsrc.so` | F:libgps_rsrc.so | E10:libgps_rsrc.so | **否** | 16756 | 16756 |
| `libInfraNetConnectionManagerEther.so` | F:libInfraNetConnectionManagerEther.so | E10:libInfraNetConnectionManagerEther.so | **否** | 29756 | 29692 |
| `libmpr.so` | F:libmpr.so | E10:libmpr.so | **否** | 3024500 | 13021036 |
| `libInfraUsbCmn.so` | F:libInfraUsbCmn.so | E10:libInfraUsbCmn.so | **否** | 93984 | 94612 |
| `libGetLog.so` | F:libGetLog.so | E10:libGetLog.so | **否** | 164920 | 160828 |
| `libosal_ulogio.so` | F:libosal_ulogio.so | E10:libosal_ulogio.so | **否** | 27712 | 27712 |
| `libInfraMetaSupplier.so` | F:libInfraMetaSupplier.so | E10:libInfraMetaSupplier.so | 是 | 13588 | 13588 |
| `libbackup.so` | F:libbackup.so | E10:libbackup.so | 是 | 9384 | 9384 |
| `libosal_uipc.so` | F:libosal_uipc.so | E10:libosal_uipc.so | **否** | 26692 | 26692 |
| `libInfraHdmi.so` | F:libInfraHdmi.so | E10:libInfraHdmi.so | **否** | 412952 | 401048 |
| `libInfraHttpClient.so` | F:libInfraHttpClient.so | E10:libInfraHttpClient.so | **否** | 36604 | 35208 |
| `libInfraPtpControl.so` | F:libInfraPtpControl.so | E10:libInfraPtpControl.so | **否** | 85752 | 81576 |
| `legacy.so` | F:legacy.so | E10:legacy.so | 是 | 66068 | 66068 |
| `libnss_dns.so.2` | F:libnss_dns-2.11.2.so | E10:libnss_dns-2.11.2.so | 是 | 22192 | 22192 |
| `libpthread.so.0` | F:libpthread-2.11.2.so | E10:libpthread-2.11.2.so | 是 | 67996 | 67996 |
| `libdl.so.2` | F:libdl-2.11.2.so | E10:libdl-2.11.2.so | 是 | 13976 | 13976 |
| `ld-linux.so.3` | F:ld-2.11.2.so | E10:ld-2.11.2.so | 是 | 101736 | 101736 |
| `libanl.so.1` | F:libanl-2.11.2.so | E10:libanl-2.11.2.so | 是 | 14124 | 14124 |
| `libnss_files.so.2` | F:libnss_files-2.11.2.so | E10:libnss_files-2.11.2.so | 是 | 34540 | 34540 |
| `libnss_nis.so.2` | F:libnss_nis-2.11.2.so | E10:libnss_nis-2.11.2.so | 是 | 34576 | 34576 |
| `libuuid.so.1` | F:libuuid.so.1.3.0 | E10:libuuid.so.1.3.0 | 是 | 36202 | 36202 |
| `libm.so.6` | F:libm-2.11.2.so | E10:libm-2.11.2.so | 是 | 427692 | 427692 |
| `libgcc_s.so.1` | F:libgcc_s.so.1 | E10:libgcc_s.so.1 | 是 | 252937 | 252937 |
| `libblkid.so.1` | F:libblkid.so.1.1.0 | E10:libblkid.so.1.1.0 | 是 | 140236 | 140236 |
| `libnsl.so.1` | F:libnsl-2.11.2.so | E10:libnsl-2.11.2.so | 是 | 67600 | 67600 |
| `librt.so.1` | F:librt-2.11.2.so | E10:librt-2.11.2.so | 是 | 22468 | 22468 |
| `libSTMcmdexecute.so` | F:libSTMcmdexecute.so | E10:libSTMcmdexecute.so | 是 | 4688 | 4688 |
| `libresolv.so.2` | F:libresolv-2.11.2.so | E10:libresolv-2.11.2.so | 是 | 67488 | 67488 |
| `libcrypt.so.1` | F:libcrypt-2.11.2.so | E10:libcrypt-2.11.2.so | 是 | 38532 | 38532 |
| `libutil.so.1` | F:libutil-2.11.2.so | E10:libutil-2.11.2.so | 是 | 14008 | 14008 |
| `libc.so.6` | F:libc-2.11.2.so | E10:libc-2.11.2.so | 是 | 984292 | 984292 |
| `libxt_esp.so` | F:libxt_esp.so | E10:libxt_esp.so | 是 | 7539 | 7539 |
| `libxt_owner.so` | F:libxt_owner.so | E10:libxt_owner.so | 是 | 11878 | 11878 |
| `libxt_TEE.so` | F:libxt_TEE.so | E10:libxt_TEE.so | 是 | 8317 | 8317 |
| `libxt_policy.so` | F:libxt_policy.so | E10:libxt_policy.so | 是 | 13674 | 13674 |
| `libipt_ecn.so` | F:libipt_ecn.so | E10:libipt_ecn.so | 是 | 7535 | 7535 |
| `libxt_iprange.so` | F:libxt_iprange.so | E10:libxt_iprange.so | 是 | 10676 | 10676 |
| `libip6t_frag.so` | F:libip6t_frag.so | E10:libip6t_frag.so | 是 | 8966 | 8966 |
| `libip6t_ah.so` | F:libip6t_ah.so | E10:libip6t_ah.so | 是 | 8151 | 8151 |
| `libipt_MIRROR.so` | F:libipt_MIRROR.so | E10:libipt_MIRROR.so | 是 | 5007 | 5007 |
| `libipt_ttl.so` | F:libipt_ttl.so | E10:libipt_ttl.so | 是 | 7324 | 7324 |
| `libxt_RATEEST.so` | F:libxt_RATEEST.so | E10:libxt_RATEEST.so | 是 | 8996 | 8996 |
| `libip6t_dst.so` | F:libip6t_dst.so | E10:libip6t_dst.so | 是 | 8819 | 8819 |
| `libxt_hashlimit.so` | F:libxt_hashlimit.so | E10:libxt_hashlimit.so | 是 | 18660 | 18660 |
| `libipt_CLUSTERIP.so` | F:libipt_CLUSTERIP.so | E10:libipt_CLUSTERIP.so | 是 | 9481 | 9481 |
| `libip6t_LOG.so` | F:libip6t_LOG.so | E10:libip6t_LOG.so | 是 | 9809 | 9809 |
| `libxt_connlimit.so` | F:libxt_connlimit.so | E10:libxt_connlimit.so | 是 | 8179 | 8179 |
| `libxt_ipvs.so` | F:libxt_ipvs.so | E10:libxt_ipvs.so | 是 | 11220 | 11220 |
| `libxt_CLASSIFY.so` | F:libxt_CLASSIFY.so | E10:libxt_CLASSIFY.so | 是 | 6621 | 6621 |
| `libipt_ULOG.so` | F:libipt_ULOG.so | E10:libipt_ULOG.so | 是 | 8840 | 8840 |
| `libipt_icmp.so` | F:libipt_icmp.so | E10:libipt_icmp.so | 是 | 9550 | 9550 |
| `libip6t_ipv6header.so` | F:libip6t_ipv6header.so | E10:libip6t_ipv6header.so | 是 | 9173 | 9173 |
| `libxt_u32.so` | F:libxt_u32.so | E10:libxt_u32.so | 是 | 8680 | 8680 |
| `libxt_multiport.so` | F:libxt_multiport.so | E10:libxt_multiport.so | 是 | 12019 | 12019 |
| `libxt_length.so` | F:libxt_length.so | E10:libxt_length.so | 是 | 7423 | 7423 |
| `libxt_recent.so` | F:libxt_recent.so | E10:libxt_recent.so | 是 | 10193 | 10193 |
| `libxt_SET.so` | F:libxt_SET.so | E10:libxt_SET.so | 是 | 11268 | 11268 |
| `libxt_conntrack.so` | F:libxt_conntrack.so | E10:libxt_conntrack.so | 是 | 20607 | 20607 |
| `libxt_cpu.so` | F:libxt_cpu.so | E10:libxt_cpu.so | 是 | 6671 | 6671 |
| `libipt_SNAT.so` | F:libipt_SNAT.so | E10:libipt_SNAT.so | 是 | 8905 | 8905 |
| `libxt_NFLOG.so` | F:libxt_NFLOG.so | E10:libxt_NFLOG.so | 是 | 8142 | 8142 |
| `libxt_physdev.so` | F:libxt_physdev.so | E10:libxt_physdev.so | 是 | 8748 | 8748 |
| `libxt_CHECKSUM.so` | F:libxt_CHECKSUM.so | E10:libxt_CHECKSUM.so | 是 | 6372 | 6372 |
| `libxt_MARK.so` | F:libxt_MARK.so | E10:libxt_MARK.so | 是 | 9645 | 9645 |
| `libip6t_REJECT.so` | F:libip6t_REJECT.so | E10:libip6t_REJECT.so | 是 | 7725 | 7725 |
| `libxt_connbytes.so` | F:libxt_connbytes.so | E10:libxt_connbytes.so | 是 | 8343 | 8343 |
| `libxt_osf.so` | F:libxt_osf.so | E10:libxt_osf.so | 是 | 8027 | 8027 |
| `libxt_set.so` | F:libxt_set.so | E10:libxt_set.so | 是 | 10715 | 10715 |
| `libxt_standard.so` | F:libxt_standard.so | E10:libxt_standard.so | 是 | 5285 | 5285 |
| `libipt_ah.so` | F:libipt_ah.so | E10:libipt_ah.so | 是 | 7615 | 7615 |
| `libxt_TRACE.so` | F:libxt_TRACE.so | E10:libxt_TRACE.so | 是 | 5002 | 5002 |
| `libxt_dscp.so` | F:libxt_dscp.so | E10:libxt_dscp.so | 是 | 8010 | 8010 |
| `libip6t_mh.so` | F:libip6t_mh.so | E10:libip6t_mh.so | 是 | 10032 | 10032 |
| `libip6t_eui64.so` | F:libip6t_eui64.so | E10:libip6t_eui64.so | 是 | 5002 | 5002 |
| `libxt_CT.so` | F:libxt_CT.so | E10:libxt_CT.so | 是 | 9036 | 9036 |
| `libxt_quota.so` | F:libxt_quota.so | E10:libxt_quota.so | 是 | 6345 | 6345 |
| `libxt_NFQUEUE.so` | F:libxt_NFQUEUE.so | E10:libxt_NFQUEUE.so | 是 | 7561 | 7561 |
| `libxt_IDLETIMER.so` | F:libxt_IDLETIMER.so | E10:libxt_IDLETIMER.so | 是 | 7045 | 7045 |
| `libxt_comment.so` | F:libxt_comment.so | E10:libxt_comment.so | 是 | 6921 | 6921 |
| `libxt_tcpmss.so` | F:libxt_tcpmss.so | E10:libxt_tcpmss.so | 是 | 7289 | 7289 |
| `libxt_rateest.so` | F:libxt_rateest.so | E10:libxt_rateest.so | 是 | 11860 | 11860 |
| `libipt_ECN.so` | F:libipt_ECN.so | E10:libipt_ECN.so | 是 | 7425 | 7425 |
| `libxt_TOS.so` | F:libxt_TOS.so | E10:libxt_TOS.so | 是 | 9869 | 9869 |
| `libip6t_icmp6.so` | F:libip6t_icmp6.so | E10:libip6t_icmp6.so | 是 | 9326 | 9326 |
| `libxt_TPROXY.so` | F:libxt_TPROXY.so | E10:libxt_TPROXY.so | 是 | 7510 | 7510 |
| `libxt_string.so` | F:libxt_string.so | E10:libxt_string.so | 是 | 10563 | 10563 |
| `libip6t_hbh.so` | F:libip6t_hbh.so | E10:libip6t_hbh.so | 是 | 8851 | 8851 |
| `libipt_REJECT.so` | F:libipt_REJECT.so | E10:libipt_REJECT.so | 是 | 8786 | 8786 |
| `libxt_TCPMSS.so` | F:libxt_TCPMSS.so | E10:libxt_TCPMSS.so | 是 | 7031 | 7031 |
| `libxt_helper.so` | F:libxt_helper.so | E10:libxt_helper.so | 是 | 6815 | 6815 |
| `libxt_connmark.so` | F:libxt_connmark.so | E10:libxt_connmark.so | 是 | 7802 | 7802 |
| `libxt_tos.so` | F:libxt_tos.so | E10:libxt_tos.so | 是 | 9067 | 9067 |
| `libxt_CONNSECMARK.so` | F:libxt_CONNSECMARK.so | E10:libxt_CONNSECMARK.so | 是 | 6835 | 6835 |
| `libipt_REDIRECT.so` | F:libipt_REDIRECT.so | E10:libipt_REDIRECT.so | 是 | 7360 | 7360 |
| `libip6t_hl.so` | F:libip6t_hl.so | E10:libip6t_hl.so | 是 | 7132 | 7132 |
| `libxt_statistic.so` | F:libxt_statistic.so | E10:libxt_statistic.so | 是 | 8837 | 8837 |
| `libxt_dccp.so` | F:libxt_dccp.so | E10:libxt_dccp.so | 是 | 10177 | 10177 |
| `libxt_mark.so` | F:libxt_mark.so | E10:libxt_mark.so | 是 | 7742 | 7742 |
| `libxt_socket.so` | F:libxt_socket.so | E10:libxt_socket.so | 是 | 5000 | 5000 |
| `libxt_LED.so` | F:libxt_LED.so | E10:libxt_LED.so | 是 | 7862 | 7862 |
| `libipt_addrtype.so` | F:libipt_addrtype.so | E10:libipt_addrtype.so | 是 | 10377 | 10377 |
| `libxt_pkttype.so` | F:libxt_pkttype.so | E10:libxt_pkttype.so | 是 | 7576 | 7576 |
| `libip6t_HL.so` | F:libip6t_HL.so | E10:libip6t_HL.so | 是 | 7153 | 7153 |
| `libxt_time.so` | F:libxt_time.so | E10:libxt_time.so | 是 | 12255 | 12255 |
| `libipt_SAME.so` | F:libipt_SAME.so | E10:libipt_SAME.so | 是 | 7935 | 7935 |
| `libxt_tcp.so` | F:libxt_tcp.so | E10:libxt_tcp.so | 是 | 10781 | 10781 |
| `libxt_TCPOPTSTRIP.so` | F:libxt_TCPOPTSTRIP.so | E10:libxt_TCPOPTSTRIP.so | 是 | 7977 | 7977 |
| `libxt_CONNMARK.so` | F:libxt_CONNMARK.so | E10:libxt_CONNMARK.so | 是 | 11609 | 11609 |
| `libipt_LOG.so` | F:libipt_LOG.so | E10:libipt_LOG.so | 是 | 9925 | 9925 |
| `libipt_unclean.so` | F:libipt_unclean.so | E10:libipt_unclean.so | 是 | 5005 | 5005 |
| `libxt_sctp.so` | F:libxt_sctp.so | E10:libxt_sctp.so | 是 | 12659 | 12659 |
| `libxt_NOTRACK.so` | F:libxt_NOTRACK.so | E10:libxt_NOTRACK.so | 是 | 5008 | 5008 |
| `libipt_realm.so` | F:libipt_realm.so | E10:libipt_realm.so | 是 | 8640 | 8640 |
| `libipt_DNAT.so` | F:libipt_DNAT.so | E10:libipt_DNAT.so | 是 | 8905 | 8905 |
| `libxt_SECMARK.so` | F:libxt_SECMARK.so | E10:libxt_SECMARK.so | 是 | 6809 | 6809 |
| `libxt_cluster.so` | F:libxt_cluster.so | E10:libxt_cluster.so | 是 | 8692 | 8692 |
| `libxt_limit.so` | F:libxt_limit.so | E10:libxt_limit.so | 是 | 10191 | 10191 |
| `libxt_mac.so` | F:libxt_mac.so | E10:libxt_mac.so | 是 | 7254 | 7254 |
| `libipt_TTL.so` | F:libipt_TTL.so | E10:libipt_TTL.so | 是 | 7171 | 7171 |
| `libip6t_rt.so` | F:libip6t_rt.so | E10:libip6t_rt.so | 是 | 10663 | 10663 |
| `libxt_state.so` | F:libxt_state.so | E10:libxt_state.so | 是 | 7603 | 7603 |
| `libxt_udp.so` | F:libxt_udp.so | E10:libxt_udp.so | 是 | 8296 | 8296 |
| `libipt_NETMAP.so` | F:libipt_NETMAP.so | E10:libipt_NETMAP.so | 是 | 7370 | 7370 |
| `libxt_DSCP.so` | F:libxt_DSCP.so | E10:libxt_DSCP.so | 是 | 7860 | 7860 |
| `libipt_MASQUERADE.so` | F:libipt_MASQUERADE.so | E10:libipt_MASQUERADE.so | 是 | 7214 | 7214 |
| `ld-linux.so.3` | F:ld-2.11.2.so | F:ld-2.11.2.so | 是 | 101736 | 101736 |
| `libgcc_s.so.1` | F:libgcc_s.so.1 | F:libgcc_s.so.1 | 是 | 252937 | 252937 |
| `libc.so.6` | F:libc-2.11.2.so | F:libc-2.11.2.so | 是 | 984292 | 984292 |
| `libupdatercommon.so` | F:libupdatercommon.so | F:libupdatercommon.so | **否** | 37004 | 37048 |
| `AVBB_SCN_STOP_PANELEVF.so` | F:AVBB_SCN_STOP_PANELEVF.so | E10:AVBB_SCN_STOP_PANELEVF.so | 是 | 10806 | 10806 |
| `AVBB_SCN_START_MICSENSE.so` | F:AVBB_SCN_START_MICSENSE.so | E10:AVBB_SCN_START_MICSENSE.so | 是 | 10501 | 10501 |
| `AVBB_SCN_STOP_VIDEO.so` | F:AVBB_SCN_STOP_VIDEO.so | E10:AVBB_SCN_STOP_VIDEO.so | 是 | 10388 | 10388 |
| `AVBB_SCN_OPTAXIS_PANELEVF.so` | F:AVBB_SCN_OPTAXIS_PANELEVF.so | E10:AVBB_SCN_OPTAXIS_PANELEVF.so | 是 | 4828 | 4828 |
| `AVBB_SCN_L_R_SETTING_PANELEVF.so` | F:AVBB_SCN_L_R_SETTING_PANELEVF.so | E10:AVBB_SCN_L_R_SETTING_PANELEVF.so | 是 | 4832 | 4832 |
| `AVBB_SCN_STOP_HDMI.so` | F:AVBB_SCN_STOP_HDMI.so | E10:AVBB_SCN_STOP_HDMI.so | 是 | 13908 | 13908 |
| `CAMERA_SCN_EE_START_STOP.so` | F:CAMERA_SCN_EE_START_STOP.so | E10:CAMERA_SCN_EE_START_STOP.so | 是 | 18317 | 18317 |
| `AVBB_SCN_STOP_MICSENSE.so` | F:AVBB_SCN_STOP_MICSENSE.so | E10:AVBB_SCN_STOP_MICSENSE.so | 是 | 10496 | 10496 |
| `MPR_SCN_EXEC_FACTORY_MODE.so` | F:MPR_SCN_EXEC_FACTORY_MODE.so | E10:MPR_SCN_EXEC_FACTORY_MODE.so | **否** | 5633 | 6192 |
| `AVBB_SCN_START_HDMI_INPUT_SUB.so` | F:AVBB_SCN_START_HDMI_INPUT_SUB.so | E10:AVBB_SCN_START_HDMI_INPUT_SUB.so | 是 | 17200 | 17200 |
| `AVBB_SCN_STOP_AUDIO.so` | F:AVBB_SCN_STOP_AUDIO.so | E10:AVBB_SCN_STOP_AUDIO.so | 是 | 11845 | 11845 |
| `AVBB_SCN_START_MIC.so` | F:AVBB_SCN_START_MIC.so | E10:AVBB_SCN_START_MIC.so | 是 | 12108 | 12108 |
| `CAMERA_SCN_FACE_REC_DATA.so` | F:CAMERA_SCN_FACE_REC_DATA.so | E10:CAMERA_SCN_FACE_REC_DATA.so | **否** | 7552 | 7552 |
| `CAMERA_SCN_SET_ZOOM_DRV.so` | F:CAMERA_SCN_SET_ZOOM_DRV.so | E10:CAMERA_SCN_SET_ZOOM_DRV.so | 是 | 6453 | 6453 |
| `AVBB_SCN_START_HDMI_SUB.so` | F:AVBB_SCN_START_HDMI_SUB.so | E10:AVBB_SCN_START_HDMI_SUB.so | 是 | 14318 | 14318 |
| `CAMERA_SCN_EXTERNAL_RAW_RECORD.so` | F:CAMERA_SCN_EXTERNAL_RAW_RECORD.so | E10:CAMERA_SCN_EXTERNAL_RAW_RECORD.so | 是 | 7793 | 7793 |
| `CAMERA_SCN_MOVIE_REC_START.so` | F:CAMERA_SCN_MOVIE_REC_START.so | E10:CAMERA_SCN_MOVIE_REC_START.so | 是 | 7104 | 7104 |
| `AVBB_SCN_START_HDMI.so` | F:AVBB_SCN_START_HDMI.so | E10:AVBB_SCN_START_HDMI.so | 是 | 20568 | 20568 |
| `AVBB_SCN_L_R_SETTING.so` | F:AVBB_SCN_L_R_SETTING.so | E10:AVBB_SCN_L_R_SETTING.so | 是 | 4824 | 4824 |
| `AVBB_SCN_START_AUDIO.so` | F:AVBB_SCN_START_AUDIO.so | E10:AVBB_SCN_START_AUDIO.so | 是 | 10546 | 10546 |
| `CAMERA_SCN_EE_SETUP_START_STOP.so` | F:CAMERA_SCN_EE_SETUP_START_STOP.so | E10:CAMERA_SCN_EE_SETUP_START_STOP.so | 是 | 17087 | 17087 |
| `CAMERA_SCN_VIDEOLIGHT_SW.so` | F:CAMERA_SCN_VIDEOLIGHT_SW.so | E10:CAMERA_SCN_VIDEOLIGHT_SW.so | 是 | 6604 | 6604 |
| `AVBB_SCN_START_VIDEO.so` | F:AVBB_SCN_START_VIDEO.so | E10:AVBB_SCN_START_VIDEO.so | 是 | 10631 | 10631 |
| `AVBB_SCN_START_HDMI_INPUT.so` | F:AVBB_SCN_START_HDMI_INPUT.so | E10:AVBB_SCN_START_HDMI_INPUT.so | 是 | 17367 | 17367 |
| `AVBB_SCN_STOP_HDMI_INPUT.so` | F:AVBB_SCN_STOP_HDMI_INPUT.so | E10:AVBB_SCN_STOP_HDMI_INPUT.so | 是 | 18951 | 18951 |
| `CAMERA_SCN_EE_SET_PRM.so` | F:CAMERA_SCN_EE_SET_PRM.so | E10:CAMERA_SCN_EE_SET_PRM.so | 是 | 8964 | 8964 |
| `MPR_SCN_EXEC_WIFI_TEST.so` | F:MPR_SCN_EXEC_WIFI_TEST.so | E10:MPR_SCN_EXEC_WIFI_TEST.so | 是 | 5634 | 5634 |
| `AVBB_SCN_START_PANELEVF.so` | F:AVBB_SCN_START_PANELEVF.so | E10:AVBB_SCN_START_PANELEVF.so | 是 | 13081 | 13081 |
| `AVBB_SCN_STOP_MIC.so` | F:AVBB_SCN_STOP_MIC.so | E10:AVBB_SCN_STOP_MIC.so | 是 | 11849 | 11849 |
| `init` | F:init | E10:init | **否** | 28692 | 28580 |
| `dosfsck` | F:dosfsck | E10:dosfsck | 是 | 64790 | 64790 |

## 2. 表 a —— `appFw.so` 未定义符号可解析性

| 口径 | 可解析 | 占比 | 未解析 |
|------|--------|------|--------|
| 仅 E10_CORE（nflasha15/lib + 根文件系统 lib） | 568 | 79.2% | 149 |
| 仅 E10_ALL（含 initrd / updater 库） | 568 | 79.2% | 149 |
| 仅 F_ALL（ZV-1F 全部库） | 714 | 99.6% | 3 |
| MERGED（两树合并） | 714 | 99.6% | 3 |

> `appFw.so` 的 DT_NEEDED 共 13 条：`libNetCommon.so`, `libGOConnectionManager.so`, `AdjChkConfig.so`, `libqrencode.so`, `libInfraSKD.so`, `libpthread.so.0`, `libstdc++.so.6`, `libm.so.6`, `libdl.so.2`, `libgcc_s.so.1`, `libc.so.6`, `ld-linux.so.3`, `librt.so.1`

**关键观察（实测）**：ZV-E10 自己的库只能满足 `appFw.so` 的 79.2%；差距 149 个符号必须由 ZV-1F 侧文件补齐。

### 2.1 各根对象的传递闭包（DT_NEEDED 闭包，MERGED 口径）

| 根对象 | 树 | 大小 | NEEDED 条数 | 闭包大小 | 缺失 NEEDED | 未定义符号 | 闭包内可解析 | 闭包内未解析 |
|--------|----|------|-------------|----------|-------------|------------|--------------|--------------|
| `appFw.so` | F | 53189440 | 13 | 21 | 无 | 717 | 301 | 416 |
| `appFwBoot.so` | F | 8396 | 7 | 9 | 无 | 36 | 9 | 27 |
| `libInfraSKD.so` | F | 7259708 | 7 | 9 | 无 | 142 | 138 | 4 |
| `libObj.so` | F | 3929768 | 12 | 29 | 无 | 688 | 656 | 32 |
| `libmpr.so` | F | 3024500 | 8 | 10 | 无 | 302 | 246 | 56 |
| `libSysDef.so` | F | 6033848 | 7 | 9 | 无 | 6 | 4 | 2 |
| `AdjChkConfig.so` | F | 82440 | 7 | 9 | 无 | 180 | 31 | 149 |
| `libIMDB.so` | F | 40596 | 7 | 9 | 无 | 6 | 4 | 2 |
| `libInfraCryptCore.so` | F | 7759348 | 7 | 9 | 无 | 156 | 137 | 19 |
| `libInfraCCPF.so` | F | 906032 | 19 | 32 | 无 | 411 | 391 | 20 |

> **闭包口径的严格性**：动态链接器在加载 `appFw.so` 时，全局查找域 = 其 DT_NEEDED 传递闭包（RTLD_GLOBAL 的 dlopen 会扩充该域）。上表第 8 列即“严格闭包口径”，第 2 列是“整个 MERGED 都摆上桌”的乐观口径。

### 2.2 `appFw.so` 未定义符号按前缀分组的覆盖情况（回答 `ux::` / `BMP_` / `osal_` / `GRM_` / `Fsys_` / `InfraInput::` 到底谁来提供）

| 前缀分组 | UND 总数 | E10 可提供 | 仅 F 可提供 | F 也无法提供 | 主要提供库（Top4） |
|----------|----------|------------|-------------|--------------|--------------------|
| `ux::` | 144 | 108 | 36 | 0 | `libObj.so`×144 |
| `std::` | 83 | 83 | 0 | 0 | `libstdc++.so.6`×83 |
| `mpr::` | 49 | 0 | 49 | 0 | `libmpr.so`×49 |
| `MWF::` | 49 | 49 | 0 | 0 | `libMWF.so`×49 |
| `C_DataflowInfra_Entry_ImageSizeInfo::` | 24 | 24 | 0 | 0 | `dataflowInfraFramework.so`×24 |
| `libra::` | 24 | 12 | 12 | 0 | `libObj.so`×24 |
| `osal_` | 19 | 19 | 0 | 0 | `libosal_uipc.so`×14、`libosal_utm.so`×5 |
| `GCM_::` | 17 | 15 | 2 | 0 | `libGOConnectionManager.so`×17 |
| `Fsys_` | 16 | 15 | 1 | 0 | `libInfraFsys.so`×16 |
| `sidb::` | 15 | 0 | 15 | 0 | `libmpr.so`×15 |
| `Backup_` | 14 | 14 | 0 | 0 | `libbackup.so`×14 |
| `C++(other)` | 13 | 13 | 0 | 0 | `libstdc++.so.6`×13 |
| `pthread_` | 12 | 12 | 0 | 0 | `libpthread.so.0`×12、`libc.so.6`×7 |
| `Json::` | 11 | 11 | 0 | 0 | `libjsoncpp.so.23`×11 |
| `InfraInput::` | 10 | 7 | 3 | 0 | `libObj.so`×10 |
| `C_DataflowInfra_Entry_YcStateInfo::` | 10 | 10 | 0 | 0 | `dataflowInfraFramework.so`×10 |
| `alcor::` | 9 | 9 | 0 | 0 | `libObj.so`×9 |
| `NetCommonLibrary_::` | 9 | 9 | 0 | 0 | `libNetCommon.so`×9 |
| `network::` | 9 | 0 | 9 | 0 | `libCertificateManager.so`×9 |
| `C_DataflowInfra_StdData_YC::` | 8 | 8 | 0 | 0 | `dataflowInfraFramework.so`×8 |
| `vtable for __cxxabiv1::` | 7 | 7 | 0 | 0 | `libstdc++.so.6`×7 |
| `DataflowInfraMsg::` | 7 | 7 | 0 | 0 | `dataflowInfraFramework.so`×7 |
| `int sidb::` | 7 | 0 | 7 | 0 | `libmpr.so`×7 |
| `DataflowInfraMsgEx::` | 7 | 7 | 0 | 0 | `dataflowInfraFramework.so`×7 |
| `stella::` | 6 | 4 | 2 | 0 | `libObj.so`×6 |
| `EVP_` | 6 | 6 | 0 | 0 | `libInfraSKD.so`×6、`libcrypto.so.3`×6、`libcrypto.so.1.1`×6、`libInfraCryptCore.so`×6 |
| `GRM_` | 5 | 1 | 4 | 0 | `libObj.so`×5 |
| `DataflowInfraSender::` | 4 | 4 | 0 | 0 | `dataflowInfraFramework.so`×4 |
| `CIFInfraIndctr::` | 4 | 4 | 0 | 0 | `libObj.so`×4 |
| `C_DataflowInfra_Entry_ImageOutTimInfo::` | 4 | 4 | 0 | 0 | `dataflowInfraFramework.so`×4 |
| `DataflowInfraReceiver::` | 4 | 4 | 0 | 0 | `dataflowInfraFramework.so`×4 |
| `SHA256_` | 3 | 3 | 0 | 0 | `libInfraSKD.so`×3、`libcrypto.so.3`×3、`libcrypto.so.1.1`×3、`libInfraCryptCore.so`×3 |
| `QRcode_` | 3 | 3 | 0 | 0 | `libqrencode.so`×3 |
| `UXEngine::` | 3 | 3 | 0 | 0 | `libObj.so`×3 |
| `kikilog_` | 2 | 2 | 0 | 0 | `libInfraKikiLogGenerator.so`×2 |
| `__aeabi_uidi...` | 2 | 2 | 0 | 0 | `libgcc_s.so.1`×2 |
| `DefInfraCCPFStatic::` | 2 | 0 | 2 | 0 | `libInfraCCPFStatic.so`×2 |
| `__aeabi_unwi...` | 2 | 2 | 0 | 0 | `libgcc_s.so.1`×2 |
| `InfraDial::` | 2 | 1 | 1 | 0 | `libObj.so`×2 |
| `SKData_` | 2 | 2 | 0 | 0 | `libInfraSKD.so`×2 |
| `BMP_` | 2 | 0 | 2 | 0 | `libObj.so`×2 |
| `date_` | 2 | 2 | 0 | 0 | `libutil_date.so`×2 |
| `__cxa_guard_...` | 2 | 2 | 0 | 0 | `libstdc++.so.6`×2 |

其余 84 个分组各只含 1 个符号（绝大多数是 libc/libm/libgcc 的单个函数，如 `malloc`/`memcpy`/`__aeabi_idiv` 等，全部可在 E10 侧解析）：`AccessLog_`、`_Jv_Register...`、`__aeabi_atex...`、`__aeabi_d2lz`、`__aeabi_d2ul...`、`__aeabi_idiv`、`__aeabi_idiv...`、`__aeabi_l2d`、`__aeabi_ldiv...`、`__aeabi_llsl`、`__aeabi_llsr`、`__aeabi_ul2d`、`__aeabi_ul2f`、`__aeabi_uldi...`、`__assert_fai...`、`__cxa_finali...`、`__cxa_pure_v...`、`__dynamic_ca...`、`__errno_loca...`、`__gmon_start...`、`__once_proxy`、`__tls_get_ad...`、`__xstat`、`atof`、`atoi`、`ceil`、`ceilf`、`close`、`dlclose`、`dlopen`、`dlsym`、`execl`、`fabsf`、`fclose`、`feof`、`fopen`、`fread`、`free`、`fsync`、`fwrite`…。

> “E10 可提供”与“仅 F 可提供”不互斥地覆盖全部；`仅 F 可提供` 即必须借 ZV-1F 的那部分。
> 关键结论：**`ux::`、`BMP_`、`libra`、`osal_`、`GRM_`、`Fsys_`、`InfraInput::` 这些新菜单符号全部有提供者**，
> 其中大部分只能在 ZV-1F 侧找到 —— 这正是必须借库的原因，但它们**不是**残余缺口。
> 尤其是 `ux::`（144 个）与 `BMP_`（2 个）**全部由 ZV-1F 的 `libObj.so` 提供**，而 ZV-E10 同名 `libObj.so` 只能提供其中 108 个 → 再次印证同名库必须让 ZV-1F 胜出（见 §8）。

## 3. 表 b —— 必须从 ZV-1F 借入的库（按“能独特补齐多少缺口符号”排序）

缺口定义：`gap = UND(appFw.so) \ 定义符号(E10_ALL)`，共 **149** 个。

| # | SONAME | ZV-E10 大小 | ZV-1F 大小 | Δ | 可补缺口符号数 | 其中唯一提供者 |
|---|--------|-------------|------------|---|----------------|----------------|
| 1 | `libmpr.so` | 13021036 | 3024500 | -9996536 | 71 | 71 |
| 2 | `libObj.so` | 21305528 | 3929768 | -17375760 | 61 | 61 |
| 3 | `libCertificateManager.so` | 不存在 | 189824 | 新增 | 9 | 9 |
| 4 | `libGOConnectionManager.so` | 70228 | 72108 | +1880 | 2 | 2 |
| 5 | `libInfraCCPFStatic.so` | 不存在 | 143808 | 新增 | 2 | 2 |
| 6 | `libInfraFsys.so` | 428492 | 549168 | +120676 | 1 | 1 |

> 仅列出至少能补 1 个缺口符号的 ZV-1F 库。完整列表见 JSON 的 `gap.f_contrib`。

## 4. 表 c —— MERGED 之后仍然未解析的符号

### 4.1 `appFw.so` 残余（3 个）

按标识符前缀分组：

| 前缀分组 | 符号数 | 来源判定分布 |
|----------|--------|--------------|
| `_Jv_Register...` | 1 | absent=1 |
| `__gmon_start...` | 1 | absent=1 |
| `infra_wrapper_bb::` | 1 | absent=1 |

符号明细（全部 3 条，含加载期语义）：

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | 惰性/立即重定位 | 引用模块 |
|------|--------|------------|------------|------------------|----------|
| `_Jv_RegisterClasses` | `_Jv_RegisterClasses` | 两树内无提供者 | WEAK 未定义(非致命) | lazy=0, eager=1 | `appFw.so` |
| `_ZN16infra_wrapper_bb6detail12adf_ext_typeERKNS_17adf_ext_type_in_tERNS_18adf_ext_type_out_tE` | `infra_wrapper_bb::detail::adf_ext_type(infra_wrapper_bb::adf_ext_type_in_t const&, infra_wrapper_bb::adf_ext_type_out_t&)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | lazy=1, eager=0 | `appFw.so` |
| `__gmon_start__` | `__gmon_start__` | 两树内无提供者 | WEAK 未定义(非致命) | lazy=1, eager=1 | `appFw.so` |

### 4.2 整个新菜单根集合残余（25 个，分组统计）

| 前缀分组 | 符号数 | 来源判定分布 | 归属判定 |
|----------|--------|--------------|----------|
| `PTPT_` | 10 | absent=10 | **两树内无任何提供者（硬缺口）** |
| `InfraRemoteSrcBGRead::` | 6 | absent=6 | **两树内无任何提供者（硬缺口）** |
| `GpsMgr::` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `_Jv_Register...` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `__gmon_start...` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `app_adjchk_ui::` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `arch_` | 1 | kernel=1 | 内核模块 `__ksymtab` 导出 [推测] |
| `bool std::` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `getauxval` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `infra_wrapper_bb::` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |
| `std::` | 1 | absent=1 | **两树内无任何提供者（硬缺口）** |

分组内的代表符号：

**`PTPT_`**（10 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `PTPT_close_ip` | `PTPT_close_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_fin_ip` | `PTPT_fin_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_free_buffer_ip` | `PTPT_free_buffer_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_get_buffer_ip` | `PTPT_get_buffer_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_init_ip` | `PTPT_init_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_open_ip` | `PTPT_open_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_recv_data_ip` | `PTPT_recv_data_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_send_data_ip` | `PTPT_send_data_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_send_event_ip` | `PTPT_send_event_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| `PTPT_set_payloadsize_ip` | `PTPT_set_payloadsize_ip` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |

**`InfraRemoteSrcBGRead::`**（6 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_ZN20InfraRemoteSrcBGRead11entryTargetEjj` | `InfraRemoteSrcBGRead::entryTarget(unsigned int, unsigned int)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| `_ZN20InfraRemoteSrcBGRead14acquirePhyAddrEPjj` | `InfraRemoteSrcBGRead::acquirePhyAddr(unsigned int*, unsigned int)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| `_ZN20InfraRemoteSrcBGRead14releasePhyAddrEj` | `InfraRemoteSrcBGRead::releasePhyAddr(unsigned int)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| `_ZN20InfraRemoteSrcBGRead14setRequestInfoEjjj` | `InfraRemoteSrcBGRead::setRequestInfo(unsigned int, unsigned int, unsigned int)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| `_ZN20InfraRemoteSrcBGRead5clearEv` | `InfraRemoteSrcBGRead::clear()` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| `_ZN20InfraRemoteSrcBGRead6updateEv` | `InfraRemoteSrcBGRead::update()` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |

**`GpsMgr::`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_ZN6GpsMgr26SetAllInvalidDefStrGpsInfoEPN9DefStruct7GpsInfoE` | `GpsMgr::SetAllInvalidDefStrGpsInfo(DefStruct::GpsInfo*)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libmpr.so` |

**`_Jv_Register...`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_Jv_RegisterClasses` | `_Jv_RegisterClasses` | 两树内无提供者 | WEAK 未定义(非致命) | 0/104 | `AdjChkConfig.so`, `CameraOnDemandSaver.so` |

**`__gmon_start...`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `__gmon_start__` | `__gmon_start__` | 两树内无提供者 | WEAK 未定义(非致命) | 104/104 | `AdjChkConfig.so`, `CameraOnDemandSaver.so` |

**`app_adjchk_ui::`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_ZNK13app_adjchk_ui22infra_wrapper_dataflow12ds_infra_pin4dumpEv` | `app_adjchk_ui::infra_wrapper_dataflow::ds_infra_pin::dump() const` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `AdjChkConfig.so` |

**`arch_`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `arch_phys_to_cache` | `arch_phys_to_cache` | 内核导出 | 惰性绑定(仅首次调用才报错) | 2/0 | `dataflowInfraFramework.so`, `libObj.so` |

**`bool std::`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_ZSt12regex_searchIN9__gnu_cxx17__normal_iteratorIPKcSsEESaISt9sub_matchIS4_EEcSt12regex_traitsIcEEbT_SA_RSt13match_resultsISA_T0_ERKSt11basic_regexIT1_T2_ESt6bitsetILj11EE` | `bool std::regex_search<__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > >, std::allocator<std::sub_match<__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > > > >, char, std::regex_traits<char> >(__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > >, __gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > >, std::match_results<__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > >, std::allocator<std::sub_match<__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<char>, std::allocator<char> > > > > >&, std::basic_regex<char, std::regex_traits<char> > const&, std::bitset<11u>)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libmpr.so` |

**`getauxval`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `getauxval` | `getauxval` | 两树内无提供者 | WEAK 未定义(非致命) | 1/1 | `libInfraCryptCore.so` |

**`infra_wrapper_bb::`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_ZN16infra_wrapper_bb6detail12adf_ext_typeERKNS_17adf_ext_type_in_tERNS_18adf_ext_type_out_tE` | `infra_wrapper_bb::detail::adf_ext_type(infra_wrapper_bb::adf_ext_type_in_t const&, infra_wrapper_bb::adf_ext_type_out_t&)` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `appFw.so` |

**`std::`**（1 个）

| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |
|------|--------|------------|------------|------------|----------|
| `_ZNSt11basic_regexIcSt12regex_traitsIcEE10_M_compileEv` | `std::basic_regex<char, std::regex_traits<char> >::_M_compile()` | 两树内无提供者 | 惰性绑定(仅首次调用才报错) | 1/0 | `libmpr.so` |

### 4.3 来源判定的方法与含义

| 判定 | 含义 | 判定方法（实测命令） |
|------|------|----------------------|
| 内核 (`kernel`) | 由内核模块通过 `__ksymtab` 导出；**不是**用户态动态符号，动态链接器看不到 | `readelf -W --dyn-syms *.ko` 实测为 0 行 → 改为对 `.ko` 原始字节做标识符抽取（`nm -D` 无效，已实测） |
| 可执行文件 (`executable`) | 由 `bin/ sbin/ sabin/ tool/` 里的 ELF 可执行文件导出（需 `-rdynamic`） | `readelf -W --dyn-syms` 取其 DEF 符号集 |
| 字符串出现 (`raw-string`) | 符号名以字符串形式出现在某文件里 → 只能说明被引用/`dlsym` 查找，**不等于有定义** | `grep -rlF -f <symbols>` 全树字节扫描；**已剔除“引用者自身的 .dynstr”**（导入方必然含该名字，否则是自我印证的假证据） **[推测]** |
| 无 (`absent`) | 两棵树内任何文件都不含该符号名 → 真正缺失 | 同上，取补集 |

## 5. 表 d —— 最终判定：闭包是否可用有限文件集达成

### 5.1 按“加载期是否致命”分类（决定性判据）

| 加载期语义 | 个数 | 是否阻塞加载 | 说明 |
|------------|------|--------------|------|
| 惰性绑定(仅首次调用才报错) | 22 | 否 | 仅 R_ARM_JUMP_SLOT；无 BIND_NOW → 加载期不报错，调用到才报错 |
| WEAK 未定义(非致命) | 3 | 否 | WEAK UND，glibc 允许为 0，永远不报错（旧 gcc 遗留） |

> 判据来源：`readelf -W -r` 的重定位类型 + `readelf -W -d` 是否含 `BIND_NOW`/`DF_1_NOW`。
> 目标模块若未设 `BIND_NOW`（`appFw.so` 实测未设），则 `R_ARM_JUMP_SLOT` 为惰性绑定，
> 未定义符号**不阻塞加载**，只在被调用时失败。

**遮蔽复核（UNION_ALL）**：忽略同名 SONAME 取舍规则，把两棵树**每一个**库的导出符号全部并起来（共 223304 个符号），残余为 `appFw.so` **3** 个 / 新菜单根集合 **25** 个 —— 与 `MERGED(prefer=F)` 口径下的 3 / 25 完全一致，说明残余不是因为“该拿 ZV-E10 的大库却被 ZV-1F 小库遮蔽”造成的。

### 5.2 按“提供者位于何处”分类

| 类别 | 数量 | 是否阻塞移植 |
|------|------|--------------|
| 用户态 ELF 库可解析（MERGED） | 714 | 否 |
| 内核模块 `__ksymtab` 提供 | 1 | 否（内核侧提供，用户态动态链接器本就看不到） **[推测]** |
| 可执行文件导出提供 | 0 | 否（需该进程已加载） **[推测]** |
| 仅字符串出现（疑似 `dlsym`/运行时查找） | 0 | 需运行时验证 **[推测]** |
| **两树内完全无提供者** | **24** | 见 5.1 的加载期语义判定 |

### 5.3 最终判定

**闭包达成。** 在新菜单根集合的 25 个残余符号中，**没有一个**是加载期立即绑定的致命缺口；
全部为 WEAK 未定义或仅经 PLT 惰性引用。因此存在有限文件集（`E10_ALL` ∪ 上述 6 个 ZV-1F 库）使新菜单**可以成功加载**。

剩余风险不在"能否加载"，而在"运行时是否走到那条代码路径"：

- `appFw.so` 内 1 个 GLOBAL 惰性缺口 `_ZN16infra_wrapper_bb6detail12adf_ext_type...`，一旦其 PLT 桩被调用即会 `symbol lookup error`。**[推测]** 该符号属于 `detail::` 内部实现，且在整个 ZV-1F 镜像内无提供者，说明 ZV-1F 自身运行时也不会走到该路径。
- `InfraRemoteSrcBGRead*`（6 个）与 `PTPT_*_ip`（10 个）在 **ZV-E10 与 ZV-1F 两棵树里都是 UND**，
  即捐赠方 ZV-1F 自身也是带着这些未解析导入运行的 → 属于既有状态，不是移植引入的新缺口。

**残余符号逐个解读（标 [推测] 者为推断）**

| 符号 / 组 | 实测事实 | 解读 |
|-----------|----------|------|
| `_Jv_RegisterClasses`、`__gmon_start__` | 两树内无定义；导入方为 `WEAK UND`；`appFw.so` 中分别有 1、2 处重定位 | 旧版 ARM GCC / gprof 的运行时遗留钩子，惯例即为 WEAK 未定义，glibc 直接解析为 0，**永不报错**。全树数百个模块都带着它，属正常现象 **[推测]** |
| `_ZN16infra_wrapper_bb6detail12adf_ext_type...` | GLOBAL；仅 `appFw.so` 引用；1 处 `R_ARM_JUMP_SLOT`；两树皆无定义 | `detail::` 内部辅助函数，疑为某个 `infra_wrapper_bb` 静态库未随 `appFw.so` 打包/未导出。因是 PLT 惰性绑定，**加载不受影响**；仅当该分支被执行才报错 **[推测]** |
| `PTPT_*_ip`（10 个） | `libInfraPtpControl.so` 中 20 处 `R_ARM_JUMP_SLOT`；两树皆 UND | PTP/IP 扩展协议族；ZV-1F 自身也未解析 → 既有状态 **[推测]** |
| `_ZN20InfraRemoteSrcBGRead*`（6 个） | `libInfraRemote.so` 中 6 处 `R_ARM_JUMP_SLOT`；两树皆 UND（ZV-E10 那个 8.7MB 版本同样未定义） | 捐赠方既有状态；ZV-E10 现网也是带着它运行的 **[推测]** |
| `getauxval` | `libInfraCryptCore.so` 中 WEAK UND；两树 glibc 均为 2.11.2，**均无 `getauxval`**（glibc 2.16 才引入） | 代码显式按“可能不存在”处理（否则不会声明为 WEAK），glibc 解析为 0。**非加载期问题**；若调用方未判空则运行期才崩 **[推测]** |
| `_ZNSt11basic_regex..._M_compileEv`、`_ZSt12regex_search...` | `libmpr.so` 中各 1 处 `R_ARM_JUMP_SLOT`；两树皆 UND | libstdc++ 正则模板实例未被树中的 `libstdc++.so.6.0.14` 导出；仅走正则路径时触发 **[推测]** |
| `arch_phys_to_cache` | 出现在两树 `backup.ko`、`dmm.ko` 的 `__ksymtab` 原始字节中；`dataflowInfraFramework.so`、`libObj.so` 中为 PLT 引用 | 内核导出符号，用户态动态链接器看不到；属内核-用户态既有约定，非本次移植缺口 **[推测]** |


### 5.4 两树内完全无提供者的符号（精确名单，24 个）

| # | 符号 | 加载期语义 | lazy/eager | 引用模块 |
|---|------|------------|------------|----------|
| 1 | `PTPT_close_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 2 | `PTPT_fin_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 3 | `PTPT_free_buffer_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 4 | `PTPT_get_buffer_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 5 | `PTPT_init_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 6 | `PTPT_open_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 7 | `PTPT_recv_data_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 8 | `PTPT_send_data_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 9 | `PTPT_send_event_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 10 | `PTPT_set_payloadsize_ip` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraPtpControl.so` |
| 11 | `_Jv_RegisterClasses` | WEAK 未定义(非致命) | 0/104 | `AdjChkConfig.so`, `CameraOnDemandSaver.so`, `appFw.so` |
| 12 | `_ZN16infra_wrapper_bb6detail12adf_ext_typeERKNS_17adf_ext_type_in_tERNS_18adf_ext_type_out_tE` | 惰性绑定(仅首次调用才报错) | 1/0 | `appFw.so` |
| 13 | `_ZN20InfraRemoteSrcBGRead11entryTargetEjj` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| 14 | `_ZN20InfraRemoteSrcBGRead14acquirePhyAddrEPjj` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| 15 | `_ZN20InfraRemoteSrcBGRead14releasePhyAddrEj` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| 16 | `_ZN20InfraRemoteSrcBGRead14setRequestInfoEjjj` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| 17 | `_ZN20InfraRemoteSrcBGRead5clearEv` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| 18 | `_ZN20InfraRemoteSrcBGRead6updateEv` | 惰性绑定(仅首次调用才报错) | 1/0 | `libInfraRemote.so` |
| 19 | `_ZN6GpsMgr26SetAllInvalidDefStrGpsInfoEPN9DefStruct7GpsInfoE` | 惰性绑定(仅首次调用才报错) | 1/0 | `libmpr.so` |
| 20 | `_ZNK13app_adjchk_ui22infra_wrapper_dataflow12ds_infra_pin4dumpEv` | 惰性绑定(仅首次调用才报错) | 1/0 | `AdjChkConfig.so` |
| 21 | `_ZNSt11basic_regexIcSt12regex_traitsIcEE10_M_compileEv` | 惰性绑定(仅首次调用才报错) | 1/0 | `libmpr.so` |
| 22 | `_ZSt12regex_searchIN9__gnu_cxx17__normal_iteratorIPKcSsEESaISt9sub_matchIS4_EEcSt12regex_traitsIcEEbT_SA_RSt13match_resultsISA_T0_ERKSt11basic_regexIT1_T2_ESt6bitsetILj11EE` | 惰性绑定(仅首次调用才报错) | 1/0 | `libmpr.so` |
| 23 | `__gmon_start__` | WEAK 未定义(非致命) | 104/104 | `AdjChkConfig.so`, `CameraOnDemandSaver.so`, `appFw.so` |
| 24 | `getauxval` | WEAK 未定义(非致命) | 1/1 | `libInfraCryptCore.so` |

### 5.5 内核导出符号（`__ksymtab`，精确名单）

| # | 符号 | 所在 .ko（前3） |
|---|------|-----------------|
| 1 | `arch_phys_to_cache` | `E10:backup.ko`, `E10:dmm.ko`, `E10:grm_ma.ko` |

## 6. 版本化符号的“软缺口”（soft gap）

对 `appFw.so` 的带版本导入，检查 MERGED 中的提供者是否提供**同版本字符串**。
基名可解析但版本不匹配的情况共 **0** 条。

无。所有版本化导入的版本字符串在 MERGED 中均有对应提供者。

## 7. 两树同名库的 API 破坏面（导出符号增删）

比较口径：两树中 **SONAME 相同且文件大小不同** 的库，共 **58** 个；导出符号（GLOBAL/WEAK，DEF）集合做差。

- ZV-1F **新增**（E10 无）导出符号总计：**18562**
- ZV-1F **丢失**（E10 有）导出符号总计：**34689** ← 这是 ABI 破坏风险的主要来源

| SONAME | E10 大小 | F 大小 | Δ | F 新增 | F 丢失 |
|--------|----------|--------|---|--------|--------|
| `libInfraRemote.so` | 8708516 | 1273012 | -7435504 | 3216 | 29474 |
| `libmpr.so` | 13021036 | 3024500 | -9996536 | 5920 | 3038 |
| `libObj.so` | 21305528 | 3929768 | -17375760 | 74 | 1458 |
| `libInfraIPTC.so` | 109676 | 40696 | -68980 | 3 | 304 |
| `libInfraBluetoothLowEnergy.so` | 264596 | 541388 | +276792 | 1273 | 98 |
| `libInfraFsys.so` | 428492 | 549168 | +120676 | 540 | 69 |
| `libInfraMediaDetect.so` | 78248 | 66508 | -11740 | 17 | 48 |
| `libInfraBluetooth.so` | 90200 | 98952 | +8752 | 69 | 38 |
| `libInfraHttpClientUtil.so` | 15936 | 15380 | -556 | 37 | 37 |
| `libInfraMediaCommon.so` | 331236 | 421776 | +90540 | 177 | 35 |
| `libInfraUsbCmn.so` | 94612 | 93984 | -628 | 22 | 24 |
| `libGOConnectionManager.so` | 70228 | 72108 | +1880 | 20 | 15 |
| `libInfraMtpServer.so` | 171296 | 278948 | +107652 | 581 | 10 |
| `libNetService.so` | 674776 | 678984 | +4208 | 23 | 7 |
| `libInfraHttpClient.so` | 35208 | 36604 | +1396 | 4 | 6 |
| `libInfraHdmi.so` | 401048 | 412952 | +11904 | 52 | 4 |
| `libusbcmd.so` | 15996 | 16000 | +4 | 3 | 3 |
| `libInfraBluetoothCore.so` | 168992 | 217392 | +48400 | 258 | 2 |
| `libInfraNetInitializer.so` | 21132 | 21608 | +476 | 3 | 2 |
| `libInfraAirplaneMode.so` | 7912 | 8028 | +116 | 2 | 2 |
| `libInfraIPsec.so` | 29388 | 27968 | -1420 | 2 | 2 |
| `libInfraNetConnectionManagerEther.so` | 29692 | 29756 | +64 | 2 | 2 |
| `libWlanControlCommon.so` | 14804 | 14792 | -12 | 2 | 2 |
| `libJiritsu.so` | 175092 | 178952 | +3860 | 1 | 2 |
| `libInfraMediaMount.so` | 36416 | 44908 | +8492 | 19 | 1 |
| `libUsbControl.so` | 94388 | 98356 | +3968 | 7 | 1 |
| `CameraOnDemandSaver.so` | 10096 | 10544 | +448 | 1 | 1 |
| `libInfraAccessLogWrapper.so` | 3344 | 3364 | +20 | 1 | 1 |
| `libInfraBluetoothWoLEBoot.so` | 7808 | 7904 | +96 | 1 | 1 |
| `libInfraPtpExtCmd.so` | 34064 | 34136 | +72 | 1 | 1 |
| `MPR_SCN_EXEC_FACTORY_MODE.so` | 6192 | 5633 | -559 | 0 | 1 |
| `libInfraSKD.so` | 10184 | 7259708 | +7249524 | 6169 | 0 |
| `dataflowInfraFramework.so` | 172680 | 176368 | +3688 | 18 | 0 |
| `libInfraDlnaControl.so` | 365952 | 386048 | +20096 | 17 | 0 |
| `libInfraPtpControl.so` | 81576 | 85752 | +4176 | 12 | 0 |
| `libBTUtils.so` | 16472 | 18456 | +1984 | 8 | 0 |
| `libBTAdjust.so` | 10984 | 10912 | -72 | 3 | 0 |
| `libUsbBusPower.so` | 21616 | 22696 | +1080 | 3 | 0 |
| `libRsrcHotshoe.so` | 22428 | 22808 | +380 | 1 | 0 |
| `init` | 28580 | 28692 | +112 | 0 | 0 |

### 7.1 Top-20 丢失符号（E10 有、ZV-1F 无）—— 逐库

**`libInfraRemote.so`**（丢失 29474 个）：

- `_ZN11InfraRemote15ClosePtpSessionEPv`
- `_ZN11InfraRemote19actionControlDeviceEPN3MWF6ObjMsgEPv`
- `_ZN11InfraRemote23GetPredictedMaxFileSizeEPvPN3MWF6ObjMsgES0_`
- `_ZN11InfraRemote9GetIfTypeEv`
- `_ZN13FsysWrapperExC1Et`
- `_ZN13FsysWrapperExC2Et`
- `_ZN13FsysWrapperExD1Ev`
- `_ZN13FsysWrapperExD2Ev`
- `_ZN13InfraRemoteIP11getObjectIDEPcj22PTPT_PAIRING_NECESSITY`
- `_ZN13InfraRemoteIP12m_MacAddressE`
- `_ZN13InfraRemoteIP12m_PtptStatusE`
- `_ZN13InfraRemoteIP15m_InterfaceNameE`
- `_ZN13InfraRemoteIP15startSsdpServerEt22PTPT_PAIRING_NECESSITYjPKcS2_`
- `_ZN13InfraRemoteIP16getInterfaceNameEPc`
- `_ZN13InfraRemoteIP21guardCheckPtptStoppedEPN3UFW3FsmEiPv`
- `_ZN13InfraRemoteIP24actionExitRemoteStoppingEPN3UFW3FsmEiPv`
- `_ZN13InfraRemoteIP32setDigitalImagingServiceDescInfoEt22PTPT_PAIRING_NECESSITYjPKcS2_`
- `_ZN14InfraRemotePtp29actionSetPredictedMaxFileSizeEPN3UFW3FsmEiPv`
- `_ZN14InfraRemoteUsb17SetInputEvtAllOffEPv`
- `_ZN14MtpwHandleList16push_folder_itemEjj`

**`libmpr.so`**（丢失 3038 个）：

- `_Z15GetOnewTidOfMPRm`
- `_Z32FileAccCache_SetLogMprFileIfInfommmm`
- `_Z32avi_GetDCFDateFileAccessPathListP11_avi_EditormmmmPPcPN10CS_AviAcsr12_ConsistencyEPmS6_`
- `_Z34avi_sdk_SetThumbPictFileAccessFlagP10CS_AviAcsrb`
- `_Z37avi_GetDCFDateValidFileAccessPathListP11_avi_EditormmmmPPcPmS3_S3_`
- `_ZN10CS_AviAcsr28GetDCFDateFileAccessPathListEmmmmPPcPNS_12_ConsistencyEPmS4_`
- `_ZN10CS_AviAcsr33GetDCFDateValidFileAccessPathListEmmmmPPcPmS2_S2_`
- `_ZN10DcfChpList18DubbingPostProcessEN3MPR5ErrCdEmb`
- `_ZN10DcfChpList24UpdateDubbingEndPositionEN3MPR5ErrCdEm`
- `_ZN10DiscMMgrIf13NewDvdDcfMMgrEPN5MprIf17DvdDcfCreateParamEPPS_`
- `_ZN10DubLoadLib14CheckMMgrStateEP4MMgrmNS0_8LoadTypeEPmPN3MPR7FmtTypeE`
- `_ZN10DubLoadLib28FormatForBlankDiscForDubbingEN3MPR7FmtTypeENS0_7VAspectE`
- `_ZN10DubLoadLib4LoadEP4MMgrN3MPR11ContentTypeE`
- `_ZN10DubLoadLib6CreateEP4MMgrS1_S1_mmN3MPR11DubbingTypeENS2_14FileSystemTypeEPPS_`
- `_ZN10DubLoadLib6UnloadEP4MMgrN3MPR11ContentTypeE`
- `_ZN10DubLoadLibC1EP4MMgrS1_S1_mmN3MPR11DubbingTypeENS2_14FileSystemTypeE`
- `_ZN10DubLoadLibC2EP4MMgrS1_S1_mmN3MPR11DubbingTypeENS2_14FileSystemTypeE`
- `_ZN10DubPlayRec18AudioPlayerPbStartEN3MPR9PlaySpeedE`
- `_ZN10DubPlayRec24AudioPlayerChangePbSpeedEN3MPR9PlaySpeedE`
- `_ZN10FeaMemInfo17GetEncoderMemInfoEPN7MemInfo6MemTblEPN5MprIf14EncoderMemInfoE`

**`libObj.so`**（丢失 1458 个）：

- `Application_System_Init`
- `CapIdSync_Init`
- `InfraDatabase_Exit`
- `InfraDatabase_Init`
- `InfraMediaPwrMgr_act`
- `InfraMediaPwrMgr_exit`
- `InfraMediaPwrMgr_inact`
- `InfraMediaPwrMgr_init`
- `InfraMediaPwrMgr_res`
- `InfraMediaPwrMgr_sus`
- `InfraSalvageStatus_Exit`
- `InfraSalvageStatus_Init`
- `InfraStillPool_Exit`
- `InfraStillPool_Init`
- `InfraStillPool_RegisterCommand`
- `InfraStillPool_UnregisterCommand`
- `Model3dPbToInstance`
- `ModelAccyInfoToInstance`
- `ModelAirplaneToInstance`
- `ModelAudioLevelMeterToInstance`

**`libInfraIPTC.so`**（丢失 304 个）：

- `CapIdSync_Clear`
- `CapIdSync_Init`
- `CapIdSync_Release`
- `CapIdSync_SendBuffer`
- `CapIdSync_UpdateStatus`
- `_ZN10UpmWrapper10GenerateIdEjPj`
- `_ZN10UpmWrapper10GetOffTypeEPNS_7OffTypeEj`
- `_ZN10UpmWrapper11BITRATE_CNVENS_7BitRateEPa`
- `_ZN10UpmWrapper12OFF_TYPE_CNVEiPNS_7OffTypeE`
- `_ZN10UpmWrapper12SetFullPowerEj`
- `_ZN10UpmWrapper12SetHalfPowerEj`
- `_ZN10UpmWrapper12SetPwrSaveONEj`
- `_ZN10UpmWrapper13SetPwrSaveOFFEj`
- `_ZN10UpmWrapper14FRAME_RATE_CNVENS_9FrameRateEPa`
- `_ZN10UpmWrapper14IMAGE_SIZE_CNVENS_9ImageSizeEPa`
- `_ZN10UpmWrapper14INH_FACTOR_CNVEiPNS_9InhFactorE`
- `_ZN10UpmWrapper14POWER_TYPE_CNVEiPNS_9PowerTypeE`
- `_ZN10UpmWrapper15TEMPERATURE_CNVEiPNS_16TemperatureRangeE`
- `_ZN10UpmWrapper16CONNECT_STAT_CNVEiPNS_11ConnectStatE`
- `_ZN10UpmWrapper16CONTENT_TYPE_CNVENS_11ContentTypeEPa`

**`libInfraBluetoothLowEnergy.so`**（丢失 98 个）：

- `_ZN16DIPairingService20pvtGetMatchingCBListENS_9EN_REQ_IDEmPNS_9cbListElmE`
- `_ZN16DIPairingService24pvtSendPairingAlreadyEvtEm`
- `_ZN17DILocationService10m_serverIFE`
- `_ZN17DILocationService12pvtStopTimerEv`
- `_ZN17DILocationService13pvtStartTimerEh`
- `_ZN17DILocationService15m_timerIntervalE`
- `_ZN17DILocationService15pvtTimerHandlerEPPvS0_`
- `_ZN17DILocationService16m_isServerNotifyE`
- `_ZN17DILocationService17ClSetLocationInfoEmPN25nsInfraBluetoothLowEnergy19ST_DILS_INFORMATIONEPFvNS_17ST_MSG_ASYNC_COMPEE`
- `_ZN17DILocationService18ClStopLocationInfoEmPFvNS_17ST_MSG_ASYNC_COMPEE`
- `_ZN17DILocationService18pvtTimerExpiredEvtEPNS_16ST_DIL_TIMER_EVTE`
- `_ZN17DILocationService18pvtWriteDescripterEmN29InfraBluetoothLowEnergyCommon10ST_BT_UUIDEb`
- `_ZN17DILocationService19ClStartLocationInfoEmPFvNS_17ST_MSG_ASYNC_COMPEE`
- `_ZN17DILocationService20pvtGetMatchingCBListENS_9EN_REQ_IDEmPNS_9cbListElmE`
- `_ZN17DILocationService20pvtIsClientLongWriteEm`
- `_ZN17DILocationService20pvtSendCompForCancelEm`
- `_ZN17DILocationService22pvtChangeTimerIntervalEj`
- `_ZN17DILocationService22pvtSeCommonReqWriteEvtEPKhtPhS2_`
- `_ZN17DILocationService27pvtAddServerLongWriteBufferEmttPh`
- `_ZN17DILocationService27pvtGetClientLongWriteBufferEmPNS_28ST_DILS_CL_LONG_WRITE_BUFFERE`

**`libInfraFsys.so`**（丢失 69 个）：

- `FsysDep_BackupFlush`
- `FsysDep_BackupFlushByID`
- `FsysDep_BackupRead`
- `FsysDep_BackupWrite`
- `Fsys_GetFileSystemInfo`
- `Fsys_PrFILE_SearchNumFreeClusterGroups`
- `MMC_Erasable_ID`
- `MMC_Prohibit_ID`
- `PFDRV_IsWProtected`
- `PFFAT_EXFAT_FreeChainWithErase`
- `PFFAT_FreeChainWithErase`
- `PFFILE_EXFAT_BuildPartToDisk`
- `PFFILE_EXFAT_InitLastSFD`
- `PFFILE_EXFAT_InitMainSFD`
- `PFFILE_EXFAT_SetLastSFD`
- `PFFILE_EXFAT_SetMainSFD`
- `PFFILE_EXFAT_SpeedClassControl_StartRec`
- `PFFILE_EXFAT_SpeedClassControl_UpdateDIR`
- `PFVOL_EXFAT_ClearSpeedClassCtrlInfo`
- `PFVOL_SetSpeedCtlEnable`

**`libInfraMediaDetect.so`**（丢失 48 个）：

- `_ZN24InfraMediaDetectThreadRM16RegistInfraInputEv`
- `_ZN24InfraMediaDetectThreadRM18CheckInsertedMediaEPNS_9MediaInfoEj`
- `_ZN24InfraMediaDetectThreadRM18UnregistInfraInputEv`
- `_ZN24InfraMediaDetectThreadRM22CallbackPointFromInputEPvS0_`
- `_ZN26InfraMediaDetectThreadBase10GetKeyCodeEj`
- `_ZN26InfraMediaDetectThreadBase15NotifyTempEventEjjja`
- `_ZN26InfraMediaDetectThreadBase18NotifyToInfraInputEiii`
- `_ZN26InfraMediaDetectThreadBase19GetInputEventEnableEv`
- `_ZN29InfraMediaDetectThreadDefault19CheckInitFixedMediaEj`
- `_ZN29InfraMediaDetectThreadDefault20SetInOutEventWaitCntEj`
- `_ZN30InfraMediaDetectThreadMSenDrop10EnableDropEjPv`
- `_ZN30InfraMediaDetectThreadMSenDrop10RegistDropEjPv`
- `_ZN30InfraMediaDetectThreadMSenDrop10ThreadInitEv`
- `_ZN30InfraMediaDetectThreadMSenDrop12GetMsgHeaderEv`
- `_ZN30InfraMediaDetectThreadMSenDrop12UnregistDropEjPv`
- `_ZN30InfraMediaDetectThreadMSenDrop20ParseInternalMessageEjPN3MWF6ObjMsgE`
- `_ZN30InfraMediaDetectThreadMSenDrop28CallbackPointFromMediaSensorEPvS0_`
- `_ZN30InfraMediaDetectThreadMSenDrop6ResumeEv`
- `_ZN30InfraMediaDetectThreadMSenDrop7SuspendEv`
- `_ZN30InfraMediaDetectThreadMSenDrop8StopDropEjPv`

**`libInfraBluetooth.so`**（丢失 38 个）：

- `_ZN14InfraBluetooth10pvtStartBTENS_13EN_START_TYPEEb`
- `_ZN14InfraBluetooth11m_isBsaOpenE`
- `_ZN14InfraBluetooth13pvtCmpFromCmdEl`
- `_ZN14InfraBluetooth14pvtSetBtStatusEm`
- `_ZN14InfraBluetooth15register_notifyEv`
- `_ZN14InfraBluetooth16pvtStopBTSettingEv`
- `_ZN14InfraBluetooth17m_hdlAirplaneModeE`
- `_ZN14InfraBluetooth17m_qidAirplaneModeE`
- `_ZN14InfraBluetooth17unregister_notifyEv`
- `_ZN14InfraBluetooth19pvtCoreSecAddDeviceEmb`
- `_ZN14InfraBluetooth21osal_cb_airplane_modeEPvS0_`
- `_ZN14InfraBluetooth22m_isExecutedBTStartAPIE`
- `_ZN14InfraBluetooth22pvtCoreSecRemoveDeviceEPcm`
- `_ZN14InfraBluetooth22pvtSendAirplaneModeMsgEPN20DefInfraAirplaneMode15ST_MSG_SYNC_REQE`
- `_ZN14InfraBluetooth23pvtGetUnifiedIdFromAmIdEj`
- `_ZN14InfraBluetooth23pvtSendFatalErrToMyselfEl`
- `_ZN14InfraBluetooth24jobAirplaneUpdateSettingEPvjS0_`
- `_ZN14InfraBluetooth28pvtGetUnifiedIdFromRequestIdEl`
- `_ZN14InfraBluetooth30pvtCoreDmSetConfigBTVisibilityEbb`
- `_ZN14InfraBluetooth32jobDropInhAirplaneModeProcessingEPvjS0_`

**`libInfraHttpClientUtil.so`**（丢失 37 个）：

- `_ZN15HttpClientInfra10ParamCheck11isValidCharEc`
- `_ZN15HttpClientInfra10ParamCheck14checkPortNoStrERKSs`
- `_ZN15HttpClientInfra10ParamCheck14isAllowSymbolsEc`
- `_ZN15HttpClientInfra10ParamCheck15getIpAddressNumERKSsPSt6vectorISsSaISsEE`
- `_ZN15HttpClientInfra10ParamCheck16checkPassWordStrERKSs`
- `_ZN15HttpClientInfra10ParamCheck16checkUserNameStrERKSs`
- `_ZN15HttpClientInfra10ParamCheck16isValidChar_hostEc`
- `_ZN15HttpClientInfra10ParamCheck17checkIpAddressStrERKSs`
- `_ZN15HttpClientInfra10ParamCheck18checkParameterAddrERKSs`
- `_ZN15HttpClientInfra10ParamCheck18checkParameterPortERKSs`
- `_ZN15HttpClientInfra10ParamCheck18checkParameterPortERKj`
- `_ZN15HttpClientInfra10ParamCheck19checkParameterRegexERKSsPKc`
- `_ZN15HttpClientInfra10ParamCheck22checkParameterHostNameERKSs`
- `_ZN15HttpClientInfra10ParamCheck22checkParameterPasswordERKSs`
- `_ZN15HttpClientInfra10ParamCheck22checkParameterUserNameERKSs`
- `_ZN15HttpClientInfra12MutexContextC1Ev`
- `_ZN15HttpClientInfra12MutexContextC2Ev`
- `_ZN15HttpClientInfra5Mutex4lockEv`
- `_ZN15HttpClientInfra5Mutex6unlockEv`
- `_ZN15HttpClientInfra5Mutex7tryLockEv`

**`libInfraMediaCommon.so`**（丢失 35 个）：

- `_Z13make_buf_listPcP15mmc_pbufferlistPm`
- `_Z16make_physbuflistPcP20mcmnd_physbufferlistPm`
- `_Z21mcmn_mcmd_drv_rwprintiPPc`
- `g_mcmn_drv_mmc_devinfo`
- `g_mcmn_drv_tntfs_devname`
- `mcmn_buf_ClearDirectFileArea`
- `mcmn_buf_GetDirectFileArea`
- `mcmn_buf_IsDirectFileArea`
- `mcmn_buf_SetDirectFileArea`
- `mcmn_buf_ShowDriectFileArea`
- `mcmn_buf_ShowDriectFileAreaArg`
- `mcmn_drv_GetManufactureID`
- `mcmn_drv_dbgerr_set_access`
- `mcmn_drv_dbgerr_set_format`
- `mcmn_drv_dbgerr_set_info`
- `mcmn_drv_dbgerr_set_open`
- `mcmn_drv_default_getpartname`
- `mcmn_drv_default_read`
- `mcmn_drv_default_read_impl`
- `mcmn_drv_default_write`

**`libInfraUsbCmn.so`**（丢失 24 个）：

- `_ZN13CUsbGadgetAdb11proc_keventEmhhPv`
- `_ZN13CUsbGadgetSen11proc_keventEmhhPv`
- `_ZN15CUsbGadgetStill15proc_stop_eventEmhhPv`
- `_ZN15CUsbGadgetStill16proc_class_eventEmhhPv`
- `_ZN15CUsbGadgetStill16proc_start_eventEmhhPv`
- `_ZN15CUsbGadgetStill17proc_vendor_eventEmhhPv`
- `_ZN15CUsbGadgetStill19proc_clr_halt_eventEmhhPv`
- `_ZN15CUsbGadgetStill19proc_set_halt_eventEmhhPv`
- `_ZN15CUsbHostStorage24proc_host_storage_keventEmhhPv`
- `_ZN17CUsbGadgetStorage11proc_keventEmhhPv`
- `_ZN17CUsbGadgetStorage19proc_storage_keventEmhhPv`
- `_ZN17CUsbGadgetStorage22proc_extcommand_keventEmhhPv`
- `_ZN18CUsbGadgetBusPower16proc_kevent_stopEmhhPv`
- `_ZN18CUsbGadgetBusPower17proc_kevent_startEmhhPv`
- `_ZN18CUsbGadgetBusPower22proc_kevent_vendor_reqEmhhPv`
- `_ZN19CUsbGadgetStreaming17proc_kevent_audioEmhhPv`
- `_ZN19CUsbGadgetStreaming17proc_kevent_videoEmhhPv`
- `_ZN7CUsbSvc15proc_kevent_otgEmhhPv`
- `_ZN7CUsbSvc18proc_kevent_gadgetEmhhPv`
- `_ZN7CUsbSvc22proc_kevent_host_errorEmhhPv`

**`libGOConnectionManager.so`**（丢失 15 个）：

- `_ZN4GCM_17NetServiceWrapper11StartWpsCmdEmPKc`
- `_ZN4GCM_17NetServiceWrapper12setWpsHandleEm`
- `_ZN4GCM_17NetServiceWrapper13ErrCnv_Ns2GcmEl`
- `_ZN4GCM_17NetServiceWrapper17GetCredentialInfoEPcjS1_j`
- `_ZN4GCM_17NetServiceWrapper22ErrCnv_NsWpsResult2GcmEm`
- `_ZN4GCM_17NetServiceWrapper25GetCredentialInfoForMultiEPcjS1_j`
- `_ZN4GCM_17NetServiceWrapper28GetCredentialInfoForPcRemoteEPcjS1_j`
- `_ZN4GCM_25GOConnectionManagerCmdMgr13ErrCnv_Ns2GcmEl`
- `_ZN4GCM_25GOConnectionManagerCmdMgr22ErrCnv_NsWpsResult2GcmEm`
- `_ZN4GCM_5NSLib17GetParamReqWpsEvtEPmPiPPcS4_S1_PN10NetService12ST_MSG_EVENTE`
- `_ZN4GCM_5NSLib18GetParamWpsCompEvtEPmPiPN10NetService12ST_MSG_EVENTE`
- `_ZN4GCM_5NSLib19ParseNSMsgReqWpsEvtEPmPN3MWF6ObjMsgEPNS_19stEvtObjMessageInfoE`
- `_ZN4GCM_5NSLib19ParseNSMsgReqWpsEvtEPmPiS2_PPcS4_PN3MWF6ObjMsgEPNS_19stEvtObjMessageInfoE`
- `_ZN4GCM_5NSLib19WpsCompResult2ErrCdEm`
- `_ZN4GCM_5NSLib23RequestWpsType2AuthTypeEm`

### 7.2 Top-20 新增符号（ZV-1F 有、E10 无）—— 逐库

**`libInfraSKD.so`**（新增 6169 个）：

- `ACCESS_DESCRIPTION_free`
- `ACCESS_DESCRIPTION_it`
- `ACCESS_DESCRIPTION_new`
- `ADMISSIONS_free`
- `ADMISSIONS_get0_admissionAuthority`
- `ADMISSIONS_get0_namingAuthority`
- `ADMISSIONS_get0_professionInfos`
- `ADMISSIONS_it`
- `ADMISSIONS_new`
- `ADMISSIONS_set0_admissionAuthority`
- `ADMISSIONS_set0_namingAuthority`
- `ADMISSIONS_set0_professionInfos`
- `ADMISSION_SYNTAX_free`
- `ADMISSION_SYNTAX_get0_admissionAuthority`
- `ADMISSION_SYNTAX_get0_contentsOfAdmissions`
- `ADMISSION_SYNTAX_it`
- `ADMISSION_SYNTAX_new`
- `ADMISSION_SYNTAX_set0_admissionAuthority`
- `ADMISSION_SYNTAX_set0_contentsOfAdmissions`
- `AES_cbc_encrypt`

**`libmpr.so`**（新增 5920 个）：

- `_Z12ff4_mpr_freePvS_`
- `_Z14ff4_mpr_mallocPvy`
- `_Z14ff4_mpr_memcmpPvPKvS1_y`
- `_Z14ff4_mpr_memcpyPvS_yPKvy`
- `_Z14ff4_mpr_memsetPvS_iy`
- `_Z15ff4_mpr_memmovePvS_yPKvy`
- `_Z15ff4_mpr_strncmpPvPKcS1_y`
- `_Z15ff4_mpr_strncpyPvPcyPKcy`
- `_Z16ff4_mpr_fread_64PvS_yPy`
- `_Z16ff4_mpr_fseek_64Pvxi`
- `_Z16ff4_mpr_fsize_64PvPy`
- `_Z16ff4_mpr_ftell_64PvPy`
- `_Z17ff4_mpr_fwrite_64PvPKvyPy`
- `_ZN10KkaDataMgr16SetWriteUnitInfoEPKN14Mp4WriterTable13WriteUnitInfoEjPN3mpr8BaseListE`
- `_ZN10libXavcMp411PbAttributeC1EyN3mpr10CodingTypeENS1_13PlayDirectionEjbb`
- `_ZN10libXavcMp411PbAttributeC2EyN3mpr10CodingTypeENS1_13PlayDirectionEjbb`
- `_ZN10libXavcMp416CnvVieoParamCombERN3mpr14VideoParamCombEPNS_17VideoRecAttributeE`
- `_ZN10libXavcMp416ConvertSearchWayEN3mpr13PlayDirectionE`
- `_ZN10libXavcMp417CnvAudioParamCombEN3mpr14AudioParamCombE`
- `_ZN10libXavcMp422CnvAudioParamToBitrateEN3mpr14AudioParamCombEN9DefStruct12AudioChannelE`

**`libInfraRemote.so`**（新增 3216 个）：

- `_Z13CheckShotMarkjj`
- `_ZN11InfraRemote12SendPTPEventEPvS0_`
- `_ZN11InfraRemote15ClosePtpSessionEPvPN3MWF6ObjMsgE`
- `_ZN11InfraRemote22SetPathStoreUploadFileEjPvS0_`
- `_ZN11InfraRemote24ClearPathStoreUploadFileEjPvS0_`
- `_ZN11InfraRemote31actionFlushDatabaseToMediaEventEPN3UFW3FsmEiPv`
- `_ZN11MintHttpNacC1Ev`
- `_ZN11MintHttpNacC2Ev`
- `_ZN11MintHttpNacD0Ev`
- `_ZN11MintHttpNacD1Ev`
- `_ZN11MintHttpNacD2Ev`
- `_ZN13InfraRemoteIP11getObjectIDEPcj22PTPT_PAIRING_NECESSITYS0_j`
- `_ZN13InfraRemoteIP11makeSaveKeyEPN3MWF6ObjMsgEPSs`
- `_ZN13InfraRemoteIP12m_HttpPortNoE`
- `_ZN13InfraRemoteIP12m_macAddressE`
- `_ZN13InfraRemoteIP13getSessionCntEPS_`
- `_ZN13InfraRemoteIP14execDeavtivateEPN3UFW3FsmEiPvSsj`
- `_ZN13InfraRemoteIP14m_HttpIpAdressE`
- `_ZN13InfraRemoteIP15startSsdpServerEt22PTPT_PAIRING_NECESSITYj`
- `_ZN13InfraRemoteIP16eraseSessionDataEPvSs`

**`libInfraBluetoothLowEnergy.so`**（新增 1273 个）：

- `_ZN16DIPairingService10pvtImCloseEtj`
- `_ZN16DIPairingService12SeNotifyInfoERN25nsInfraBluetoothLowEnergy26ST_DIPS_SERVER_NOTIFY_INFOEh`
- `_ZN16DIPairingService14pvtSendDIPSEvtENS_12ST_MSG_EVENTE`
- `_ZN16DIPairingService15m_semNotifyListE`
- `_ZN16DIPairingService15m_tmpSeReqWriteE`
- `_ZN16DIPairingService15pvtSeReqReadEvtEN29InfraBluetoothLowEnergyCommon12ST_MSG_EVENTE`
- `_ZN16DIPairingService15pvtSeSendNotifyEthPhhPKc`
- `_ZN16DIPairingService17m_cameraFwVersionE`
- `_ZN16DIPairingService17m_cameraModelNameE`
- `_ZN16DIPairingService17pvtLoadCameraInfoEv`
- `_ZN16DIPairingService17pvtNotifyCallbackEjRN9BTService11CccdManager11NotifyParamE`
- `_ZN16DIPairingService18pvtLoadCameraModelEv`
- `_ZN16DIPairingService20CompleteEvtJudgeInfoEPj`
- `_ZN16DIPairingService20pvtGetMatchingCBListENS_9EN_REQ_IDEjPNS_9cbListElmE`
- `_ZN16DIPairingService21pvtCreateDIPSCharUUIDEPKcPN29InfraBluetoothLowEnergyCommon10ST_BT_UUIDE`
- `_ZN16DIPairingService22pvtConvertToPacketDataEPhj`
- `_ZN16DIPairingService22pvtLoadCameraFwVersionEv`
- `_ZN16DIPairingService23pvtEncodePacketLongDataEhtPhS0_`
- `_ZN16DIPairingService24pvtSendPairingAlreadyEvtEj`
- `_ZN16DIPairingService26m_savedPairHandleJudgeInfoE`

**`libInfraMtpServer.so`**（新增 581 个）：

- `_ZN3mpr21readStillImageInParamC1Ev`
- `_ZN3mpr21readStillImageInParamC2Ev`
- `_ZN3mpr30evtReadStillImageFinishedParamC1Ev`
- `_ZN3mpr30evtReadStillImageFinishedParamC2Ev`
- `_ZN4sidb10AudioParam5clearEv`
- `_ZN4sidb10DateChangeC1Ev`
- `_ZN4sidb10DateChangeC2Ev`
- `_ZN4sidb10DateChangeclEy`
- `_ZN4sidb10VideoParam5clearEv`
- `_ZN4sidb10recordInfo5clearEv`
- `_ZN4sidb10recordInfoC1Ev`
- `_ZN4sidb10recordInfoC2Ev`
- `_ZN4sidb11DateEqualToC1Ey`
- `_ZN4sidb11DateEqualToC2Ey`
- `_ZN4sidb11DateEqualToclEy`
- `_ZN4sidb11DcfTransfer5clearEv`
- `_ZN4sidb11ValueChangeIjEC1Ev`
- `_ZN4sidb11ValueChangeIjEC2Ev`
- `_ZN4sidb11ValueChangeIjEclEj`
- `_ZN4sidb14onMemoryColumn5clearEv`

**`libInfraFsys.so`**（新增 540 个）：

- `Fsys_Combine`
- `Fsys_FormatAsyncV2`
- `Fsys_FormatV2`
- `Fsys_Lock`
- `Fsys_PGEnter`
- `Fsys_PGExit`
- `Fsys_PGLoopClose`
- `Fsys_PGLoopInit`
- `Fsys_PGLoopOpen`
- `Fsys_PGLoopRead`
- `Fsys_PGLoopWrite`
- `Fsys_PGLoopWriteAsync`
- `Fsys_PGReEnter`
- `Fsys_PGStartWriteThru`
- `Fsys_PGStopWriteThru`
- `Fsys_PutsForFsysMbpMcmd`
- `Fsys_ReadAsync`
- `Fsys_RecordLog`
- `Fsys_RecordLog64`
- `Fsys_RecordLogStr`

**`libInfraBluetoothCore.so`**（新增 258 个）：

- `BSA_TmDiagStats`
- `BSA_TmDiagStatsInit`
- `BSA_TmDisconnect`
- `BSA_TmDisconnectInit`
- `BSA_TmGetHandle`
- `BSA_TmGetHandleInit`
- `BSA_TmGetMemUsage`
- `BSA_TmGetMemUsageInit`
- `BSA_TmHciCmd`
- `BSA_TmHciInit`
- `BSA_TmLeTestCmd`
- `BSA_TmPing`
- `BSA_TmPingInit`
- `BSA_TmQos`
- `BSA_TmQosInit`
- `BSA_TmReadRawRssi`
- `BSA_TmReadRawRssiInit`
- `BSA_TmReadVersion`
- `BSA_TmReadVersionInit`
- `BSA_TmSetTestMode`

**`libInfraMediaCommon.so`**（新增 177 个）：

- `_Z10tk1test_rwiPPc`
- `_Z11isTk1DevicePc`
- `_Z13make_buf_listPcP15mmc_pbufferlistPj`
- `_Z13mcmn_mcmd_lediPPc`
- `_Z14mcmn_mcmd_timeiPPc`
- `_Z15tk1test_ioctlrwiPPc`
- `_Z16make_physbuflistPcP20mcmnd_physbufferlistPj`
- `_Z16mcmn_mcmd_msm_rwiPPc`
- `_Z17tk1test_ioctlrwvliPPc`
- `_Z17tk1test_ioctlrwvpiPPc`
- `_Z19mcmn_mcmd_time_infoiPPc`
- `_Z19mcmn_mcmd_time_stopiPPc`
- `_Z20mcmn_mcmd_time_pauseiPPc`
- `_Z20mcmn_mcmd_time_printiPPc`
- `_Z20mcmn_mcmd_time_startiPPc`
- `_Z20mcmn_mcmd_time_writeiPPc`
- `_Z21mcmn_mcmd_time_resumeiPPc`
- `_Z22ioexptest_get_registeriPPc`
- `_Z22ioexptest_set_registeriPPc`
- `_Z23ioexptest_get_inputportiPPc`

**`libObj.so`**（新增 74 个）：

- `BMP_WriteLockBits`
- `BMP_WriteUnlockBits`
- `GRM_bitmapCreateFromPhys`
- `GRM_bitmapRelease`
- `GRM_fbrmCheckUpdateFinished`
- `GRM_fbrmRegisterTriggerFrameworkCb`
- `_ZN10InfraInput12SetKeyRepeatEibii`
- `_ZN10InfraInput17SetKeyRepeatResetEi`
- `_ZN10InfraInput18SetEventKeyListAllEi`
- `_ZN10InfraInput20GetPhyKeyStatusCacheEiP21_InfraInputPhyKeyData`
- `_ZN2ux4core13GraphicsImage4copyEPvssssS2_ssssbbb`
- `_ZN2ux4core13GraphicsImage5clearEPvRKN6stella6CColorE`
- `_ZN2ux4core6Kernel23sys_setCurrentDirectoryEPKc`
- `_ZN2ux6gfxeng10TextSprite11setClipSizeEff`
- `_ZN2ux6gfxeng10TextSprite13setDictionaryEPN5libra10DictionaryE`
- `_ZN2ux6gfxeng10TextSprite13setTextLayoutERKN5alcor15LayoutAttributeE`
- `_ZN2ux6gfxeng10TextSprite20setImageLoadCallbackEPFvPS1_PN5libra9ImageDataEPvES6_`
- `_ZN2ux6gfxeng10TextSprite7setFontERKN5libra9RecordPtrE`
- `_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv`
- `_ZN2ux6gfxeng14GraphicsEngine15setScreenRotateENS_4core14GraphicsSystem12ScreenRotateE`

**`libInfraBluetooth.so`**（新增 69 个）：

- `_ZN14InfraBluetooth10pvtImCloseEtj`
- `_ZN14InfraBluetooth10pvtStartBTENS_13EN_START_TYPEEbj`
- `_ZN14InfraBluetooth11m_isMgtOpenE`
- `_ZN14InfraBluetooth11pvtApoResetEv`
- `_ZN14InfraBluetooth12m_ActiveFlagE`
- `_ZN14InfraBluetooth13jobGetLocalDiEPvjS0_`
- `_ZN14InfraBluetooth13jobSetLocalDiEPvjS0_`
- `_ZN14InfraBluetooth13m_SettingFlagE`
- `_ZN14InfraBluetooth13pvtCmpFromCmdEi`
- `_ZN14InfraBluetooth14pvtSetBtStatusEj`
- `_ZN14InfraBluetooth16pvtStopBTSettingEj`
- `_ZN14InfraBluetooth17jobPairingDisableEPvjS0_`
- `_ZN14InfraBluetooth17pvtBackupIDCovertEv`
- `_ZN14InfraBluetooth18m_isCoreBleEnabledE`
- `_ZN14InfraBluetooth18m_isStartBTProcessE`
- `_ZN14InfraBluetooth19pvtCoreDmGetDidConfEPN16nsInfraBluetooth15ST_GET_LOCAL_DIE`
- `_ZN14InfraBluetooth19pvtCoreDmSetDidConfEv`
- `_ZN14InfraBluetooth19pvtCoreSecAddDeviceEjb`
- `_ZN14InfraBluetooth19pvtCoreTmDisconnectEPc`
- `_ZN14InfraBluetooth19pvtDeleteSavedBtDevEPN16nsInfraBluetooth19ST_DEL_SAVED_BT_DEVEb`

**`libInfraHdmi.so`**（新增 52 个）：

- `_ZGVZN7MWFWrap6InhMgr11GetInstanceEvE4inst`
- `_ZN16MODULE_NAMESPACE7OSALLogILt154ELi4EE10log_printfEiPKcz`
- `_ZN16MODULE_NAMESPACE7OSALLogILt154ELi4EE10log_recordItEEiiPKT_j`
- `_ZN4HDMI10ApiHandler23handle_reg_inh_observerEN9__gnu_cxx17__normal_iteratorIPhSt6vectorIhSaIhEEEES7_j`
- `_ZN4HDMI10HdmiInhMgr11SetObserverEj`
- `_ZN4HDMI10HdmiInhMgrC1E17E_HDMI_I_INH_KIND`
- `_ZN4HDMI10HdmiInhMgrC2E17E_HDMI_I_INH_KIND`
- `_ZN4HDMI12StateWrapper14RegInhObserverEjj`
- `_ZN4HDMI14HdmiInhMgrImpl11SetObserverEj`
- `_ZN4HDMI14HdmiInhMgrImplC1E17E_HDMI_I_INH_KIND`
- `_ZN4HDMI14HdmiInhMgrImplC2E17E_HDMI_I_INH_KIND`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEE9write_logES5_`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEEC1ERKS6_`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEEC1Ev`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEEC2ERKS6_`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEEC2Ev`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEED1Ev`
- `_ZN4HDMI15write_param_logINS_14MSG_PARAM_COMPEXadL_ZNS_7msg_log12msg_log_impl31write_all_inh_observer_comp_logERKS1_S5_EEXadL_ZNS3_31write_all_inh_observer_comp_logES5_S5_EEED2Ev`
- `_ZN4HDMI15write_param_logINS_26MSG_PARAM_REG_INH_OBSERVEREXadL_ZNS_7msg_log12msg_log_impl26write_all_inh_observer_logERKS1_S5_EEXadL_ZNS3_26write_all_inh_observer_logES5_S5_EEE9write_logES5_`
- `_ZN4HDMI15write_param_logINS_26MSG_PARAM_REG_INH_OBSERVEREXadL_ZNS_7msg_log12msg_log_impl26write_all_inh_observer_logERKS1_S5_EEXadL_ZNS3_26write_all_inh_observer_logES5_S5_EEEC1ERKS6_`

**`libInfraHttpClientUtil.so`**（新增 37 个）：

- `_ZN4util10ParamCheck11isValidCharEc`
- `_ZN4util10ParamCheck14checkPortNoStrERKSs`
- `_ZN4util10ParamCheck14isAllowSymbolsEc`
- `_ZN4util10ParamCheck15getIpAddressNumERKSsPSt6vectorISsSaISsEE`
- `_ZN4util10ParamCheck16checkPassWordStrERKSs`
- `_ZN4util10ParamCheck16checkUserNameStrERKSs`
- `_ZN4util10ParamCheck16isValidChar_hostEc`
- `_ZN4util10ParamCheck17checkIpAddressStrERKSs`
- `_ZN4util10ParamCheck18checkParameterAddrERKSs`
- `_ZN4util10ParamCheck18checkParameterPortERKSs`
- `_ZN4util10ParamCheck18checkParameterPortERKj`
- `_ZN4util10ParamCheck19checkParameterRegexERKSsPKc`
- `_ZN4util10ParamCheck22checkParameterHostNameERKSs`
- `_ZN4util10ParamCheck22checkParameterPasswordERKSs`
- `_ZN4util10ParamCheck22checkParameterUserNameERKSs`
- `_ZN4util12MutexContextC1Ev`
- `_ZN4util12MutexContextC2Ev`
- `_ZN4util5Mutex4lockEv`
- `_ZN4util5Mutex6unlockEv`
- `_ZN4util5Mutex7tryLockEv`

## 8. 同名 SONAME 取舍规则的敏感性（重要）

同一个脚本用另一种冲突规则各跑一遍（`--prefer F` vs `--prefer E10`）：

| 规则 | MERGED 库数 | MERGED 定义符号数 | `appFw.so` 可解析 | 占比 | `appFw.so` 残余 | 新菜单根集合残余 |
|------|-------------|-------------------|-------------------|------|-----------------|------------------|
| `--prefer F`（本报告正文） | 327 | 188499 | 714 / 717 | 99.6% | 3 | 25 |
| `--prefer E10` | 327 | 210815 | 580 / 717 | 80.9% | 137 | 283 |

**实测结论：冲突规则的选取是决定性的。** 让 ZV-1F 胜出（`--prefer F`）时 `appFw.so` 残余仅 3 个；让 ZV-E10 胜出（`--prefer E10`）时残余暴涨到 137 个。

原因：ZV-1F 的 `libmpr.so` / `libObj.so` / `libSysDef.so` / `libInfraSKD.so` / `libInfraRemote.so` 等与 ZV-E10 同名但内容完全不同（新增菜单框架），若用 ZV-E10 的旧版覆盖它们，新菜单所需的 `ux::` / `BMP_` / `libra` 等符号立刻大量缺失。

> 操作含义：部署时必须让 ZV-1F 版本的这些库在动态链接器搜索路径中**优先**被找到（覆盖同名文件，或调整 `LD_LIBRARY_PATH`/`/etc/ld.so.conf` 顺序）。

两种规则下的贪婪最小集相同（都是 6 个文件）：`libmpr.so`、`libObj.so`、`libCertificateManager.so`、`libGOConnectionManager.so`、`libInfraCCPFStatic.so`、`libInfraFsys.so`

## 9. 附录 A —— 原始命令清单（可复现）

脚本对每个 ELF 文件执行的核心命令：

```bash
readelf -h  <file>                       # 架构/类型判定
readelf -W -d <file>                     # 取 NEEDED / SONAME
readelf -W --dyn-syms <file>             # 取 DEF / UND 动态符号
readelf -W --dyn-syms <kmod>/*.ko        # 实测返回 0 行（.ko 无 .dynsym）
readelf -W --dyn-syms <rootfs lib>       # 读根文件系统库的 SONAME
grep -rlF -f /tmp/abi_residual_pat.txt <tree roots>   # 残余符号原始字节扫描
c++filt  < <symbol list>                 # 反修饰（仅用于分组展示）
```

实际执行过的命令（按脚本记录顺序去重）。共执行 **2678** 次 `readelf`/`grep`/`c++filt` 调用；因对每个 ELF 文件的调用形式完全同构，此处只列出前 120 条代表性记录，完整可复现清单由 `abi_closure.py` 本身定义（脚本对每个文件都执行同样两条 `readelf`）。

```bash
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libUsbBusPower.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libUsbBusPower.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libJiritsu.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libJiritsu.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libtestcmd_scn.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libtestcmd_scn.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libmintx.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libmintx.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libEMG.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libEMG.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraDlnaControl.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraDlnaControl.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraDevInfoSender.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraDevInfoSender.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ModelCautionConfig.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/ModelCautionConfig.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libosal_utm.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libosal_utm.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libGOConnectionManager.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libGOConnectionManager.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetoothCore.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetoothCore.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/viewUnified3.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/viewUnified3.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libnl-genl-3.so.200.17.0
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libnl-genl-3.so.200.17.0
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libjemalloc_ds.so.1
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libjemalloc_ds.so.1
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetoothLowEnergy.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetoothLowEnergy.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libexpat.so.1.6.11
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libexpat.so.1.6.11
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libupdatertalk.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libupdatertalk.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraMediaMount.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraMediaMount.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libpopt.so.0.0.0
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libpopt.so.0.0.0
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraStreamingUsb.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraStreamingUsb.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/CautionRsrcConfig.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/CautionRsrcConfig.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libnl-3.so.200.17.0
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libnl-3.so.200.17.0
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraCameraProfile.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraCameraProfile.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/CameraOnDemandSaver.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/CameraOnDemandSaver.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libObj.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libObj.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libz.so.1.2.11
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libz.so.1.2.11
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libNetService.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libNetService.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libupdaterufunc.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libupdaterufunc.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libcom_err.so.2.1
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libcom_err.so.2.1
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetoothWoLEBoot.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetoothWoLEBoot.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libutil_date.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libutil_date.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraMediaDetect.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraMediaDetect.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libUsbChargingApp.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libUsbChargingApp.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraIPsec.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraIPsec.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libUsbControl.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libUsbControl.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libdig_level.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libdig_level.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libAK8975.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libAK8975.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libpcre.so.1.2.12
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libpcre.so.1.2.12
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraCacheMgr.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraCacheMgr.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/viewUnified7.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/viewUnified7.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraLensCommunicator.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraLensCommunicator.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraUsbHostStorage.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraUsbHostStorage.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libcompassutil.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libcompassutil.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libopus.so.0.8.0
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libopus.so.0.8.0
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libstdc++.so.6.0.14
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libstdc++.so.6.0.14
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libNetCommon.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libNetCommon.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libjsoncpp.so.23
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libjsoncpp.so.23
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libnl-cli-3.so.200.17.0
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libnl-cli-3.so.200.17.0
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraSKD.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraSKD.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraFsys.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraFsys.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libIMDB.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libIMDB.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libBackupManager.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libBackupManager.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraHighlight.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraHighlight.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libbsa.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libbsa.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraMscServer.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraMscServer.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetooth.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libInfraBluetooth.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libupdatermedia.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libupdatermedia.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libRsrcHotshoe.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libRsrcHotshoe.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libBTAdjust.so
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libBTAdjust.so
readelf -W -d <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libprocps.so.6.0.0
readelf -W --dyn-syms <repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked/lib/libprocps.so.6.0.0
```

**一键复现全部数字：**

```bash
python3 <repo>/port/abi_closure.py --prefer F   --outdir <repo>/port
python3 <repo>/port/abi_closure.py --prefer E10 --outdir <repo>/port/prefer_e10
```

## 10. 附录 B —— 完整结果 JSON

所有表格的机器可读原始数据（含未截断的完整符号名单）：

`<repo>/port/abi_closure.json`

---

*报告由 `abi_closure.py` 自动生成。VERIFIED = 命令实测；[推测] = 推断。*
