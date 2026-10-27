# 轮次 18：bind 方案核心机制的本机实证（不是推理，是跑出来的）

> 整个 bind 方案依赖三个 ld.so 行为假设。本轮在本机用最小复现逐条实证。

## 一、实验设计

```
v1  = 模拟 E10 的 libIMDB.so（函数返回 1），SONAME=libIMDB.so，放在 sysroot/lib/
v2  = 模拟 M2 的 libIMDB.so（函数返回 2），SONAME=libIMDB.so，放在别处
v3  = 模拟"bind 后磁盘上的新文件"（函数返回 3），替换 sysroot/lib/libIMDB.so
main      = DT_NEEDED libIMDB.so 的程序（模拟 im.elf 的 DT_NEEDED）
dlopen_t  = 按路径 dlopen("sysroot/lib/libIMDB.so") 的程序
            （模拟 im.elf 选项 Is=/usr/lib/libObj.so 那条路径 dlopen）
```

## 二、四个实验与结果

| # | 条件 | 结果 | 含义 |
|---|---|---|---|
| 1 | 正常加载（LD_LIBRARY_PATH 指向 v1） | 返回 **1** | 基线：拿到 E10 版 |
| 2 | `LD_PRELOAD=v2`（SONAME 同为 libIMDB.so） | 返回 **2** | ★ **SONAME 顶替成功，v1 根本不被加载** |
| 3 | `LD_PRELOAD=` SONAME 不同的库 | 返回 **2** | 额外收获：preload 符号优先级最高，仍有兜底 |
| 4 | `LD_PRELOAD=v2` + 按路径 dlopen 已被换成 v3 的文件 | 返回 **3** | ★ **按路径 dlopen 走文件系统查找，拿到 bind 后的新文件** |

## 三、为什么这四条恰好覆盖了 bind 方案的全部语义

```
bind 方案依赖的三个 ld.so 行为：
  ① im.elf 的 DT_NEEDED "libIMDB.so" 要能被 preload 的 M2 版顶替
        → 实验 2 证实：返回 2（M2 的值），E10 的 v1 未被加载 ✓
  ② im.elf 按路径 dlopen "/usr/lib/libObj.so" 要能拿到 bind 后的 M2 文件
        → 实验 4 证实：返回 3（磁盘新文件）✓
  ③ ①和② 互不干扰（都在同一个进程里发生）
        → 实验 4 同时有 preload(v2) 和路径查找(v3)，各自返回正确值 ✓

⇒ 三个假设全部成立，且是【实际执行】验证（返回的是函数值，不只是符号解析）
```

## 四、附带发现（有价值）

```
实验 3 表明：即使 SONAME 不同，LD_PRELOAD 对象的符号也会优先于 DT_NEEDED 库的符号。

⇒ 兜底价值：万一在真机上 SONAME 匹配出现意外（例如 ld-2.11.2 行为与新版有别），
  符号层面仍然会优先用 /log/libIMDB_m2.so 的实现，不会退回 E10 版。
⇒ 换句话说，这个方案比预想的更稳。

⚠️ 但仍要记住：ld-2.11.2（相机）是较老的 glibc，本轮实验用的是本机新版 glibc。
   SOAME 匹配是 glibc 长期稳定的行为，但不能 100% 排除老版本差异
   ⇒ 所以 bindm2 的自诊断日志仍然必要（它会报告 libIMDB.so 的实际尺寸）
```

## 五、轮次 18 结论

```
bind 方案的三个核心假设，从"推理成立"升级为"本机实证成立"：
  ✓ SONAME 顶替（实验 2）
  ✓ 按路径 dlopen 走文件系统（实验 4）
  ✓ 两者共存（实验 4）

剩下的不确定性只有：
  · 老 glibc（2.11.2）与新版的细微差异（自诊断可覆盖）
  · 真机上 loop/mount 的权限与可用性（bindm2 日志会逐条报告）
  · appFw 与 E10 CP 的运行时握手（只能真机验证）
```
