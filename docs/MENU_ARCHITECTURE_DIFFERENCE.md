# 老菜单 vs 新菜单：架构级差异（实测对比结论）

> 素材全部来自本地 eMMC 镜像备份（`nflasha15.img` = `/usr`），已与真机交叉校验
> （`ref_e10` 与 `nflasha15.img` 的 `viewUnified2.so`、`global.xdb` SHA-256 完全一致）。

---

## 一、结论先说：这不是"换几个数据文件"，是**两套不同的 UI 架构**

| | **ZV-E10（老菜单）** | **ZV-1F（新菜单）** |
|---|---|---|
| 应用框架 | `im.elf`(23 KB) + **单体 `libObj.so` 21 MB** | **`appFw.so` 53 MB** + `libObj.so` 3.9 MB |
| UI 引擎 | `viewUnified2..8.so`（7 个模块）| 无独立 viewUnified |
| **菜单屏定义** | **214 个 `view*.uxc` 数据文件** | **5 个 `master_*.uxc`** |
| 资源索引 | `global.xdb` 12,252 B（313 条）| `global.xdb` 13,320 B（381 条）|
| `share/app` 文件数 | **308** | **164** |
| 菜单逻辑位置 | **数据驱动**（`view*.uxc`）| **编译进 `appFw.so`** |

---

## 二、关键实测证据

### ① 文件名分类

**ZV-E10（214 个 view*.uxc）：**
```
viewSettingMenu.uxc    19,476 B   ← 设置菜单
viewGlobalMenu.uxc      3,316 B
viewFnMenu.uxc          7,896 B
viewBaseMenu.uxc           84 B
LayoutMaster_Menu.uxc   3,512 B
... 共 214 个
```

**ZV-1F（0 个 view*.uxc，只有 5 个 master）：**
```
LayoutMaster_Menu.uxc        3,636 B
LayoutMaster_Other.uxc       2,592 B   ← ZV-E10 没有这个
master_browser.uxc
master_camera.uxc
master_network.uxc
master_normal.uxc
master_other.uxc
```

### ② ★ `appFw.so` 里**零** `view*.uxc` 引用

```
ZV-1F appFw.so: view*.uxc 引用数 = 0
ZV-1F appFw.so: global.xdb  引用数 = 0
```

**⇒ 新菜单的屏幕定义**不在数据文件里，而是编译进 `appFw.so`。**

### ③ 相同的部分

```
style.uxb          E10: 540 B   ZV-1F: 540 B   完全一致
LayoutMaster_Menu.uxc   3,512 → 3,636        不同
color_cmn.uxc            312 → 388           不同
```

**⇒ 基础样式共享，但布局母版和配色都改了。**

---

## 三、⇒ 这对「在 ZV-E10 上跑新菜单」意味着什么

**必须整体替换：**

```
appFw.so         53 MB      ← 新菜单的**逻辑**
libObj.so        3.9 MB     ← ZV-1F 的精简版
libInfraSKD.so   7.2 MB
share/app/*      70 MB      ← 5 个 master + 152 个 uxc + 图像
────────────────────────────
合计 ~132 MB
```

**且必须满足三个条件，缺一不可：**

| 条件 | 现状 |
|---|---|
| **启动时可见**（不能靠 bind）| **做不到** —— `/usr` 只读，无启动钩子 |
| **ABI 兼容** | **未知** —— appFw 依赖 ZV-1F 的其它库和硬件抽象 |
| **可回滚** | 需写 flash ⇒ **不可回滚** |

**⇒ 在真机上试新菜单：不可行。**

---

## 四、为什么 bind 方案也不行（已实测）

**`im.elf`（PID 157）在启动时把 19 个资源文件全部 **mmap** 进内存：**
```
049c8000-049cb000 r--p ... /usr/share/app/global.xdb    ← r--p 私有只读映射
...
```
**且 `im.elf` 的 PPID=1（双重 fork 脱离），**没有任何进程会重启它**。
⇒ kill 掉 = 永久黑屏。**

**⇒ bind 改了运行中的进程看不到；重启又没有守护者；reboot 则 bind 消失。
三条路全堵死。**

---

## 五、唯一现实的路径：QEMU

**真机（安全）：提供素材**
```
· nflasha15.img 就是完整的 /usr，已备份、已校验
· 需要任何文件：mount -o loop,ro nflasha15.img 即可
· 不需要从相机拉
```

**QEMU（试错）**
```
· 直接指向 zv1f_ui 那套文件
· 崩了重启，零风险
· 可以逐步替换：先 global.xdb，再 master_*，最后 appFw.so
```

**★ 素材已经有了：`_archive/dirs/zve10_portable/zv1f_ui/`（132 MB，与新分析完全对应）**

---

## 六、QEMU 侧可直接用的结论

1. **ZV-E10 的菜单是数据驱动的** ⇒ 在 QEMU 里可以先只换 `view*.uxc` / `global.xdb` 试试
2. **ZV-1F 的菜单是代码驱动的** ⇒ 换 `appFw.so` 才能真正切到新菜单
3. **`style.uxb` 两边相同** ⇒ 基础样式兼容，这是个好消息
4. **`libObj.so` 从 21 MB 缩到 3.9 MB** ⇒ 功能迁到了 appFw，说明 ZV-E10 的 libObj
   里可能有新菜单**不需要**的老代码，替换时要一并考虑
