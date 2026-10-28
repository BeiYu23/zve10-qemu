# 事故记录：ptrace 风暴导致内核 oops

**时间**：2026-10-26 凌晨
**影响**：相机 UI 卡死，需要重启
**责任**：我（AI）的 PC 采样器实现过于激进

---

## 一、发生了什么

为了实现「找出 im.elf 里真正在执行的代码」，我写了一个 PC 采样器：
- 枚举 `/proc/157/task` 下的**全部 201 个线程**
- 每轮对每个线程 `PTRACE_ATTACH` → `GETREGS` → `PTRACE_DETACH`
- 共 80 轮

**⇒ 约 16,000 次 attach/detach。**

**结果：**
- 采样到的 8192 个 PC **全部**在 `libpthread` / `libc`（说明线程都在阻塞等待，没采到绘制代码）
- **随后相机 UI 卡死**
- QEMU 侧报告：`dmesg` 被持续的 oops 刷屏，`Modules linked in` 表明内核出过错

## 二、根因

**相机的内核是 `3.0.27_nl-rt106+`（实时内核）。**

**对 201 个线程高频 ptrace attach/detach：**
- 每次 attach 都会让目标线程进入 stop 状态
- 大量线程被反复暂停/恢复，扰乱了实时调度
- **最终诱发内核 oops，显示管线（RTOS/GPU 侧）卡死**

## 三、★★★ 关键发现：其实**不需要 ptrace**

**同一天早前的测试已证明：**

```
open /proc/157/mem O_RDWR = 3
pread 8 = 2d e9 f0 4f 99 b0 14 46
pwrite(same bytes) = 8              ★ 直接写 /proc/PID/mem 成功
re-read, identical=YES ★
```

**⇒ `pwrite` 到 `/proc/<pid>/mem` **直接可用，完全不需要 ptrace**。**

**ptrace 那次只是因为顺带测试才跑的，而它成功了 —— 但它是**不必要的风险**。**

## 四、以后的硬性规则

| 操作 | 允许 | 说明 |
|---|---|---|
| **`pread`/`pwrite` 到 `/proc/PID/mem`** | ✅ **首选** | 实测可读写代码段，无内核风险 |
| **ptrace attach 单个线程，采样一次就 detach** | ⚠️ 谨慎 | 只在必须时用 |
| **ptrace 遍历全部线程** | ❌ **禁止** | 本次事故的直接原因 |
| **ptrace 循环 80 轮** | ❌ **禁止** | 放大风险 |

**★★★★ 补丁工具的 `test/apply/restore` 全部用 `pwrite` 实现，
不涉及 ptrace ⇒ 补丁本身是安全的（实测 `MATCH ★` 恢复成功）。**

## 五、恢复方式

```
① cam.py run "/bin/busybox reboot -f"     ← agent 还活着时
② 物理断电重启                             ← agent 也挂了时
③ 内存改动不持久 ⇒ 重启即完全恢复
```

**★ 本次使用 ① 恢复成功，相机重启后正常枚举为 `054c:0d95`。**

## 六、后续改进

**PC 采样如果还要做，必须改成：**
- **只采 1-2 个线程**（比如 im.elf 的主线程）
- **attach 一次，连续采 N 次，再 detach**（而不是反复 attach）
- **或者放弃采样，改用「patch 单个候选函数 + 观察」的方式逐个试**

**★★ 后者的代价更低：每次 patch 一个函数，看屏幕变不变，立刻恢复。
不需要碰 ptrace，也不会有内核风险。**
