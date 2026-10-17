# 进程内探针（LD_PRELOAD）

## 为什么需要它

App 的 `stdout/stderr` 被重定向、日志走 ulogio 内存环缓冲 ⇒ **看不到它在干什么**。
本探针把 App 进程内的关键事件直接写到 `/dev/console`（串口可见）。

## 注入点（已实测）

三条路都试过，结论：

| 注入方式 | 结果 |
|---------|------|
| `/setting/mode/preload`（`bootin.elf` 会设 `LD_PRELOAD`） | ❌ 未生效（有条件判断） |
| 修改 rootfs `/etc/ld.so.preload` | ❌ 未生效（rootfs 的 `/etc` 被覆盖） |
| **`nflasha10`（= 真机 `/etc` 分区）根目录放 `ld.so.preload`** | ✅ **生效**（实测 5 个进程加载） |

```sh
# 把库放进 rootfs (nflasha7, ext2) 的 /lib/
debugfs -w -R "write libgrmlog.so /lib/libgrmlog.so" nflasha7.img
# 把 preload 条目放进 nflasha10 (vfat) 根目录
printf '/lib/libgrmlog.so\n' > ld.so.preload && mcopy -i nflasha10.img ld.so.preload ::/
```

## 探针覆盖点

| 探针 | 用途 |
|------|------|
| `GRM_gpermFillrect` / `GRM_gpermRectblit` / `GRM_bitmapWriteLockBits` 等 | 绘图边界（GPE 原语） |
| `dlopen` | 库加载序列与失败项 |
| `ux::gfxeng::GraphicsEngine::sys_getInstance` | UI 图形引擎是否初始化 |
| `OBJAVBBCBASECLASS::SeqController::CreateAndAddSequence` | 屏幕/序列是否创建 |
| `WlanAdjust::start` / `wlanStart` | 网络初始化（可改成"返回成功"桩） |

`GRM_*` 与上述 C++ 符号在 `libObj.so` 里都有 `R_ARM_JUMP_SLOT`
⇒ 全部可被 `LD_PRELOAD` 拦截（这是本机型能做进程内探针的技术前提）。

## 实测结论（关键）

```
GRMLOG alive            : 5        ← 探针已注入 5 个进程
GRMLOG GRM_*            : 0        ← ★ 从未调用任何绘图接口
GraphicsEngine          : 0        ← ★ 图形引擎从未初始化
CreateAndAddSequence    : 0        ← ★ 从未创建屏幕/序列
dlopen                  : 55~61 次，全部成功（无失败项）
viewUnified*.so         : 0        ← ★ View 层从未加载
```

**⇒ 「没有画面」不是因为缺少 GPU 模型，而是 UI 从未被创建。**
App 的初始化按序加载基础设施库，停在网络栈后不再前进。

## WiFi 桩实验（部分成功）

给 `WlanAdjust::start` 返回成功，App **越过 WiFi 又加载了 6 个库**
（`libWiredLanResource`/`libWiredLanAdjust`/`libNetServiceEther`/
`libInfraNetConnectionManagerEther`/`libInfraEthernetService`/
`libNetService`/`libInfraIPsec`），guest 时间 34.9s → 39.1s。

但**随后崩在 `uipc` 共享区**（`page permission fault @0x0479c800`）：
桩只返回 0、没有建立真实状态 ⇒ 后续用到未初始化数据。
**⇒ 半桩不是正确方向；要么完整模拟该服务，要么改走「强制创建屏幕」的路线。**

## 安全规则（血泪）

1. **绝不能有强未定义符号**（`extern void *dlsym(...)` 必须加 `__attribute__((weak))`）
   —— 否则没有 libdl 的进程（busybox/ash）会 `symbol lookup error` 直接起不来。
   这正是真机变砖的同款失败模式；QEMU 里只是丢个 shell，真机没有第二次机会。
2. **不要拦 `ioctl` 这类基础设施级调用**（实测：App 在 0xc 处提前崩溃）。
   ioctl 层面的观测请放到**内核**里做（`UIPCRET`/`UIPCARG`）。
3. 只拦"目标进程才会用到"的符号（GRM_*、dlopen、特定 C++ 入口）风险最低。
