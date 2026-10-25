# 假里程碑纠正：屏幕上的"整屏底色"是 0x5A 填充，不是 App 绘制

> 触发：用户质疑「实体机 UI 从来没有这个颜色」+ zv-e10-re 的「没有 CPU 可见帧缓存」结论

## 一、数学反推

观察到的颜色 **(90, 73, 214)** 在 RGB565 下正好等于 **`0x5A5A`**：

```
R5 = round(90  * 31 / 255) = 11
G6 = round(73  * 63 / 255) = 18
B5 = round(214 * 31 / 255) = 26
(11 << 11) | (18 << 5) | 26 = 0x5A5A
```

反向验证 `0x5A5A → (90, 73, 214)`，**零误差**。
⇒ 缓冲区内容是重复的 `0x5A` 字节。

## 二、铁证：本项目 QEMU 模型自己的注释

`hw/display/bionz_streamfb.c`：

```c
/*
 * Cheap "did the guest write anything at all?" check.  The stream buffer is
 * memset() to 0x5a by the VDF init code and stays like that until the App
 * actually renders, so a uniform buffer means "no frame yet".
 */
static bool streamfb_buffer_uniform(const uint8_t *src, size_t size, uint32_t *value)
```

**三件事同时成立：**
1. 缓冲区由 **VDF init 代码 `memset(0x5A)`**
2. 它会**一直保持 0x5A 直到 App 真正渲染**
3. **均匀缓冲 = 「还没有帧」**

**模型里本来就有 `streamfb_buffer_uniform()` 用来判定这件事 ——
"整屏同色"恰恰是它用来判定**未渲染**的条件。**

## 三、被推翻的结论

| 原判定 | 实际 |
|---|---|
| 「App 原生绘制：整屏底色 (90,73,214)」 | **❌ 是 VDF 初始化填充，App 从未渲染** |
| 「distinct=1，画面单一」 | **distinct=1 是「没写」的判据，不是成功的判据** |

**与另外两条观测自洽：**
- `GRM2 日志 = 0` ⇒ App 没调用 GRM 绘制
- `INPUTINJ = 0` ⇒ App 没注册输入

## 四、为什么这条重要

**真机侧（zv-e10-re 实测）：**
- **没有 CPU 可见帧缓存**；显示是 descriptor-driven（APL/AIC + XDMAC + HME），面板由私有 `ldec` 驱动
- `0x20312000` 在真机上 **98.7% 为零**，不是扫描源
- `/dev/mem` 只能按页读（`dd bs=4` 返回 0 字节文件）

**⇒ `0x20312000` 只是 QEMU 侧**方便**的显示出口（模型镜像它），
**不能作为「App 画了」的证据**。**

## 五、正确的证据标准（照 a7c2-re）

a7c2 不依赖帧缓存，而是：

```
① 在 guest 用户态 interpose 绘制 API（他们的 mpgpe::Gperm*，本项目的 GRM_gperm*）
② 软件执行绘制请求（plan → rows/copy，带边界检查）
③ 帧投递走 App 自己的输出路径（冻结 metadata + 租期内复制 → 容器 → 读回校验 → 再通知完成）
④ 独立 viewer 读已提交容器 → 显示
```

**⇒ 帧是**从拦截到的绘制调用重建**的，不需要"找到帧缓存"。**

## 六、新的验收判据

```
屏幕上出现颜色        ⇏  进展
GRM 调用被命中且成功执行   ⇒  才是进展
```

**建议：在 `GRM_gpermFillrect` 的 interpose 里，首次命中时打一条**无条件**日志，
作为「App 开始绘制」的唯一标志；并把 `streamfb_buffer_uniform()` 接入验收脚本，
**uniform = 未渲染**。
