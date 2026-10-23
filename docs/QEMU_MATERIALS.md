# QEMU 对话所需材料清单（2026-10-23）

> 本文回答 QEMU 对话提出的核心疑问，并列出"还缺什么、谁能给、怎么抓"。
> 所有结论都来自本仓库已有的真机 dump，可离线复核。

---

## 一、三个已验证事实（直接推翻"缺应用层"的假设）

### 1. ZV-E10 的 `/usr/lib` 里**从来就没有** `appFw.so` / `gui.so`

```
nflasha15.img (/usr, 真机原始 dump)  →  /lib:
  ✗ appFw.so   ✗ gui.so   ✗ libNVM.so
  ✗ libSaveLoadSettings.so   ✗ libInfraWebApi.so
  ✓ libObj.so (21,305,528 B)
  ✓ viewUnified2.so (14,284,732 B) … viewUnified8.so
  ✓ libIMDB.so
共 210 个文件
```

**这不是缺陷，是架构**：ZV-E10 用的是**老 UI 架构**（`viewUnified2..8.so` +
`libObj.so` + `.uxc` 资源）。`appFw.so` 单体架构属于 **ZV-1F / ZV-E10 II**
（见 `ZVE10_PORT_ROADMAP.md`）。所以「没有 appFw.so」不是「缺应用层的机器」，
而是**这台机型本来就这样**。

### 2. 那台**正常出 UI 的相机**，`/usr/lib` 就是这 210 个文件

真机 `/proc` 快照（`zve10_delivery/a_p0/usr_lib_ls.txt`，10-16 抓取）：

```
-r-xr-xr-x 1 57285 1000 21305528 Jun 4 2026 libObj.so
-r-xr-xr-x 1 57285 1000 14284732 Jun 4 2026 viewUnified2.so
… viewUnified3..8.so 全部在位
```

而 10-16 → 10-17 那一整天，相机是**完全正常**的（UI ✓ / WiFi ✓ / 服务模式 ✓）。
⇒ 结论：**菜单就是 `libObj` + `viewUnified*` + `.uxc` 画的。**

### 3. `Application_System_Init` 的真实现在 `libObj.so` 里，且它是**异步**的

```
libObj.so 导出: 008a2409  144 B  FUNC GLOBAL DEFAULT  Application_System_Init
```

反汇编（`0x8a2408`，Thumb）：

```
memset(buf, 0, 32)
snprintf(buf, 32, ...)                    ; 拼一个名字
bl  0x8a2314                              ; AbstractUtilityManager::createRequestModelExecuteEvent 附近
strncpy(...)
ldr r0, [got]                             ; 线程入口
blx osal_create_thread@plt                ; ★ 创建线程
blx osal_set_cpu@plt        (r1 = 15)     ; 绑核
... 传入线程句柄继续初始化
```

⇒ **`Application_System_Init` 自己不建 view**，它 `osal_create_thread` 拉起一个
线程，UI/事件循环在那个线程里。这正好对上 QEMU 对话在 `im.elf` 里看到的
`Appframework` / `uxengine_res1..2` 线程。

补充：`AbstractSequenceFactory::CreateSequence` 在 `libObj.so` 里是 **UND
（导入）**，不在 libObj 内部——序列/场景的创建来自外部库（MWF 侧）。
`ux::gfxeng::GraphicsEngine` 的导出确认存在（`sys_getInstance`、
`getFramebufferWidth/Height`、`setOverflowResizeFontAttribute` 等），
即**软件渲染路径在 libObj 内**。

---

## 二、结论：**不需要 agent，也不需要另一台零售机**

| QEMU 对话的假设 | 实际 |
|---|---|
| 若正常开机有菜单 ⇒ 菜单不是 appFw/gui 画的 | ✅ **正确** |
| 若没有 appFw/gui ⇒ 可能是缺应用层的机器 | ❌ **错**，这是 ZV-E10 的正常架构 |
| 可能需要 5 个文件（appFw/gui/…）从零售机取 | ❌ 这台就是零售机，只需要在 QEMU 里把启动序列打通 |

⇒ QEMU 路线继续攻「启动序列 / 参数」，**不必写 agent、不必换机器**。

---

## 三、清单 A：现在就能给的材料（已在本仓库，无需相机）

| # | 材料 | 位置 | 用途 |
|---|---|---|---|
| A1 | `/usr` 完整 ext2 镜像（300 MB，210 个 lib + 全部 .uxc） | `zve10_delivery/nflasha15.img` | 已解包参考树 |
| A2 | 真机 `/usr/lib` 完整 ls（210 行，含权限/属主/大小） | `zve10_delivery/a_p0/usr_lib_ls.txt` | 证明零售态构成 |
| A3 | 真机内核 cmdline | `real_cmdline.txt` | QEMU 启动参数基准 |
| A4 | **真机冷启动完整 klog（1 MB）** | `zve10_delivery/klog_mem_COLDBOOT_1M.bin` | 与 QEMU 启动日志逐步对比 |
| A5 | 真机冷启动/暖启动文本日志 | `zve10_delivery/COLD_BOOTLOG.txt`、`BOOTLOG_sorted.txt` | 对比模块加载顺序 |
| A6 | 真机挂载表、设备节点、dmesg、proc 快照 | `zve10_delivery/a_p0/`、`a_p2/` | QEMU 环境对齐 |
| A7 | `libObj.so` 反汇编 / 符号（含 `Application_System_Init`） | 可从 A1 用 `debugfs dump` 取出 | 定位 UI 线程入口 |

