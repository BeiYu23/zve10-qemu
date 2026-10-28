# 内存不足假说：已用启动参数证伪（2026-10-28）

> 起因：另一个分析线程推测"新菜单跑不起来是因为 128 MB 内存装不下 61 MB 的 appFw.so"。
> 结论：**假说错误。ZV-1M2 的 Linux 可用内存与 ZV-E10 完全相同（141.7 MB）。**

---

## 一、决定性证据：`kemco.txt`（真启动参数文件）

位置：`/mnt/build/fw/unpacked_<model>/firmware.tar_unpacked/0800_appli/setting/kemco.txt`
（updater 的 `boot/` 目录下也有一份，是 bootloader 实际读取的内存映射）

### ZV-E10

```
mem=1G@0                       物理 DDR 1024 MB
mem=256M@0x80000000@1          第二个 DDR bank 256 MB
memrsv=1M@0                                  1.0 MB
memrsv=0x8000@0xF00000                       0.03 MB
memrsv=0x1A5E000@0x02E68000                 26.4 MB
memrsv=0xAEEB000@0x0A915000                174.9 MB
memrsv=680M@0x15800000:uc                  680.0 MB   ← CP/DSP 大块
memrsv=256M@0x80000000:uc                  256.0 MB   ← 整个第二 bank 也保留
─────────────────────────────────────────────
物理声明 1280.0 MB | 保留 1138.3 MB | ★ Linux 141.7 MB
```

### ZV-1M2

```
mem=1G@0                       物理 DDR 1024 MB
mem=224M@0x80000000@1          第二个 DDR bank 224 MB
memrsv=1M@0                                  1.0 MB
memrsv=0x8000@0xF00000                       0.03 MB
memrsv=0x1A5E000@0x02E68000                 26.4 MB
memrsv=0xB0EB000@0x0A915000                176.9 MB
memrsv=678M@0x15A00000:uc                  678.0 MB   ← CP/DSP 大块
memrsv=224M@0x80000000:uc                  224.0 MB
─────────────────────────────────────────────
物理声明 1248.0 MB | 保留 1106.3 MB | ★ Linux 141.7 MB
```

**⇒ 两者 Linux 可用内存字面相同：141.7 MB。实测 E10 的 MemTotal = 128,796 kB（内核镜像/页表等约占 13 MB）。**

## 二、由此得出的必然推论

```
appFw.so 的可写段 memsz = 81.7 MB（.bss 72.6 MB + .data 9.0 MB）
而 ZV-1M2 的 Linux 只有 141.7 MB —— 和 E10 一样
⇒ appFw.so 在 M2 上能跑
⇒ 其 .bss 必然是【惰性分配、绝大多数页从不触碰】
⇒ "文件 61.3 MB / memsz 81.7 MB" 不能作为"装不下"的依据
```

**⇒ 任何"换成 appFw 就 OOM"的推论都不成立。**

## 三、顺带发现的三处真实硬件差异（来自同一个 kemco.txt）

```
                    ZV-E10                        ZV-1M2
PCIe                pcie.ch0/ch1_enable=Y         无（完全没这几行）
                    pcie_ports=native ...
cfg.poff            10,5                         0,10
gpio.port           （无）                        gpio.port=0x0 gpio.bitmask=0x100
cmpr.mem            0x8000@0x15800000            0x8000@0x15A00000
warm.mem_dst        0xC00000@0x15900000          0xC00000@0x15B00000
warm.mem_work       0x600000@0x16500000          0x600000@0x16700000
```

- **ZV-E10 有 PCIe 通道配置，ZV-1M2 没有** —— 真实硬件差异，与菜单无关
- **`gpio.port=0x0 gpio.bitmask=0x100`（M2 独有）** = 开机时读 GPIO bit8 判断启动方式，
  这对应 M2 的**按钮式电源键**；E10 用拨动开关，走另一套 `cfg.poff`
- 这些都在 E10 自己的 kemco.txt 里，我们没动过 ⇒ 启动判定保持 E10 原生行为 ✓

## 四、结论与后续

```
✗ 内存不足  —— 已证伪（决定性数据见上）
✓ 仍成立  : 符号闭包完整、glibc 2.4 满足、软浮点 ABI 一致、
            入口签名一致、模块覆盖 = M2 原厂、资源齐全

⇒ 剩余唯一嫌疑: 运行时【成像/CP 协议】不一致
   M2 的 appFw.so 通过 LIRO/UIPC 与 CP(av-cam.bin) 交互，
   而 av-cam.bin 属平台层（必须配 E10 的传感器/镜头/ND 硬件）
   ⇒ 若 M2 的 appFw 在启动早期做了同步的 CP 握手，
     E10 的 CP 用另一套协议应答 ⇒ 阻塞 ⇒ 完全静默（无 UI、无日志）

⇒ 这条无法靠静态分析定论，也基本无法靠换文件绕过 ——
  除非能证明 M2 的 appFw 与 CP 协议在两个机型间兼容。
```
