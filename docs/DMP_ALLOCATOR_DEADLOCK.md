# DMP 分配器死锁：全局锁在无限等待 sem 时被长期占用

> 依据：ZV-E10 `libObj.so` 反汇编
> 关联：`docs/MEMORY_LAYOUT_CORRECTED.md`、`docs/QEMU_BOOT_PROGRESS.md`

## 结论

`grm_init` 的 worker 线程之所以卡住，是因为它在回调里进入 DMP 显存分配器的
引用计数生命周期，**在持有一把全局 mutex 的情况下**尾调用到 `dmp_allocator_lib_init`，
而后者开头就无条件等待 semaphore `0x18004B5`（timeout = -1）。

锁被长期占用 ⇒ 请求方、其它 worker、`initializeConfig` 线程全部堵在这把锁上。

## 链条

```
worker (tid=143)
  持有队列锁         (libObj+0x7D7C37)
  进入回调
  libObj+0x7857F0            引用计数 acquire
      pthread_mutex_lock(&dmp_lifecycle_mutex)      ← 持锁
      refcount 0 -> 1
      dmp_allocator_mmap_init()
          libObj+0x6BD110   只有 12 字节：
            push {r7,lr}; add r7,sp,#0
            ldmia.w sp!, {r7,lr}
            b.w dmp_allocator_lib_init@plt           ← 尾调用
              libObj+0x6BCB58   dmp_allocator_lib_init (1032 B)
                6bcb5c  ldr  r0, =0x018004B5
                6bcb60  mov.w r1, #0xFFFFFFFF
                6bcb64  blx  osal_wai_sem_tmo             ← 无限等待
      （永不返回）⇒ 全局锁永不释放
```

## 为什么会连锁堵死

`0x7857F0` 与配对的 `0x785880`（释放）都用同一把全局 mutex 保护引用计数：

- acquire：`mutex_lock` → `refcount++` → 首次调用 `dmp_allocator_mmap_init()` → `mutex_unlock`
- release：`mutex_lock` → `refcount--` → 归零调 `libObj+0x6BCF60`（`dmp_allocator_lib_init+0x408`）→ `mutex_unlock`

首次初始化发生在**持锁区间内**，所以初始化一旦阻塞，所有 acquire/release 都排队。

## 修复方向

不要只绕过 semaphore —— `dmp_allocator_lib_init` 有 1032 字节，sem 只是第一步。
更彻底的做法是让整个 init 立即返回，从而缩短锁的持有期：

```c
/* 方案 A（推荐，粒度小） */
extern "C" int dmp_allocator_mmap_init(void) { return 0; }

/* 方案 B（更彻底） */
extern "C" int dmp_allocator_lib_init(void) { return 0; }
```

两者都是 `libObj.so` 的 GLOBAL DEFAULT 导出符号，调用点走 PLT，可被 LD_PRELOAD 拦截。

## 已知代价

DMP 分配器会处于"假就绪"状态。后续真正申请显存的地方会拿到无效结果，
可能崩在更远处 —— 但那时死锁这一层已经清掉，问题性质变了：
从"多线程互锁"变成"单个子系统没实现"，可定位性高得多。

## 判据

`grm_init` 的 `pthread_cond_wait` 是否被 signal —— worker 一旦返回就会 signal。
