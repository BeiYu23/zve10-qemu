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

## 六、谁真正 SetSignal（libObj 全局统计）

| 方法 | 调用点数量 |
|---|---:|
| `GetSignal` | **30** |
| `SetSignal` | **2** |
| `ClearSignal` | 0（`GetSignal` 返回时自清）|

**30 : 2 的比例说明信号是「事件驱动置位」，不是业务代码直接置的。**

### 两个 `SetSignal` 调用点都在错误/中止路径

| 站点 | 所属函数 | 特征 |
|---|---|---|
| `0x95335A` | `0x9532A8`（448 B）| 含 `79` / `65` / `0x7000` 等常量，命令/诊断路径 |
| `0xA0A97E` | `0xA0A934`（172 B）| 先 `MonCtrl::ConPrintfMessage` 打日志、带错误码 `0x23E`，**出错后置信号叫醒等待者** |

### 正常置位机制：`EventHandlerUseSignal`

```c
MWF::EventHandlerUseSignal::EventHandlerUseSignal(ObjSignal *sig)   // libMWF @0x2A080, 40 B
{
    MWF::EventHandler::EventHandler(this);
    this->[0x00] = EventHandler_vtable + 8;
    this->[0x04] = sig;          // 绑定 signal
}
```

**事件到达时由处理器（虚调用）去 `sig->SetSignal()`。信号 = 「某个事件到了」的抽象。**

## 七、★ 结论：手动 SetSignal 不能推进流程

**等待者等的是**事件**。手动置位只是把它们叫醒，醒来检查状态发现没变，随即回到等待。**

**⇒ 这也解释了「外部看门狗周期性 SetSignal 无效」。**

## 八、正确的排查顺序

```
① 按信号对象分组，看各等待线程分别在等哪个 obj
② interpose EventHandlerUseSignal 构造函数，记录 (handler, signal) 配对
③ 从 handler 的 vtable 反查它处理哪类事件
④ 追该事件源为何不产生事件
```

**最便宜的判据：若某个信号对象**根本没有** handler 绑定，
说明「该创建 handler 的代码没跑」—— 那才是明确缺失的环节。

## 九、已证伪的假设

**曾怀疑 `dlopen(viewUnified2.so)` 卡在其构造函数里 `0x5D5000` 的无限 `osal_wai_sem_tmo`。
QEMU 侧实测：无 patch 运行时**所有** osal 无限等信号量都有返回（S=81 / E=81）⇒ 假设不成立。**
