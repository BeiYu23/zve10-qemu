# ZV-E10 II 固件落地与分析（2026-10-24）

> 目的：验证"从 ZV-1F + ZV-E10 II 取材"这个说法。
> **结论：ZV-E10 II 不能用于 ZV-E10 的移植**（架构代际不同），ZV-1F 仍是唯一可行供体。

---

## 一、下载与解包（可复现）

| 项 | 值 |
|---|---|
| 机型码 | **ZV-E10M2**（不是 ILCE-ZVE10M2） |
| 版本 | **1.02**，发布 2025-11-11 |
| 固件页 | `https://support.d-imaging.sony.co.jp/www/cscs/firm/?mdl=ZV-E10M2&area=jp&lang=jp&ref=update` |
| 直链（本次使用） | `https://support.d-imaging.sony.co.jp/download/NEX/rF53df825a/BODYDATA.DAT?fm=jp` |
| 落地 | `/mnt/build/downloads/BODYDATA_ZVE10M2_V102.DAT` |
| 大小 | **1,091,548,640 B**（1.02 GiB） |
| md5 | `db6b85db08c73d1c5c0f8c2571cb4a90` |
| 解包工具 | `/mnt/build/ct_backup/fwtool_a7iv/fwtool.py`（密钥变体自动判定 `CXD90057_k8`） |
| 解包产物 | `/mnt/build/fw/unpacked_zve10m2/` |
| 分区 | `nflasha3`(system) / `nflasha5` / `nflasha7`(rootfs) / `nflasha8` / `nflasha15`(usr, 808 MiB) + `updater.img` |
| `version.txt` | `33002_03.2025101101` ✓ 与既有报告一致 |

复现：
```sh
export https_proxy=http://127.0.0.1:7890 http_proxy=http://127.0.0.1:7890
curl -sSL -A "Mozilla/5.0" -o BODYDATA_ZVE10M2_V102.DAT \
  "https://support.d-imaging.sony.co.jp/download/NEX/rF53df825a/BODYDATA.DAT?fm=jp"
python3 /mnt/build/ct_backup/fwtool_a7iv/fwtool.py unpack \
  -f BODYDATA_ZVE10M2_V102.DAT -o /mnt/build/fw/unpacked_zve10m2
```

---

## 二、★ 决定性事实：ZV-E10 II 是 **ARM64 / ARGO** 世代

```
lib/appFw.so : ELF 64-bit LSB shared object, ARM aarch64   ← ★ 不是 ARM32
bin/*.elf    : ARM aarch64, interpreter /lib/ld-linux-aarch64.so.1
```

**ZV-E10 是 ARM32（CXD90045 / cortex-a5）。⇒ 它的可执行代码一个字节都用不了。**

### 三代 appFw 的 ASTRA 支持度对比（关键指标）

| 机型 | 架构 | 大小 | **ASTRA 相关串** | "not supported" 总数 |
|---|---|---:|---:|---:|
| **ZV-1F** | **CXD90045 / ASTRA** | 53,189,440 | **37** | 57 |
| **ZV-E10 II** | **CXD90057+58 / ARGO** | 93,620,512 | **4** | 27 |

`appFw` 里出现的架构枚举：**`ARGO` / `ASTRA` / `IPSX`** 三种。
ZV-E10 II 属于 **ARGO**（它自己的新世代），对 ASTRA 的支持已**从 37 处退化到 4 处**
—— 基本是残留引用，不是可用支持。

⇒ **ZV-1F 是唯一真正带 ASTRA 支持的新 UI 供体。**

---

## 三、资源层对比：ZV-E10 II 是更新一代，不能与 ZV-1F 的 appFw 混用

`/usr/share/app`：

| 项 | ZV-E10 II | ZV-1F |
|---|---:|---:|
| 文件数 | **232** | 164 |
| 语言数 | 34 | 34（**完全相同**） |
| `LayoutMaster_Menu.uxc` | 3,636 B | 3,636 B（同尺寸） |
| `LayoutMaster_Other.uxc` | 2,592 B | 2,592 B（同尺寸） |
| `image.uxb` / `lang.uxb` / `style.uxb` / `color.uxb` | 同尺寸 | 同尺寸 |
| `global.xdb` | **16,112 B**（md5 `34f4039b…`） | **13,320 B**（md5 `83a8e2e9…`）**不同** |
| `area_check_data.dat` | 1,142,255 B | 1,273,040 B |
| 同名文件 | 共 123 个：**仅 12 个字节相同，111 个不同** | — |

**架构标签化资源**：
- ZV-E10 II 有 6 个 `image_XX_YY_IPSX.uxc`（**IPSX 专用图**），**0 个 ASTRA**
- ZV-1F 有 `image_XX_34/43.uxc`（无架构后缀）

**字符串资源代际差异**：
- ZV-E10 II：每种语言三套 `string_<lang>_f.uxc` + `_func_j.uxc` + `_g.uxc`
- ZV-1F：每种语言仅 `string_<lang>_f.uxc`

⇒ **ZV-E10 II 的资源是为"更新一代 appFw"准备的**。
若把它塞给 ZV-1F 的 ARM32 appFw，格式/索引（`global.xdb` 不同）大概率不匹配。

---

## 四、ZV-E10 II 独有的新东西（与我们的移植无关）

顶层目录比 ZV-1F 多一个 **`data/`**：

```
data/ai_dict     AI 主体识别词典（新一代 AI 对焦）
data/gridline    网格线资源
data/lut         LUT（可导入查找表）
```

都是 ZV-E10 不具备的新功能，**移植时不需要**。

`/system`（nflasha3）只有：`av-cam.bin` / `bonobo.bin` / `bt_firm.hcd` /
`initrd.img` / `sabin` / `vmlinux.bin` / `wole_app.bin`
—— **没有任何机型能力配置文件**，再次印证：能力值来自平台运行时，不是数据文件。

---

## 五、对"从 ZV-1F + ZV-E10 II 取材"的判断

| 说法 | 我的验证结论 |
|---|---|
| 用 ZV-E10 II 的**代码** | ❌ 不可能：**ARM64**，ZV-E10 是 ARM32 |
| 用 ZV-E10 II 的**资源** | ⚠️ 风险高：资源属**更新一代**，`global.xdb` 不同、多出字符串变体、图为 IPSX 专用 |
| 用 ZV-E10 II 的**能力表** | ❌ 不存在（全盘无能力表，`VAL_HAITA` 只在 appFw 代码里） |
| 用 ZV-E10 II 做**参考** | ✅ 有价值：能看到最新一代 UI 的组织方式（`LayoutMaster`、语言变体、`data/` 结构） |

**我的判断**：如果对方确实"成功用 ZV-E10 II 取材移植到 ZV-E10"，
那他取的很可能是**很有限的资源或参考信息**，而**主体代码必然来自 ZV-1F**。
仅凭 ZV-E10 II 在架构上无法支撑 ZV-E10 的运行。

**对当前工作的意义**：不必等待/依赖 ZV-E10 II。
**ZV-1F 单源方案在架构上是唯一自洽的选择**，且它的 ASTRA 支持度（37 处）远高于 ZV-E10 II（4 处）。

---

## 六、附带确认（对已有结论的加固）

1. ZV-E10 II 的 `vmlinux.bin` 是 ARM64 boot Image（对应既有报告 §3）
2. 有 `initrd.img`（5,218,304 B 级别），`version.txt = 33002_03.2025101101`
3. 其 `/usr` 为 808 MiB 分区（我们是 300 MiB）
4. `data/` 目录的存在与既有报告"新分区 `/cp` `/usr/data/lut` `/usr/data/ai_dict/usr`"吻合
