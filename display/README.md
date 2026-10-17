# 显示链路：现状、边界与探针

本目录放"让画面出来"这条线上**可复用的代码与结论**。

## 显示链路全貌

```
机身 App (im.elf)
  └─ libObj.so (21MB, UI 主库)
       ├─ ux::gfxeng          图形引擎（GraphicsEngine / Screen / Sprite / TextSprite）
       ├─ andromeda           场景图（Window / Canvas / Scene3D / Node2D）
       │                      Render2DWalker / Render3DsWalker —— ★ 纯软件遍历器
       ├─ stella / libra      主题 / 资源
       └─ GRM_*               图形驱动接口（本文件的主角）
             │
             ▼
       /dev/dmpgles2  ──►  grm_gles.ko ("SUGILITE Driver")  ──►  Mali GPU
```

### 已确认的事实（有日志/符号证据）

- **`libObj.so` 是被加载的**：`[16.858105] IMOPEN OK /usr/lib/libObj.so`
- App 目前只打开 `/dev/uipc`、`/dev/mem`、`/dev/usb/kikilog`
  ⇒ **尚未 `open("/dev/dmpgles2")`**，即还没进入显示初始化
- `GRM_*` 这一组函数**定义在 `libObj.so` 内部**（不是外部导入），
  它们就是 UI 与图形驱动之间的**边界**
- UI 层的遍历器（`andromeda::Render2DWalker` / `OffScreenRender3D`）是**纯软件**的
  ⇒ **出画面不必然需要建模 Mali**

## GRM 边界（hook 点）

| 函数 | 作用 |
|------|------|
| `GRM_gpermFillrect` | 矩形填充（GPE 原语） |
| `GRM_gpermRectblit` | 矩形 blit（GPE 原语） |
| `GRM_bitmapCreate` / `GRM_bitmapWriteLockBits` | 位图创建 / CPU 写入加锁 |
| `GRM_bitmapGetRowPixelCount` / `GRM_bitmapGetPhysicalAddress` | 位图几何与物理地址 |
| `GRM_screenGetOnBitmap` / `GRM_fbrmSetScreenPreference` | 屏幕/帧缓冲关联 |
| `GRM_fbrm*YUV` 系列 | YUV 帧缓冲操作 |

**思路**：把 `GRM_gpermFillrect` / `GRM_gpermRectblit` / `GRM_bitmapWriteLockBits`
换成"软件实现 + 记录"，就能在不建模 GPU 的前提下拿到**真实的绘制指令流与像素**，
再推给 QEMU 的 `ramfb` 显示出来。

## `libgrm.c` —— GRM 录制桩（Phase 1）

一个**零依赖**（只用 `svc 0` 系统调用）的 `libgrm.so` 实现，用于：

- 被 `LD_PRELOAD` 或 `dlopen` 注入 App
- **录制** App 实际调用了哪些 GRM 函数、参数是什么
- 先把"指令流"摸清楚，再决定软件实现的范围

构建（示意）：

```sh
arm-buildroot-linux-gnueabihf-gcc -shared -fPIC -nostdlib -O2 -o libgrm.so libgrm.c
```

> 说明：这是一个**探针**，不是完整实现；它的价值在于把"未知的绘图接口"
> 变成"可枚举的调用序列"。

## 三条可选路线与取舍

| 路线 | 做法 | 工作量 | 备注 |
|------|------|--------|------|
| **A（推荐）** | 软件 GPE（Fill/Rectblit）+ 位图 CPU 写入 + `ramfb` 提交 | 中 | 社区 a7C II 工作已验证同类路径；本机型的对应物就是 `GRM_gperm*` |
| **B** | 直接驱动 `andromeda::Render2DWalker` 把场景图软件光栅化 | 中~大 | 绕开 GRM，但需要理解 Node2D 树与资源（`libra::ImageData`） |
| **C** | 建模 Mali / SUGILITE GPU | 研究级 | 不建议作为第一步 |

## 当前最大的未知

App 在 ~33s 之后**不再产生任何 console 可见活动**（但进程仍活到 70s+），
且始终没有打开显示设备。需要先回答：

1. 它卡在哪一步？（等待某个服务？等待 CP/CMOS？）
2. 是否在等待实机才有的硬件响应？

**⇒ 下一步最高优先级：拿到 App 的内部日志**（内存环缓冲
`klog.addr=0x035A5000 size=0x20000`、`alog.addr=0x03524FC0 size=0x60040`）。
拿到之后，路线 A/B 的实施才有明确起点。
