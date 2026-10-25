# 让 QEMU 迭代变快（已实测）

## 结论（一句话）
**每次跑 QEMU 从 110~150 秒降到约 40 秒（`run_fast.sh` 提前退出），
并且可以做到"一次启动 + 多次调试"（GDB 直连，不再为每个假设重启）。**

---

## 1. 已实现的改进：`/mnt/build/run_fast.sh`

```bash
OUT=/mnt/build/tmp/xx EXTRA="<geometry...>" ./run_fast.sh <IMG> [MAXSEC=120] [IDLE=12] [MARK=INITCFG_EXIT]
```
它做了四件事：
1. **去掉 Xvfb**：用 `-display none` + QMP `screendump`。
   实测 `screendump` 在 `-display none` 下一样能出图（640x480 ppm）。
   ⇒ 少一个 68MB 的常驻进程，也不会再留孤儿 `Xvfb`。
2. **提前退出**：满足任一条件立即结束并抓图
   * 出现 marker（默认 `INITCFG_EXIT`）
   * 连续 `IDLE` 秒没有"有意义的新日志"（忽略 `AAB3/BRDSKIP/IMNANOSLEEP` 这类刷屏）
   * 到达 `MAXSEC`
3. **最小运行时间** `MINRUN`（默认 40s），避免把早期启动的空窗期误判成"卡死"。
4. 结束后自动 `kill` + 转 png。

实测：`elapsed=40s reason=idle`，`EN=1 / EX=0`（结果与 110s 跑法一致）。

**踩过的坑**：最初用"文件大小不变"判断空闲 —— 失败，因为串口一直在刷 UIPC 计数。
改成"统计**有意义日志行**的数量"才生效。

---

## 2. 下一步可做的：GDB 直连，一次启动多次实验

本机已有 `gdb-multiarch`，且 **libObj 基址跨运行稳定在 `0x05E82000`**（实测 `x/4xw 0x5e82000` 读到 ELF 魔数）。

```bash
# 启动时加 -gdb tcp::1235（其余参数同 run_fast.sh）
gdb-multiarch -q -batch -ex 'target remote :1235' \
  -ex 'info registers pc sp' \
  -ex 'x/8xw 0x5e82000+0x7d7e00'
```

**能带来什么**：之前"每个假设都要改探针 → 重做镜像 → 重启 40 秒"，
现在可以**在同一个运行实例里**：
* 硬件断点停在关键点（例如 `0x05E82000+0x7D7E06`，即读 `[r7,#20]` 前那条），
  直接读当次 `r7`、`[r7+20]`、回调 `arg`，**一次就把"是不是同一对象"钉死**；
* 直接改写内存/寄存器验证假设（例如把结果字段改成 0 再 continue），
  不用重新编译 probe；
* 失败也只是 `continue`，不需要重启。

⚠️ 注意：`savevm/loadvm` 快照**不可用**（`bionz_streamfb` 等自研设备没有注册 vmstate），
所以"恢复到已启动状态"这条路走不通，**GDB 直连是当前最优的省时方式**。

---

## 3. 其它可叠加的小优化

| 优化 | 说明 | 预期收益 |
|---|---|---|
| 串口降噪 | guest 里 UIPC 每秒刷几千行（`ZZIOCTL ... 0x8000000a/0b/05`，单轮 15k+ 行），写盘与 grep 都慢 | 中 |
| 收紧 `MAXSEC` | 现在实质有用信息都在前 ~40s | 小 |
| 并行准备镜像 | `dd/debugfs` 与上一次运行可重叠 | 小 |
| 复用 `OUT` 目录 | 只删 `serial.log`，保留脚本/中间产物 | 小 |
| 一次跑多信息 | 与其分多轮验证，不如一轮把所有相关点都 dump（探针里多加几行日志成本极低） | 大 |

---

## 4. 建议的日常工作流

```
① 改探针 → 编 .so（秒级）
② 用 run_fast.sh 打镜像并跑（~40s，自动抓图 + 提前退出）
③ 若结论不确定 → 不要改探针，直接开 -gdb 实例，用断点/读内存回答
④ 只有"需要改 guest 代码/镜像"时才重启
```
