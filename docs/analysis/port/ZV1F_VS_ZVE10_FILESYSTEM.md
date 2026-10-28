# ZV-1F vs ZV-E10：文件系统与其余分区的完整对比

> 材料：ZV-1F 固件 **已下载并解包**（此前只有 `/usr`，现在有全套分区）。
> 结论：**移植方案"只重建 /usr + 补 3 个目录"的判断基本成立，但漏了 1 个小文件。**

---

## 一、ZV-1F 固件落地（可复现）

| 项 | 值 |
|---|---|
| 直链 | `https://support.d-imaging.sony.co.jp/download/NEX/Otd23ee7d4/BODYDATA.DAT?fm=jp` |
| 大小 / md5 | 443,505,056 B / `65cca37cca60dd1ae38a104bcd47ccff` |
| 落地 | `/mnt/build/downloads/BODYDATA_ZV1F.dat` |
| 解包 | `/mnt/build/fw/unpacked_zv1f/` |
| 包内分区 | `nflasha15`(usr) / `nflasha3`(system, **VFAT**) / `nflasha7`(rootfs) / `nflasha5` / `updater.img` |

---

## 二、★ initrd：**409 个条目完全相同，只有 `/sbin/init` 差 112 字节**

| 项 | ZV-E10 | ZV-1F |
|---|---|---|
| `initrd.img` 大小 | 2,183,168 | 2,183,168（**同**） |
| md5 | `12362d21e94b…` | `adc8e1de08dd…` |
| 文件系统 | ext2 | ext2 |
| 条目数 | 409 | 409 |
| **仅一侧存在的文件** | **0** | **0** |
| 大小不同的文件 | **只有 `sbin/init`**：28,580 vs 28,692 | |

### `/sbin/init` 的差异（字符串级）

| 仅 ZV-E10 | 仅 ZV-1F |
|---|---|
| `/dev/nflasha18` | `/dev/NONE` |
| | `tmpfs` |
| | `/tmp_wbshd` |
| | `/usr/kmod/iris_dial.ko` |
| | `WBSHD=/tmp_wbshd/shared.XXXXXX` |

⇒ **ZV-1F 的 init 多做两件事**：把 `tmpfs` 挂到 `/tmp_wbshd`、insmod 列表里多了 `iris_dial.ko`。

**但 `iris_dial.ko` 在 ZV-1F 自己的 `/usr/kmod/` 里也不存在**（两侧均无）
⇒ 那是**可选模块，加载失败属正常**，不需要我们提供。

⇒ **启动链几乎完全同构，这是移植最有利的一条证据。**

---

## 三、`/rootfs`：**真实差异只有 4 项**

递归对比（ZV-E10 用 debugfs 走盘，ZV-1F 用 `find`）：

```
ZV-E10 /rootfs: 623 条
ZV-1F  /rootfs: 172 条
仅 ZV-1F 有 : 4 条
仅 ZV-E10 有: 455 条（其中 381 条是 dev/ 设备节点 —— 我们的是活系统快照）
```

### 仅 ZV-1F 有的 4 项

| 项 | 说明 |
|---|---|
| `tmp_cloud/` | 目录 |
| `tmp_remote/` | 目录 |
| `tmp_wbshd/` | 目录（init 会把 tmpfs 挂上去） |
| ~~`lib/libSTMcmdexecute.so`~~ | **此条已撤回，见下方更正** |

> ### ⚠️ 更正（2026-10-24 复核）
> 本文初版声称 `lib/libSTMcmdexecute.so` 只有 ZV-1F 才有、移植方案漏了它 —— **这是错的**。
>
> 实测：**ZV-E10 的 `/rootfs` 本来就有该文件**，两侧 **md5 完全相同**
> （`66a9271377e8017ef8679a0b02a861ee`，4,688 B，属主 57285:1000）。
>
> 误报原因：初版用 `comm` 比较两份清单，而 `comm` 对输入排序敏感（当时出现
> "file is not in sorted order" 警告），产生了假差异。改用 Python 集合比较后，
> **仅 ZV-1F 独有的就是 3 个目录**：`tmp_cloud/`、`tmp_remote/`、`tmp_wbshd/`。
>
> ⇒ **移植方案的原始表述（只补 3 个目录）是正确的，无需额外补文件。**

### 仅 ZV-E10 有的 455 条 = 快照假差异

- **381 条 `dev/*`**：设备节点。我们的 `/rootfs` 来自**活机 dump**，ZV-1F 来自**固件包**（不含 dev 节点）
- **42 条 `bin/*`**：ZV-E10 把 busybox applet 做成**符号链接**（`ash chmod cp dd … sh tar`），
  ZV-1F 的 `bin/` 只有 11 个**实体文件**（busybox + cat/chattr/ps/top/vmstat 等）
