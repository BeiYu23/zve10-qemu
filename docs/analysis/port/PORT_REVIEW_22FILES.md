# 移植决策复核：22 个 `KEEP_E10_REVIEW` 文件

> 对象：`port/out/manifest.csv` 中 reason = "存在差异但未被新 UI 依赖链引用" 的 22 个文件。
> 方法：两版原件逐项对比（vermagic / 导入导出符号 / 字符串 / 依赖交叉），全部可复现。

---

## 结论汇总

| 判定 | 数量 | 文件 |
|---|---:|---|
| **★ 应改为 TAKE_F** | **1** | `share/avsystemsound/beeplist.txt` |
| **建议改 TAKE_F（更优）** | 1 | `bin/startBTCore.sh` |
| 保持 KEEP_E10（已验证安全） | 20 | 15 个 kmod + 3 个 bin + 1 个有依赖差异的 kmod |

---

## 一、★ 唯一的实质错误：`beeplist.txt` 应该换成 ZV-1F 版

| 事实 | 证据 |
|---|---|
| ZV-E10 版 **77 行**，表头 `DSLR E-Mount` | 列：`BeepID_Name, BeepID_Number, MemoryType, FileName, RsrcAcsType, Priority, KindID, continuous, contLength, contFirstLength` |
| ZV-1F 版 **72 行**，表头 `DSC(5Way_Mid)` | 列**多出 `Size` 与 `FileOffset`** |
| ZV-E10 上由 **`libObj.so`** 解析 | `libObj.so` 内 15 处相关串 |
| ZV-1F 上由 **`appFw.so`** 解析 | `appFw.so` 内 12 处相关串 |
| **而 manifest 里 `lib/libObj.so` = `TAKE_F`** | 换成 ZV-1F 版 |

⇒ 混合固件里，解析 `beeplist.txt` 的是 **ZV-1F 的 libObj + appFw**，
它们期望 **ZV-1F 的列格式（带 Size/FileOffset）**；
保留 ZV-E10 的旧格式 ⇒ **蜂鸣/提示音加载很可能失败**。

**建议**：`share/avsystemsound/beeplist.txt` → `TAKE_F`。
（配套：`share/avsystemsound/SysSound.bin` 在 manifest 里已是 `ADD_F`，一致。）

> 修正命令（若重新装配）：
> `build_hybrid.py --force-take share/avsystemsound/beeplist.txt`

---

## 二、建议改 TAKE_F：`bin/startBTCore.sh`

ZV-1F 版是**超集且有平台自适应**：

```sh
if [ -e /dev/ttyBT ]; then   # ASTRA      ← ★ ZV-E10 的平台代号
    BTDEV="/dev/ttyBT"
else                          # IPSX
    BTDEV="/dev/ttyAMA3"
fi
echo "dev=$BTDEV" > /tmp_bt/startBTCore.sh.log
```

- ZV-E10 版**硬编码** `/dev/ttyBT`，无日志
- ZV-1F 版**自动探测**，且在 ZV-E10 上会走 `ASTRA` 分支（`/dev/ttyBT` 存在）
- 两版调用 `bsa_server` 的参数完全相同

⇒ 换成 ZV-1F 版**更安全**（有自适应 + 有日志便于排查）。保留 E10 版也能工作。

---

## 三、17 个 kmod：15 个接口完全一致

**vermagic 全部一致**（`3.0.27_nl-rt106+`）⇒ 全部可加载。

**导入符号集逐一对比，15 个模块完全相同**（仅构建差异）：

```
backup  dma330  i2c  kikilog  libcomposite  liro  mmcioWrapper
osal_uipc  osal_ulogio  udate  usb_portMonitor  usbg_audio
usbg_sen  usbg_video  utimer
```

⇒ **保持 KEEP_E10 完全正确**（kmod 与硬件绑定，且接口无变化）。

### 两个有依赖差异的（仍然安全）

| 模块 | ZV-1F 版额外需要 | 结论 |
|---|---|---|
| `usbg_stillimage.ko` | `__udif_kmalloc` / `__udif_kfree` / `udif_km_debug` | **ZV-E10 内核已实测导出这三个符号** ⇒ 换也能加载；但没必要，保持 E10 版即可 |
| `usbg_storage.ko` | 只需 `printk`（E10 版还需 `osal_snd_sync_direct/msg` + `do_gettimeofday`） | `osal_uipc.ko` 两版**都导出** `osal_snd_sync_direct/msg`，`do_gettimeofday` 由内核提供 ⇒ 保持 E10 版安全 |

---

## 四、4 个 bin：全部蓝牙相关，硬件绑定

| 文件 | BT 相关串数 | 结论 |
|---|---:|---|
| `bsa_server` | 2713 | ZV-E10 的 Broadcom BSA 栈，芯片配置绑定 ⇒ **保持** |
| `btRFTest.elf` | 23 | 两侧都硬编码 `/dev/ttyBT` ⇒ **保持** |
| `adjstctl.elf` | 6 | 调整控制，无平台分支 ⇒ **保持** |
| `startBTCore.sh` | 2 | 见 §二 ⇒ **建议换** |

两侧都有 `bcmdhd.ko`（Broadcom WiFi/BT 组合）⇒ **ZV-E10 与 ZV-1F 都有蓝牙硬件**，
所以这组文件不是"该不该有"的问题，而是"哪版配置对"—— 硬件绑定 ⇒ 保持 E10 版。

---

## 五、★ 意外收获：新 UI 已内置 ASTRA（ZV-E10 平台）支持

`appFw.so` 里存在**平台抽象层**：

```
VAL_FEATURE_MANAGER_system_architecture_TYPE_SYSTEM_ARCHITECTURE_ASTRA
VAL_FEATURE_MANAGER_system_architecture_TYPE_SYSTEM_ARCHITECTURE_IPSX
VAL_HAITA_lens_mount_system_architecture_TYPE_LENS_MOUNT_FIXED_TYPE_SYSTEM_ARCHITECTURE_ASTRA
"%s%s(%d) ASTRA not supported : PID_SET_BULB_MODE"
"%s%s(%d) ASTRA not supported : PID_SET_EXPOSURE_CANCEL"
"%s%s(%d) Astra not supported : PID_SET_FOCUS_BRK_EXEC_COUNT"
"%s%s(%d) Astra not supported : PID_SET_FLASH_SHUT_SPEED"
```

⇒ **线索一：新 UI 是多架构设计**，`ASTRA` 是它**认识并支持**的一个架构取值
（ZV-E10 的 libObj 里也出现 `ASTRA` 1 次）。
⇒ **线索二：它已经为 ASTRA 缺失的 PID 写了显式守卫**（bulb 模式、曝光取消、闪光同步速度等），
说明**"跑在 ASTRA 上"是被预期的场景**，而不是意外。

**这条显著降低了此前可行性报告里 L3（运行时接缝）的风险评级**：
新 UI 不是"只能跑在 ZV-1F 硬件上"，它自带 ASTRA 分支和特性守卫。

---

## 六、复核建议（最终）

```
保持 KEEP_E10 : 15 个 kmod + 3 个 bin（bsa_server / btRFTest.elf / adjstctl.elf）
改为 TAKE_F   : share/avsystemsound/beeplist.txt     ← 唯一的实质修正
可选改 TAKE_F : bin/startBTCore.sh                   ← 更优但非必须
保持 KEEP_E10 : kmod/usbg_stillimage.ko / kmod/usbg_storage.ko（已验证安全）
```

风险等级：**22 个文件里只有 1 个存在真实功能风险**，其余均验证安全。
