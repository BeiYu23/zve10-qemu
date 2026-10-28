# M2 hybrid 的已验证 / 未验证清单（对移植线提问的答复）

## 一、有实际执行测试吗？**没有，零次**

```
无运行、无 QEMU、无 dlopen 测试、无动态链接测试、无 ld.so 解析测试
唯一验证是文件级：e2fsck、md5、属主、ext2 结构、分区容量
```

**⇒ 只能说"文件就位且 hash 正确"，不能声称"可加载"。**

## 二、DT_NEEDED / 符号版本 / 重定位闭包：**没有闭包**

**已做（列举级）：**
- `im.elf` 的 15 个 DT_NEEDED（仅列出）
- `libIMDB` 的 7 个 DT_NEEDED（仅列出）
- 16 个 DIFFER 库的导出符号对比
- `lib/*.so` 123 项逐字节比对

**未做：**
- 全库 DT_NEEDED 传递闭包（每个 NEEDED 是否在 `/usr/lib` 或 `/root/lib` 有定义者）
- 符号版本 VERDEF/VERNEED 匹配
- 重定位闭包
- rootfs 侧（`/root/lib` glibc/ld.so）对全部库的满足性
- libIMDB 185 个 dlopen 注册名 → 是否每个都能找到文件

## 三、★ 逐文件 hash 复审：已补做全部 478 项

```
文件: /mnt/build/fw/port_m2/out/KEEP_E10_HASH_AUDIT.csv   51,853 B
列  : path, status, e10_md5, m2_md5
```

| status | 数量 |
|---|---:|
| IDENTICAL | 409 |
| **DIFFER** | **77** |
| SYMLINK_OR_MISSING | 9 |

**77 个 DIFFER 的分布（构建器保留 E10 版但内容不同）：**

| 目录 | 数量 | 是否审计 |
|---|---:|---|
| `kmod/` | 32 | ❌ 未审计（含 grm_gles/grm_ma/bcmdhd/dmm/cfg80211/input 等）|
| `sabin/` | 23 | ❌ 未审计（DSP 固件）|
| `lib/` | 16 | ✅ 做过导出符号对比 |
| **`bin/`** | **5** | ❌ **未审计 —— 含 `im.elf` / `bootin.elf` / `bk.elf` / `testcmd.elf` / `wpa_supplicant`** |
| `scenario/` | 1 | ❌ |

**⇒ 77 个里只有 16 个做过符号级审计，其余 61 个未审计。**

## 四、"四件套"的入口依赖证据（可复现）

| # | 证据 | 命令 |
|---|---|---|
| 1 | `im.elf` 的 DT_NEEDED **含 libIMDB.so** | `readelf -d ref_m2/bin/im.elf \| grep libIMDB` |
| 2 | **E10 libIMDB 注册 `appFw.so`；M2 注册 `appFwBoot.so`** | `strings libIMDB.so \| grep -E '^appFw.*\.so$'` |
| 3 | **libIMDB 的 NEEDED 里 libObj 出现 0 次** ⇒ libObj 必为运行时 dlopen | `readelf -d libIMDB.so \| grep libObj` |
| 4 | M2 libIMDB 注册 185 个服务 | `strings libIMDB.so \| grep -cE '^lib.*\.so$'` |

**E10 的 libIMDB**：`ref_e10/usr/lib/libIMDB.so` 存在；另存 `/mnt/build/e10x/libIMDB.so`。

## 五、★ 必须补报：实际是**六件**，且两版构建决策不一致

**`bin/im.elf` / `bin/bootin.elf` 两版内容不同：**

```
im.elf      E10=f1c746e3   F=a4824008   M2=d2f47a82   E10↔M2 差 13,725 B (59%)
bootin.elf  E10=d8bfb4f7   F=3545261c   M2=3545261c   E10↔M2 差  2,481 B (16%)
```

**两个构建的实际产物不同：**

| 构建 | im.elf | bootin.elf |
|---|---|---|
| **已交付 v4/v5（ZV-1F 供体）** | **a4824008 = ZV-1F** | **3545261c = ZV-1F** |
| **当前 M2 构建** | **f1c746e3 = ZV-E10** | **d8bfb4f7 = ZV-E10** |
| `port_v2/decisions.json` 声明 | KEEP_E10 | KEEP_E10 |

**⇒ v4/v5 镜像内容与 `port_v2/decisions.json` 不一致
⇒ 说明 v5 是更早构建逻辑的产物，决策表后来重跑生成
⇒ 这两个文件该用谁的、要不要换 —— 未审计，是"四件套"外的第五、第六个变量**

**正面结果：两版 `bootin` 的 preload 白名单完全相同**
（`/setting/mode/preload` + 四个 `/usr/tool/*.so`）⇒ preload 机制跨版一致。

## 六、已证实 / 待证

**已证实（文件级）：**
```
· 镜像结构完整、e2fsck 干净、属主 57285:1000、容得下（227.60/300 MiB）
· 4 个 UI 核心库 = M2 原版 md5
· 资源 224 项含 _vertical 35 / _g 34
· GPU 两个 .ko 仅 build-id 20 字节不同（严格边界，见 GPU_EVIDENCE_AND_CORRECTIONS.md）
· 两版 bootin preload 白名单一致
```

**待证（全部）：**
```
· 任何执行/加载行为
· DT_NEEDED 闭包 / 符号版本 / 重定位闭包 / rootfs 满足性 / 185 个 dlopen 注册名
· 77 个 DIFFER 里未审计的 61 个
· im.elf 与 bootin.elf 该用哪版
· 新菜单运行时对 /setting 等分区的影响
```

## 七、可读结果路径

```
/mnt/build/fw/port_m2/out/nflasha15_hybrid.ext2      镜像（300 MiB）
/mnt/build/fw/port_m2/out/MANIFEST.json             923 条 size+md5
/mnt/build/fw/port_m2/out/LIBLIST.json              196 条库清单
/mnt/build/fw/port_m2/out/KEEP_E10_HASH_AUDIT.csv   478 条 hash 复审（本轮新增）
/mnt/build/fw/port_m2/out/decisions.json            4330 条决策
```
