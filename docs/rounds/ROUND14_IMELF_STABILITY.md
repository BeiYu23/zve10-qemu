# 轮次 14：启动锚点 im.elf 的依赖稳定性确认

## 一、为什么查这个

`im.elf` 是整条启动链的锚点：init → bootin → **im.elf** → libIMDB → appFwBoot → appFw。
如果它的核心依赖在 v9 里被换成 M2 版且行为不同，锚点就会不稳，前面的分析全部白费。

## 二、im.elf 的 15 个 DT_NEEDED 逐个核对

| 依赖 | v9 里的版本 | 归属 | 风险 |
|---|---:|---|---|
| `libjemalloc_ds.so.1` | 2,936 | **两机型完全相同** | 无 |
| `libbackup.so` | 9,384 | **两机型完全相同** | 无 |
| `libosal_utm.so` | 4,900 | **两机型完全相同** | 无 |
| `libssl.so.3` | 412,204 | **两机型完全相同** | 无 |
| `libcrypto.so.3` | 2,656,568 | **两机型完全相同** | 无 |
| `libstdc++.so.6` | 19（符号链接） | **两机型完全相同** | 无 |
| **`libIMDB.so`** | **40,596** | **M2 版** | ★ 有意（刷机方案正需要） |
| **`libosal_uipc.so`** | **26,692** | **E10 版** | ★ 有意（锚点保护，6.44% 差异但导出相同） |
| `libdl.so.2` / `libpthread.so.0` / `libSTMcmdexecute.so` / `libm.so.6` / `libgcc_s.so.1` / `libc.so.6` / `librt.so.1` | — | rootfs 提供 | 无 |

**⇒ 结论：im.elf 的依赖里只有两个版本有实质差异，且两个都是【有意选择】：**
```
libIMDB.so     = M2 版  ← 新架构的模块表，正是要的
libosal_uipc.so = E10 版 ← 锚点保护（若 v9 失败，第一个单变量实验就是换它）
其余全部是两机型共用的同一份文件 ⇒ 零风险
```

## 三、这一条为什么重要

```
之前所有分析都假设"im.elf 能正常跑"。
如果它的 libbackup / libosal_utm / libjemalloc 被换成 M2 版且语义不同，
可能出现：im.elf 启动到一半、消息队列注册失败、甚至 dlopen 失败。
现在确认：这些库两机型【共用同一份文件】，不存在版本分叉。
⇒ im.elf 作为启动锚点是稳的
```

## 四、两个方案的最终配置对照

### 方案 A：loop + bind（不刷机）

```
preload = /usr/tool/libefence.so : /log/libIMDB_m2.so : /log/bindm2.so
镜像    = /mnt/build/nflasha15_v9.ext2 放在 /log/m2usr.img（或 /sd、/tmp）

libIMDB  → 靠 /log/libIMDB_m2.so 的 SONAME 顶替（DT_NEEDED 阶段）
其他所有 → 靠 bind（构造函数阶段，早于所有运行时 dlopen）
libosal_uipc → 仍是 E10 的（/usr/lib 被 bind 成 M2 镜像，
               但 v9 里那份就是 E10 的）✓ 一致
```

### 方案 B：fastboot 刷 /usr

```
镜像 = /mnt/build/nflasha15_v9.ext2
libIMDB → 直接是 M2 版（DT_NEEDED 自然满足，不需要 SONAME 技巧）
其余同上
```

**⇒ 两个方案最终生效的文件集合完全相同，只是生效机制不同。**
**⇒ 因此 A 的测试结果可以直接预测 B 的结果（唯一差别是 A 多了一层 bind）。**

## 五、轮次 14 结论

```
im.elf 依赖稳定性：确认无风险（除两个有意选择的版本外全部共用）
两方案的生效文件集合等价 ⇒ A 能验证 B
静态分析到此已无新的高危项可查
```
