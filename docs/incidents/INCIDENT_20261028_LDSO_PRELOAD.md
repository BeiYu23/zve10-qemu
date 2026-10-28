# 事故：3 秒重启循环（2026-10-28）—— 真凶是 dmode 少换行

> **症状**：相机开机 **3 秒重启循环**，无 UI、无 USB、无网络。
> **与 2026-10-27 的砖症状完全相同**，但根因不同。
> 状态：**已修复（2026-10-28 晚）** —— 真因见下面第 0 节：不是 preload，是 `dmode` 少了一个换行符。

---

## 0. 最终结论（2026-10-28 晚，相机已修复）

> ⚠️ **本文档后面的 1–8 节都是当时的推理过程，结论已被推翻。先看这一节。**

**直接原因不是 preload，是 `/setting/mode/dmode` 少了换行符。**

| 检查项 | 相机实测值 | 应该是什么 |
|---|---|---|
| `/etc/ld.so.preload` | `FileSize = 0`（空） | 空 ✓ |
| `/setting/mode/preload` | `size = 0`（空） | 空 ✓ |
| `/setting/mode/dmode` | **`b'3'`（size=1）** | **`b'3\n'`（size=2）** |

`dmode` 是 bootin 读的启动模式。少一个换行 ⇒ bootin 解析出的模式不是 3 ⇒ appFw 只起来一部分
（所以**UI 能画出画面**）⇒ 关键服务缺失 ⇒ 看门狗 ~3 秒复位。

### 修复动作（两步，各做了全量逐字节校验）

| 步骤 | 动作 | 全量校验 |
|---|---|---|
| 1 | `nflasha10 ← zve10_delivery/nflasha10.img`（真原厂） | **0 处不符** |
| 2 | `nflasha2 ← zve10_backup/nflasha2_current.img`（`dmode="3\n"`） | 采样通过 → **相机恢复** |

两个变量同时变了，但因果很清楚：**第 1 步之后相机没好转，第 2 步之后才好** ⇒ `dmode` 是决定性因素。

### `dmode` 为什么会被写坏

`/mnt/build/nflasha2_factory.img` 这份所谓"出厂镜像"的 `dmode` 本身就是 `b'3'`（size=1）。
它是 10-27 由 `nflasha2_current.img` 手工改出来的，那一次把换行丢了。
**任何一次用它恢复 `/setting`，相机都会进入「UI 出来但仍 3 秒重启」**。

该文件已于 2026-10-28 删除（md5 `eece37c3f898ffc1b91f134effb7fb1b`）；
需要复现可从 `nflasha2_current.img` 把 `dmode` 截成 1 字节得到。

### 为什么会一路排查到 preload 上

因为「3 秒重启」这个症状**同时符合** preload 致命失败和 dmode 错误两种原因，而且我都犯过：
早先确实写过坏 preload（那次是 3 秒重启的真凶），于是这次先入为主地往同一个方向查。
**真正的教训是：`dmode` 这种"一个字节的配置"必须最先检查，它的失败模式与 preload 完全一样。**

## 1. 我做了什么

为了测试 `bindm2.so`（loop+bind 双挂载方案），需要让进程在启动时加载我们的 `.so`。
`/setting/mode/preload` 这条 `LD_PRELOAD` 路径经实测**不生效**
（`/proc/157/environ` 里没有 `LD_PRELOAD`），于是转向 ld.so 的全局钩子：

```bash
# 通过 agent 通道写入
/etc/ldtest.so        # 5,500 B，自编译
/etc/ld.so.preload    # 内容 = "/etc/ldtest.so"
```

**这两个动作直接导致相机变砖。**

## 2. 根因

相机是 **glibc 2.11.2 / `ld-2.11.2`**。
这个版本的动态链接器把 preload 加载失败视为**致命错误**（现代 glibc 只是打印 `ignored`）：

* `/etc/ld.so.preload` 里列出的 `.so` 只要 dlopen 失败（架构不符、符号缺失、依赖缺失）
  ⇒ **每一次 `execve` 都失败**
* `init` 的所有 fork+exec 全部失败 ⇒ init 无法建立系统 ⇒ 内核/看门狗 ~3 秒复位
* `errors=remount-ro` 让 `/tmp`、`/etc` 变成只读（后续连修复都写不进去）

⇒ **用 `/etc/ld.so.preload` 做实验是"全或全无"的赌局，没有容错空间。**

