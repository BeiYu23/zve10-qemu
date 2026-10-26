# ZV-1M2 菜单移植计划（资源已就绪）

> 目标：把 **ZV-1M2** 的新菜单移植到 ZV-E10 上真机启动。
> ZV-1M2 功能比 a6700 还多，有竖屏 UI。

---

## 一、★★★★★ 关键发现：ZV-1M2 与 ZV-1F 布局完全一致

```
文件                    ZV1M2      ZV1F       ZVE10
master_camera.uxc       101868     101868     101804
master_network.uxc       35836      35836      35584
master_browser.uxc       28052      28052      27328
master_other.uxc         13700      13700      14696
master_normal.uxc         4112       4112      -
LayoutMaster_Menu.uxc     3636       3636       3512
LayoutMaster_Other.uxc    2592       2592      -
color_cmn.uxc              388        388        312
style_cmn.uxc             2004       1940       1888   ← M2 与 ZV1F 不同
global.xdb               16112      13320      12252   ← M2 最大
```

**⇒ ZV-1M2 的**菜单骨架与 ZV-1F 完全相同**，
差异集中在：`global.xdb`（索引更多）+ `style_cmn` + 图像 + 字串变体。
**⇒ 之前对 ZV-1F 做的所有分析**直接可用**。**

## 二、★★★ 图像带 `_ASTRA` 后缀 = 与 ZV-E10 同 SoC

```
image_00_43_ASTRA.uxc   10.01 MB
image_00_34_ASTRA.uxc    9.92 MB
image_01_34_ASTRA.uxc    7.03 MB
image_01_43_ASTRA.uxc    6.99 MB
image_02_34_ASTRA.uxc    7.89 MB
image_02_43_ASTRA.uxc    7.86 MB
                       ────────
                       49.7 MB
```

**ASTRA = CXD90045 的平台代号，和 ZV-E10 **同一个 SoC** ⇒ 硬件层面兼容。**

**★ `global.xdb` 里还索引了 `_IPSX` 变体（另一个平台），但固件里**没有那些文件**
⇒ 跨机型共用索引的设计，运行时按平台选。**ZV-E10 只需 ASTRA 那套。**

## 三、资源已提取（`modpkg/`）

```
modpkg/
├── zv1m2_replace/      ★ 15 文件 / 664 KB  —— 用【现有代码】就能测
│   ├── module.prop
│   └── system/share/app/   （14 个替换类文件）
└── zv1m2_min/          27 文件 / 1.5 MB  —— 完整结构（需 Phase 3）
    ├── module.prop
    └── system/share/app/   （11 新增 + 14 替换 + 1 相同）
```

**提取方式：`debugfs -R "dump /share/app/<file>"`（直接读 ext2 镜像，**不挂载，零风险**）**

### `zv1m2_replace` 的 14 个文件（全部是 ZV-E10 已有的）

```
global.xdb                    16,112   ← ★ 核心索引
master_camera.uxc            101,868
master_network.uxc            35,836
master_browser.uxc            28,052
master_other.uxc              13,700
LayoutMaster_Menu.uxc          3,636
color_cmn.uxc                    388
style_cmn.uxc                  2,004
string_english.uxc            46,552
string_english_f.uxc         147,420
string_simplifiedchinese.uxc   42,304
string_simplifiedchinese_f.uxc 127,672
string_canadianfrench.uxc      41,176
string_canadianfrench_f.uxc    43,216
```

**★★★ 全部是**替换**，不是新增 ⇒ `apply_overlay_tree()`（单文件 bind）**直接能加载**，
不需要 Phase 3 目录级覆盖。**

### `zv1m2_min` 多出的 11 个新增文件

```
color.uxb                                52
LayoutMaster_Other.uxc                2,592
master_normal.uxc                     4,112
string_english_func_j.uxc           159,448
string_english_g.uxc                 42,372
string_english_horizontal.uxc       108,176
string_english_vertical.uxc         108,628
string_simplifiedchinese_func_j.uxc 139,648
string_simplifiedchinese_g.uxc       39,160
string_simplifiedchinese_horizontal.uxc 100,384
string_simplifiedchinese_vertical.uxc   100,476
```

**⇒ 这 11 个必须 Phase 3（符号链接农场）才能加载。**

## 四、分三步实测

### 第 1 步【664 KB，现有代码】只替换

```
部署：/log/zvegisk/mods/zv1m2-replace/  ← zv1m2_replace 的内容
      /zvegisk/boot_mode = "new"
重启
```

**预期：**
- **✅ 菜单结构变了**（布局/文字不同）
- **⚠️ 缺图**（image_00_34_ASTRA 等不存在）⇒ 可能空白/错乱
- **⚠️ 缺 `master_normal.uxc` 等新增文件** ⇒ 某些功能进不去

**★ 无论结果如何都有价值：**
- **能起来 ⇒ 引擎接受 ZV-1M2 的索引 ⇒ 值得继续**
- **崩了 ⇒ watchdog 回滚 ⇒ 知道缺什么**

### 第 2 步【+49.7 MB】加图像

**需要先实现 Phase 3 目录级覆盖。**

### 第 3 步【+26 MB】加全部语言包

**或者只加英语+中文（约 700 KB）。**

## 五、⚠️ 风险与回退

| 风险 | 回退方式 |
|---|---|
| **UI 黑屏/错乱** | **拔 SD / 卡上 `boot_mode=old`** |
| **连续黑屏** | **boot_count>3 自动删 preload** |
| **快速循环** | **phase 标记 2 次即自删 preload** |
| **彻底救不回** | **BootROM fastboot + 改 1 字节（已实机验证）** |

**★ 第 1 步用的是**替换**，原文件都还在 `/usr` 里（bind 只是覆盖视图），
**回退=重启**（bind 消失）。**

## 六、下一步

```
① 确认 zv1m2_replace 的文件完整性（魔数/大小已验）
② 传到相机 /log/zvegisk/mods/
③ 先用 agent 手动测一次 apply_overlay_tree（不设 dmode）
④ 无问题 → boot_mode="new" → 重启
```

**★★★ 第 ①③ 步都可以在**不碰开机配置**的情况下做完。**
