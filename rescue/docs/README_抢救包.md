# ZV-E10 抢救包 v2（2026-10-17）—— 已在 QEMU 里端到端验证 ✓

## 事故与目标
相机 `/etc/ld.so.preload` 指向一个被覆盖坏的 `.so`（gotdump.so，会 fork 扫 /proc）
⇒ 所有动态进程都加载它 ⇒ 启动循环。**分区/固件/boot 链一个字节未损坏 ✓**

## ★ 修复等价条件（任一条）
```
① 拿到任意 root 执行 ⇒ rm -f /etc/ld.so.preload            （最简单）
② 裸写 eMMC：12,582,912 字节 → 用户区偏移 0x0AC00000（块号 0x56000）
   内容 = nflasha10.img
   校验 md5 = 3a75a7c02c80420ec48b763164b02fbc ✓
        CRC32 = 0xBEFB387E ✓
③ 极致最小方案（正在验证）：只改 FAT 根目录里 ld.so.preload 那一项（32 字节）
```
⚠️ **偏移注意**: `.img` 文件内偏移是 0xB000000，但真机 eMMC 用户区 = **0xAC00000**
（.img 有 boot0 2MB + boot1 2MB 前缀）★ 差 4MB，写错会写到错误区域 ✗

## 已在 QEMU 端到端验证的内容 ✓（真机技术风险已烧掉）
```
W0 写入闭环: 写 2 块 + 读回 CRC 一致 + 镜像内容真的改变（"W0TEST!!" 标记实证 ✓）
W2 完整写入: 24,576 块（12MB）48 批全写 ⇒ 主机侧 md5 完全一致 ✓
            CRC32 0xBEFB387E ✓
边界安全:   前后各 64KB 未变 ✓（无越界写入 ✓）
FAT 完好:   mdir 能正常列出所有文件 ✓
修复后状态: ld.so.preload=0 / zve10_agent.so=0 / gotdump.so=0 ✓
日志:       W2_success.log
```

## 包内文件
```
nflasha10.img            12,582,912 B  /etc 干净镜像（md5 3a75a7c0… ✓）
verified_payload/        ★ QEMU 里验证通过的 ARMv7 payload（写 12MB 版）
   payload.bin   9,896 B   · main.c 16,571 B（CMD25 写 + CRC32 + W0/W1/W2 三阶段）
armv7_payload/           我前期的只读探测版（读 SDHCI / 读块 512B 验证）
sdcard/                  相机 boot 分区完整副本（MD5 与 p1chksum 逐条核对 ✓）
   boot/{vmlinux.bin,initrd.img,kemco.txt,loadaddr.txt} + p1chksum + dat2/dat3
tools/                   上游串行启动工具（RP2040 桥 / probe / poc）
ARMv7_方案_定稿.md       参数与步骤
行动简报.md              A7M4 fastboot 机制与接线
W2_success.log           QEMU 端到端成功日志
```

## 硬件参数（全部实证 ✓）
```
SoC CXD90045 · ARMv7(A9) 32位 · 无虚拟化扩展
UART 控制台   uart0 @0xF2000000 · 115200 8N1（PL011）
串行启动     ★ 48000 baud · 记录类型 0xFFFF = 加载并跳转 · 载荷落点 0xFE020000
SDHCI/eMMC   0xF0304000（+0x124 emmc0 / +0x130 emmc1 私有寄存器）
SRAM         0xFE000000-0xFEFFFFFF（16MB；payload@0xFE020000，镜像@0xFE100000 ✓）
eMMC 用户区   nflasha10 = 0x0AC00000（块 0x56000）
```

## 真机唯一门槛（未解决 ✗）
```
ZV-E10 一代【没有 Multi 口】⇒ A7M4 的 RP2040 桥无对应接口
不拆机时可能的接触点: USB-C SBU1(A8)/SBU2(B8) · MI 热靴 · 电池仓触点 · 3.5mm 麦克风口
⇒ 需要: USB-C 全针引出测试板 / 逻辑分析仪 / USB-TTL，确认哪一脚是 UART
```

## 纪律（血泪教训）
```
✗ 绝不把未经独立一次性进程验证的 .so 放进 /etc/ld.so.preload
✓ 验证: LD_PRELOAD=<so> /bin/true ⇒ 确认 constructor 不 fork、不阻塞、不扫 /proc
```
