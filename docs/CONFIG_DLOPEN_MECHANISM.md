# ★ `initializeConfig` 的真相：它是 **dlopen 配置模块**，不是自己读配置

> 回答 QEMU 侧："`0x3E014D` 该由谁回、回什么格式"

---

## 一、`initializeConfig` 的真实结构（`libObj+0x14BE85`，40 B）

```c
initializeConfig() {
    r4 = operator new(24);       // 0x14BE86
    configCtor(r4);              // 0x14BE90 → 0x14BD58
    *getConfigSlot = r4;         // 0x14BE98
    return;                      // 0x14BE9A
}
```

而 `0x14BD58` 在 `0x14BD90` 调用 **`0x14BC64`**，后者是：

```asm
14bc6c: r1 = 0x101                  ; dlopen flags
14bc74: blx dlopen                  ; ★ dlopen(路径, 0x101)
14bc78: [out] = r0
14bc7e: if (r0 == 0) { dlerror(); printf(...); return NULL; }
14bc92: blx dlsym(handle, 符号名)    ; ★ 再取符号
```

**⇒ `initializeConfig` = 加载一个「配置模块」并调用它的初始化入口。**

## 二、★ 它要加载的两个模块（字符串实证，位于 `0xE4D3B0` 附近）

```
'sym( %s ) failed - dlerror() = %s\n'
'libObj.so'          'initializeModelConfig'   'getModelConfig'
'viewUnified2.so'    'initializeViewConfig'    'getViewConfig'
```

⇒ 两条路径：
| 模块 | 初始化入口 | 取配置函数 |
|---|---|---|
| `libObj.so` | `initializeModelConfig()` | `getModelConfig()` |
| **`viewUnified2.so`** | **`initializeViewConfig()`** | **`getViewConfig()`** |

**失败时会 `printf` 到 stdout** —— **QEMU 侧应该去 guest 的 stdout/串口看有没有 `sym( %s ) failed`**

## 三、两个入口的实现（都很短）

```c
// viewUnified2.so @0x67D740
initializeViewConfig() {
    r4 = operator new(0x58);        // ★ ViewConfig 大小 = 0x58 (88)
    ViewConfig_ctor(r4);            // 0x67D6E4
    *global = r4;
}

// libObj.so @0x3C4350
initializeModelConfig() {
    r4 = operator new(8);           // ModelConfig 大小 = 8
    ModelConfig_ctor(r4);           // 0x3C432C
    *global = r4;
}
```

**`ViewConfig` 构造函数（`viewUnified2+0x67D6E4`）** 内部依次调用：
```
0x6235B8 → 0x67D5C6 → 0x48FFE4  (CmnViewProcessDataMgr 附近)
0x45C90C  (CmnViewModelWrpTouchPanel 附近)
0x5E4CE0  (CmnPanelCommonPattern 附近)
```
**⇒ 队列交互与应答解析就在这几条链里**（不在 `initializeConfig` 本身）。

## 四、回答你的三个子问题（Q3）

| 选项 | 判断 |
|---|---|
| (a) 特定 `type` 值 | **可能，但不是根因** —— 先确认是谁在收 |
| (b) 64 字节内某字段是返回码 | **很可能** —— 解析代码在 `viewUnified2.so` 的 config 链里 |
| **(c) 回错对象** | **★ 最可能** |

**理由**：`initializeConfig` 是 **dlopen 配置模块**，配置对象由**模块自己的构造函数**建立。
如果该构造函数要向某个服务请求配置，**那个服务才是应答方**，而不是任意一条队列。

⇒ **`0x3E014D` 是不是"配置服务"的队列，需要先确认它的 `port` 归属**
（`0x3E014D` = port `0x003E`、bit15=0 本地、local `0x014D`）。
你们已经能 `osal_stat_msgq(0x3E014D)` 看到 `[0,1,0,0]` —— 说明**队列存在、有 1 个客户端**。
⇒ **先查这 1 个客户端是谁**（谁注册的 CB），比猜包格式更有效。

## 五、★ 给 QEMU 的三条具体建议

### ① 立刻能做的事：interpose `dlopen` / `dlsym`，打参数
```c
void *my_dlopen(const char *p, int f) {
    log("DLOPEN %s flags=%x", p, f);
    void *h = real_dlopen(p, f);
    log("DLOPEN ret=%p %s", h, h ? "" : dlerror());
    return h;
}
void *my_dlsym(void *h, const char *s) {
    log("DLSYM  %s", s);
    return real_dlsym(h, s);
}
```
**能直接看出**：加载的是 `viewUnified2.so` 还是 `libObj.so`、哪个符号、成不成功。

### ② 看 guest stdout
失败会 `printf("sym( %s ) failed - dlerror() = %s\n", ...)`。
如果 `initializeConfig` 是**失败返回**（不是卡住），stdout 里应该有这行。

### ③ 定位 `0x3E014D` 的客户端
既然 `osal_stat_msgq` 显示 **1 个客户端**，就查这个客户端是谁注册的
（`osal_reg_msg_queue_cb(qid, cb, arg)` 三参数，你们已经确认过签名）。
**找到这个 cb 的所属模块，就知道应答该由谁产生、格式由谁定义。**

---

## 六、和上一轮结论的关系

上一轮我们说"`0x8A2E70` 没返回"。现在知道它卡在 `initializeConfig` 里，
而 `initializeConfig` 干的是 **dlopen + dlsym**。

⇒ **所以"卡住"很可能就是卡在 `dlopen` 里**（或 dlopen 出来的模块构造函数里）。
⇒ **`ENTER=1 EXIT=0` 与"卡在 dlopen"完全相容。**

**⇒ ① 的探针能立刻区分**：
- 看到 `DLOPEN viewUnified2.so` 但没有 `DLOPEN ret=...` ⇒ **卡在 dlopen 里**
- 看到 `DLOPEN ret=0x...` 正常 ⇒ dlopen 成功，卡在后面（模块 ctor 的队列等待）
