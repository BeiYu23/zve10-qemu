# ROM 的 eMMC 初始化序列（从 BootROM 反汇编，实测待验证）

## 关键缺失：PHY 寄存器块 0xF0304200（我们此前从未碰过）
```asm
str  ip,[0xF0304200]      ; = 0
str  r2,[0xF0304208]      ; = 7
str  r3,[0xF0304200]      ; = 1        ← 使能 PHY
str  r2,[0xF0304100]      ; = 7        ← SDHCI base+0x100
r2 = [0xF0304204]
r2 &= ~2 ;        [0xF0304204]=r2
r2 &= ~(0x3FF<<16); r2 |= 0x01E00000 ; [0xF0304204]=r2
r2 |= 2 ;          [0xF0304204]=r2
```
ROM 还在 SRAM 存了指针：`[0xFE010024]=0xF0304000`、`[0xFE010028]=0xF0304200`

## 标准 SDHCI 部分（照 ROM）
```
CLOCK_CONTROL = 0x0001；轮询 bit1（<=10000 次）
HOST_CONTROL |= 0x34
[0x38]=0 ; [0x3A]=0
INT_ENABLE = 0x000B ; ERR_INT_ENABLE = 0x77FF
POWER_CONTROL = 0x0A          ← 不是 0x0F
[0x158] = 0x40                ← Sony vendor
[0x124] = 0x200               ← Sony vendor
TIMEOUT_CONTROL(0x2E) = 0x0E
CLOCK_CONTROL |= 0x04         ← 最后才开 SD 时钟
```

## 每条命令前后
```asm
[0x124] |= 0x20000     ; 开
... 发命令 + 等完成 ...
[0x124] &= ~0x20000    ; 关
```

## 数据阶段（ROM 实测参数）
```
单块读：
  BLOCK_SIZE(0x04) = 512
  BLOCK_COUNT(0x06) = 0
  [0x58] = 缓冲区地址
  [0x5C] = 0
  TRANSFER_MODE(0x0C) = 0x31     ; DMA_EN | TRNS_READ | MULTI
  ARG(0x08) = LBA
  COMMAND(0x0E) = 0x111A         ; CMD17 + R1 + index + data
多块读：
  BLOCK_COUNT = len/512
  TM = 0x37
  COMMAND = 0x123A               ; CMD18
其它命令字（ROM 原样）：
  CMD7 = 0x071A   CMD0 = 0x0000   CMD1 = 0x0102
```

## ROM 保存的状态区（payload 可读）
```
0xFE030000 : INT_STATUS      （仅错误时写）
0xFE030002 : ERR_INT_STATUS  （仅错误时写）
0xFE030004 : [base+0x3C]
0xFE030006 : [base+0x54]
0xFE030008..0x14 : RESPONSE[0..3]  ← 每次 CMD13 轮询都刷新，可读到最后一次卡状态
```

## 已实现的 payload
`payload_rom_init.c` / `payload_rom_init.bin`（1,184 B）
报告码（N 次长闪循环）：
```
1 = 读到 FAT 引导扇区（成功）
2 = 读到了但内容不对
3 = CMD17 超时
4 = CMD1 超时
5 = CMD0 超时
6 = CMD2/CMD3 超时
7 = CMD7 超时
```
