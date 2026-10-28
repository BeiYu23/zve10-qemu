# IMDB 表是家族级超集：180 条库记录里 108 条在本机不存在

> 起因：QEMU 侧发现 `libBizInitializer.so`（enable=0x01）在 `target=default`
> 下"应该加载却没加载（0 映射）"，怀疑另有加载条件。本文给出答案。

## 一、答案是：那文件根本不存在

真机 `/usr/lib` 实测（`nflasha15.img`）：

```
✗ libBizInitializer.so      ✗ appFw.so        ✗ subapp.so
✗ libgrm.so                 ✗ libgpe.so       ✗ libuxengine.so
✗ libNVM.so                 ✗ libAdjustUIProxy.so   ✗ libInfraMediaPwrMgr.so
```

**不是加载条件问题，是文件缺失。**

## 二、量化：IMDB 表是"家族级超集"

| 项 | 数量 |
|---|---:|
| 真机 `/usr/lib` 条目 | **209** |
| IMDB 表 `kind=library` 记录 | **180** |
| 其中本机**有**对应文件 | **72** |
| 其中本机**没有**对应文件 | **108** |

⇒ 60% 的 IMDB 库记录在本机不存在。原因：IMDB 表是
**A6100/A6400/A6600/ZV-E10 家族共用构建**的产物（老菜单本来就是家族超集，
`image_EVF.uxc` 之类的 EVF 资源也是同一现象）。

完整对照表见 [`data/imdb_vs_disk.tsv`](../data/imdb_vs_disk.tsv)（180 行，末列 `on_disk`）。

## 三、★ 图形/UI 栈的真实构成（本机只有这些）

```
libObj.so           21 MB   ← 含 22 个 GRM_* 导出（GRM_bitmapCreate /
                              GRM_fbrmFillrectYUV / GRM_fbrmSetScreenPreference …）
viewUnified2..8.so  18 MB   ← View 层（7 个）
libMWF.so                   ← 消息/框架层
libSysDef.so                ← 系统定义（调色板 id 密集）
libmpr.so / libmprctrl.so / libmprdummy.so
libBizFw.so / libBizFw2.so
CautionConfig.so / CautionRsrcConfig.so / ModelCautionConfig.so
```

**没有独立的 `libgpe.so` / `libgrm.so` / `libuxengine.so`** ——
GPE/GRM/UX 引擎的能力**都在 `libObj.so` 内部**（与移植报告里
"GRM_* 定义在 libObj.so 内部"一致）。

⇒ **不要等这三个库加载**，它们永远不会出现；也不会有人从外部提供
GPE/uxengine 的入口。

## 四、另一个容易误判的点：`libObj.so` 不经 IMDB 加载

`libObj.so` **不在 IMDB 库表里**。`im.elf` 里有一个硬编码字面量：

```
/usr/lib/libObj.so
```

⇒ `libObj.so` 是 im.elf **直接按路径 dlopen** 的，绕过 IMDB。
所以"在 IMDB 里找 libObj 的装载记录"是找不到的。

## 五、对 QEMU 侧的意义（三条可直接用）

1. **`libBizInitializer` 的谜团解除**：文件不存在，0 映射是正常结果。
   也**推翻**"它是 `Application_System_Init` / `DPro_App_UI_init` 的 stub 提供者"
   —— 本机这两个符号的真实现都在 `libObj.so`
   （`libObj.so` 导出 `Application_System_Init` @ `0x8a2409`，144 B）。
2. **MISSING 是常态**：108 条都缺，dlopen 失败被容忍。不要为了"补齐"
   去伪造这些库，那会引入与真机不等价的状态。
3. **判断"是否加载成功"应该以 `libObj` + `viewUnified*` 为准**，
   不要把家族超集里的条目当作进度指标。
