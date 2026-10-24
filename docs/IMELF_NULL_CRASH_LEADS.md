# `im.elf` 早期 NULL 崩溃：排除项与新线索（2026-10-24）

> 背景：v4 镜像（ZV-1F 的 im.elf + libObj）在 QEMU 里启动到 `EXECVE /usr/bin/im.elf` 后，
> `address=0x0 trap=0x0e` 崩溃；栈上指针全在 `0x04xxxxxx`（库区），im.elf 本体（ET_EXEC @0x8000）不在栈上。

---

## 一、已排除：`/etc`（任务 A 的结论）

**新 UI 的二进制完全不含 `/etc` 路径引用**。扫 `libObj.so` / `appFw.so` / `appFwBoot.so` /
`im.elf` / `libInfraInput.so`，只得到 4 条 `/proc` 路径、0 条 `/etc` 路径。

另外 **ZV-1F 的 `/etc` 不在固件包里**（AV 包只含 nflasha3/5/7/15，`nflasha10` 不发布），
所以"ZV-1F 的 /etc 对比"在现有材料下**做不到，也不需要**。

⇒ `IMOPEN FAIL //etc/ld.so.cache` 属正常（ZV-E10 原厂启动同样 FAIL 多次）。

---

## 二、已排除：`/system` 依赖

新 UI 只引用一条真实 `/system` 路径：

| 路径 | 引用者 | 本机状态 |
|---|---|---|
| `/system/av-cam.bin` | **im.elf** | 两侧都有（ZV-E10 17,289,388 B / ZV-1F 15,706,444 B），硬件绑定 |

其余 `/system/musashi/*.cpp` 是 `libObj.so` 里的 **`__FILE__` 调试串**（断言日志用），不是真实文件。

⇒ 且 **v3（ZV-E10 的 im.elf + ZV-E10 的 av-cam.bin）同样崩**，所以 av-cam.bin 不像主因。

---

## 三、已排除：缺库

### 3.1 im.elf 的 6 个 dlopen 目标全部存在

```
libObj.so(3,929,768) libIMDB.so(40,596) libbackup.so(9,384)
libosal_utm.so(4,900) libosal_uipc.so(26,692) libSTMcmdexecute.so(→ /lib, 在 rootfs)
```

### 3.2 ZV-1F `libIMDB.so` 引用 194 个 `.so`，混合镜像缺 122 个 —— 但都是"正常缺失"

| 分类 | 数量 | 说明 |
|---|---:|---|
| **ZV-1F 自带却被我们误删** | **0** | ✅ 无 |
| 两边固件都不带（IMDB 预留项） | 116 | ZV-1F 自己也没有 ⇒ 正常 |
| 仅 ZV-E10 有（我们按 DROP_LEGACY/OLD_UI 删了） | 6 | `libInfraBGMMeta/CacheMgr/Highlight/MakeScenario`、`libmprctrl/libmprdummy` —— **ZV-1F 也不带**，且 ZV-1F 的 IMDB 同样列着它们 ⇒ 删掉符合 ZV-1F 行为 |

⇒ **缺库不是 NULL 的原因。**

---

## 四、★ 新线索（高概率）：`/proc/udm/upm_bootmode`

### 4.1 事实

| 项 | 内容 |
|---|---|
| 引用者 | **两版 `im.elf` 都读 `/proc/udm/upm_bootmode`** |
| 提供者 | **`upm.ko`**（符号 `upm_bootmode_read_proc`、`proc_upm_bootmode`） |
| QEMU 侧现状 | **`upm` 已被换成 stub** |

⇒ **stub 若不创建 `/proc/udm/upm_bootmode`，im.elf 读失败**。
这正是"早期、在库调用里解引用 NULL"的典型形态。

### 4.2 新 UI 依赖的全部 `/proc` 节点与提供者

| 节点 | 引用者 | 提供者 | stub 后是否受影响 |
|---|---|---|---|
| **`/proc/udm/upm_bootmode`** | **im.elf** | **upm.ko** | **★ 受影响（upm 被 stub）** |
| `/proc/liro/start` | upm.ko | liro.ko + upm.ko | 否（liro.ko 未 stub，仍提供） |
| `/proc/tmonitor` | appFw.so | liro.ko + usb_portMonitor.ko | 否（两者都未 stub） |
| `/proc/modules` | libObj.so | 内核 | 否 |
| `/proc/meminfo` | appFw.so | 内核 | 否 |

⇒ **唯一的风险点就是 `upm` 的 proc 节点。**

### 4.3 建议

让 upm stub **额外创建 `/proc/udm/` 目录 + `upm_bootmode` 文件**，
`read_proc` 返回一个合理值（先试 `"0\n"`；若行为不对再按需调整）。

老菜单线能过，是因为老 UI 对它失败更宽容；新 UI（ZV-1F 的 im.elf）可能直接解引用。

---

## 五、给 QEMU 侧的授权与建议

1. **可以改测试镜像**（注入 `/lib/libgrmlog.so` + 包壳 `/usr/bin/im.elf`）—— 那是测试件，
   只要求改完把 md5 列表同步回来，便于我在交付镜像里对齐。
2. 建议**先做最小实验**：只让 upm stub 加 `/proc/udm/upm_bootmode`，不动其它，看崩溃是否消失。
   这能一步定性"是不是它"。
3. 若仍崩，再用探针抓 maps 定位 `pc=0x05B4F3BE` 属于哪个库 —— 那时范围已经很小。
