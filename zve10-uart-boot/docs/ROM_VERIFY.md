# ZV-E10 BootROM 逐项验证（对照 A7C 笔记）

## ① 入口跳转（笔记称 0xFFFF013C）
```asm
ffff013c: ldr r1,[pc,#4]     ; r1 = 0xFE005000
ffff013e: ldr r0,[r1,#12]    ; r0 = [0xFE00500C]  ← SET_ENTRY 写入的目标
ffff0140: bx  r0
```
✅ 与笔记完全一致。bx 到偶数地址=ARM 态（我们的 payload 全是 ARM，实测可行）；
   若要用 Thumb 代码则必须 addr|1。

## ② 记录解析入口（笔记称 0xFFFF0802）
```asm
ffff100a: ... bl 0xffff0802     ; 解析记录流
          cmp r5,#0x3001        ; 0x3001 = 超时/无数据 → 正常启动
          bne  错误码路径
```
✅ 一致。收到 0x3001 表示"没收到有效记录"，ROM 继续正常开机。

## ③ 组关闭规则（笔记称 0xFFFF0B2E）
```asm
ffff0b2e: cmp sl,r0       ; sl = 组首 type，r0 = 当前记录 type
ffff0b30: bne  继续接收    ; 不同则不算关组
```
✅ 一致 ⇒ 组首/组末必须同 type（我们全用 SET_UNLOCK）。

## ④ 组关闭前的 flag 检查（笔记称 flag0|flag1 全 0 → 0x2004）
```asm
ffff0b34: ldr r0,[pc,#108] ; = 0xFE010004
ffff0b36: ldrd r1,r0,[r0]  ; r1=[0xFE010004] flag0, r0=[0xFE010008] flag1
ffff0b3a: orrs r0,r1
ffff0b3c: beq  → 0x2004
```
✅ 一致。SET_UNLOCK 记录置 flag0，SET_ENTRY 置 flag1。

## ⑤ 串口采样寄存器
```asm
ffff0724: r6 = [0xffff08e4] = 0xF2915000
ffff0734: ldr r0,[r6,#16]   ; MISC_MODEREG
ffff0736: ubfx r4,r0,#1,#1  ; bit1 = BOOT_SERIAL_IN
```
✅ 一致（我们接收器也用 0xF2915010 bit1）。

## ⑥ 串口启动使能 strap
```asm
ffff1126: r5 = [0xffff138c] = 0xF2915000? (实为 0xFE010000 指针来源)
ffff1128: ldr r0,[r5,#16]   ; = [0xFE010010] strap 字
ffff112c: ubfx r0,r0,#4,#1  ; bit4
ffff1132: cbz → 跳过
ffff1138: bl 0xffff0c5e / bl 0xffff0f48   ; → 串口启动状态机
```
✅ bit4 = 串口启动使能；bit2 = eMMC 正常启动路径。

## ⑦ 解锁 token
```
0x8CB102DC / 0xA86DA25E / 0x2CF25621 都不是 ROM 里的常量
⇒ ROM 运行时从 boot0 偏移 0x200 读取（V,C），并校验 crc32(V||C)==0xFFFFFFFF
⇒ 每台机器的 token 由 boot0 决定；我们是 V=0x8CB102DC, C=0xA86DA25E（已从本机 boot0 提取）
```
✅ 与笔记一致。A6700 的 0x2CF25621 不适用于本机。

## ⑧ 波形窗口（笔记：慢块 225.4–293.6 µs）
我们的 PIO 前导：`set pindirs,1 [23]` = 24 指令 × 10.416 µs = 250.0 µs
                  `set pindirs,0 [22]` = 23 指令 = 239.6 µs
✅ 两者都在窗口内（A7C 的 slow=11 → 229.2 µs 实测失败，slow=12 → 250 µs 成功）。

## 结论
A7C 笔记的所有关键地址/规则在 ZV-E10 的 BootROM 中**逐条吻合**（同为 CXD90045 代）。
A6700 仓库只有与 SoC 无关的做法（sense 同步、Pico 命令、记录编码）可以借鉴。
