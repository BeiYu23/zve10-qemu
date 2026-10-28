# ZV-1M2 确认：ASTRA 平台，可作供体（已完整解包）

## 一、★★★ 铁证：资源文件名里写着 ASTRA

```
ZV-1M2 /share/app:
  image_00_34_ASTRA.uxc    image_00_43_ASTRA.uxc
  image_01_34_ASTRA.uxc    image_01_43_ASTRA.uxc
  image_02_34_ASTRA.uxc    image_02_43_ASTRA.uxc
```

**⇒ ZV-1M2 是 ASTRA 平台（CXD90045 系），确定无疑。**

## 二、解包方法（关键：用 ma1co fwtool 的 k8 crypter）

之前失败的原因：用了 `research/firmware/fdat_decrypt.py`（只实现 `CXD90045` 一代）。

**正确的工具在 `/mnt/build/ct_backup/fwtool_a7iv/fwtool/sony/fdat.py`：**

```python
('CXD90045_k0', lambda: AesCbcCrypter(key_aes, key_cxd90045_k0)),
('CXD90045_k8', lambda: AesCbcCrypter(key_aes, key_cxd90045_k8)),   ← ZV-1F / ZV-1M2
('CXD90057_k0', lambda: AesCbcCrypter(key_aes, key_cxd90057_k0)),
('CXD90057_k8', lambda: AesCbcCrypter(key_aes, key_cxd90057_k8)),   ← ARGO
```

**验证（第一块校验和）：**
```
CXD90045_k8 + ZV-1F   cks=0xCD0D calc=0xCD0D ✅  UDTRFIRM
CXD90045_k8 + ZV-1M2  cks=0xA058 calc=0xA058 ✅  UDTRFIRM   ★
CXD90057_k8 + 两者    均 ❌
```

**⇒ ZV-1M2 与 ZV-1F 用【同一个 crypter】⇒ 同 SoC 族。**

解包结果：`firmware.dec` 624,726,016 B → tar 101 项 → `nflasha15` 530,345,984 B（506 MB）

## 三、三机型对比（实测）

| 项 | ZV-E10 | ZV-1F | **ZV-1M2** |
|---|---|---|---|
| model ID | `0x01030010` | `0x11020010` | **`0x21020010`** |
| crypter | `CXD90045` | **`CXD90045_k8`** | **`CXD90045_k8`** |
| **SoC** | **ASTRA** | **ASTRA** | **★ ASTRA（文件名铁证）** |
| `/usr` 分区 | 300 MB | 300 MB | **506 MB** |
| `appFw.so` | —（老菜单） | 53,189,440 | **37,150,720** |
| `libObj.so` | 21,305,528 | 3,929,768 | **3,931,624** |
| `.uxc/.uxb` 数 | 299 (老 view*) | 152 | **224** |

## 四、★ ZV-1M2 的资源结构（比 ZV-1F 更强）

```
框架（与 ZV-1F 同名）：
  LayoutMaster_Menu.uxc   LayoutMaster_Other.uxc
  master_browser / camera / network / normal / other.uxc
  color_cmn.uxc  style_cmn.uxc  lang_cmn.uxc
  color.uxb  lang.uxb  style.uxb  image.uxb

★ ZV-1M2 独有：34 个文件各有 3 种变体
  _f        34 个   （ZV-1F 也有）
  _g        34 个   ← ★ 新变体
  _vertical 34 个   ← ★★ 竖屏布局（对应 2.00 版"横竖布局"功能）
```

**⇒ ZV-1M2 = 同一套框架 + 竖屏变体 + 新语言变体。**

## 五、⇒ 结论

| 问题 | 答案 |
|---|---|
| 是 BIONZ X / CXD90045 吗？ | **是，ASTRA 平台（资源文件名直接写了 ASTRA）** |
| 比 ZV-1F 晚吗？ | **是**，2023-05 vs 2022-10 |
| 功能更多吗？ | **是**：Cloud 直传、Wi-Fi 强度、**横竖布局（`_vertical` 资源）**、新 `_g` 变体 |
| 能当供体吗？ | **★ 能，已完整解包** |
| 比 ZV-1F 更好吗？ | **是**：同框架 + 更多资源变体；但 `appFw.so` 更小（37 MB vs 53 MB），需确认功能差异 |

## 六、下一步建议

**ZV-1M2 现在可用于：**
1. 取其 `.uxc` 资源做「新菜单内容」的候选（竖屏布局是新功能）
2. 对比其 `appFw.so`（37 MB，更小）与 ZV-1F 的（53 MB）——**可能更精简、更适合移植**
3. 用其 model ID `0x21020010` 做官方更新包的机型注册实验
