# ★ 第二个启动阻塞点：8 个「新 UI 依赖库」被错配成 ZV-E10 版

> 2026-10-28 goal round 3。这比 round 2 的 libmprctrl 缺口更严重。

## 一、成因：build_hybrid.py 的分类判据有 bug

```python
# decide() 里，两个镜像都有同名文件但内容不同时：
elif path in ALWAYS_TAKE_F_EXACT or ... or path in lib_take_f or aggressive:
    TAKE_F          # 取新机型版本
else:
    KEEP_E10_REVIEW  # "存在差异但未被新 UI 依赖链引用，默认保留 ZV-E10"
```

**⇒ "未被新 UI 依赖链引用" 这个判定是错的。**
**`lib_take_f` 由反向闭包算出，但算漏了 8 个库。**

## 二、漏掉的 8 个库（都在新 UI 的真实依赖闭包里）

| 库 | M2 版 | E10 版 | 差异 |
|---|---:|---:|---|
| **`libInfraSKD.so`** | **7,259,708** | **10,184** | **相差 700 倍** |
| **`libSysDef.so`** | **6,033,848** | **3,267,652** | 相差近 2 倍 |
| `libNetCommon.so` | 122,876 | 122,876 | 同大小、内容不同 |
| `libmintx.so` | 1,532,236 | 1,532,236 | 同大小、内容不同 |
| `libMWF.so` | 247,864 | 247,776 | 内容不同 |
| `libGOConnectionManager.so` | 72,108 | 70,228 | 内容不同 |
| `libInfraUsbCmn.so` | 93,984 | 94,612 | 内容不同 |
| `libUsbControl.so` | 98,356 | 94,388 | 内容不同 |

**⇒ 符号名恰好都能解析（所以符号闭包检查全过），但内容/数据完全不同
⇒ 运行时会行为异常或崩溃 —— 这类问题静态符号检查抓不到，只有比 md5/尺寸才能发现。**

## 三、反向闭包的正确算法

```
从新 UI 三个入口出发，只在【M2 自己的 /usr/lib】里递归解析 DT_NEEDED：
    appFw.so / appFwBoot.so / AdjChkConfig.so
⇒ 闭包 = 17 个库，全部由 M2 提供
⇒ 其中 8 个在 hybrid 里是 E10 版 ⇒ 必须换成 M2 版
```

## 四、修复结果：v8

```
/mnt/build/nflasha15_v8.ext2
  249,102,336 B
  md5 f09c7e96f937ecec3be16b254055c0a5
  e2fsck 通过；985 文件；233,034/243,264 blocks（空闲 ~10 MB）
```

### 复验（三项全过）

```
① 全镜像传递依赖闭包：224 个根 / 185 个库 ⇒ 零缺失
② M2 /usr/lib 覆盖：0 缺口
③ 关键组件符号闭包：
     appFwBoot.so       UND 36   缺 0  ✅
     libMWF.so          UND 103  缺 0  ✅
     libSysDef.so       UND 6    缺 0  ✅
     libInfraSKD.so     UND 150  缺 0  ✅
     libNetCommon.so    UND 129  缺 0  ✅
     libmintx.so        UND 145  缺 0  ✅
     appFw.so           UND 753  缺 2  ← 与 M2 原版完全相同的懒绑定缺口
     libObj.so          UND 688  缺 1  ← arch_phys_to_cache（内核导出，运行时解析）
     AdjChkConfig.so    UND 180  缺 1  ← 与 M2 原版相同
```

### 附带结论

```
换成 M2 版 libNetCommon.so 后，它不再 DT_NEEDED libmprctrl.so
（M2 版依赖 libMWF + libmintx）
⇒ round 2 补的 libmprctrl.so 是"保留 E10 版 libNetCommon"的副作用
⇒ 保留它无害，但说明根因是【版本错配】而不是【缺文件】
```

## 五、方法论教训（与 round 2 同类）

```
round 2 教训：只查直接依赖，漏了传递依赖
round 3 教训：只比符号名，没比【内容/尺寸】
⇒ 判定"两个库是否可互换"时，必须同时满足：
   a) 传递依赖闭包完整
   b) 被依赖方提供了所有符号
   c) 两边文件内容一致（或至少尺寸/关键数据段一致）
   【c】是最容易被忽略的一条 —— 符号对上不代表行为对上
```

## 六、v8 关键文件体检（全部在位）

```
bin/im.elf 23,240 / bin/bootin.elf 15,336 / bin/sen.elf 19,208 / bin/launch_shell.elf 5,140
lib/libIMDB.so 40,596 / lib/appFwBoot.so 8,312 / lib/appFw.so 64,273,528 / lib/libObj.so 3,931,624
lib/libInfraSKD.so 7,259,708 / lib/libSysDef.so 6,033,848 / lib/libNetCommon.so 122,876
lib/libMWF.so 247,864 / lib/libmintx.so 1,532,236 / lib/libmprctrl.so 263,812
share/app/global.xdb 16,112 / style_cmn.uxc 2,004 / area_check_data_vertical.dat 1,024,593
share/avsystemsound/beeplist.txt 15,586
模式全部 555（库/可执行）/ 644（数据），与原厂一致
```
