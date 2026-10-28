# M2（ZV-1M2 新菜单）与 E10（ZV-E10 老菜单）的**架构无关**资源对照

基准：M2 = `/mnt/build/zv_m2probe/root`（从 `nflasha15_v9.ext2` 解包，858 文件 / 230 MB）；
E10 = `/mnt/build/ref-chocotan-zve10/work/usr`。

## 一、目录结构：几乎完全一致（可移植性强）

```
M2 : bin codec kmod lib local lost+found sabin sbin scenario share sbin tool wifi-scripts
E10: bin codec kmod lib local           sabin sbin scenario share      tool wifi-scripts wlan
⇒ 差异：M2 多 lost+found；E10 多 wlan/
```

## 二、菜单资源目录 `share/app`（核心）

```
                    M2        E10
文件数              232       308
总大小             82 MB     81 MB
.uxc 资源文件      220       299
```
* `.uxc` = 单个菜单资源（字符串/图片/布局）；
* `.uxb` = 打包/编译后的资源包（`style.uxb` / `lang.uxb` / `image.uxb`）；
* **两代共有 85 个同名资源**，其中关键的：
  `LayoutMaster_Menu.uxc` / `lang.uxb` / `lang_cmn.uxc` / `style.uxb` / `style_cmn.uxc` / `fontlist.dat`
  ⇒ **菜单框架（布局主表/语言包/样式表）是同一套格式**，可跨代移植。

### M2 新增（新菜单特有）
```
LayoutMaster_Other.uxc          ← 新菜单的"其它"布局主表
color.uxb / image.uxb           ← 新菜单的调色板/图片包
master_normal.uxc               ← 主菜单布局
image_00_34_ASTRA.uxc … (多张 ASTRA 图标，34/43 两套比例)
area_check_data_vertical.dat    ← 竖屏区域数据
string_<语言>_func_j.uxc        ← 新菜单"功能键"那套字符串
（大量 string_*_horizontal / _vertical，用于横竖屏切换）
```

### E10 独有（老菜单特有，M2 已弃用）
```
IRShooting.uxc
image_DSC_Entry_Only_43.uxc / image_DSC_Only_43.uxc / image_DSLR_Only_43.uxc
image_EVF.uxc / image_Fn_Menu_43.uxc / image_Rich_Help_Guide_43.uxc / image_cmn_43.uxc
```

## 三、语言资源：M2 自带简体中文（可直接切）

```
string_simplifiedchinese.uxc
string_simplifiedchinese_f.uxc
string_simplifiedchinese_func_j.uxc
string_simplifiedchinese_g.uxc
string_simplifiedchinese_horizontal.uxc
string_simplifiedchinese_vertical.uxc
```
⇒ 新菜单**确实内建中文**（含横屏/竖屏、功能键、f/g 变体），
  之前"相机菜单是日文"只是默认语言选择问题，不是缺资源。

## 四、可换镜头（E-mount）列表：**在字符串资源里**，不是独立数据表

```
`share/data/` 只有 kikilog_pub.pem / pub.key ⇒ 没有镜头表
grep 'LensModel|镜身|レンズ' share/app/*  ⇒ 命中 string_japanese*.uxc
⇒ 镜头名/菜单文案都在 string_*.uxc（每种语言一份）
⇒ 镜头型号支持列表（若有）应在 libInfraLensCommunicator.so 或 string 资源内
```

## 五、音效资源（与架构无关，可直接复用）

```
share/avsystemsound/: Dslr_Emnt_T_Menu_Open.bin / Dslr_Emnt_T_Select.bin /
                     Dslr_Emnt_Shutter_Ka.bin / SysSound.bin …（菜单开关、选中、快门音）
⇒ 新菜单的交互音效是独立 bin 文件，可直接对比两代差异
```

## 六、对"移植"的实际意义

1. **菜单框架资源（LayoutMaster/lang/style + fontlist.dat）两代同名同格式** ⇒ 可对照改写；
2. **M2 新增的资源是"增量"**（ASTRA 图标、color/image 包、func_j 字符串、竖屏数据），
   不是替换旧格式 ⇒ **移植时要补的是文件，不是改解析器**；
3. **中文字体/字符串齐全** ⇒ 语言问题可解；
4. **镜头名在字符串资源里** ⇒ 镜头列表差异可在 .uxc 层直接对比，不必逆二进制协议。
