# `dmp_allocator` 断言的真因：**我们把关键内核模块 stub 掉了**

## 一、先纠正断言归属

| 项 | 值 |
|---|---|
| 断言所在 | **`libObj.so`**（不是 appFw.so！） |
| 函数 | **`dmp_allocator_lib_init`**，**导出符号**，`libObj + 0x11BFE5`，1032 字节 |
| 断言表达式 | `e == MA_ErrNone`（`libObj` 的 `.rodata`） |
| 文件串 | `dmp_allocator.c`、`gl_common` |

> QEMU 侧用 icheck/ncheck 得到 `/lib/appFw.so` —— **实测 appFw.so 里 `dmp_allocator` 零命中**，
> 全部在 `libObj.so`。

## 二、第 108 行到底在检查什么

```asm
11c05c: str  r0,[sp,#0]
11c060: str  r0,[sp,#4]
11c062: add  r3, r7, #0x3c        ; out 缓冲
11c066: ldr  r0, [r4, #0]         ; ctx
11c068: bl   0x12AC3C             ; ★ 返回非 0 ⇒ 断言
11c06c: cbz  r0, 11c07e           ; ==0 继续
11c070: movs r2, #108             ; dmp_allocator.c:108
11c07c: b    11c058               ; __assert_fail("e == MA_ErrNone", ...)
```

`0x12AC3C` 内部：

```c
if (!ctx) return -2;                 // 0xFFFF FFFE
e = sub_129764(ctx);                 // memcmp(ctx+4, <magic>, 4)
if (e) return e;                     // 不匹配 → 0xFFFF8182
return sub_129F90(ctx, a, b, c);     // 真正的工作
```

⇒ **断言失败 = 这个分配器上下文没被正确初始化（magic 不符 / 底层池拿不到）。**

## 三、★ 它到底依赖什么：`/dev/dmpgles2`

`libObj.so` 里 `dmp_allocator` 区域引用的设备路径：

```
/dev/dmpgles2      ← ★ DMP GLES 显存分配器设备节点
/dev/mem
```

该文件里的断言表达式还包括 **`mpool_0x12C000 != ((void *)0)`**
⇒ **分配器期望在固定地址 `0x12C000` 有一块已映射的池。**

**`/dev/dmpgles2` 由谁创建？**

```
grm_gles.ko   author=DMP/Sony
              src=Guiengine_161H/driver/grm_gles/./src/sugi_dev.c
              唯一含 "dmpgles2" 的 kmod
```

配套的还有 `grm_ma.ko`（`ma_memspace` / `ma_init` / `ma_res` 等 MA 分配器后端）
和 `dmm.ko`（Display Memory Manager，需 `DmmConfig`）。

## 四、★★ 根因：我们把 `backup` / `grm_ma` 换成了 stub

QEMU 侧为了绕过 upm 卡点，把 **`backup / accel / mag / compass / grm_ma / upm`** 全换成了最小 stub。

**但 a7c2 的做法恰恰相反 —— 他们加载真实的模块：**

```sh
insmod /usr/kmod/dmm.ko   DmmConfig=/setting/DmmCfg.bin
insmod /usr/kmod/grm_ma.ko
insmod /usr/kmod/backup.ko Backup_load=1 Backup_save=0      # 且先 cp seed-backup.bin /setting/Backup.bin
insmod /usr/kmod/stream.ko ; insmod /usr/kmod/stream2.ko
insmod /usr/kmod/utimer.ko
insmod /usr/kmod/osal_ulogio.ko ulogio_initfile=/setting/ulogio.bin
insmod /usr/kmod/devno.ko / osal_utm.ko / osal_uipc.ko
# 另有约 38 个模块来自原厂 initrc/insmod_drv2
```

**a7c2 的 shim 只做用户态 API 级拦截（GPE 绘图、OSD、DMM 服务），从不 stub 内核模块。**

⇒ 这解释了我们的两次崩溃：

| 崩溃 | 真因 |
|---|---|
| `MonDrv::Init` → `Failed to mmap!` → MonQueue NULL | **`backup.ko` 被 stub** ⇒ `Backup_read_4Byte` 拿到假地址 ⇒ mmap 失败 |
| `dmp_allocator_lib_init` line 108 断言 | **`grm_ma` / `grm_gles` 被 stub / 未加载** ⇒ 没有 `/dev/dmpgles2` 与 MA 池 |

## 五、混合镜像里这些模块都在（不用额外准备）

```
grm_gles.ko    15,648 B      grm_ma.ko   24,800 B
dmm.ko         85,588 B      backup.ko   73,764 B
stream.ko       3,384 B      stream2.ko   4,144 B
utimer.ko      20,156 B      osal_ulogio / osal_uipc / osal_utm 均在
```

ZV-E10 的 `/setting/DmmConfig.bin`（78,120 B）也在。

## 六、建议（按顺序）

1. **恢复真实 `backup.ko`**（`Backup_load=1`）+ 一份有效的 `/setting/Backup.bin`
   → `MonDrv::Init` 的 mmap 应自然成功 → **MonQueue interposer 变成不必要**
2. **恢复真实 `grm_ma.ko`，并补 `grm_gles.ko`、`dmm.ko`（带 DmmConfig）、`stream/stream2`**
   → 出现 `/dev/dmpgles2` → `dmp_allocator` 拿到池 → line 108 断言消失
3. **只保留 `upm` 的 stub**（它才是真正会卡的那个）
4. 若 `/dev/mem` 的 mmap 仍失败，再考虑内核 `CONFIG_STRICT_DEVMEM=n`

> 核心判据：**a7c2 靠"真模块 + 用户态 API shim"成功，不是靠 stub 内核模块。**
