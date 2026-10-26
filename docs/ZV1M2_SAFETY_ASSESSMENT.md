# ZV-1M2 移植的不可逆风险评估（历史记录，不作为安全准入）

> **安全审计 NO-GO（2026-10-26）：** 下文“客户端库相同即配置兼容”“字符串相同即不会自动更新”“只读 bind 即不写 flash”等推论不成立；备份不等于副作用隔离。保留文件比对与备份的历史输出，但撤回安全结论。详见 [开机部署审计](ZVEGISK_PREDEPLOY_AUDIT.md)。

> 用户问题：「hybrid 镜像传上去之前，你没有确认他不会对相机有什么不可逆改变？
> 比如写入 zv1m2 的专有设置？导致切回老菜单/原厂开不了机/死机？
> 或者更改 RTOS/MCU 设置/flash 内容？」

---

## 一、✅ 已排除的风险（逐个实测）

| 检查项 | 方法 | 结果 |
|---|---|---|
| **内核模块错配** | `kmod/` 103 个 `.ko` 与 ZV-E10 逐字节比对 | **★ 103/103 全部相同** |
| **更新器被替换** | `crypter.elf` 比对 | **★ 字节相同** |
| **UFP 更新模式脚本** | `up.sh` 比对 | **★ 字节相同** |
| **固件兼容检查脚本** | `compat_firmware.sh` 比对 | **★ 字节相同** |
| **模式切换脚本** | `change_mode.sh` 比对 | **★ 字节相同** |
| **其他启动脚本** | `swap_prepare.sh` / `us_remove_modes.sh` / `usb_insmod.sh` / `appdbglv.sh` / `doemparse.sh` / `startBTCore.sh` | **★ 全部相同** |
| **写 /setting 的通道** | `libbackup.so` 比对 | **★ 字节相同 ⇒ 写入格式与 E10 一致** |
| **`/bin` 差异** | 逐文件比对 | **只有 4 个监控工具** |
| **外设固件更新代码** | `CTP_FW_UPDATE` / `OUTSIDE_EVF_CPU_FLASH_WRITE` / `Erase OK` | **E10 本来就有 ⇒ 正常启动不执行** |

### `/bin` 的唯一差异

```
hybrid 独有:  app_monio.elf / mpr_monio2.elf / checkServerCert
E10 有 hybrid 无: mpr_monio.elf（同名替换品）
内容不同:     ulogio（E10=10 B / hybrid=0 B，无意义）
```

**⇒ 全是**监控/证书检查工具**，不是刷写路径。**

---

## 二、⚠️ 发现的真实风险点

### 风险 1：`appFw.so` 里有完整的 **updater 框架**

```
INFRA_NOTIFY_UPDATER_READY / WRITE / EXIT / ERROR
INFRA_NOTIFY_UPDATER_DISP_RESET_BTN
infra_wrapper_updater
finalize_updater_mass_storage
MODEL_REQUEST_FW_UPDATE_EVENT
MODEL_START_FW_UPDATE_EVT
COMMON_FW_UPDATE          ← M2 新增（E10 没有）
```

**★★★ 新 UI 里**有固件更新菜单**。如果误入并确认，可能触发真正的固件写入。**

**★ 但这需要**用户手动操作**（进菜单 → 选择 → 确认），不是自动的。**

### 风险 2：`appFw.so` 会写 `/setting/` 下的文件

```
/setting/acctime1.bin    ← 累计使用时间
/setting/acctime2.bin
/setting/facedmy.bin     ← 人脸识别示例数据
/setting/facerec%d.bin   ← 人脸识别数据
/setting/sen/smode       ← ★ 传感器强制模式（init 会读！）
/setting/sig.txt
```

**★★★ `/setting/sen/smode` 最危险** —— `init` 的 `check_forced_senser` 会读它。
如果 M2 写了 E10 不认识的值，**下次启动可能异常**。

### 风险 3：`appFw.so` 引用 `/dev/nflasha23` 和 `/dev/nflasha30`

```
nflasha23  存在（64 MB，未挂载）
nflasha30  分区表里 size=0（不存在）⇒ 打开会失败，无害
```

---

## 三、🛡️ 三道结构性保障

### ① 我们**不写 flash**

```
/usr 是 ext2 只读分区 ⇒ 挂载就是 ro
我们只做 mount --bind 覆盖视图 ⇒ 重启即恢复，flash 一个字节不变
/etc/ld.so.preload 也不会碰
```

### ② **已备份 /setting 和 /etc 到 SD 卡**（本轮完成）

```
/tmp/sd/zvegisk_backup/
├── setting/    15 个文件  ★ Backup.bin md5 = d5a08a37...（与源一致）
├── etc/        20 个文件  ★ 含 ld.so.preload（0 字节）
└── lens/       57 个文件

★ 另外本地还有 18/18 分区的完整镜像（/root/fw/zve10_backup/，含 SHA-256）
```

### ③ **拔卡/重启即回原厂**

```
拔 SD 卡        ⇒ boot_mode 读不到 ⇒ 不加载任何东西
boot_mode="old" ⇒ 不 bind
不设 dmode      ⇒ 连 zvegisk.so 都不加载
```

---

## 四、风险矩阵（最终结论）

| 风险 | 可能性 | 后果 | 可恢复性 |
|---|---|---|---|
| **内核模块错配** | **❌ 已排除** | — | — |
| **/usr 被写坏** | **❌ 不可能**（ro + 只 bind） | — | — |
| **更新器被误触发** | **低**（需手动进菜单确认） | **高** | **★ BootROM fastboot（已实机验证）** |
| **/setting 被写入不兼容值** | **中** | **中**（启动异常） | **★ 已备份，可恢复** |
| **外设固件被刷** | **低**（E10 同代码，正常不执行）| **高** | ⚠️ 需进一步评估 |
| **`/dev/nflasha23` 被写** | **低** | **中** | **★ 有 64MB 镜像备份** |

---

## 五、测试纪律（把风险压到最低）

```
① 测试时【不要进】新菜单里任何"固件更新 / 版本升级"相关菜单项
② 第一次启动只验证"能不能起来"，不做任何设置修改
③ 一旦 UI 起来，立刻记录 /proc/mounts 和日志，然后尽快回退
④ 每次测试前确认 boot_mode 和 dmode 状态
⑤ 如果 /setting 被改动，从 SD 备份恢复
```

---

## 六、备份清单（本轮已完成）

```
SD 卡 /zvegisk_backup/
├── setting/    20 MB  15 文件   ★ 含 Backup.bin / DmmConfig.bin / psd.bin / sen/smode
├── etc/        ~1 MB  20 文件   ★ 含 ld.so.preload（0 字节）
└── lens/       ~? MB  57 文件

本地（/root/fw/zve10_backup/）：18 个分区的完整镜像 + manifest.json（SHA-256）
```

**★★★ 恢复方法（如有必要）：**
```
1. USB 大容量存储挂载 SD
2. 从 SD 拷回 /setting 或 /etc 的内容
3. 或直接用本地 18 分区镜像恢复
4. 最坏情况：BootROM fastboot 改 1 字节（已实机验证过）
```
