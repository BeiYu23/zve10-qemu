# ZV-E10 (CXD90045) ARMv7 最小 payload —— 定稿（所有数字都已确定 ✓）

## 0. 目标（唯一）
往 eMMC 偏移 **0x0AC00000** 写 **12,582,912** 字节（nflasha10.img，md5 `3a75a7c02c80420ec48b763164b02fbc`）
⇒ 删掉/覆盖坏掉的 `/etc/ld.so.preload` ⇒ 相机恢复 ✓

## 1. 入口与加载（来自 a7M4 上游 fastboot.ld / start.S ✓）
```
加载地址 = 0xFE020000        （ESRAM，64K 窗口；真机 SRAM = 0xfe000000-0xfeffffff ✓）
入口 = 0xFE020000           （payload 第一条指令）
进入状态 = MMU 关 / 无栈 / 高异常级（A7M4 是 EL3；A9 预计 PL2(HYP) 或 PL1）
串行启动波特率 = 【48000】  ★ 不是 115200（115200 是 Linux/PL011 控制台 ✓）
串行记录类型 0xFFFF = 加载并跳转 ✓
```

## 2. 第一阶段 payload（≤4KB，纯 ARMv7/A32）
```
① 设置栈（自己分配，如 0xFE02F000 向下）
② 关 MMU/DCache（进入时应已关；保险起见读 SCTLR 断言）
③ PL011 UART0 @ 0xF2000000，115200 8N1 ⇒ 打日志（"ZV10 ARMv7 payload alive"）
④ 读 SDHCI @ 0xF0304000：+0x40 CAPABILITIES / +0xFE HOST_VERSION / +0x124 emmc0 / +0x130 emmc1
   ⇒ hex 从 UART 吐回（验证基址对不对 ✓，只读零风险 ✓）
⑤ 读 BootROM 0xFFFF0000 48KB ⇒ hex 从 UART 吐回（顺便把"读不到 BootROM"这件事解决 ✓）
⑥ 然后等待第二阶段（UART 收数据）或直接进入写块流程
```

## 3. SDHCI 写块（来自 QEMU 侧给的 a7M4 sdhci.c 序列 ✓）
- **IP 差异**: a7M4 = Arasan；ZV-E10 = Synopsys DW（`f_sdh30.c`）⇒ 标准寄存器 0x00–0xFE 同 ✓，私有位不同 ✗
- **必须跑完整 mmc_init()**（不像 a7M4 那样 ROM 已初始化卡 ✗）
- 关键步骤（顺序不能错）：
  1. 保存 CLOCK_CONTROL(0x2C) / HOST_CONTROL(0x28)
  2. `BASE_KHZ = ((CAP(0x40)>>8)&0xFF)*1000`，0 则按 48000；`div = BASE_KHZ/(2*400)`，0 则 1
  3. `sdhci_reset(0x2F, RESET_CMD|RESET_DATA)` → 轮询自清
  4. `write8(0x28, saved_host & ~0x22)`（先 1-bit）
  5. `set_clock(div)`：先清 SD_CLK_EN → 写分频+INT_CLK_EN → 等 INT_CLK_STABLE(≤500ms) → 再置 SD_CLK_EN
  6. CMD0(0) → mdelay(2) → CMD1(0x40FF8080, 轮询 bit31, ≤1s) → CMD2(RESP_LONG) → CMD3(rca=1) → CMD7
  7. CMD6 EXT_CSD[183]=8 (BUS_WIDTH) / [185]=1 (HS_TIMING)
  8. ★★★ `read16(0x34) |= BUF_READ_RDY(bit5)|BUF_WRITE_RDY(bit4)`；`write16(0x36, 0x3ff)`
        （ROM 没开这两个位 ⇒ PIO 会永久等待 ✗✗）
  9. 恢复 `write8(0x28, saved_host)` / `write16(0x2C, saved_clk)`
- **写块 CMD24/25**：
  - wait PRESENT_STATE(0x24) 的 CMD_INHIBIT(bit0)/DATA_INHIBIT(bit1) == 0
  - `write16(0x04, 512)` / `write16(0x06, nblocks)`
  - `mode = BLK_CNT_EN` ；多块加 `MULTI|AUTO_CMD12`；**方向位不设**（=写）；PIO ⇒ DMA_ENABLE=0
  - `write32(0x08, start_block)`（块号，不是字节偏移）
  - **`write16(0x0E, (cmd<<8)|flags)` ← 必须 16 位写**（32 位写会打到 Transfer Mode ✗✗）
  - 等 INT_STATUS(0x30) bit0 CMD_COMPLETE（W1C）
  - 每块：等 bit4 BUF_WRITE_RDY（W1C）⇒ 往 0x20 连写 128 次 32 位（512B）
  - 等 bit1 XFER_COMPLETE（≤1s）
  - **最后 poll CMD13**：`resp & 0xFDF98080` 为错；`resp & READY_FOR_DATA(bit8)` 且 STATE==TRAN(4) 才 OK
  - 12MB = 24576 块；建议每批 512 块（256KB）后 poll CMD13 ✓
- 出错恢复：`sdhci_reset(RESET_CMD|RESET_DATA)` + 清 INT_STATUS(0x30) + 清 ERR_INT_STATUS(0x32)

## 4. 私有/vendor 窗口
```
0xF0304000-0xF03040FF  标准 SDHCI（到 0xFE）✓
0xF0304124   emmc0（4B 常量，判 HS200）✓ 可读
0xF0304130   emmc1（4B 常量）✓ 可读
⇒ 避开 0x100-0x123 / 0x128-0x12F / 0x134+ ✓
```

## 5. 时钟/复位（明确：**第一版不碰** ✗）
```
0xF8000000 = ARM Cortex-A9 SCU（不是时钟控制器 ✗ 我原来那条错了）
真正时钟在 sugi_*: clk0=0xf3400280 / pll0=0xf3400080 / rst0..3=0xf28040c4/0xf28041a8/0xf28050b8/0xf280518c
但写语义未知 ✗ ⇒ 依赖 bootloader 已把 eMMC 时钟跑起来（很可能 ✓）
若 mmc_init 读 CAPABILITIES 就死 ⇒ 再回来处理时钟
```

## 6. 上机顺序（一次上机做完三件事 ✓）
```
1) RP2040 灌第一阶段（48 kbaud，记录格式来自 uart_boot.ino/serialboot.pio ✓）
2) 看 UART0(115200) 输出:
     · "alive" ⇒ payload 跑起来了 ✓
     · SDHCI 值像样 ⇒ 0xF0304000 正确 ✓
     · BootROM 48KB hex ⇒ 拿到 dump ✓
3) 全部正常 ⇒ 灌第二阶段（写 eMMC）⇒ 写 0x0AC00000 12MB ⇒ 校验 md5 ⇒ 重启 ⇒ 修好 ✓✓
```

## 7. 兜底
- 写 eMMC 前 **先把 nflasha10 的当前内容读到 RAM 并回吐一份**（万一写坏还能对照 ✓）
- 官方升级程序不写 nflasha10（实测过 ✓）⇒ 它救不了，但也不影响我们的修复
