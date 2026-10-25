# MWF::ObjSignal 语义（GetSignal / SetSignal）

> 依据：ZV-E10 `libMWF.so` 反汇编
> `ObjSignal::GetSignal(u32)` @ 0x2E90D（180 B）、`SetSignal()` @ 0x2E8E1（42 B）

## 一、对象布局（推断）

| 偏移 | 类型 | 字段 |
|---|---|---|
| `+0x08` | cond | `pthread_cond_t` |
| `+0x38` | ptr | `MWF::ObjLock *` |
| `+0x3C` | u8 | **信号标志位**（唯一决定"能否拿到信号"的字段）|

对象较小（约 0x40 字节）。

## 二、`SetSignal()` —— 唯一的置位方

```c
void SetSignal()
{
    ObjLock::Lock(this->[0x38]);
    if (*(u8 *)(this + 0x3C) == 0) {     // 只有未置位才动作
        *(u8 *)(this + 0x3C) = 1;         // 置标志
        pthread_cond_signal(this + 8);    // 唤醒
    }
    ObjLock::Unlock(this->[0x38]);
}
```

## 三、`GetSignal(u32 tmo)` —— 参数是**超时毫秒**，不是信号 ID

```c
int GetSignal(u32 tmo)
{
    ObjLock::Lock(this->[0x38]);

    if (*(u8 *)(this + 0x3C) != 0) { r5 = 0; goto consume; }   // 已置位 ⇒ 立即成功
    if (tmo == 0) { r5 = 110; goto consume; }                   // 立即超时

    if (tmo == 0xFFFFFFFF)                                       // ★ -1 ⇒ 无限等
        r5 = pthread_cond_wait(this + 8, lock + 4);
    else {
        gettimeofday(&tv);
        deadline = tv + tmo 毫秒;
        r5 = pthread_cond_timedwait(this + 8, lock + 4, &deadline);
    }

consume:
    *(u8 *)(this + 0x3C) = 0;                                    // ★ 成功与超时都清标志
    if      (r5 == 110) r5 = 5;                                  // ETIMEDOUT → 5
    else if (r5 == 0)   r5 = 0;                                  // 成功 → 0
    else if (r5 == 12)  r5 = 6;                                  // ENOMEM → 6
    else                r5 = 0xFFFF;
    ObjLock::Unlock(this->[0x38]);
    return r5;
}
```

**返回码映射即 errno 翻译：`0` 成功、`5` 超时（ETIMEDOUT=110）、`6` ENOMEM=12、其它 `0xFFFF`。**

## 四、★ 关键推论：伪造返回值没有意义

- **拿到信号 ≠ 业务状态就绪。`GetSignal` 只是"叫醒"，它不改变任何业务字段。**
- **`GetSignal` 每次返回都清标志 ⇒ 信号是一次性的消费语义。**
- **因此"外部看门狗周期性 SetSignal"会与调用方的消费竞速：
  置位 → 被消费清掉 → 状态仍不对 → 继续循环。**

**⇒ 伪造 `GetSignal` 返回 0 只会让调用方**空转得更快**，不会推进流程。**

## 五、正确排查方向

1. 打点「`GetSignal` 返回 0 之后调用方检查什么状态」——看它读哪个全局/字段
2. 追查该状态由哪个子系统置位
3. 只有当确认该状态在当前环境**永远不可能就绪**时，才考虑伪造那个状态字段本身
