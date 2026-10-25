# ZV-E10 固件里的「好玩的隐藏项目」

> 结合 `cokeenet/zv-e10-re` + 本地固件实测核实（`ref_e10` / `nflasha15.img`）

---

## 一、★★★ Sony 官方开发者模式（最安全，也是最意外的发现）

### `change_mode.sh` —— 官方启动模式切换器（设备上**确实存在**，4,022 B）

```
参数     dmode      效果
0        0          内核启动后就停住          ← 分阶段调试
1        1          全局基础设施后停住
2        2          LIRO RTOS 启动后停住
3        3          正常（默认）
4/5/6    1n/2n/3n   nounified driver 变体
7        3L         LeakTracer
8        3w         sslibc Wfree 检查
9        3k         KMC ICE
a        3g         目标 GDB
b        3s         gdbserver
d        3d         DUMA 内存调试器
e        3e         Electric Fence
f        3p         jemalloc profiler
10       3p         mcheckb
p <路径>  3p         ★★★ 任意自定义 .so
```

**⚠️ 实测：设备上只有 `LeakTracer.so`(31 KB) 和 `libjemalloc_tsh.so` 存在；
`libefence.so` / `libmcheckb.so` / `libduma.so` **不存在** ⇒ 那几个模式会启动失败（但不砖）。**

### 配套的隐藏机制

| 路径 | 作用 |
|---|---|
| **`/setting/mode/dmode`** | 启动模式（单字符标志，`init` 读）|
| **`/setting/mode/preload`** | LD_PRELOAD 目标（`bootin.elf` 读）|
| **`/setting/env.txt`** | 注入环境变量给所有服务 |
| **`/setting/theme/`** | 持久化主题资源 |
| **`/setting/updater/mode*`** | updater 模式文件 |
| **`/setting/sen/smode`** | 强制 Senser 模式 |

### ★★★★★ 与当初变砖路径的关键差异

| | `/etc/ld.so.preload`（当初变砖）| **`/setting/mode/preload`（官方）** |
|---|---|---|
| 位置 | `/etc`（与 /tmp /var 共用 **12 MB**）| `/setting`（**独立 vfat 分区**）|
| 谁读 | **动态加载器读所有进程** | **只有 `bootin.elf`** |
| 失效影响 | **init 都起不来** | **只影响 im.elf** |
| 恢复 | 要改 flash | **`change_mode.sh 3` 即可** |

**⇒ Sony 的官方机制**天然安全得多**。**

---

## 二、★★★★★ Open Gate 3:2 —— 用满传感器拍视频

**传感器 6000×4000 (3:2)，但原厂视频只录 16:9（3840×2160）。
Open Gate = **3240×2160 的 3:2 全传感器读出**。**

**已解出的 4 个内存补丁点：**

| 位置 | 改动 |
|---|---|
| `libmpr.so @0x5422e6` | 白名单 bitmask `0x2b → 0x2f`（允许 Aspect 2）|
| `libObj.so @0x978bea` | `ConvertRecFormat`: `movs r2,#1` → `#2` |
| `libObj.so @0x2d229c` | `InfraMovieEncoderSeqSetAspect` 载荷 → Aspect 2 |
| `libObj.so @0xeb0f26` | Live View 分类表 entry 4 → Case 1 (3240×2160) |

**★ 全部是**内存补丁**（`mprotect` + cache flush），零 flash 修改。**

---

## 三、★★★ UVC 摄像头解锁（最容易看到效果）

**原厂：1280×720 @ 25/30 fps，~30 Mbps（只用 **9%** USB 带宽）**

**解锁后：**
```
1080p      @ 30 / 60 fps
3.2K 3:2   (3240×2160) @ 24 / 30 fps   ← Open Gate 直接走 USB
2.2K 3:2   (2160×1440) @ 30 fps
码率        300–320 Mbps（88–93% USB 2.0 带宽）
```

**★ 同样是纯内存补丁。**

---

## 四、还没实现但已设计好的好玩想法

### ① 动态切换画幅（用 Sony 自己的菜单）

**菜单项 `cmnViewSettingNodeRootStlImageAspectRatio` 已经存在，可绑到 Fn / C1。**

```
BackupManager 属性 ID：0x1070012（普通）/ 0x10703b2（MR 模式）
返回值：0 = 3:2   1 = 16:9   2 = 4:3   3 = 1:1
```

**⇒ Hook `ConvertRecFormat`，按这个设置动态决定 Open Gate 还是 16:9。**

### ② 物理热键切换

**C1 / Trash 键切换 Open Gate ↔ 16:9，带 OSD 提示。**

### ③ 自定义画框线 + 变形宽银幕反挤压预览

**在 Live View 上画遮幅/参考线，实时水平反挤压（1.33x/1.5x/1.8x/2.0x）。**

### ④ 码率解锁

**`libmpr.so` 的 `[pstRecMovieProfile] nBitrate` 表 → 提到 150/200 Mbps。**

### ⑤ 原生 4:3 全传感器视频

**5333×4000 裁剪 → 2880×2160 或 3240×2430（2.0x 变形镜的黄金比例）。**

### ⑥ Magic Lantern 式「侧挂 UI」

**不动 `im.elf`（它负责对焦/测光/防抖/镜头通信），另起一个轻量 UI 进程：**
```
· 直接开 /dev/fb0（或硬件叠加层）
· 读 /dev/input/event* 拿按键
· 长按某键 1.5 秒切出自定义菜单
· 功能：画幅选择 / 反挤压 / 码率滑块 / 假色 / 波形 / 矢量示波器 / 快门角显示
```

### ⑦ 完全自定义相机 OS（极端）

**直接链接 `libObj.so` / `libmpr.so` / `av-cam.bin`（BIONZ X RTOS），绕开 Sony UI。**

---

## 五、安全建议（照这个顺序玩）

```
① 先备份（✅ 已完成 18/18 分区）
② 用 change_mode.sh 3L / 3p 试官方调试模式（安全）
③ 用 UVC 解锁（纯内存，最容易看到效果）
④ Open Gate（纯内存，但需要 hook 多个函数）
⑤ 侧挂 UI（工作量大）
```

**⚠️ 无论哪一步，都遵循 `docs/SAFETY_RULES.md`：**
- **不写 `/etc`**
- **不 `remount,rw /usr`**
- **新 .so 必须先经用户同意**
- **用 `change_mode.sh p <path>` 而非 `/etc/ld.so.preload`**

**★★ 唯一残留风险：若 preload 的 .so 在构造函数里 hang，`bootin.elf` 会被阻塞 ⇒
可能进不了 USB 维护模式 ⇒ 需要断电。**
**⇒ 缓解：constructor 必须 fork 后立即返回，绝不阻塞。**
