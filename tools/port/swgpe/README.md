# 软件渲染 shim（ZV-1F 新菜单）

把十轮反汇编分析的结论固化成一份**可直接编译**的 shim。
目标：让 App 的绘制走 CPU，而不是等一个不存在的 GPU。

## 编译

```bash
arm-buildroot-linux-gnueabihf-gcc -nostdlib -shared -fPIC -O2 \
    -o libswgpe.so swgpe_shim.c
```

**关键**：`-nostdlib` 必须保留 —— 它保证没有任何 `DT_NEEDED`，
从而不会引入 `ld-linux-armhf.so.3` 这类工具链专属加载器依赖
（相机用的是 `//lib/ld-linux.so.3`，glibc 2.11）。

**自检**：
```bash
nm -D libswgpe.so | grep GRM_
# 必须看到未修饰的名字：
#   GRM_gpermFillrect
#   GRM_gpermRectblit
#   GRM_bitmapWriteLockBits
#   GRM_bitmapWriteUnlockBits
#   GRM_bitmapCreateFromPhys
#   GRM_fbrmCheckUpdateFinished
# 若看到 _Z16GRM_gpermFillrectv 之类 ⇒ 用了 C++ 编译且没加 extern "C"，无效
readelf -d libswgpe.so | grep NEEDED   # 应为空
```

## 部署

```sh
# /usr/bin/im.elf 换成 shell 包壳（不要用 ELF 包壳，会引入加载器依赖）
mv /usr/bin/im.elf /usr/bin/im.elf.real
cat > /usr/bin/im.elf <<'SH'
#!/bin/sh
LD_PRELOAD=/usr/lib/libswgpe.so exec /usr/bin/im.elf.real "$@"
SH
chmod 755 /usr/bin/im.elf
```

## 预期的分阶段现象（用来定位问题）

| 阶段 | 现象 | 说明 |
|---|---|---|
| 1 | 串口出现 `SWGPE loaded` | shim 加载成功 |
| 2 | 出现 `CREATEPHYS ...` | App 走到位图创建 ⇒ **shim 机制生效** |
| 3 | 出现 `GPERM_FILL`（若加日志） | 走到绘制 |
| 4 | 屏幕出现规则色块 | **成功** |
| 5 | 色块颜色不对 | 位域/通道序需调（用纯绿测） |
| 6 | 花屏/斜条 | stride 或 bpp 错 |
| 7 | 画几帧后静默卡死 | 计数下溢或仍有等待路径 |

## 已知未完成项（代码里已标注）

1. **`GRM_bitmapCreateFromPhys` 只做探针，返回 0**
   —— 参数语义未完全确定。若 App 依赖返回值，需要改成
   `dlsym` 拿原函数再调（见下方"回退方案"）
2. **`GRM_gpermRectblit` 只记录不绘制** —— `RectBlitParam` 的 `+0x04/+0x08` 待实测
3. **16bpp 位域按 RGBA4444 假设** —— 需纯绿测试确认是 565 / 5551 / 4444
4. `+0x14` 计数只打印不维护（`Release` 会减它）

## 回退方案（若 CreateFromPhys 必须真实执行）

```c
/* -nostdlib 下也能用 dlsym —— 它本身就是加载器提供的 */
extern void *dlsym(void *, const char *);
#define RTLD_NEXT ((void *)-1)
void *GRM_bitmapCreateFromPhys(...)
{
    static void *(*real)(...);
    if (!real) real = dlsym(RTLD_NEXT, "GRM_bitmapCreateFromPhys");
    return real ? real(...) : 0;
}
```
（注意：`-nostdlib` 时 `dlsym` 需要自己声明，如上。）

## 纯绿测试（一次定格式）

在 `GRM_gpermFillrect` 里强制用固定色填一次：

```c
static const u8 kGreen[4] = { 0, 0xFF, 0, 0 };
fill_rect(bmp, kGreen, 0, 0, 16, 16);
```

然后读 `pix` 前 2 字节：

| 读到的值 | 格式 |
|---|---|
| `0x07E0` | RGB565 |
| `0x07C0` | RGBA5551 |
| `0x03E0` | ARGB1555 |
| `0x0F00` | RGBA4444 |

**为什么用纯绿**：绿在四种布局里位段各不相同；红在 565 与 5551 里同位，无法区分这两者。
