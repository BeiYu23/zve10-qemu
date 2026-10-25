# 新菜单上实体机：进度、产物、风险与步骤

> 决策变更：**放弃模拟器，直接上实体机**（用户：救砖有 fastboot 兜底）。
> 这实际上**简化**了问题 —— 实体机有真 GPU，所有 QEMU shim 都不需要了。

---

## 一、进度：做到哪一步了

### 1.1 已完成并验证（实体机可用）

| 产物 | 大小 | 说明 |
|---|---:|---|
| **`nflasha15_hybrid_v4.ext2`** | 314,572,800 | **`/usr` 混合分区**（ZV-1F 库 + ZV-E10 基础） |
| `nflasha7_hybrid.img` | 8,388,608 | ZV-E10 rootfs + 3 个空目录 |
| `zve10_hybrid_full_v4.img` | 1,983,905,792 | boot 区(4 MiB) + 用户区 整盘 |

**验证状态**：
- `/usr` 207.72 MiB 合并，85 MiB 剩余，`e2fsck` 干净
- 属主 **57285:1000**（与原厂一致）
- `appFw.so` = ZV-1F 版（53,189,440 B）
- `libObj.so` = ZV-1F 版（3,929,768 B）
- 0 个 `viewUnified*`（老菜单库已全部替换）
- **13 个未动分区 md5 与原厂逐一相同**
- 分区布局按表校验 16/16 正确（含 `boot area 4 MiB` 偏移修正）

### 1.2 移植决策依据（三条硬事实）

1. **ZV-1F 与 ZV-E10 同 SoC：CXD90045 / ASTRA（ARM32）** ⇒ 库可互换
2. **GPE 层语义等价**：`GRM_gpermFillrect`(314 B) / `gpermRectblit`(826 B) 两版同尺寸，
   差异仅 11.5%/8.2% 且全是 `bl/blx` 相对调用 ⇒ 同一份源码
3. **`format→bpp` 查表两版字节相同**（`0x29D88E` vs `0xFD6E7A`）

### 1.3 rootfs 必须用 ZV-E10 的（关键）

```
ZV-E10 rootfs/lib: ld-linux.so.3 → ld-2.11.2.so   ✓
ZV-1F  rootfs/lib: 只有 ld-2.11.2.so，缺 ld-linux.so.3 符号链接
```

`im.elf` 的 PT_INTERP 是 `//lib/ld-linux.so.3` ⇒ **用 ZV-1F 的 rootfs 会直接起不来**。
⇒ 当前方案保留 ZV-E10 rootfs，只补 3 个空目录，正确。

---

## 二、⚠️ 绝对不能用于实体机的东西

### 2.1 `zve10_hybrid_full_v5.img` —— **禁止**

**v5 = v4 + `libIMDB.so` 的 10 处 mask 补丁**（QEMU 专用）：

```
v4 libIMDB: 91eca62f9cae6ee2e7ee34c31002a968   ← 用这个
v5 libIMDB: 2bc435014c841d7d369886164adac5b4   ← 打过补丁，禁用 USB 供电/tethering
差异: 10 字节（0x7e2c, 0x7fb8, 0x7fe4, 0x8010, 0x803c, 0x8068, 0x8094, 0x8748, 0x8774, 0x88a8）
```

**⇒ 会摘掉 USB 供电/充电/加密等服务。QEMU 专用，实体机禁用。**

### 2.2 所有 QEMU shim —— **禁止安装**

| shim | QEMU 里的作用 | 实体机后果 |
|---|---|---|
| `libswgpe.so`（gperm CPU 绘制） | 绕开不存在的 GPU | **绕过真 GPU**，画面不会出现 |
| `libmonconsole.so`（MonQueue） | 绕开 /dev/mem mmap 失败 | 无必要，可能干扰 |
| `fake_gles.ko` | 替代 GPU 驱动 | **破坏真驱动** |
| `WriteLockBits` 中性化 | 绕开 GPU fence 等待 | **破坏同步**，画面撕裂/不刷新 |
| `CheckUpdateFinished → 0` | 假完成信号 | 与真 GPU 完成信号冲突 |
| 任何 kmod stub | — | **必须用原厂模块** |

