# 轮次 24：三次 mount 调用的全部参数在二进制级逐项验证

> 上一轮修掉了 r7 装载 bug。本轮继续查同类风险：
> **syscall 号对了，但参数顺序/内容错了同样会静默失败。**

## 一、验证方法

用 objdump 反汇编 `bindm2.so`，对每处 `svc` 追溯：
- `ldr rX,[pc,#imm]` + `add rX,pc` 的字面量 → 解出实际指向的字符串
- 立即数装载 → 解出 flags/data

## 二、三次 mount 的参数（全部正确）

### ext2 挂载
```asm
710: ldr r2,[pc,#1896]    ; → "ext2"
714: mov r1, r6           ; r1 = "/tmp/m2root"（旧 r6）
718: mov r0, r9           ; r0 = loop 设备路径
71c: mov r3, #1           ; MS_RDONLY
720: mov r4, #0           ; data = NULL
724: mov r6, #21          ; syscall = mount
728: add r2, pc, r2       ; r2 = "ext2"
72c: mov r7, r6           ; r7 = 21
730: svc 0
```
**解码结果：r2='ext2' ✓  r3=1(MS_RDONLY) ✓  r4=0 ✓  r7=21 ✓**

### 第一个 bind
```
r0 = '/tmp/m2root/lib'     ← source ✓
r1 = '/usr/lib'            ← target ✓
r2 = 0                     ← fstype = NULL ✓
r3 = 0x1000                ← MS_BIND ✓
r4 = 0                     ← data = NULL ✓
r7 = 21                    ← mount ✓
```

### 第二个 bind
```
r0 = '/tmp/m2root/share'   ← source ✓
r1 = '/usr/share'          ← target ✓
r2 = 0    r3 = 0x1000    r4 = 0    r7 = 21   ✓
```

## 三、由此确认的关键结论

```
sy5(n,a,b,c,d,e) → (r7,r0,r1,r2,r3,r4) 的参数映射
被【两个 bind 的完整解码】证实正确：
  源码 (SYS_mount, "/tmp/m2root/lib", "/usr/lib", 0, MS_BIND, 0)
  二进制 (r7=21, r0='/tmp/m2root/lib', r1='/usr/lib', r2=0, r3=0x1000, r4=0)

⇒ 同一个包装器的三次调用，参数传递全部正确
⇒ 参数顺序/内容这条风险排除
```

## 四、连续两轮的反汇编体检发现了什么

```
轮次 23: r7 装载 bug —— 三次 mount 里两次会用错 syscall（open 而非 mount）
轮次 24: 参数顺序验证 —— 全部正确，无问题

⇒ 说明"二进制级体检"这个方法是有效的：
   轮次 23 抓到了编译期就埋下、运行期才发作的静默错误
   轮次 24 确认了剩余部分没有问题
```

## 五、当前 bindm2 的二进制状态

```
bindm2.so  10,104 B  md5 5ceac0b6c7a98910
  · 34 处 svc，每处 r7 均在 asm 内部装载（已逐一验证）
  · 三次 mount 的五个参数全部正确（本轮验证）
  · 0 NEEDED / 0 未定义符号 / 0 外部重定位 / 0 VFP 指令
  · 幂等保护、多路径查找、自诊断均已就位
```