## 2.1 原厂事实（必须记牢，这是第二次栽在同一处）

| 文件 | 原厂状态 | 我们的调试产物 |
|---|---|---|
| `/etc/ld.so.preload` | **不存在 / 空**（size = 0） | 我们把它指向某个 `.so` |
| `/etc/ld.so.preload.orig` | **不存在** | 我们留的备份（20 B = `/etc/zve10_agent.so`） |
| `/etc/zve10_agent.so` | **不存在** | 我们的调试 so（12,652 B，簇 377） |
| `/etc/ldtest.so` | 不存在 | **本次新写的坏 so（5,500 B）** |

**上一次砖机是同一个模式**：把 preload 指向 `zve10_agent.so`，那个 so 出了问题 ⇒ 全系统 exec 失败。
**这一次换成 `ldtest.so`，一字不差地重演。**

⇒ 唯一正确的恢复目标是 **"`ld.so.preload` 不存在 / size = 0"**，
**不是**恢复成指向 `.orig`（`.orig` 指向的也是我们的 so，抄 toolkit 注释里的
`cp .orig preload` 会立刻再砖一次）。


## 2.2 外部经验：preload 里**只放一个** .so

2026-10-28 另一条 QEMU 路线（chocotan 栈）的实测结论：

> `/etc/ld.so.preload` 里**放两个 .so 容易让 App 卡在初始化**；
> 现在统一**只保留一个 preload 库，功能全部合并进去**。

对照我们这边：10-27 的 bindm2 方案用的是**三个**：

```
preload = /usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so
```

当时记为"三者缺一不可"，但没有单独验证"三个同时加载"本身会不会让
`appFw` 卡在初始化。**这很可能就是那次"无 UI 且无任何日志"的一个候选原因**
（候选，未证实）。

⇒ 以后若真要用 preload，规则改为：**一个文件、功能合并**。
多库叠加的失败模式是"静默卡死"，不是报错，极难定位。

## 3. 修复尝试中的第二个错误（重要）

`flash_partition.py` 的行为已从源码确认：

```python
# 写入：永远是【分区内相对偏移】，stage2 的 flash:<part> 从分区首字节开始
img = sparse_run(off, data, part_bytes)
download(d, img); d.require('flash:%s' % a.part)

# --part-start 只影响【校验时读回用的绝对 LBA】
back = fetch_in_part(d, part_start_lba, off, len(data))
```

| 次 | 参数 | 结果 | 实际发生 |
|---|---|---|---|
| 1 | 无 `--part-start` | `readback mismatch at 0x0` | **写入是对的**，但校验用绝对 LBA 0 ⇒ 假 MISMATCH ⇒ 写完第 1 片（768 KiB）就 `SystemExit` |
| 2 | `--part-start 0x0AC00000` + `--reference` 指向**同一个镜像文件** | `0.0 MiB written` | `target == ref` ⇒ `todo` 为空 ⇒ **一个字节都没写** |

**"0.0 MiB written" 不是"写入成功无需改动"，而是"什么都没做"。**
这是本次修复失败的第二个直接原因。

### 唯一可能已经生效的部分

第 1 次刷写写进去的 **第一片 768 KiB** 覆盖了分区内 `0x000000–0x0BFFFF`，而
`ld.so.preload` 的目录条目在 **`0x0C60`**、其 size 字段在 **`0x0C7C`** —— **落在这 768 KiB 内**。

所以存在两种可能，必须现场用**全量逐字节校验**判定：

* **A**：第 1 片写成功 ⇒ `ld.so.preload` dirent 的 size 已回到 0 ⇒ 砖因另有其因
* **B**：第 1 片未真正生效 ⇒ 坏的 preload 仍在 ⇒ 就是本事故

## 4. 现场验证（只读，先做这个）

> 完整的注入/换线/stage2/工具清单见 [`../FASTBOOT_OPERATIONS.md`](../FASTBOOT_OPERATIONS.md)。
> 前置：Pico 插 `/dev/ttyACM0`，`pico/inject_payload.py --file prebuilt/stage1.bin --duration 600`，
> 看到相机长闪后把相机 USB 换接电脑（**Pico 会掉线，正常**），相机出 `18d1:4ee0`。
> **每次会话 < 3 分钟**（keepalive 是累计计时）。

