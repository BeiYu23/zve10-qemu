# av-cam.bin（CP 固件）对比：创意风格 vs 创意外观的落地证据

> 起因：我此前断言"E10 的 CP 里没有 Creative Look 的色彩实现"，那是**推断**。
> 本文把它变成**实测证据**。

## 一、两份 av-cam.bin 的形态

```
格式: Sony ORIL 容器（魔数 "ORIL"，头 0a0000ea 4f52494c 04000000）

ZV-E10  17,289,388 B   md5 cdcae9d4fdbf66a33704a4c7564e346d
ZV-1M2  17,195,164 B   md5 f8a0c402db2ba53216fd05f50da9e43d
共同前缀 72 字节（ORIL 头），其余 69% 字节不同
```

**字符串集合：**
```
E10 唯一 13,980   M2 唯一 17,701   共有 51,218
```

## 二、★★★ 决定性证据：`CheckSet*` 命令处理器

**`CheckSet*` 是 CP 的 LIRO 命令校验入口，即"CP 实际能处理哪些命令"。**

```
ZV-E10 : 210 个
ZV-1M2 : 216 个
共有   : 210 个   ← E10 是 M2 的【真子集】，E10 独有 0 个
M2 独有:   6 个
```

### 2.1 M2 独有的 6 个（E10 的 CP 无能力处理）

```
CheckSetClarity      ← ★ Creative Look 参数
CheckSetFade         ← ★ Creative Look 参数
CheckSetFineness     ← ★ Creative Look 参数
CheckSetHighlight    ← ★ Creative Look 参数
CheckSetShadow       ← ★ Creative Look 参数
CheckSetFlipImage    ← 与 Creative Look 无关（图像翻转）
```

**⇒ 6 个里有 5 个正好是 Creative Look 的专属参数。**

### 2.2 配套的 SET 日志串（同样只在 M2 里）

```
M2 独有:
  FW: SET_CLARITY[%d]        FW: SET_FADE[%d]
  FW: SET_HIGHLIGHT[%d]      FW: SET_SHADOW[%d]
  FW: LiroSeq ParamCheckErr@%s[clarity] cur:%d req:%d
  FW: LiroSeq ParamCheckErr@%s[fade] cur:%d req:%d
  FW: LiroSeq ParamCheckErr@%s[sc_Highlight] cur:%d req:%d
  FW: LiroSeq ParamCheckErr@%s[shadow] cur:%d req:%d
  %s[%d] CreativeLook DSLR Test
  %s[%d] CreativeLook MovieMode Test
  %s[%d] CreativeLook pictureEffect Test

E10 独有: 【无】
```

**⇒ `LiroSeq` 说明这条链走 LIRO 框架：`appFw → LIRO → CP`。
⇒ `SET_*` 是 CP 侧的实际写入点，`ParamCheckErr` 是越界校验。
⇒ **E10 的 CP 里这些串一个都没有 ⇒ 收到这些命令时无法处理。****

## 三、两边 CP 相同的部分（210 个处理器）

**包括全部基础相机控制：**
```
CheckSetAeMeterMode  CheckSetAfMode  CheckSetAfArea  CheckSetAPSCMode
CheckSetAgcLimit     CheckSetAutoReviewTime  CheckSetAntiBlinkMode  …
（共 210 个，E10 与 M2 完全一致）
```

**⇒ CP 的能力面 97% 相同，差异被压缩到极少数新增命令。**

## 四、两边的风格名都在 CP 里（但含义不同）

**两边 av-cam.bin 都含相同的 10 个老风格名：**
```
Standard Vivid Portrait Landscape Sunset Sepia Neutral Deep Light Clear
（各出现 2 次，两边完全一致）
```

**⚠️ 但这不等于 M2 使用它们 —— 更可能是共享代码库保留的遗留枚举。
M2 的 UI 侧（appFw）用的是 `FL/IN/SH/VV` 那套 Creative Look 命名，
而 CP 侧通过数值枚举而非字符串接收。**

## 五、⇒ 对移植的精确结论

**把创意外观（Creative Look）搬到 ZV-E10 后：**

| 层 | 结果 | 依据 |
|---|---|---|
| UI 显示 12 个预设 + 8 个参数 | ✅ 正常（appFw 自带） | appFw 里 875 处 creative_look |
| 选择并存储 | ✅ 正常 | 参数只是数值 |
| **contrast / saturation / sharpness** | ✅ **生效** | 200 个共有处理器覆盖 |
| **highlight / shadow / fade / clarity / fineness** | ❌ **不生效（命令被拒）** | **CP 缺 5 个 `CheckSet*` 处理器** |

**⚠️ 且 CP 侧有 `ParamCheckErr@%s[...] cur:%d req:%d` 日志 ——
E10 的 CP 收到未知命令时的行为未知：可能忽略、可能返回错误码。
⇒ 不会崩（没有对应处理器），但可能在上层留下错误状态。**

## 六、附带发现

**`CheckSetFlipImage` 只在 M2 的 CP 里 ⇒ 图像翻转也是 E10 CP 不支持的功能。
（与 Creative Look 无关，单列。）**

## 七、方法论备注

**我原先的结论方向正确，但依据是"CP 里没有 creative 字样"——那是**弱证据**。
这次用 `CheckSet*` 处理器集合的【集合差】来证明，是可复现的强证据。**

**⇒ 判据升级：不问"有没有相关字符串"，而问"有没有对应的命令处理器"。**
