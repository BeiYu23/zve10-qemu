# C2 结果：ZV-E10 的 `PRM_*` / `VAL_*` 参数体系（2026-10-23）

> 纯离线解析，全部基于 `zve10_delivery/nflasha15.img`（真机 /usr 原始 dump）。
> 结论先说：**ZV-E10 上不存在"名字→值"的参数表**，QEMU 无法靠读表来回答参数
> 查询；`PRM_*`/`VAL_*` 是日志与 OSD 状态词汇，不是平台配置项。

---

## 一、规模与分布

全 `/usr` 扫描结果：

| 词汇 | 唯一数量 | 主要位置 |
|---|---:|---|
| `PRM_*` | **810** | `libObj.so`（238 数据块）、`libBizFw.so`（21）、`libBizFw2.so`（5） |
| `VAL_*` | **204** | `libObj.so`、`area_check_data.dat`（66） |

`PRM_` 按子系统前缀分组：

```
PRM_APP (156)  PRM_IN (68)   PRM_SET (66)   PRM_DISP (52)  PRM_INPUT (48)
PRM_SRVC (42)  PRM_DELIVER (23)  PRM_OUT (23)  PRM_SYS (20)  PRM_OPEN (18)
PRM_PATH (16)  PRM_EFT (15)  PRM_SDIOUT (15)  PRM_GET (14)  PRM_HDMI (14)
PRM_YC (14)    PRM_OSD (12)  PRM_EVF (11) …
```

## 二、关键否定结论：没有名字→值表

`libObj.so` 里 `PRM_` 出现 599 次，其中 **596 次嵌在日志格式串里**，
只有 3 次是独立字符串：

```
[ObjEffect]L_PRM_3D_ZOOM_MAIN_IMAGE_INFO_LEFT Data is NOT Found)
[ObjEffect]ZOOM_CMP[3D]:(END)
```

⇒ `PRM_*` 在编译期是枚举/宏，运行时是**数字 ID**，字符串只在调试日志里出现。
**没有可提取的 (name, id, default_value) 表** —— 这一点必须告诉 QEMU 侧，
否则他们会一直找一张不存在的表。

## 三、`area_check_data.dat` 完整解出（OSD 覆盖层，不是菜单）

```
magic      "UXAC"
version    2
entryCount 10000        recordCount 27508
strBytes   353887       reserved 0
dataStart  0x18 + 10000*16 = 0x27118   ✓ 与文档格式一致，实测吻合
内容       10000 个 STRID_* 字符串 ID + 66 个 VAL_* 状态名
```

那 66 个 `VAL_*` 是 **OSD 显示状态**词汇，例如：

```
VAL_SHOOTING  VAL_REC  VAL_IMAGE_PLAYBACK  VAL_ADJUSTMENT
VAL_SHOOTING_INTERVAL  VAL_SHOOTING_SILENT_SHOOTING
VAL_SHOOTING_SHUTTERTYPE_GUIDE_{ELECTRONIC,MECHANICAL}_SHUTTER
VAL_IMAGE_PLAYBACK_SPEED_NON_ICON_GUIDE ...
```

⇒ 这套数据是"**在什么拍摄状态下显示哪块 OSD 区域**"，属于覆盖层逻辑，
**与"菜单页由谁创建"是两条独立链路**。不要把它当成菜单参数表。

## 四、资源侧现状（菜单真正依赖的东西，已在镜像里）

| 资源 | 大小 | 说明 |
|---|---:|---|
| `/share/app/global.xdb` | **12,252 B** | ★ `viewUnified2.so` 内字面量 `global.xdb` 指向它，资源索引 |
| `/share/app/lang.uxb` | 2,896 B | 语言库 |
| `/share/app/style.uxb` | 540 B | 样式库 |
| `/share/app/viewGlobalMenu.uxc` | 3,316 B | 全局菜单页 |
| `/share/app/*.uxc` | **299 个** | 全部菜单页/图像资源 |

`.uxc` 容器头实测：

```
viewGlobalMenu.uxc : 75 78 63 07 00 00 00 00   ("uxc" + 0x07 + ver 0)
color_cmn.uxc      : 75 78 63 07 00 00 00 00
image_cmn_43.uxc   : 75 78 63 64 07 00 00 00   (变体类型字节不同)
```

⇒ **QEMU 不缺资源**：这些文件都在它已经挂载的 `/usr` 里。
若菜单不出，问题在**加载/创建时序**，不在资源缺失或参数值。

## 五、对 QEMU 侧的修正建议

| 原计划 | 实际应做 |
|---|---|
| 提取 PRM/VAL 参数表给 QEMU 回答 | ❌ 表不存在。改为**运行时追踪**：在 QEMU 里 hook 参数查询点，看 App 实际问了什么数字 ID |
| 认为卡住是"平台参数缺失" | ⚠️ 更可能是 **UIPC 服务/网络栈**没起来（与你们实测"停在网络栈"一致） |
| 等 C2 结果再动 | ✅ **B4（应用日志环）优先级更高**：384 KB 的 App 自述日志能直接指出停在哪 |

**我的建议顺序**：① 你们抓 B4/B2（真机 App 日志 + im.elf maps）→ ② 我继续 C4
（`.uxc` 容器 + `global.xdb` 引用链，纯离线）→ ③ 再回头看 PRM。

## 六、附：完整词汇清单

810 个 `PRM_*` + 204 个 `VAL_*` 的完整列表见
`tools/param_vocabulary.txt`（可作 QEMU 日志解析的对照词表用）。
