# 新菜单的软件 GPE 替换点：GRM_* 全套（21 个）

## 一、结论

**ZV-1F 的 `appFw.so` 不使用 `mpgpe::*`**（那是 A7C II 的接口）。
它通过 **`ux::gfxeng::*`**（GraphicsEngine / Sprite / TextSprite / Node）绘图，
而该层的**底部**是 **ZV-1F `libObj.so` 里的 `GRM_gperm*`**。

## 二、与 ZV-E10 完全同名同大小

| 函数 | ZV-1F libObj | ZV-E10 libObj | 大小 |
|---|---|---|---:|
| `GRM_gpermFillrect` | `0x1178A5` | `0x683F4D` | **314** |
| `GRM_gpermRectblit` | `0x1179DF` | `0x684087` | **824** |

⇒ **两个固件的 GPE 底部接口是同一套**（大小完全一致）。

**含意**：
1. a7c2 的 `software_gpe`（`gpe-raster.c` / `gpe-sprite.c`）**改个符号名即可复用**
2. **一个 shim 设计同时服务老菜单和新菜单两条线**

## 三、完整替换面（ZV-1F libObj 导出 21 个 `GRM_*`）

### 绘制（核心 2 个）
```
GRM_gpermFillrect            0x1178A5  size=314
GRM_gpermRectblit            0x1179DF  size=824
```

### bitmap 管理（9 个）
```
GRM_bitmapCreate             0x1149F1  size=120
GRM_bitmapCreateFromPhys     0x114AF1  size=124
GRM_bitmapCreateComposite    0x114B6D  size=12
GRM_bitmapGetSubRectangle    0x114BB5  size=28
GRM_bitmapGetPhysicalAddress 0x114C45  size=16
GRM_bitmapWriteLockBits      0x114C85  size=16
GRM_bitmapWriteUnlockBits    0x114C95  size=16
GRM_bitmapRelease            0x114CB5  size=16
GRM_bitmapGetRowPixelCount   0x114DAF  size=16
```

### 帧缓冲 / YUV（9 个）
```
GRM_fbrmSetScreenPreference      0x115305  size=88
GRM_fbrmRegisterTriggerFrameworkCb 0x1153E1 size=12
GRM_fbrmInitializeYUV            0x1153F9  size=8
GRM_fbrmTerminateYUV             0x115401  size=8
GRM_fbrmUpdateYUV                0x115431  size=8
GRM_fbrmPrepareUpdateYUV         0x115421  size=8
GRM_fbrmFillrectYUV              0x115439  size=8
GRM_fbrmRectblitYUV              0x115441  size=8
GRM_fbrmCheckUpdateFinished      0x115371  size=24
```

### 取屏幕位图（1 个）
```
GRM_screenGetOnBitmap        0x1153D9  size=8
```

## 四、对"窗口模型"的最终判断（与移植侧一致）

- 真机 App **不直接写显存**：它把绘制命令填进 128KB 环，由 GPU 渲染
- ⇒ **软件渲染必须在 `GRM_gperm*` 这一层用 CPU 实现**（a7c2 的做法）
- ⇒ "fake 一个大窗口让 App 直接画"能出画面，但是**调试构造**，不是真机路径
- `dmpgles2` 应保持 **128KB 忠实契约**（`vm_pgoff==0`、单块环别名、`0x200`/`0x2000000` 保持 BUG）

## 五、更正记录（我自己错的两处）

1. **"arena 顺序切片 + 相邻 VA" 模型错误**：
   真驱动环是**单块 vmalloc(128KB)，init 时只分配一次**，每次 mmap 是**同一块的别名**。
2. **"48 个 128KB 块"是数字巧合，不是证据**：
   任何值除以 `0x20000` 都有商和余数；`0x6000E4 / 0x20000 = 48 余 0xE4` 是算术必然。
   真正有意义的是**两次余数都是 `0xE4`**，即偏离窗口末的常量偏移 —— 这一点三方一致。