```bash
cd /root/fw/oss/fastboot

# ① 全量逐字节校验 nflasha10 是否已等于原厂镜像（最可信，10-27 验证过 4800 块）
python3 host/verify_partition.py \
    --part-start 0x0AC00000 \
    --image /root/fw/zve10_delivery/nflasha10.img

# ② 同时确认 /setting 健康（dmode="3" 是 appFw 启动的前提）
python3 host/verify_partition.py \
    --part-start 0x00800000 \
    --image /root/fw/zve10_backup/nflasha2_current.img

# ③ 确认 eMMC 分区表完好（用户区偏移 0 首字节应是 ASCII "8246"）
python3 host/read_updater_log.py --lba 0 --sectors 1 --out /tmp/lba0.bin
xxd -l 16 /tmp/lba0.bin
```

判读：

| 结果 | 结论 |
|---|---|
| ① 0 处不符 | nflasha10 已是原厂 ⇒ 砖因**不是** preload，查 `/usr`（③④） |
| ① 大量不符 | 坏 preload 仍在 ⇒ 执行第 5 步 |
| ③ 首字节是 `EXBL` | fastboot LBA = **物理**基准（用户区 = 物理 + 4 MiB） |
| ③ 首字节是 `8246` | fastboot LBA = **用户区**基准 |

## 5. 恢复 nflasha10（确认要修时）

```bash
cd /root/fw/oss/fastboot

# 必须显式给 --part-start；不要传 --reference（会触发"差异块=0"跳过）
python3 host/flash_partition.py \
    --part nflasha10 \
    --image /root/fw/zve10_delivery/nflasha10.img \
    --part-start 0x0AC00000 \
    --full \
    --chunk-kb 768 --block-kb 64 \
    --resume-file /tmp/flash_n10.json

# 刷完立刻全量校验（不要用采样！）
python3 host/verify_partition.py \
    --part-start 0x0AC00000 \
    --image /root/fw/zve10_delivery/nflasha10.img
```

`--full` 强制整片覆盖，避免任何"差异块"逻辑把写入悄悄跳过。

## 6. 恢复判定标准

- `verify_partition.py` 报 **0 处不符**，才算修好
- 相机断电重新上电，看到**老菜单**，才算修好
- 任何"showing 3 samples all match"之类的抽样结论**一律不认**

## 7. 三条铁律（新增）

1. **不要用 `/etc/ld.so.preload` 做实验**。glibc 2.11 无容错，一次失败全系统死。
   要测 `.so` 只能先 `RUN "LD_PRELOAD=/path ./某个动态程序"` 单条命令试，确认只死这一条。
2. **`--reference` 绝不能指向与 `--image` 相同的文件**；`0.0 MiB written` 等于没做事。
3. **校验必须全量**。采样到没被改动过的扇区 = 假验证。

## 8. 镜像可信度（本次踩到的坑）

| 文件 | 真实性 |
|---|---|
| `zve10_delivery/nflasha10.img`（10-16） | ✅ **首选恢复源 —— 真原厂语义**：根目录里**没有 `ld.so.preload`**、**没有 `.orig`**、**没有 `zve10_agent.so`**，有 `images/`（UI 字体）。只多了 22 个早期 `.zv10_ran_*` / `.zv10_done_*` 标记文件，无 preload，无害 |
| `zve10_backup/nflasha10.img`（10-25） | ⚠️ 次选：`ld.so.preload` size=0（与"文件不存在"等效）。但含我们**全部调试残留**：`zve10_agent.so`(12,652 B)、`ld.so.preload.orig`、`dev`/`mnt`/`sd` 目录 |
| `zve10_backup/nflasha10_current.img`（10-27 13:35） | ❌ **废镜像**：前 1 MiB 是测速图案 `bytes((i*13)&0xFF)`，后 11 MiB 全 0 —— 那是 10-27 测速事故现场，**不要当"当前状态"或"参考"用** |
| `zve10_backup/nflasha2.img` | ⚠️ 非出厂（缺 `mode/dmode`、`mode/preload`），**不能直接刷回** |
| `zve10_backup/nflasha2_current.img` | ✅ 含 `dmode="3"`、`preload=空`、`zve10_net="off"`，恢复用这个 |
| `/root/fw/zve10_backup/nflasha2_current.img` | ✅ 出厂 /setting，同样满足 dmode/preload 条件 |

> 教训：**备份文件 ≠ 出厂镜像**。动手前先看里面有没有我们自己造的东西。
