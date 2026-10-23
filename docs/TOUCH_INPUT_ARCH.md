# ZV-E10 老菜单的触摸输入架构（给 QEMU 注入用）

> 目标：在 QEMU 里给旧 UI 加触摸。**纯离线分析**，无硬件操作。
> 所有符号与地址来自真机 `libObj.so`（21,305,528 B，
> md5 `12cf982dd3eaded426b0b9c703a681cd`，已与真机逐字节核对一致）。

---

## 一、结论先说：**UI 不走 evdev**

`libObj.so` 里**完全没有** `/dev/input`、`evdev`、`EVIOC`、`input_event` 之类的引用。

触摸走的是相机**自己的一套 IPC 事件**：

```
触摸面板驱动 (input.ko / i2c)
   → Event 码 EVENT_INPUT_TP_RAW
   → InputService::callback_tp_raw(int, _InfraInputEvent*)
        ├ snd_InfraInputEvent_to_InputServant(...)   ← UIPC 转发
        ├ callback_convert / callback_panel / ...
        └ pushFilterEvent(Event*)                    ← 进 UI 事件链
   → Widget 层按 ux::wgtsys::MouseEvent 处理（命中测试/点击）
```

⇒ **QEMU 不可能靠"加个 QEMU 输入设备"就让老菜单响应**，必须在事件层注入。

---

## 二、三个可注入点（全部是 `libObj.so` 的**导出符号**，可 `dlsym`）

| 层次 | 符号 | 偏移 | 说明 |
|---|---|---:|---|
| **UI 事件层（推荐）** | `InputService::pushFilterEvent(Event*)` | `0x0011e5d5` | 直接推一个 UI 事件进过滤链，最省事 |
| UI 事件层 | `InputService::createEvent(unsigned long, unsigned char)` | `0x0011c89b` | 构造 Event（code + flags） |
| 原始触摸层 | `InputService::callback_tp_raw(int, _InfraInputEvent*)` | `0x0011cb75` | 喂原始触摸，会走完整转换链 |
| 面板层 | `InputService::callback_panel(int, _InfraInputEvent*)` | `0x0011fae1` | 面板/感应区事件 |
| 辅助 | `InputService::forceReleaseTp()` | — | 强制释放触摸（避免卡住按下态） |
| 辅助 | `InputService::changeSlideZoomStatus(int)` | — | 滑动缩放状态 |
| 辅助 | `InputService::setNonSensingArea(int,int)` | — | 非感应区（边缘手势） |

**实例来源**（都是成员函数，需要对象）：
- `InputService::m_pMyInstance` —— **全局单例指针，数据符号，偏移 `0x01459548`**
- `InfraInput::GetInstance()` / `InfraInput::ReleaseInstance()` —— 另一套输入单例

⇒ LD_PRELOAD 探针里：读 `libObj` 基址 + `0x01459548` 得到 `InputService*`，
再按基址 + 偏移调用上面任一函数。

---

## 三、`_InfraInputEvent` 结构（从 `callback_tp_raw` 反汇编推断）

```
+0x00  void *  payload / 坐标块指针（代码里当基址用：adds r1, r6, #1/#2/#3）
+0x04  u16     X（r8）
+0x06  u16     Y（r9）
+0x08  u32     event code —— 传给 0x11c778 的内部判定函数，返回值是状态码
               （实测分支：1 / 2 / 3，猜测为 按下 / 移动 / 抬起）
```

⇒ 构造触摸序列时：`X/Y` + 一个 `code`，三态切换（1→2→…→3）。

---

## 四、给 QEMU 侧的推荐做法

### 方案 A（最快验证）：UI 事件层注入
```c
void *svc = *(void **)((char *)libObj_base + 0x01459548);
Event *ev = ((Event *(*)(unsigned long, unsigned char))
             ((char *)libObj_base + 0x0011c89b))(event_code, flags);
((void (*)(void *, Event *))((char *)libObj_base + 0x0011e5d5))(svc, ev);
```
优点：绕开原始 TP 的换算与感应区判定。
需要先确定"点击"对应的 `event_code`（可从 `callback_convert` 的事件表里取）。

### 方案 B（更真实）：原始 TP 层注入
构造 `_InfraInputEvent{X,Y,code}` 三态序列，调 `callback_tp_raw`。
优点：走完整链路（含 UIPC 转发、手势识别）。
风险：`code` 语义要试出来（用 1/2/3 三分支做 oracle）。

### 方案 C（最贴近硬件）：伪造 evdev/uinput
不推荐 —— UI 不读 evdev，`/dev/uinput` 只对走 evdev 的进程有用。

---

## 五、与"适配新菜单"的关系

新菜单（ZV-1F `appFw.so`）是**另一套 UI 架构**，输入层也不同（单体 appFw 内部）。
但两者都最终落到 `ux::wgtsys` 的控件层——`ux::wgtsys::Widget::calcLocalFlickSpeed(MouseEvent const&)`
在 `libObj.so` 里同样存在。

⇒ **"触摸注入"这件事可以先在旧菜单上打通**（本机就是旧菜单），
把它做成一个通用 shim，新菜单移植后同一套事件注入大概率可复用。

---

## 六、待确认（需要 QEMU 侧配合）

1. `event_code` 的具体取值表（点击 / 长按 / 滑动）——可从 `InputService::callback_convert`
   的分支里逆出来，或用一个 event_code 扫描做 oracle
2. `m_pMyInstance` 在 UI 起来之前是否为 NULL（需要在 `Application_System_Init` 之后才有效？
   —— 这正好和"view 建不起来"是同一个前置条件）
3. `pushFilterEvent` 是否依赖 ViewManager 已就绪（很可能依赖，因为要派发给当前 view）
