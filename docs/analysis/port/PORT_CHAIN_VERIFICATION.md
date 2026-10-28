# 移植链路验证：真的能跑起来吗

> 结论：**链路自洽（所有关键环节都是同一机型版本，无跨版本混搭），
> 但【从未实证运行过】—— 这是纸面验证，不是实测。**

---

## 一、✅ 已验证自洽的部分

### 1.1 启动入口是同一个二进制

```
im.elf     : ZV-E10 / ZV-1F / ZV-1M2  全部 23,240 B
bootin.elf : 三机型全部 15,336 B
两者加载的库列表完全一致
```

**⇒ 启动链入口没有变化 ⇒ "新菜单"不是靠不同的 im.elf 实现的，
而是靠 IMDB 注册的服务与 /usr/lib 的内容。**

### 1.2 ★ 关键分叉：IMDB 注册的资源名不同

```
ZV-E10 的 libIMDB 注册:  appFw.so
ZV-1F  的 libIMDB 注册:  appFwBoot.so     ← 名字不同！
```

**⇒ 混用会断链。我们混合镜像里 libIMDB 用的是 ZV-1F 版 ⇒ 与 appFwBoot.so 匹配 ✅**

### 1.3 UI 主链各环节的版本归属（实测 md5）

| 文件 | 混合镜像 | 归属 |
|---|---|---|
| `im.elf` | `a4824008` | ✅ ZV-1F |
| `bootin.elf` | `3545261c` | ✅ ZV-1F |
| `libIMDB.so` | `2bc43501` | ⚠️ ZV-1F **+QEMU 补丁**（v5；实体机须用 v4 未打补丁版） |
| `appFwBoot.so` | `7a90047e` | ✅ ZV-1F |
| `appFw.so` | `09a3e9e2` | ✅ ZV-1F |
| `libObj.so` | `7749275a` | ✅ ZV-1F |
| `libMWF.so` | `814cbc0c` | ✅ ZV-1F |

**⇒ UI 主链 7 个关键组件全部是 ZV-1F 版，没有混搭。**

### 1.4 ★★ 480 个 KEEP_E10 文件的 ABI 风险已排除

```
KEEP_E10 总数 480，其中共享库 108 个

字节级比对结果：
  完全相同 : 100
  有差异   :   0     ← ★ 零差异
  跳过/缺失:   8     （符号链接，非普通文件）
```

**⇒ 我们"保留 ZV-E10 版"的那 108 个库，在两版固件里【本来就是同一个文件】。**
**⇒ 不存在跨版本 ABI 冲突。**

### 1.5 `uxengine` / `grm` / `gpe` 不是独立库

```
libIMDB 注册表里列了 libuxengine.so / libgrm.so / libgpe.so / libBizInitializer.so
但两边固件里都没有这些文件
```

实测：`uxengine` 实现**在 `libObj.so` 内部**（13 处匹配）、`appFw.so` 里 5 处；
`grm`/`gpe` 只以**内核模块**形式存在（`kmod/grm_gles.ko`、`grm_ma.ko`）。

**⇒ IMDB 的列表是"尝试加载"清单，缺失不致命。不是链路上的空洞。**

---

## 二、⚠️ 仍然存在的风险（诚实清单）

| # | 风险 | 严重度 | 能否预先验证 |
|---|---|---|---|
| 1 | **从未实证运行** —— QEMU 里 appFw 从未成功加载过 | **高** | ❌ 只能上机 |
| 2 | v5 镜像含 IMDB mask 补丁（禁用 USB 供电等） | **高** | ✅ 用 v4 |
| 3 | 8 个"跳过"的符号链接未验证 | 低 | ✅ 可查 |
| 4 | ZV-1F 的库可能期望 ZV-E10 没有的外设 | 中 | ⚠️ 同 SoC，风险偏低 |
| 5 | 资源索引方案差异（`global.xdb` 313 vs 154 项） | 中 | 已分析，见下 |
| 6 | **触摸手势层**：ZV-E10 硬件支持，但新菜单的手势 UI 未在此硬件验证 | 中 | ❌ 只能上机 |

### 2.1 关于第 5 项：这是最可能"跑起来但不对"的地方

```
ZV-E10 global.xdb 引用 313 个名字（含 214 个 view*.uxc）
ZV-1F  global.xdb 引用 154 个名字（只有 image_* + string_*）
```

**我们的混合镜像用的是 ZV-1F 的 `global.xdb` + ZV-1F 的 152 个 `.uxc` ⇒ 内部自洽 ✅**

**⇒ 不是"用 ZV-E10 的资源配 ZV-1F 的引擎"，而是整套都是 ZV-1F 的 ⇒ 索引自洽。**

---

## 三、⇒ 回答"真的能跑起来吗"

### 3.1 静态链路：**自洽，没有明显断点**

```
im.elf(ZV-1F) → libIMDB(ZV-1F) → appFwBoot.so + appFw.so + libObj.so + libMWF.so（全 ZV-1F）
              → 100 个 KEEP_E10 库与 ZV-1F 字节相同（无 ABI 冲突）
              → /usr/share/app 全套 ZV-1F 资源（索引自洽）
```

**⇒ 从静态依赖看，这条链没有结构性矛盾。**

### 3.2 但这不是"能跑"的证明

**三个未被验证的假设：**
1. **ZV-1F 的 `appFw.so` 能在 ZV-E10 的硬件初始化序列下启动** —— 同 SoC，但外设清单可能不同
2. **新菜单的触摸手势在 ZV-E10 的 `input.ko` 上报格式下可用** —— 未验证
3. **没有任何运行时证据** —— QEMU 那次 appFw 根本没加载（被 fake_gles 的 -EINVAL 挡住）

**⇒ 准确说法：链路**没有已知的静态缺陷**，但**没有任何一次成功运行**。**

---

## 四、建议的最小验证方案

**用官方更新通道（不拆机、不 fastboot 裸写）：**

```
1. 用 v4 镜像（无 IMDB 补丁）构建 BODYDATA 包
   - 骨架：ZV-E10 官方 Ver 2.03 包（model 0x01030010、crypter CXD90045）
   - 内容：替换 0700_part_image/dev/nflasha15 为我们的混合 /usr
   - 重算所有 *_sum 的 CRC32
   - 重加密 FDAT（CXD90045 密钥已知）
   - 重建 UFU + DCRC32

2. 或更简单：fastboot 只写 /setting/updater/mode 标志 + SD 卡放官方包
   → 让 Sony 自己的 updater 跑（它有失败恢复）

3. 观察点（按顺序）：
   a. bootin.elf 退出码
   b. /proc/<pid>/maps 里有没有 appFw.so
   c. 屏幕有没有内容
```

**⇒ 第 2 条最安全：fastboot 只建一个空文件，刷写由 Sony 代码完成。**