**⇒ 实体机镜像里 `/usr` 必须是纯库文件，不含任何 shim、不含 ld.so.preload。**

---

## 三、风险清单（按可能性排序）

| # | 风险 | 可能性 | 后果 | 可恢复 |
|---|---|---|---|---|
| 1 | **屏幕不亮 / 卡在启动** | 中高 | 新 UI 依赖的某种 ZV-1F 资源缺失 | ✅ fastboot |
| 2 | **某个 ZV-1F 库期望 ZV-E10 没有的外设** | 中 | 卡住或崩溃（同 SoC，风险中等） | ✅ |
| 3 | `/setting/mode/dmode` 从空改成 `"3"` | 低 | 理论上等价默认值 | ✅ |
| 4 | 内核模块不匹配 | 低 | 已保留 ZV-E10 原厂全部模块 | ✅ |
| 5 | `av-cam.bin` 版本 | 低 | **已保留 ZV-E10 原厂**（传感器相关） | ✅ |

**⇒ 全部可通过 fastboot 恢复，可放心试。**

---

## 四、建议的上机步骤（最小风险优先）

### 步骤 0：先做只读验证

```bash
# 从设备读回 /usr 前 1 MiB，与镜像比对（确认写入正确）
fastboot read ...   # 或你惯用的读回方式
```

### 步骤 1：**只刷 `/usr`（nflasha15），其余全部保持原厂**

```
写入源：nflasha15_hybrid_v4.ext2   (314,572,800 B)
分区  ：nflasha15，表偏移 0x2D800000，大小 300 MiB
```

**⇒ 这是最小改动**：只换用户态库，rootfs/kernel/所有模块/setting 全部不动。

**为什么不第一步就刷 rootfs**：那 3 个空目录（`tmp_cloud`/`tmp_remote`/`tmp_wbshd`）
只在 ZV-1F 的 initrd 需要时才有用；先不刷可以少一个变量。

### 步骤 2：观察

| 观察点 | 说明 |
|---|---|
| 能否正常开机 | 已登录 shell / 屏幕有反应 |
| `dmesg` / 串口 | 有没有库加载失败 |
| **屏幕内容** | 全黑 / 花屏 / 有菜单元素 |
| 按键响应 | 菜单能否操作 |

### 步骤 3：若 `/usr` 起来但 UI 不显示

**⇒ 再补 rootfs 的 3 个目录**（刷 `nflasha7_hybrid.img`，或手动 mkdir）。

### 步骤 4：若仍不行

**⇒ 那时再考虑 `dmode=3`（原厂为空）。**

---

## 五、上机时的取证建议

**实体机没有 QEMU 的探针，所以先准备好取证手段：**

1. **串口日志**（`exception.console_option=115200n8` 已在 cmdline 里）
2. **`/log/kikilog`**（原厂日志，nflasha10 里已有 `kikilog.dat`）
3. **刷回原厂后对比**：保留原 `nflasha15` 的完整备份

**⇒ 最重要的一条：刷之前先 `dd` 出原厂 nflasha15 存好**（如果还没存）。

---

## 六、诚实说明：哪些是"纸面结论"

| 内容 | 验证程度 |
|---|---|
| 分区布局、文件归属、属主、e2fsck | ✅ **实测验证** |
| 同 SoC / GPE 语义等价 / bpp 表相同 | ✅ **反汇编实证** |
| **新 UI 能否在 ZV-E10 硬件上跑起来** | ❌ **从未验证**（QEMU 里 appFw 从未成功跑通） |
| 绘制路径、位图布局、stride/bpp | ✅ 反汇编实证，但**未在运行时验证过** |

**⇒ 上实体机是当前最快的验证途径**，因为 QEMU 那条线的瓶颈（无 GPU、无真硬件）
在实体机上根本不存在。
