# 轮次 10：QEMU 可行性评估 + 全目录覆盖审计

## 一、QEMU 测试台评估：不可用（结论：放弃）

### 资产齐全
```
qemu-system-arm (machine zve10)  101 MB  ✓
相机内核 Image                   242 MB  ✓
ui.cpio（完整相机用户态）        239 MB  ✓  1292 项，含 sbin/init、
                                          bootin.elf、im.elf、libIMDB、libObj、global.xdb
```

### 但基线启动就崩
```
Linux version 3.0.27_nl-rt106+ (root@cokee-mini) gcc 6.4.0 #10 ... Oct 27 21:12
Memory: 1024MB total, 800836k available      ← 内核起来了
... 第 873 行:
  [ZZMOVMAP] begin/end
  [ZZR7] r7=9f8027e0 NOT-IN-ANY-VMA
  [ZZGOT] ip=fffffff8 r3=003dd13c
  vector exception (0x000) at 0x00000000      ← 跳到地址 0
  Entering exception monitor.
用户态完全没开始（bootin / im.elf / libIMDB 出现 0 次）
```

**⇒ 这个内核是团队自己插桩的调试版本（ZZDBG/ZZMOVMAP/ZZR7/ZZGOT），当前状态就是崩的。**

### 而且 QEMU 那条战线的结论对我们不适用

`QEMU_BOOT_PROGRESS.md`（20,158 行、392 轮）的最终状态：

```
✅ 显示后端全通（GPE → streamfb → 屏幕能画）
✗ App 不产生任何绘制
根因: libObj(UI框架) → libBizFw.so 的 SeqController → GPE
      SeqController 构造需要 ObjActiveBaseAvbbc + GlobalAccessor
```

**但实测：`ObjActiveBaseAvbbc` / `SeqController` / `GlobalAccessor` 在 M2 的库里【全都没有】。**

```
⇒ QEMU 那条线逆向的是【E10 老架构】（libBizFw + BBC/序列层）
⇒ 新菜单用的是完全不同的架构（M2 的 UXEngine + appFw）
⇒ QEMU 的结论不能迁移到本任务
```

**⇒ 决定：不做 QEMU 验证，避免陷入另一条 392 轮的战线。**

## 二、顺带确认：v9 里缺 libBizFw.so 是正确

```
libBizFw.so   E10=318,780   M2=不存在   v9=不存在
libBizFw2.so  E10=249,964   M2=不存在   v9=不存在

它是 E10 的 libObj.so（21 MB 老引擎）的 DT_NEEDED；
v9 用的是 M2 的 libObj.so（3.9 MB 图形引擎），不需要它
⇒ 缺失正确 ✓
```

## 三、全目录覆盖审计（M2 的每个文件，v9 是否有）

| 目录 | M2 文件 | v9 有 | v9 缺 | 说明 |
|---|---:|---:|---:|---|
| `bin` | 42 | 51 | 0 | |
| `codec` | 6 | 6 | 0 | |
| `kmod` | 103 | 103 | **1** | `usb_max14648.ko` |
| `lib` | 149 | 171 | **0** | ✅ 完整覆盖 |
| `sabin` | 86 | 77 | **14** | M2 特有 DSP 固件 |
| `sbin` | 4 | 4 | 0 | |
| `scenario` | 29 | 33 | 0 | |
| `share` | 359 | 395 | **0** | ✅ 完整覆盖 |
| `tool` | 5 | 4 | **2** | libefence / libmcheckb |
| `wifi-scripts` | 12 | 12 | 0 | |
| `wlan` | 2 | 2 | 0 | |

### 17 个缺失项逐条解释（全部正确）

```
kmod/usb_max14648.ko   MAX14648 是 M2 的 USB 芯片驱动；E10 用 usb_darwin.ko
                       在 E10 硬件上加载 M2 的驱动是错的

sabin/*.bin ×14        M2 特有的 DSP 算法固件：
                       sa_bfnr sa_yknr sa_contplus sa_motionvideo_u0/u1
                       sa_one_shot_3d sa_camdsp_bayer sa_gm sa_iro sa_boks
                       sa_nflash2 sa_dm_af_dirdet sa_test_intr_c0a
                       dcv_psf_table_dualrec
                       这些要配 M2 的 CP/传感器；我们的 CP 是 E10 的，
                       按 E10 的 77 个名字取

tool/libefence.so      我们故意删的（Electric Fence 会拖慢 UI；
tool/libmcheckb.so     同时它们是 preload 白名单占位，要求"存在但不会被真正加载"）
```

**⇒ 没有任何无法解释的缺口。v9 在既定分层内完备。**

## 四、轮次 10 结论

```
· QEMU 不是可用的验证工具（内核崩、且其 RE 结论属另一架构）
· v9 的全目录覆盖审计：17 个缺口全部有正当理由
· 至此静态分析的所有维度都已收敛

⇒ 必须上真机。工具已就绪且经过代码复查。
```
