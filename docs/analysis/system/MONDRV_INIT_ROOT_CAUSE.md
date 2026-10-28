# 根因：`MonDrv::Init()` 失败 → `MonQueue` 未构造 → `Monitor::Init` 打印时 NULL 崩

> QEMU 侧已还原调用链：`Monitor::Init` → `MonLib::ConPutStr` → `MonDrv::PutsNoCR`
> → `MonDrv::PutcharSub` → `MonQueue::IsNearlyFull(NULL)`。
> 本文给出 `MonDrv::Init()`（`libMWF+0x311F0`）的完整行为与判据。

---

## 一、`MonDrv::Init()` 做了什么

```asm
311f6: new(28)                      ; 分配锁对象
31204: MWF::ObjLock::ObjLock()      ; 构造
31212: 存入全局                      ; MonDrv 的状态对象
31218: 另一个全局 = 0
3121c: Backup_read_4Byte(0xFA0028)  ; ★ 取「物理地址」
31222: Backup_read_4Byte(0xFA005D)  ; ★ 取「缓冲区大小」
31228: movw r1, #0x1002             ; O_RDWR|0x1000
31232: open("/dev/mem", 0x1002)     ; ★★ 打开 /dev/mem
31258: cmp r5, #-1
3126c: → puts(...) 并【提前返回】     ; fd == -1 失败路径
3127c: mmap(fd, size, RW, SHARED, 0, 物理地址)
31280: cmp r0, #-1
3128e: → puts(...) 并【提前返回】     ; mmap 失败路径
31290: 缓冲区 = 映射基址 + 0x53400
```

### 解出来的字符串（全部已核实）

| 用途 | 内容 |
|---|---|
| **open 的路径** | **`/dev/mem`** |
| fd == -1 错误 | **`ERROR : Failed to open stream driver!`** |
| mmap 失败错误 | **`ERROR : Failed to mmap!`** |
| 成功后的打印 | ` Physical Addr : %.8X` / ` Logical  Addr : %p` / ` Mon Buf Size  : %.8X` |

## 二、★ 最快的判据：直接看串口日志

`MonDrv::Init()` 在成功/失败时都会打印，**三种情况一目了然**：

| 串口出现 | 结论 |
|---|---|
| `ERROR : Failed to open stream driver!` | **`open("/dev/mem")` 失败**（CONFIG_DEVMEM / 权限 / 节点缺失） |
| `ERROR : Failed to mmap!` | **物理地址 mmap 失败**（地址不在 QEMU 已映射 RAM 内，或 CONFIG_STRICT_DEVMEM 限制） |
| ` Physical Addr : ……` + ` Mon Buf Size : ……` | **`MonDrv::Init` 成功** ⇒ 根因在别处 |

⇒ **不用写探针、不用改镜像**，翻一眼启动日志就知道走哪条分支。

## 三、两版 libMWF 用的是同一批 backup ID

```
ZV-1F  MonDrv::Init @0x311f1   备份 ID 0xFA005D（另有 0xFA0028）
ZV-E10 MonDrv::Init @0x311a5   备份 ID 0xFA005D（另有 0xFA0028）
```

⇒ 混合镜像用 ZV-1F 的 libMWF 去读 ZV-E10 的 `BACKUP.BIN`，**ID 是对得上的**，
所以"取到错的备份数据"这条可能性较低。

## 四、若日志显示 mmap 失败，重点查物理地址

`Backup_read_4Byte(0xFA0028)` 返回的物理地址，必须落在 QEMU 已映射的 RAM 里。
我们 `bionz.c` 的 CXD90045 机器只映射：

```
DDR0  0x00000000 - 0x40000000 (1 GiB)
DDR1  0x80000000 - 0xC0000000 (1 GiB)
SRAM  0xFE000000 - 0xFF000000
```

**若 backup 给出的地址在这之外 ⇒ `mmap(/dev/mem)` 必然失败。**
此时的修法不是改固件，而是：**在 QEMU 机器模型里把该物理区间也映射进 RAM**
（和 a7c2 给虚拟 scanout 预留 `[0x41e100000, 0x41e600000)` 是同一类做法）。

## 五、给 QEMU 侧的结论

1. **先看串口日志**判断是哪条分支（零成本）
2. 若是 open 失败 → 查内核 `CONFIG_DEVMEM` / `/dev/mem` 设备节点
3. 若是 mmap 失败 → 打印 `Physical Addr` 的值，对照上面三个区间
4. **不建议**用 `-nostdlib` 探针去主动调 `MonDrv::Init()`：
   它是 C++ 成员函数、内部走 libc，跨 glibc 版本调用风险高；
   而且**日志已经能给出同样信息**
