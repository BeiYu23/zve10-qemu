# "只替换 appFw 就能开机" —— 可行性与适用边界

## 一、★ 实际的新菜单启动链（已解出）

```
libIMDB（启动期 DT_NEEDED）
  → dlopen "appFwBoot.so"（E10 的 libIMDB 注册名是 "appFw.so"）
  → dlsym  "Application_System_Init"     ← ★ libIMDB 找的是这个
        appFwBoot.so 导出（284 B）
        → 内部 Main()（_Z4MainPv, 120 B）
              → dlopen "appFw.so"
              → dlsym  "Application_Start"   ← ★ appFw.so 唯一导出
                    ZV-1M2 appFw.so：唯一 FUNC 导出 = Application_Start
```

**⇒ `appFw.so` 只导出 1 个符号是设计如此的 —— 它是被 `appFwBoot` 调用的，不是被 libIMDB 直接调的。**

### 1.1 两代架构对比

| | ZV-E10（老菜单） | ZV-1F / ZV-1M2（新菜单） |
|---|---|---|
| `Application_System_Init` 导出者 | **`libObj.so`（21 MB 单体）** | **`appFwBoot.so`（8 KB）** |
| `Application_Start` 导出者 | 无 | **`appFw.so`** |
| libIMDB 注册名 | **`appFw.so`** | **`appFwBoot.so`** |
| `libObj.so` 角色 | 应用框架 | 图形引擎（3.9 MB） |

**⚠️ 注意：E10 的 libIMDB 注册 `appFw.so`，但 E10 的 /usr/lib 里【没有这个文件】**
（加载失败被静默忽略，与 `libgrm.so`/`libgpe.so` 同类）。
E10 的老菜单实际由 `libObj` 的 `Application_System_Init` 驱动。

**⇒ 这就是"往 E10 放一个 appFw.so 会被 libIMDB 加载"的机制基础。
但放进去的是 ZV-1M2 的 appFw.so ⇒ 它只导出 `Application_Start`，
libIMDB 要找的 `Application_System_Init` 它没有 ⇒ dlsym 失败。**

## 二、★ 符号闭包实测：能否只换 appFw

**ZV-1M2 `appFw.so`：543 个 UND 符号。**

| 基座 | 可满足 | 缺失 | 结论 |
|---|---:|---:|---|
| **ZV-E10 老菜单**（142 库） | 376 | **167** | **不可能** |
| **ZV-1F 新菜单**（135 库） | 533 | **10**（含 2 个 CRT 弱符号） | **接近可行** |

### 2.1 E10 缺失的 167 个（全部是新 UI 框架）
```
ux::gfxeng    TextSprite / GraphicsEngine / ImageElementBase / MultiLineTextSprite
              Node / Sprite（约 15 个）
ux::wgtsys    WidgetSystem / WidgetBase / Widget / GestureAnalyzer（约 13 个）
ux::resmgr    ResourceManager（6 个）
ux::core      GraphicsImage::copy / clear（2 个）
libra          Dictionary / String / Variant / Formatter / ResIDVariant（约 12 个）
sidb           Init / Select / Refine / Equivalence 等（约 20 个）
mpr            约 50 个（Init/Load/RecStart/Capture/Copy/Delete/Rotate…）
network::CertificateManager（8 个）
GRM_*          bitmapCreateFromPhys / bitmapRelease / fbrm*（4 个）
其他           InfraInput / InfraDial / GCM_ 等
```

**⇒ E10 的老菜单里根本没有这套框架 ⇒ 即使能加载，一调用就崩。**

### 2.2 ZV-1F 缺失的 10 个
```
_Jv_RegisterClasses          CRT 弱符号，可忽略
__gmon_start__               CRT 弱符号，可忽略
_ZN3mpr12AudioDecStop…       6 个 mpr::Audio*  ← 全部在 ZV-1M2 的 libmpr.so 里
_ZN3mpr12AudioEncStop…
_ZN3mpr12AudioReadWav…
_ZN3mpr13AudioDecStart…
_ZN3mpr13AudioEncStart…
_ZN3mpr13AudioWriteWav…
_ZN16infra_wrapper_bb6detail12adf_ext_type…      两个库都没有（仅 appFw 自身 UND）
_ZN21infra_wrapper_imaging6detail…set_user_matrix…
```

**⇒ 6 个在 ZV-1M2 的 `libmpr.so`；另 2 个无任何库提供（可能该代码路径不用，或需额外库）。**

## 三、⇒ 结论：社区说法的适用边界

**"只替换 appFw 就能开机" —— 只在【新菜单机型之间】成立，典型是 ZV-1F ← ZV-1M2。**

| 场景 | 是否可行 | 依据 |
|---|---|---|
| **ZV-1F 换 ZV-1M2 的 appFw** | ⚠️ **接近可行**（差 8 个真实符号，补 libmpr 后可能齐） | 533/543 满足 |
| **ZV-E10 换 ZV-1M2 的 appFw** | ❌ **不可能** | 167 个框架符号缺失 |
| ZV-E10 换整四件套 | ❓ 未验证 | 见 M2_HYBRID_UNVERIFIED_ITEMS.md |

## 四、⚠️ 两个必须注意的前提

**① 即使加载成功，也不等于功能可用**
```
appFw.so 的重定位表:
  R_ARM_JUMP_SLOT (函数, 懒绑定)  681
  R_ARM_GLOB_DAT  (数据, 立即解析) 30
⇒ 函数符号走 JUMP_SLOT ⇒ 默认懒绑定
⇒ 未解析的符号【不阻止加载】，只在首次调用时失败
⇒ "能开机" ≠ "菜单完整可用"
```

**② E10 的 libIMDB 注册名与 M2 不同**
```
E10 libIMDB → 注册 "appFw.so"（文件不存在）
M2  libIMDB → 注册 "appFwBoot.so"
⇒ 在 E10 上只放 appFw.so 会被加载，但 dlsym("Application_System_Init") 会失败
   （appFw.so 只导出 Application_Start）
⇒ 必须同时提供提供 Application_System_Init 的 appFwBoot.so，并让 libIMDB 注册它
```

## 五、附带发现

**ZV-1F 的 `appFwBoot.so` 比 ZV-1M2 的多导出一个 `Application_Boot_Select`**
（ZV-1M2 没有）。功能含义未解，可能是启动模式选择器。
