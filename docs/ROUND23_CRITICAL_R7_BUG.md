# 轮次 23：★ 发现并修复一个会让 bindm2 在真机上必然失败的致命 bug

## 一、问题

`bindm2.c` 的 syscall 包装器原本这样写：

```c
static long sy5(long n,long a,long b,long c,long d,long e){
    register long r7 __asm__("r7")=n;        // ← 问题在这里
    register long r0 __asm__("r0")=a;
    ...
    __asm__ volatile("svc 0":"+r"(r0):"r"(r7),"r"(r1),..." :"memory");
    return r0;
}
```

**反汇编发现：三次 mount 调用里，只有第一次（ext2 挂载）正确设置了 `mov r7, #21`；
两个 bind 调用处的 r7 残留的是上一次 `open` 的值 5。**

```
修复前（实测）:
  svc@0x08c8 (第一个 bind): 最近的 r7 写入 = 0x079c "mov r7, #5"   ← 5 = open！
  svc@0x08f4 (第二个 bind): 同样 r7 = 5
⇒ 两个 bind 会执行 open()，而不是 mount()
⇒ 整个方案在真机上必然失败，而且日志会显示奇怪的 errno
```

## 二、根因

```
r7 是 AAPCS 的 callee-saved 寄存器。
GCC 因此认为：跨函数调用 r7 不会变 ⇒ 可以在三次 mount 之间只装载一次。

但我自己的 logkv / logsz / logmsg 等函数内部用 r7 做 syscall（open=5），
它们没有恢复 r7 ⇒ r7 被污染成 5 ⇒ GCC 完全不知道。

⇒ 这是"内联汇编使用固定寄存器 + 跨函数假设"的经典陷阱
```

## 三、修复

```c
/* 把 r7 的装载放进 asm 内部，并声明为 clobber */
static long sy5(long n,long a,long b,long c,long d,long e){
    register long r0 __asm__("r0")=a;
    ...
    __asm__ volatile(
        "mov r7, %[nr]\n\t"
        "svc 0"
        : "+r"(r0)
        : [nr]"r"(n), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
        : "r7","memory");
    return r0;
}
```

## 四、修复后验证（指令级）

```
724: mov r6, #21              ← 编译器把常量存进 r6（callee-saved，跨调用安全）

ext2 mount:
  72c: mov r7, r6             ← 紧邻 svc
  730: svc 0

第一个 bind:
  900: mov r3, #4096          ← MS_BIND
  90c: mov r7, r6             ← 紧邻 svc
  910: svc 0

第二个 bind:
  930: mov r3, #4096          ← MS_BIND
  93c: mov r7, r6             ← 紧邻 svc
  940: svc 0
```

**⇒ 每次 svc 前都从 r6 装载 r7，r6 由编译器保证不被中间调用破坏。**

### 全量统计

```
34 处 svc，能静态解析出编号的 20 处：
  read=3 x4   open=5 x12   lseek=19 x1   mount=21 x1   ioctl=54 x2
  ⇒ 全部正确，且全部在 svc 前 1 条指令装载
剩余 14 处位于 sy/sy5 函数体内（r7 来自参数 r0，由调用方设置），属设计如此
```

## 五、为什么这个 bug 特别危险

```
① 它不会编译报错、不会加载失败、不会产生明显日志
② 表现会是：loop 挂载成功了（第一次 mount 是对的），
   但两个 bind 静默失败（实际调用了 open）
   ⇒ bindm2.log 会显示 bind /usr/lib=<某个 open 的返回值>
   ⇒ 从日志看不出是"调错了 syscall"
③ 如果没有这一轮的反汇编核对，明天真机测试会浪费一轮
   去排查"为什么 bind 没生效"
```

## 六、教训

```
凡是用内联汇编指定固定寄存器（r7/r0/...）：
  · 装载必须放在 asm 内部
  · 该寄存器必须列入 clobber 列表
  · 否则 GCC 的 callee-saved 假设会与你的实际行为冲突，
    产生"看起来对、实际错"的代码

同类风险也存在于 beacon2.so —— 它同样的写法，但只在一个函数里用一次 r7，
  没有跨调用复用的场景，因此没触发。为稳妥也应一并检查/修复。