注：`alog_mem.bin`（384 KB）**全零**——抓取时应用日志环未启用，需重抓（见清单 B）。

---

## 四、清单 B：需要从**现在这台活着的相机**抓（最高优先级）

相机已修复并能正常开机，这是抓"零售态基准"的唯一窗口。全部只读。

| # | 目标 | 命令 | 为什么关键 |
|---|---|---|---|
| B1 | **im.elf 的进程与线程** | `busybox ps` / `ls /proc/<pid>/task/*/comm` | 对照 QEMU 里的 Appframework/uxengine 线程是否齐全 |
| B2 | **im.elf 的 maps** | `cat /proc/<pid>/maps` | ★ 证明 UI 起来时到底加载了哪些 `viewUnified*.so`、`.uxc` 映射到哪 |
| B3 | im.elf 的 fd / status | `ls -l /proc/<pid>/fd`、`cat /proc/<pid>/status` | 它在等哪些设备/UIPC 队列 |
| B4 | **应用日志环（UI 起来后抓）** | 读 `alog.addr=0x03524FC0`，size `0x60040` | ★ 上次抓到全零；这是 App 自己的日志，能直接看出它走到哪一步 |
| B5 | 内核日志环 | 读 `klog.addr=0x035A5000`，size `0x20000` | 对照 QEMU |
| B6 | `/proc/tmonitor` | `cat /proc/tmonitor` | RTOS 任务表（60+ 任务名），QEMU 要模拟的一致性基准 |
| B7 | UDM 驱动状态 | `cat /proc/udm/*`（或 `a_p0/proc_udm.txt` 的完整版） | 33 个平台模块的寄存器/状态 |
| B8 | `/setting` 关键文件 | `ls -lR /setting`、`cat /setting/KEMCO.TXT` | dmode/preload/env.txt 决定启动分支 |
| B9 | GRM/GPE 调用计数（可选） | LD_PRELOAD 探针 | 确认真机上 `GraphicsEngine` 的实际调用序 |
| B10 | 屏幕首帧时序 | 串口 console（`KEMCO.TXT` 改 `ttyAM0`） | 从内核视角看 framebuffer 何时上屏 |

**抓取方式建议**：优先用**服务模式 shell**（系统正常时可用，无硬件需求）；
`B4/B5` 的物理地址读取需要 `/dev/mem` 或 `oem peek`（但 `0x03524FC0` 在当前
payload 的允许窗口外，需要扩窗口或走服务 shell）。

---

## 五、清单 C：纯离线可做（我可以直接干）

| # | 工作 | 产出 |
|---|---|---|
| C1 | 解析 `libObj.so` 里 `Application_System_Init` 拉起的线程入口（解析 GOT） | UI 主循环的确切符号 |
| C2 | 从 `libObj.so`/`libmpr.so` 提取 `PRM_*` / `VAL_*` 参数表 | ★ QEMU 需要"回答"的参数全集 |
| C3 | 对比真机 `klog_mem_COLDBOOT_1M.bin` 与 QEMU 启动日志，找第一个分叉点 | 精确定位 QEMU 卡点 |
| C4 | 解出 `.uxc` 容器格式 + `global.xdb` 引用关系 | 资源加载路径（QEMU 里验证资源读取） |
| C5 | 静态分析 `viewUnified2.so` 里 `global.xdb` 的打开点 → 谁触发 | 资源加载的调用链 |

---

## 六、给 QEMU 对话的直接回答

1. **菜单是完整的**（10-16 记录：UI ✓ WiFi ✓ 服务模式 ✓，同一份 `/usr`）。
2. **`appFw.so`/`gui.so` 不存在是正常的**——ZV-E10 是 `viewUnified*` 架构，
   那 5 个文件是 ZV-1F/ZV-E10 II 的清单，本机不需要。
3. **`Application_System_Init` 在 `libObj.so`（0x8a2409, 144 B）**，它
   `osal_create_thread` 起线程，view 创建在**那个线程**里，不在该函数内。
   `olt 0x8a2314` 处是一次 `createRequestModelExecuteEvent` 类的提交。
4. **不必写 agent、不必找零售机**；继续攻 QEMU 启动序列。
5. **最高优先级的缺口是 B4（应用日志环）和 B2（im.elf 的 maps）**——
   这两个能一次性说明"App 到底走到哪、谁没起来"。
