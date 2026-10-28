# 轮次 13：显示通路与 loop 支持的双重确认（两个潜在硬墙被排除）

## 一、★ 显示通路：M2 与 E10 用的是同一个设备

```
M2 的 libObj.so（3.9 MB 图形引擎）引用:
    /dev/dmpgles2   /dev/fb0   /dev/ldec   /dev/stream   /dev/tp   /dev/gpio
E10 的 libObj.so（21 MB 老引擎）引用:
    /dev/dmpgles2   /dev/fb0   /dev/ldec   …（同一套）
```

**真机 baseline 证据（之前从活相机抓的）：**
```
E10 的 im.elf (PID 157):
   fd 28 -> /dev/dmpgles2
   maps:  0b833000-0b853000 rw-s 00000000 ... /dev/dmpgles2   ← 显示缓冲已 mmap
   内核线程: irq/76-dmpgles2
```

**⇒ M2 的图形引擎走的是同一个 `/dev/dmpgles2` ⇒ 绘制后端大概率能工作。**

**这条很重要**：此前的担心是"M2 的 UXEngine 可能需要 M2 特有的显示通路"。
实测两版引用的是同一批设备节点，且真机上该节点已经在用。

## 二、★ loop 支持：E10 自己的库就在用 loop 设备

```
E10 的 libObj.so 引用: /dev/loop6  /dev/loop7
```

**⇒ 相机系统本身就用静态编号的 loop 设备（6/7）。**
**⇒ 我的 `bindm2` 把 `/dev/loop7` 作为退路是【有先例的】**
**⇒ loop 驱动确实启用、设备节点确实存在**

（内核侧也有独立证据：`loop: module loaded`、`loop_init`、`loop.max_loop`、`loop%d`）

## 三、一处需要留意的差异（大概率非致命）

```
M2 的 appFw.so 引用的 /dev:
    /dev/mem  /dev/mmca  /dev/mmcb  /dev/nflasha23  /dev/nflasha30
    /dev/random  /dev/urandom

其中 /dev/nflasha30 在 E10 上不存在
（E10 的 /proc/partitions 只有 1,2,3,4,5,6,7,10,11,12,13,15,16,17,18,23,B0,B1）
/dev/nflasha23 存在（PMBP/PMHOME，64 MB），E10 的 im.elf 本来就打开着它（fd 74）
```

**⇒ `/dev/nflasha30` 打不开会是 ENODEV；从命名看属于某个可选/调试分区，
   最坏情况是相关功能不可用，不会阻塞 UI。记录备查。**

## 四、对 bind 方案的进一步支撑

```
已知条件汇总（全部实测/反汇编确认）:
  ✓ loop 驱动存在，且系统自己在用 loop6/loop7
  ✓ M2 图形引擎用 /dev/dmpgles2（E10 已在用）
  ✓ M2 libIMDB 的 SONAME 与 E10 相同，无版本冲突，构造函数无副作用
  ✓ IMDB 装载器 = dlopen(路径) → dlsym(符号) → 调用，失败容忍
  ✓ im.elf 按路径 dlopen /usr/lib/libObj.so（选项 Is 的默认值）
  ✓ bind 在构造函数里执行，早于 im.elf 的 main ⇒ 早于所有运行时 dlopen

⇒ bind 方案的每一个技术环节现在都有独立证据
```

## 五、轮次 13 结论

```
又排除了两个潜在硬墙：
  ① 显示通路（同一设备）
  ② loop 支持（系统自己在用）
新增一处待观察点：
  /dev/nflasha30 不存在（预计非致命）

静态分析可做的已接近穷尽，下次必须真机。
```
