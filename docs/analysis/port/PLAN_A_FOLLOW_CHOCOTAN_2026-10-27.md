# 方案 A：按 chocotan/zve10-qemu 的路径重做（执行计划）

来源：`https://github.com/chocotan/zve10-qemu`，commit `a7f23b0`，本地
`/mnt/build/ref-chocotan-zve10`。他们已跑出原厂菜单（README 截图）。

## 他们的完整配方（已逐文件核对）

**1. 无磁盘、全 initramfs**
`start-zve10-ui.sh`：把整棵运行树打进 cpio（`work/ui.cpio`），
`-kernel Image -initrd ui.cpio -append '... rdinit=/init'`。没有 nflasha 分区、没有 ext2 根。
（我们的所有分区/`/tmp` 麻烦由此消失。）

**2. 极简模块集 + 设备号**
`source/initramfs/init-ui.sh` 按序 insmod：
`devno, dmac, osal_utm, osal_uipc, dma330_stub, backup(Backup_load=1), utimer,
osal_ulogio(initfile=/setting/ulogio.bin), kikilog, stream, stream2,
dmm(DmmConfig=/setting/DmmConfig.bin), grm_ma, grm_gles`
然后 mknod：
`uipc 251 207 / backup 251 208 / dmpgles2 248 29 / stream 249 7 / stream2 249 8 / ldec 251 205`

**3. 走原厂 launch 链，不是自己调 API**
```
LD_PRELOAD=/usr/lib/libuiobserve.so /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal
```
`target=qemu` 就是 IMDB 的 qemu 档位。**我们之前直接调 `Application_System_Init` 是绕过了这条链。**

**4. IMDB 只清 10 个 USB 类模块**
`scripts/prepare-ui-profile.py`：对 10 个模块 `targets & ~2`，
并断言保留 `appFw.so / libgrm.so / libuxengine.so`。

**5. 绘图层**
`source/ui/*`：hook `notifyDraw / drawFramebuffer / rm_execute / rm_step / engine_wait / lz_decode`，
软件实现 `fill / blit / present`，LZ77 解码，最后导出 PNG。

## 我们要照做的工作项

| # | 事项 | 现状 |
|---|---|---|
| 1 | 重建运行树为 cpio（rootfs+usr+/tmp 字体+/setting/*.bin+kmod） | 未开始；素材齐全（`firmware-root`、`zve10_backup/nflasha10.img` 字体、`FirmwareData_ZVE10V203.dat`） |
| 2 | 提供真 `appFw.so / libgrm.so / libuxengine.so` | **缺**：本地只有 7.5 KB 桩；需从 2.03 解包 |
| 3 | 按他们的顺序装模块 + mknod | 我们已有等价模块，需核对设备号 |
| 4 | 用 `bootin.elf target=qemu dmode=3 amode=normal` 启动 | 未做（我们跑的是 im.elf 或自造宿主） |
| 5 | 把他们的 `ui-observe.c` 移植为 ARMv7 的 `libuiobserve.so` | 未开始（源码在 `source/ui/`，依赖 ARMV7 适配） |
| 6 | 内核允许大 initramfs（`rdinit=/init`） | 需改 cmdline/机型参数（当前 `initrd=0x700000,8M` 太小） |

## 建议执行顺序

1. 解 2.03 的 `nflasha15`（`work/firmware-root/0700_part_image/dev/nflasha15`），拿到真 `appFw.so`/`libgrm.so`/`libuxengine.so` 及资源；
2. 用他们 `docs/setup.md` 的方法生成运行树（含 `/tmp` 字体、`/setting/Backup.bin`、`DmmConfig.bin`）；
3. 先**只换根为 cpio + `rdinit=/init` + 他们的 init-ui.sh**（不装我们的任何 shim），跑通到 `bootin.elf`；
4. 再编 `libuiobserve.so`（ARMv7 适配），接软件光栅化与 PNG 导出。
