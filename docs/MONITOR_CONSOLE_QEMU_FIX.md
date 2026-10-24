# `MonDrv` 在 QEMU 里必失败：a7c2 的官方解法 + 我们的实际情况

## 一、a7c2 踩过同一个坑，并写下了结论（源码原文）

`a7c2-re/runtime/analysis/menu-observe.c`（`MonLib monitor-console guard` 注释）：

> **MonDrv::Init fails in QEMU (/dev/mem absent at devtmpfs and unusable via mknod
> without the kernel mem driver), leaving the console queue pointers NULL; any later
> MonDrv output then SEGVs inside libMWF+0x409d0.**
> **libMWF calls its own exports through the PLT, so redirecting the queue probes to
> "queue full" reproduces the real camera's drop-output behavior safely.**

⇒ **他们的结论：不要去修 mmap，直接让控制台"输出即丢弃"。**
⇒ 而且**关键前提**：libMWF 通过 **PLT** 调用自己的导出 ⇒ **interpose 有效**。

### 他们的 interposer 清单（照抄即可）

```c
int  _ZN8MonQueue6IsFullEv      (void *q){ if(!q) return 1; /* 满 → 丢弃 */ }
int  _ZN8MonQueue12IsNearlyFullEv(void *q){ if(!q) return 1; /* 满 → PutcharSub 丢字符 */ }
int  _ZN8MonQueue7IsEmptyEv     (void *q){ if(!q) return 1; }
unsigned long _ZN8MonQueue14GetQueueRemainEv(void *q){ if(!q) return 0x40; }
int  _ZN8MonQueue7EnQueueEh     (void *q, unsigned char v){ if(!q) return -1; }
```

> 注意：这四个/五个函数**只返回常量，不需要 libc** ⇒ 用 **`-nostdlib -shared -fPIC`**
> 编译即可加载进 glibc 2.11 的进程，**完全绕开之前那个 `ld-linux-armhf.so.3` 的 ABI 问题**。

---

## 二、我们的情况比 a7c2 好一点，但结论相同

| | a7c2 | 我们（ZV-E10 QEMU） |
|---|---|---|
| `open("/dev/mem")` | **失败**（devtmpfs 里没有，也 mknod 不出来） | **成功**（`Failed to open stream driver!` = 0 次） |
| `mmap` | 不可达 | **失败**（`Failed to mmap!` 每次都有） |

⇒ 我们能 open 但不能 mmap。真机内核含 `arch/arm/mach-cxd900x0/mmap.c`，
老内核上 `/dev/mem` 对 **System RAM** 的限制（`page_is_ram` / STRICT_DEVMEM 系列）
很可能就是拦截点；而真机的监视器缓冲大概率在**保留区/IO 区**，所以真机不受影响。

⇒ 两条路：
- **A（推荐，成本最低）**：照 a7c2 做 **MonQueue interposer**，让控制台输出变 no-op
- **B**：在机器模型里划一段**客机 RAM 之外**的映射区（a7c2 的虚拟 scanout
  `[0x41e100000, 0x41e600000)` 就是这个思路），再让 backup stub 返回该区间地址

---

## 三、`Backup_read_4Byte` 的 ID 语义：查不到，也**不需要**查

| 事实 | 结论 |
|---|---|
| `0xFA0028` / `0xFA005D` 在 `Backup.bin` 里**没有任何字面量** | 不是"文件内偏移" |
| 这两个 ID 同时出现在 `libMWF` / `libmpr` / `libmprctrl` / `appFw` / `viewUnified2` | 是**跨组件的符号化备份数据 ID** |
| `Backup_read_4Byte(id)` → `backup_sync_msg(out, ?, size=4, op=1, id)` → 打包成 64 字节消息 | 真正的 ID→偏移映射在 **backup 服务任务**里，不在客户端库 |

⇒ 想要"真机同款物理地址"必须逆整个 backup 服务任务，**投入产出比很低**：
监视器缓冲**只是调试控制台**，内容无意义，**只需要"合法"而不需要"真实"**。

---

## 四、给 QEMU 侧的最终建议（按顺序）

1. **先做 A**：`-nostdlib` 编 5 个 MonQueue interposer，NULL 时返回"满/丢弃"。
   这一条**同时解决新老两条线**（两版 libMWF 的 MonQueue 代码相同，只差 0x4C 字节）。
2. 若想保留控制台输出，再考虑 B（额外映射区 + stub 返回该区间地址）。
3. **不要**去逆 backup 格式——不值得。

## 五、这条修好之后意味着什么

`Monitor::Init()` 的打印不再崩 ⇒ 启动链可以**继续往下走**到真正的 UI 初始化。
而且这是**平台层**修复，**老菜单线同时受益**。
