# 事故：ld.so.preload 变砖（2026-10-28）

> **症状**：相机开机 **3 秒重启循环**，无 UI、无 USB、无网络。
> **与 2026-10-27 的砖症状完全相同**，但根因不同。
> 状态：**未修复**。本文给出可现场执行的验证与恢复步骤。

---

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
    --image /root/fw/zve10_backup/nflasha10.img

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
    --image /root/fw/zve10_backup/nflasha10.img \
    --part-start 0x0AC00000 \
    --full \
    --chunk-kb 768 --block-kb 64 \
    --resume-file /tmp/flash_n10.json

# 刷完立刻全量校验（不要用采样！）
python3 host/verify_partition.py \
    --part-start 0x0AC00000 \
    --image /root/fw/zve10_backup/nflasha10.img
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
| `zve10_backup/nflasha10.img`（10-25） | ✅ 原厂 FAT12，`ld.so.preload` size=0，可作恢复源 |
| `zve10_delivery/nflasha10.img`（10-16） | ✅ 原厂，但 `/tmp` 是易变分区，内容与 10-25 不同属正常 |
| `zve10_backup/nflasha10_current.img`（10-27 13:35） | ❌ **废镜像**：前 1 MiB 是测速图案 `bytes((i*13)&0xFF)`，后 11 MiB 全 0 —— 那是 10-27 测速事故现场，**不要当"当前状态"或"参考"用** |
| `zve10_backup/nflasha2.img` | ⚠️ 非出厂（缺 `mode/dmode`、`mode/preload`），**不能直接刷回** |
| `zve10_backup/nflasha2_current.img` | ✅ 含 `dmode="3"`、`preload=空`、`zve10_net="off"`，恢复用这个 |
| `/mnt/build/nflasha2_factory.img` | ✅ 出厂 /setting，同样满足 dmode/preload 条件 |

> 教训：**备份文件 ≠ 出厂镜像**。动手前先看里面有没有我们自己造的东西。
