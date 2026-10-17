# aux_fix — 盲操作容错版最小修复 payload（CXD90045 / ZV-E10）

> 目标：在**看不到任何输出**的情况下，安全地让 `/etc/ld.so.preload` 失效
> 产物：`payload.bin`（6,744 B，入口 `0xFE020000`，BootROM 串行启动兼容）

## 设计原则（安全第一）

```
★ 任何一步验证不通过 ⇒ 【一个字节都不写】⇒ 相机状态不变 ⇒ 零风险
★ 只有 100% 确认找到 "ld.so.preload" 的目录项，才清它的 size 字段（+28..+31 = 0）
★ 写后读回校验；失败则尝试恢复原值
★ 幂等：重复运行安全（size 已为 0 时不做任何写）
```

## 执行流程

```
1) SDHCI 初始化（失败 ⇒ 直接退出，不写）
   reset → 1-bit → 400kHz → CMD0/1/2/3/7 → POWER_CONTROL=0x0F → INT_ENABLE=0x0033
2) 扫全部 32 个根目录块（0x56004 ~ 0x56023）
   ⇒ FAT12 根目录起始扇区(分区内)=4，512 项 × 32B = 16KB = 32 块
   ⇒ 每块扫 16 个目录项，找 LFN 含 "preload" 或短名 LD+PRE
3) 安全校验（任一不满足 ⇒ 拒绝写）
   · size == 0          ⇒ 已修过（幂等），不写
   · attr 是卷标/目录/空 ⇒ 不写
   · size > 1MB          ⇒ 不合理，不写
4) 打印快照 + 清 size 字段（+28..+31 = 0）→ 写回 1 个扇区
5) 读回校验 size==0
   · 成功 ⇒ result=0xA5
   · 失败 ⇒ 尝试恢复原值 ⇒ result=0xE5
```

## 结果码（写在 SRAM `0xFE02F000`：`[magic="ZV10"][code]`）

| code | 含义 |
| --- | --- |
| `0xA5` | 成功（或已经是 0，幂等） |
| `0xE1` | 32 个根目录块里没找到目标（**未写任何字节**） |
| `0xE2` | SDHCI/MMC 初始化失败 |
| `0xE3` | 读块失败 |
| `0xE4` | 写块失败 |
| `0xE5` | 写后读回校验失败（已尝试恢复原值） |

## LED 反馈（最终版）

LED 地址来自 ZV-E10 内核源码和真实 cmdline `error.led=0,0`：

```
GPIO 基址 : 0xF2910000
LED port  : 0, bit 0（低有效）
设输出    : write bit0 -> 0xF2910004
点亮      : write bit0 -> 0xF2910048
熄灭      : write bit0 -> 0xF2910044
```

执行顺序：

```
1 次 1 秒长闪       = LED 自检（确认地址/方向可见）
3 次 80ms 快闪      = payload 已跳转并开始执行
等待 1 秒           = 区分启动心跳和结果
2 次 500ms 长闪     = 结果帧标记
N 次 150ms 短闪     = 结果码
停 3 秒后重复       = 无限播报，直到断电
```

| 短闪次数 N | 结果码 | 含义 |
| --- | --- | --- |
| 1 | `0xA5` | 成功，或已经修复（幂等） |
| 2 | `0xE1` | 没找到目标，未写入 |
| 3 | `0xE2` | MMC 初始化失败 |
| 4 | `0xE3` | 读块失败 |
| 5 | `0xE4` | 写块失败 |
| 6 | `0xE5` | 写后校验失败，尝试恢复 |
| 7 | 其它 | 未预期结果 |

**结果播报在统一 `done:` 出口循环执行；任何调用 `set_result()` 的失败路径都会进入同一播报。**

## ✅ VM 验证结果（三个路径全过）

| 测试 | 结果 |
| --- | --- |
| **正常路径**（镜像含 preload） | `FOUND at lba=0x00056008 entry=3 LFN=[ld.so.preload]` → `orig size=0x14` → 写入 → `VERIFY OK: size=0` → `result=0xA5` |
| **主机侧校验** | `mdir` 显示 0 字节 ✓；12MB 分区**只有 1 个字节变化**（`0x00107C: 0x14→0x00`）✓ |
| **幂等性**（再跑一次） | `size already 0 -- nothing to do (idempotent OK)` → 不再写 ✓ |
| **安全路径**（干净镜像无 preload） | `NOT FOUND in 32 root blocks -- no write, camera unchanged` → `result=0xE1`；主机侧确认**改动字节数 = 0** ✓ |

## 串行启动协议参数（供 push_payload.py 使用）

```
落点/入口 : 0xFE020000
记录分条  : 512 B/条
记录格式  : [u32 len][u16 type][u32 value][payload][u32 crc32]
type      : 0x0000=SET_UNLOCK  0x0001=MEM_WRITE  0xFFFF=SET_ENTRY  0x0123=END_MARKER
记录流 1  : 19 条 / 7,018 B（bypass 版）⇒ @48000 baud 约 1.74 秒（含 gap）
记录流 2  : 18 条 / 6,996 B（`-u 0x2CF25621` 版）⇒ @48000 baud 约 1.72 秒（含 gap）
unlock    : 默认 bypass（往 0xFE02F398 写 0xdeadbeef+CRC）
            ⚠️ 该偏移是 a7M4 BootROM 特定值，CXD90045 可能不同
```

## 栈与 unlock 地址的兼容性

```
start.S 设 sp = 0xFE02F000（向下增长）
unlock bypass 写 0xFE02F398（在 sp 之上）
⇒ payload 的栈不会覆盖 unlock 值 ✓
⇒ 结果码写在 0xFE02F000（sp 之上、unlock 之下）✓ 也安全
```

## 与旧版（minimal_fix 4,908 B）的差异

| 项 | minimal_fix（旧） | **aux_fix（本版）** |
| --- | --- | --- |
| 根目录扫描 | 只读 1 个扇区（0x56008） | **扫全部 32 个块** ✓ |
| 找不到时 | 仍会尝试写 | **零字节写入** ✓ |
| 幂等性 | 无 | **有**（size==0 时不写）✓ |
| 属性/size 合理性校验 | 无 | **有** ✓ |
| 写失败恢复 | 无 | **尝试恢复原值** ✓ |
| 结果码 | 无 | **写入 SRAM + LED 无限播报** ✓ |
| 体积 | 4,908 B | **6,744 B**（LED 自检、心跳、循环结果播报） |
