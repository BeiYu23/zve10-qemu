# BootROM 串行启动协议 —— 完整规格

> 全部内容摘自上游 `ILCE-7M4-RE/scripts/push_payload.py` **源码原文**，非推测。
> ⚠️ **曾发生过的错误**：早期把 `MEM_WRITE` 记成 `0x0000`、`SET_UNLOCK` 记成 `0xfffe` ——
> **两个都错**。若按错值灌入，BootROM 完全不会写入 payload ⇒ 现象是"灌了没反应"，极难排查。

---

## ★★ 常量表（大字记住这四个）

| 常量 | 值 | 语义 | 易错点 |
| --- | --- | --- | --- |
| **`SET_UNLOCK`** | **`0x0000`** | 解锁码（特殊值） | ⚠️ **不是 `0xfffe`**！0 是特殊值 |
| **`MEM_WRITE`** | **`0x0001`** | 内存写 | ⚠️ **不是 `0x0000`**！0 会被当成 SET_UNLOCK |
| **`END_MARKER`** | **`0x0123`** | 空 payload 哑写；**重发第一条 = 会话结束** | 它本身也是一条内存写 |
| **`SET_ENTRY`** | **`0xFFFF`** | 加载并跳转；**value 字段 = 入口地址** | 只"设置"，不立即跳 |

**规则（源码注释原文）：** *"Anything that isn't 0 or ffff is interpreted as a memory write command"*
⇒ **任何不是 `0x0000` 也不是 `0xFFFF` 的值，都是内存写命令。**
⇒ 所以 `MEM_WRITE=0x0001` 只是"选了一个不是 0/FFFF 的值"。

---

## 帧格式

```python
def record(rtype, value, payload=b""):
    body = struct.pack("<IHI", 10 + len(payload), rtype, value) + payload
    return body + struct.pack("<I", zlib.crc32(body))
```

```
┌──────────────┬───────────┬──────────────┬───────────────┬─────────────┐
│ u32 len      │ u16 type  │ u32 value    │ payload       │ u32 crc32   │
│ = 10 + plen  │ SerCmd    │ 地址/入口/码 │ 最多 4086 B   │ 覆盖前面全部 │
└──────────────┴───────────┴──────────────┴───────────────┴─────────────┘
  小端（<）· len 不含 CRC · CRC 覆盖 [len..payload]，不覆盖自身
```

| 参数 | 值 | 说明 |
| --- | --- | --- |
| `LOAD_ADDR` | `0xFE020000` | payload 落点 |
| `MAX_RECORD_PAYLOAD` | **512** | 脚本保守值；**真实上限 4086** |
| `TRANSMIT_START` | `0.5` s | 前导码起始等待 |
| `RECORD_GAP` | `0.0144` s | 记录间隔 |
| 波特率 | **48000** | 串行启动链路 |
| 电平 | **3.15V · OPEN_DRAIN** | 不要 5V 推挽 |

---

## 记录序列（`boot_script`）

```
① record(END_MARKER 0x0123, value=0)                        ← 起始哑写（会话标记）
② for off in 0..len(payload) step 512:
     record(MEM_WRITE 0x0001, value=LOAD_ADDR+off, chunk)   ← payload 分条写入
③ [unlock bypass]
     record(MEM_WRITE 0x0001, value=0xFE02F398,
            unlock_code(4B) + (crc32(unlock_code) ^ 0xFFFFFFFF)(4B))
④ record(SET_ENTRY 0xFFFF, value=入口地址)                   ← ★ 只是"设置"入口
⑤ record(SET_UNLOCK 0x0000, value=unlock_code)              ← ★ 0x0000 才是 SET_UNLOCK
⑥ record(END_MARKER 0x0123, value=0)                        ← ★ 重发第一条 ⇒ 会话结束 ⇒ 此时才跳转
```

### ⚠️⚠️ 两个致命细节

**1. `SET_ENTRY` 只是"设置"入口，真正的跳转发生在最后那条 `END_MARKER`**
```
证据: 上游把 SET_ENTRY 放在 SET_UNLOCK 之前，若是立即跳转则 SET_UNLOCK 永远收不到
     + 注释: "A session is ended by re-sending the first command"
⇒ 灌入时【绝对不能省掉最后那条 END_MARKER】✗
   否则相机表现为"灌完了但什么也没发生" ⇒ 会被误判成 unlock 失败 ✗✗
```

**2. unlock 检查发生在跳转之前 ⇒ 检查不过 ⇒ payload 根本不会运行**
```
失败表现: 相机毫无变化（无害，因为 eMMC 写操作都在 payload 里）
成功表现: payload 跑了 → 修好 → 重启后相机正常
⇒ 两者现象可区分（但"unlock 失败"和"payload 跑飞"无法区分）
```

---

## unlock code（机型特定）

| 机型 | unlock code | 来源 |
| --- | --- | --- |
| a7M4 (CXD90057) | **bypass**：写 `0xdeadbeef` 覆盖栈上期望值（地址 `0xFE02F398`） | 上游默认 |
| A7IV | **`0x2CF25621`** | 源码注释 `# 0x2CF25621 for A7IV` |
| **ZV-E10 (CXD90045)** | **未知** | 已搜遍全部固件分区 + WBI，未找到 |

**⚠️ `0xFE02F398` 是 a7M4 BootROM 特定栈偏移**
（注释原文：*"requires a bootrom-specific offset (which may vary depending on the boot mode)"*）
⇒ CXD90045 可能不同。**第 1 次先试 a7M4 的**。

**⚠️ BootROM 串行启动协议是【只写】的 —— 没有读命令**
```
SerCmd 全集只有 4 种，无 peek/read
⇒ 无法通过串口读回 SRAM 里的结果码
⇒ 唯一观测方式 = 重启后相机是否正常
（除非能找到相机 TX 脚，用 RP2040 的 MODE_UART 转发）
```

---

## 两份记录流的区别（用途不同）

| 文件 | unlock 方式 | 用途 |
| --- | --- | --- |
| `aux_fix/boot_stream.bin` | **bypass**（`0xdeadbeef` + 栈覆盖） | **第 1 次尝试** |
| `aux_fix/records_unlock_a7iv.bin` | **`-u 0x2CF25621`**（直接给真码） | **第 2 次尝试**（第 1 次失败后） |

**两者只有第 ③/⑤ 步不同；其余记录完全相同。**
