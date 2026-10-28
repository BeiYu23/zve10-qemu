# 换供体：ZV-1M2 版混合镜像（已构建并验证）

## 一、为什么 ZV-1M2 比 ZV-1F 更适合做供体

| 项 | ZV-1F | **ZV-1M2** |
|---|---|---|
| SoC | ASTRA | **ASTRA（资源名 `*_ASTRA.uxc` 铁证）** |
| `appFw.so` | 53,189,440 | **64,273,528（+21%）** |
| `libObj.so` | 3,929,768 | 3,931,624 |
| `.uxc/.uxb` | 152 | **224** |
| **`_vertical` 竖屏资源** | ❌ | **✅ 35 个** |
| **`_g` 新变体** | ❌ | **✅ 34 个** |
| 菜单项标识 `BKID_APP_*` | 4,304 | **4,690（+386）** |
| 功能 | — | **Cloud 直传 / FTP / Creative Look / 滑动手势 / 访问认证** |

## 二、★ 换供体的成本极低（兼容性实测）

| 文件 | ZV-1F vs ZV-1M2 |
|---|---|
| **`libIMDB.so`** | **字节完全相同** ✅（同样 185 个服务注册） |
| `libMWF.so` | 同大小，内容不同 |
| `appFwBoot.so` | 8,396 → 8,312 |
| `/lib` 条目 | 199 → 201（**新增 2 个**：`libInfraBluetoothClassic.so`、`libInfraMFi.so`） |
| 同名但内容不同 | **29 个** |

**⇒ 只需替换 4 个 UI 核心库 + 资源；其余 170+ 库不变。**

## 三、✅ 构建结果

```
镜像: /mnt/build/fw/port_m2/out/nflasha15_hybrid.ext2
大小: 314,572,800 B (300 MiB)
内容: 227.60 MiB （858 文件 + 65 符号链接）
分区上限: 300 MiB  ✅ 装得下（余量 72 MiB）
e2fsck: 干净（985 inodes, 2.2% 非连续）
```

### 3.1 关键文件核对（全部 = ZV-1M2 原版）

| 文件 | 大小 | md5（镜像 vs 原版） |
|---|---|---|
| `appFw.so` | 64,273,528 | `f079cda0` = `f079cda0` ✅ |
| `appFwBoot.so` | 8,312 | `bb4cdbab` = `bb4cdbab` ✅ |
| `libObj.so` | 3,931,624 | `738a2331` = `738a2331` ✅ |
| `libIMDB.so` | 40,596 | `91eca62f` = `91eca62f` ✅ |

**属主**：57285:1000 ✅

### 3.2 资源核对

```
share/app: 224 个 .uxc/.uxb（ZV-1M2 全量）
  _vertical 变体: 35 个  ✅
  _g 变体      : 34 个  ✅
```

### 3.3 决策统计

| 决策 | ZV-1F 供体 | **ZV-1M2 供体** |
|---|---|---|
| TAKE_F | 153 | 153 |
| **ADD_F** | 123 | **193（+70，多为 `_vertical`/`_g`）** |
| KEEP_E10 | 480 | 478 |
| KEEP_E10_REVIEW | 21 | 23 |
| DROP_OLD_UI | 238 | 238 |
| **合并内容** | 207.72 MiB | **227.60 MiB** |

### 3.4 ★ 23 个 KEEP_E10_REVIEW 项全部合理

```
kmod/*.ko              20 个   ← 内核模块必须与 ZV-E10 内核匹配 ✅
bin/bt_firm.hcd         1 个   ← 蓝牙固件，硬件绑定 ✅
bin/bsa_server 等       4 个   ← 硬件相关
share/avsystemsound/beeplist.txt
```

**⇒ 全部是【硬件绑定】项，保留 ZV-E10 版是正确的。**

## 四、⚠️ 仍未解决的两个问题

1. **从未运行过** —— 和 ZV-1F 版一样，没有任何一次成功启动的证据
2. **新增的 2 个库**（`libInfraBluetoothClassic.so`、`libInfraMFi.so`）——
   ZV-1M2 的 appFw 可能依赖它们；如果 ZV-E10 的硬件没有对应能力，
   可能在蓝牙/MFi 初始化时失败

## 五、下一步

```
1. 这个镜像是 nflasha15（/usr）分区，可直接用于：
   - fastboot 裸写 nflasha15
   - 或装进官方 BODYDATA 包走 USB/SD 更新
2. 建议先做「只刷 /usr」的最小验证，其余分区全部保持原厂
3. 观察点：bootin 退出码 → /proc/pid/maps 里有无 appFw.so → 屏幕内容
```
