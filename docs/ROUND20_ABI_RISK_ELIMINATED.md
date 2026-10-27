# 轮次 20：三个运行时库的 ABI 风险全部澄清为零

## 一、发现的疑点

相机原厂库都是**软浮点**（armel，无 `Tag_ABI_VFP_args`），
而我的工具链是 `armv7-eabihf`（硬浮点）。
如果 ld.so 因 ABI tag 拒绝硬浮点 `.so`，整个 bind 方案会**静默失败**。

## 二、实测数据

| 库 | `Tag_ABI_VFP_args` | 来源 |
|---|---|---|
| 相机 `libc-2.11.2.so` | **无** ⇒ 软浮点 | 原厂 |
| E10 `libObj.so` | **无** ⇒ 软浮点 | 原厂 |
| M2 `libIMDB.so` | **无** ⇒ 软浮点 | 原厂 |
| `agent_v14.so` | **VFP registers** ⇒ 硬浮点 | 我编的，**真机跑通过** |
| `bindm2.so` | **VFP registers** ⇒ 硬浮点 | 我编的 |
| `beacon2.so` | **VFP registers** ⇒ 硬浮点 | 我编的 |

**⇒ 关键事实：`agent_v14.so` 是硬浮点，但它在真机上确实跑通了（既成事实）。
   ⇒ 相机 ld.so（`ld-2.11.2`）不会因为 ABI tag 拒绝硬浮点 `.so`。**

## 三、为什么能安全混用：看"有没有跨边界的浮点参数"

硬浮点 ABI 的差异只体现在**函数调用的浮点参数传递方式**（VFP 寄存器 vs 栈）。
如果没有浮点参数跨越库边界，ABI tag 就无关紧要。

实测三个库的"外部调用面"：

```
bindm2.so
  动态未定义符号     : 无
  外部符号重定位数   : 0          ← 完全不调用任何外部函数
  VFP 指令数         : 0          ← 连一条 VFP 指令都没有
  NEEDED             : 0
  ⇒ 100% 自包含的裸 syscall 代码（open/ioctl/mount/lseek/read/write）
  ⇒ ABI tag 完全无关紧要

libIMDB_m2.so
  与 M2 原版【逐字节相同】(md5 91eca62f9cae6ee2e7ee34c31002a968)
  ⇒ 与原厂库同源，零风险

beacon2.so
  同样是 0 NEEDED / 0 未定义符号的裸 syscall 代码
  ⇒ 零风险
```

## 四、结论

```
三个库的 ABI 风险:
  bindm2.so        → 0（自包含，无外部调用，无 VFP 指令）
  libIMDB_m2.so    → 0（原版拷贝）
  beacon2.so       → 0（同 bindm2）

⇒ 浮点 ABI 这条潜在风险，从"担心"变成"已排除且理解原因"

顺带确认: CPU 是 cortex-a5，带 VFPv3-D16
  ⇒ 即使将来需要编带浮点的库，硬件也支持
```

## 五、这条为什么值得一提

```
这类"静默失败"最危险：如果 ld.so 拒收 .so，
  · preload 只是不生效，不会报错到我们能看见的地方
  · 表现会是"bindm2.log 完全没有输出" ⇒ 很容易误判成"im.elf 没跑"
现在有了两条独立保障：
  ① 实证：agent_v14.so 先例证明硬浮点可加载
  ② 结构：bindm2.so 零外部调用，ABI tag 无实际影响
```
