# chocotan 栈的 QEMU WebUI 基线（另一对话提供，2026-10-28）

> **来源**：另一个 DSH 对话（`session-41b33827`）2026-10-28 通过 conversation 通道提供的实测记录。
> 属外部数据，非本仓库实测；引用时注明来源。

## 这张图到底是怎么出来的

`menu-20261028-014610.png`（已在 `/snap` 里）**不是从磁盘镜像跑出来的**，与 `menu-only-s7.img` /
`menu-only-t1.img` 无关。

- 工作目录：`/mnt/build/ref-chocotan-zve10`
- **没有用磁盘镜像**，走的是**内嵌 initramfs 的 kernel**：
  `work/kbuild-ui/arch/arm/boot/Image`
- 启动脚本：`./start-zve10-ui.sh -smp 2`，其内部等价于：

```sh
qemu-system-arm -M zve10 -cpu cortex-a9 -smp 2 -m 1024 \
  -kernel work/kbuild-ui/arch/arm/boot/Image \
  -initrd work/ui.cpio \
  -append 'console=ttyAM0,115200n8 cfg.uart=1 mem=1G@0 memrsv=0x1A5E000@0x02E68000
           memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none -serial stdio -monitor none
```

- 抓帧：**不从串口取**，而是从宿主侧读 QEMU 进程的 `/proc/<pid>/mem` 物理地址 **`0x22C48000`**
  （640×480×2，RGBA4444），脚本 `tools/cap.py`。

## 三个容易把人带偏的点

1. **用的是 `-M zve10`（chocotan 的机型定义），不是 `-M cxd90045`**；
   **`bionz-streamfb` 这个设备没有参与**这条路径。我们以前在 `run_cap.sh` 里挂
   `-global driver=bionz-streamfb,...` 的那套是另一条路，别混。
2. 日志里出现的 `UI_INIT_ENTER` / `UI_INIT_RETURN` 标记**属于 chocotan 栈**
   （`libuiobserve` / 驱动会打），不能拿它反推我们自己的 im.elf/appFw 走到哪一步。
3. `-smp 2` 经常卡在 `infra_lanc_init`；**`-smp 4` 明显更快更稳，约 3–4 分钟出菜单**。

## ★ 关键坑：`ui.cpio` 内嵌在 `Image` 里

`work/ui.cpio`（约 239 MB）是**内嵌进 `Image`（约 242 MB）**的。所以：

> **换 `/usr` 内容必须"重打包 cpio + 重编 Image"两步都做；只改磁盘分区/只换 cpio 都不会生效。**

否则跑的一直是旧代码。这条与真机路线的教训正好对称：

| 路线 | "改了却没生效"的形态 |
|---|---|
| QEMU（chocotan 栈） | 改了 `ui.cpio` 或磁盘分区，但 **`Image` 里内嵌的还是旧的** |
| 真机（ZV-E10） | 改/刷了 eMMC 分区，但**启动实际读的不是那份**（或写成了一半） |

两边症状一样：看起来在跑新配置，实际一直是旧代码。**判断"到底加载了哪一份"永远是第一步。**

## 已知 / 未知

- ✅ 该路径能出真 UI 帧（有截图与 61 次 init 的日志规模互相印证）
- ❓ **把 `/usr` 换成 M2 混合内容这条路，对方没做过，没有结论**
- 我们的 `nflasha15_v9.ext2`（M2 应用层）在这条链路上要生效，前提是把它重打进 cpio 再重编
  `kbuild-ui/.../Image`，不是刷 eMMC
