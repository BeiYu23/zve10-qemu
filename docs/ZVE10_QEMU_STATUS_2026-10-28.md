# ZV-E10 QEMU 现状（2026-10-28）

## 一句话

**原厂菜单已经能在 QEMU 里由原厂 App 自己画出来并实时抓帧**；
**按键操作还没打通**，差一个常量（InfraInput 的 osal 队列 id）。

## 已经能用的东西

| 能力 | 说明 |
|---|---|
| 启动原厂系统到菜单 | `-M zve10 -smp 4`，Sony 原版内核 + 对方补丁 + 其 defconfig |
| 原厂菜单画面 | App 自己绘制，640×480，`GEMINI.total.textrect=0x194`、`export_ready=1` |
| 实时抓帧 | 从 QEMU 进程 `/proc/<pid>/mem` 读物理 `0x22c48000`（RGBA4444） |
| 网页控制台 | `python3 tools/zve10_web.py`（启停/看屏/抓图/日志，支持手机访问） |
| SD 卡 | `/usr` `/system` 走 PL181 SD（补了 platform_data + 时钟 + 第二条 IRQ） |
| 完整系统链路 | 厂商 `/sbin/init` 可 exec（静默，待查） |

## 关键复现步骤

```bash
# 1) 内核（Sony 原包 + 对方补丁 + defconfig + 内嵌 ui.cpio）
cd /mnt/build/ref-chocotan-zve10/work
make -C linux-kernel O=$PWD/kbuild-ui2 ARCH=arm \
  CROSS_COMPILE=$PWD/armv7-eabihf--glibc--stable-2020.08-1/bin/arm-linux- Image

# 2) 启动（注意：-smp 4 比 -smp 2 快很多，菜单约 3 分钟出现）
qemu-system-arm -M zve10 -cpu cortex-a9 -smp 4 -m 1024 \
  -kernel kbuild-ui/arch/arm/boot/Image \
  -append 'console=ttyAM0,115200n8 cfg.uart=3 mem=1G@0 memrsv=0x1A5E000@0x02E68000 memrsv=128M@0x20000000:uc rdinit=/init loglevel=8' \
  -display none -serial unix:/tmp/cons.sock,server=on,wait=off \
  -serial unix:/tmp/kbd.sock,server=on,wait=off -monitor none

# 3) 抓帧（宿主机侧）
python3 tools/cap.py shot     # 输出 menu-shot.png
```

## 关键结论（踩过的坑，别再走）

1. 对方机型**不转交外部 `-initrd`** → initramfs 必须**内嵌**进 Image；
   **改了库必须重编 Image 并 `strings` 校验**，否则跑的是旧代码。
2. `-smp 2` 常常卡在 `infra_lanc_init`；**`-smp 4` 明显更快更稳**。
3. libuiobserve 已占用 UI 线程所有常用 hook 点（drawFramebuffer/rm_step/…)；
   自写 hook 只能选没人占的地址，且菜单静止时不一定被调用。
4. 拦截 `osal_snd_sync_msg` / `osal_rcv_msg_tmo` 会让 App 卡死（即使纯透传）。
5. `/dev/mem` 在客机里受限制，不能用来判断"画面是否已画好"。
6. 实验必须用 `systemd-run --collect --unit=…`（**不要 `--scope`**，会随调用者 shell 被杀）。

## 下一步（按键注入）

照 **a7c2-re** 的做法：往 InfraInput 的 osal 队列投 20 字节消息。

```c
osal_valloc_msg_wait(queue, &msg, 20, 1);
words = {key_code, sub=0, status=1, ex=0, from=1};
osal_set_msg_src(msg, src);
osal_snd_msg(queue, msg);
```

* a7c2 的实参：`queue=0x807a0020 src=0x92840747`，键码 `UP=0x3, DOWN=0x30, LEFT=0x15, RIGHT=0x1af, ENTER=0x1ac, MENU=0x1ab`
* **ZV-E10 还差自己的 queue id**（`InfraInput::RecvThread` 里读取的那个常量）

工具已就位：guest 侧注入库提供 `#N`（找 qid）与 `#K <key> <sub> <status> <ex> <from>`。