- 其余为 lib/sbin 的少量差异

⇒ 双方都用 busybox（433,504 B，同尺寸）⇒ **无功能差异**。

---

## 四、`/system`（nflasha3，**VFAT**）对比

| 文件 | ZV-E10 | ZV-1F | 判读 |
|---|---:|---:|---|
| `av-cam.bin` | 17,289,388 | 15,706,444 | ISP/DSP 固件，**传感器绑定 → 保留 ZV-E10** |
| `bonobo.bin` | 172,144 | 172,144 | **完全相同大小** |
| `bt_firm.hcd` | 61,787 | 61,787 | 蓝牙固件，相同 |
| `dfe_app.bin` | 1,956 | — | ZV-E10 独有 |
| `dfe_dat.bin` | 65,536 | — | ZV-E10 独有 |
| `initrd.img` | 2,183,168 | 2,183,168 | 见 §二 |
| `ldr_drv.bin` | 7,012 | 7,012 | 相同 |
| `lensbin_lensproperty/` | 有 | — | 镜头属性，ZV-E10 独有 |
| `lif_app.bin` | 6,716 | — | ZV-E10 独有 |
| `sabin/` | 有 | 有 | 见下 |
| `vmlinux.bin` | 4,117,760 | 3,804,032 | 内核，各自平台 |
| `wole_app.bin` | 10,848 | 11,180 | 接近 |

⇒ **`/system` 全部硬件绑定，移植必须保留 ZV-E10 的**（与既有方案一致）。

---

## 五、`/usr/kmod`：**103 : 103，只差一个模块**

```
仅 ZV-1F 有 : usb_max14648.ko     （USB 电源开关芯片 MAX14648）
仅 ZV-E10 有: usb_darwin.ko       （Darwin 电源监控 MCU 驱动）
```

⇒ 两者是对应的**USB 电源管理芯片驱动**，各自硬件绑定 ⇒ 保留 ZV-E10 的，**1:1 对称，无遗漏**。

（17 个"内容不同但名字相同"的模块已在 `PORT_REVIEW_22FILES.md` 复核：15 个导入符号集完全相同。）

---

## 六、`/usr`（nflasha15）从 manifest 看决策分布

| 目录 | 条目 | 决策分布 |
|---|---:|---|
| `share` | 3,770 | 3,220 `DROP_F_OPTIONAL`（屏幕朗读语音）/ 223 `DROP_OLD_UI` / 113 KEEP / 100 ADD_F / 77 TAKE_F / 36 ONLY |
| `lib` | 244 | **114 KEEP_E10 / 73 TAKE_F** / 21 ONLY / 18 ADD_F / 15 DROP_OLD_UI / 3 DROP_LEGACY |
| `kmod` | 104 | 85 KEEP / 17 REVIEW / 1 ONLY / 1 DROP_F_HARDWARE |
| `sabin` | 90 | 72 KEEP / 13 DROP_F_HARDWARE / 5 ONLY |
| `bin` | 53 | 37 KEEP / 9 ONLY / 4 REVIEW / 3 ADD_F |
| `scenario` | 35 | 27 KEEP / 4 ONLY / **2 TAKE_F** / 2 DROP_LEGACY |
| `codec` / `tool` / `sbin` / `wifi-scripts` / `wlan` | 少量 | 以 KEEP 为主 |

`scenario` 里仅 2 个 TAKE_F（很少但可能要紧）：
```
scenario/CAMERA_SCN_FACE_REC_DATA.so     同大小 7,552
scenario/MPR_SCN_EXEC_FACTORY_MODE.so    6,192 → 5,633
```

---

## 七、结论：移植方案需要补 1 处

| 项 | 原方案 | 本次核对 |
|---|---|---|
| 重建 `/usr`（nflasha15） | ✅ | ✅ 成立 |
| `/rootfs` 补 3 个空目录 | ✅ | ✅ **成立且完整**（复核确认无需其它文件） |
| `/system` 保留 ZV-E10 | ✅ | ✅ 成立（全硬件绑定） |
| `/usr/kmod` 保留 ZV-E10 | ✅ | ✅ 成立（1:1 对称，只差 USB 电源芯片驱动） |
| 启动链不动 | ✅ | ✅ **强化**：initrd 409 条目全同，仅 init 差 112 B，且差异是可选模块 + tmpfs 挂载 |

⇒ **整体风险比之前评估的更低**：`/rootfs` 只需补 3 个空目录，无其它文件改动。
