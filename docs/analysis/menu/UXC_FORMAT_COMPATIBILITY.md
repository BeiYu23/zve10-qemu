# ZV-1F / ZV-E10 资源格式兼容性（离线验证，不碰相机）

> 目的：决定"新菜单"走**整套替换（策略 A）** 还是 **逐步试探（策略 C）**。

## 一、★★★ 结论：格式**完全兼容**

### 1.1 `global.xdb` 头部完全相同

```
ZV-E10  12252 B   75786107 00000000 09000000 efe01d00
ZV-1F   13320 B   75786107 00000000 09000000 efe01d00
                  ↑ 前 16 字节逐字节相同（magic "uxa\x07", version 0）
```

### 1.2 `.uxc` 头部完全相同

| 文件 | ZV-E10 | ZV-1F | 头 16 字节 |
|---|---:|---:|---|
| `LayoutMaster_Menu.uxc` | 3,512 | 3,636 | **完全相同** |
| `master_camera.uxc` | 101,804 | 101,868 | **完全相同** |
| `color_cmn.uxc` | 312 | 388 | 前 12 字节相同 |
| `style_cmn.uxc` | 1,888 | 1,940 | 前 12 字节相同 |

magic = `"uxc\x07"`，version = 0，两边一致。

### 1.3 ⇒ 最关键的发现：**ZV-E10 也用同一套 uxc 布局 DSL**

```
ZV-E10 share/app:  LayoutMaster_Menu.uxc, master_browser/camera/network/other.uxc
ZV-1F  share/app:  同上 + LayoutMaster_Other.uxc, master_normal.uxc
```

**⇒ 两版不是"不同架构"，而是【同一布局系统 + 不同内容】。**

### 1.4 同名文件 76 个（含全部框架资源）

```
color_cmn.uxc  lang_cmn.uxc  style_cmn.uxc
LayoutMaster_Menu.uxc
master_browser/camera/network/other.uxc
string_*.uxc × 35（全部语言）
```

| | ZV-E10 | ZV-1F |
|---|---:|---:|
| `.uxc` 总数 | 299 | 152 |
| 同名 | **76** | |

## 二、⇒ 策略建议：**C（逐步试探）可行**

**因为格式兼容，最小改动就能观察效果：**

```
第1步  color_cmn.uxc     312 → 388 B    （配色，最安全）
第2步  style_cmn.uxc   1,888 → 1,940 B
第3步  LayoutMaster_Menu.uxc  3,512 → 3,636 B
第4步  再考虑 master_*.uxc / libObj / appFw
```

**⇒ 每一步都能观察"屏幕有没有变化"，且崩了只影响当次。**

**⚠️ 但注意：`.uxc` 只是数据，解释它的是引擎。**
ZV-E10 的 `libObj.so`(21 MB) 会不会接受 ZV-1F 布局里引用的**动作/命令**，无法离线确定
⇒ **只能实测**。格式兼容 ≠ 语义兼容。

## 三、⚠️ 符号链接农场的**两个致命细节**

### 3.1 `/usr/lib` 有 **58 个相对符号链接、0 个绝对链接**

```
libSoundTouch.so  → libSoundTouch.so.1.0.0     （库内，安全）
libanl.so         → ../../lib/libanl.so.1      ← ★ 跨到 rootfs /lib，共 8 个
libblkid.so       → ../..//lib/libblkid.so.1.1.0
```

### 3.2 ⚠️ 循环引用陷阱（会让方案直接跑不起来）

**若按"`/tmp/zovl/<name>` → `/usr/lib/<name>`"建链接：**

```
mount --bind /tmp/zovl /usr/lib
   ⇒ /usr/lib/libObj.so 现在解析为 /tmp/zovl/libObj.so
   ⇒ 而 /tmp/zovl/libObj.so → /usr/lib/libObj.so
   ⇒ ★ 无限循环
```

**✅ 正确顺序（先把原厂暴露到另一个路径）：**

```sh
# ① 暴露原厂（必须在 bind 之前）
mkdir -p /tmp/stock            # ★ 深度必须与 /usr/lib 相同（2 层）
mount --bind /usr/lib /tmp/stock

# ② tmpfs 覆盖层
mount -t tmpfs tmpfs /tmp/zovl

# ③ 每个文件链到【原厂暴露路径】，不是 /usr/lib
for f in /tmp/stock/*; do
    ln -s "/tmp/stock/$(basename $f)" "/tmp/zovl/$(basename $f)"
done

# ④ 我们的替换/新增文件（实体放 /log，零 RAM）
ln -sf /log/newmenu/appFw.so /tmp/zovl/appFw.so
ln -sf /log/newmenu/libObj.so /tmp/zovl/libObj.so

# ⑤ 最后才 bind
mount --bind /tmp/zovl /usr/lib
```

### 3.3 ⚠️ 相对链接的**深度**必须保持

`/usr/lib/libanl.so → ../../lib/libanl.so.1` 的正确解析依赖 `/usr/lib` 是 **2 层路径**。

**⇒ 暂存目录也必须是 2 层（`/tmp/stock`、`/tmp/zovl` ✓）。**
**⇒ 若用 `/mnt/stock_lib`（同样 2 层 ✓）可以；用 `/tmp/a/b/stock`（3 层 ✗）会解析错。**

**⇒ 判据：暂存路径的目录层数必须等于 `/usr/lib` 的层数。**

### 3.4 另一条：库内相对链接（50 个）会自动正确

`libSoundTouch.so → libSoundTouch.so.1.0.0` 这类，因为农场链到的是 `/tmp/stock/` 下的
**原文件**，其相对目标在 `/tmp/stock/` 内同样存在 ⇒ 解析正确。

## 四、验证清单（上机前）

- [ ] `/tmp/stock` 层数 = `/usr/lib` 层数（2 层）
- [ ] `/tmp/zovl` 内链接全部指向 `/tmp/stock/...`（**绝不含 `/usr/lib/...`**）
- [ ] `ls /tmp/zovl/libanl.so` 能解析到真实文件
- [ ] bind 后 `ls /usr/lib` 内容与原厂一致 + 我们的替换生效
- [ ] **先测 `ls` 和 `cat`，再启动 App**
