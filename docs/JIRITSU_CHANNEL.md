# ZV-E10 一代：JIRITSU 命令通道已定位（本轮突破）

> 目标：复刻 `YuuLuo/a7c2-service-preload` 的方法（Senser 服务模式 → Jiritsu 文件命令 → `ld.so.preload` → root）。
> 本轮把**真正的命令表**从相机固件里挖出来了，并确定了 MOUNT / SET_FILE_PATH 已经打通。

---

## 1. 关键突破：真正的命令表在相机侧的名字表里

之前一直在 `block 0x600–0x60f` 里敲门（那只是 backup/terminal 服务，只有 `(0x603,5)`/`(0x603,6)` 放行）。
真正的命令表在 **`libsencore.so`（Senser 核心）** 的命令名表里，记录格式：

```
{u16 f1; u16 osal_id; u16 block; u16 code; char name[..]}
```

同格式的旁证：`adjstctl.elf` 里 `{0x01d7,0x0003,0x0404,0x00fe,"RAM_READ"}`
→ 我们打 `(0x0404,0x00fe)` 果然 **res=0 被接受** ✅（字段顺序得到验证）

### JIRITSU 完整命令表（block = **0x1001**）

| 命令 | code | 状态 |
| --- | --- | --- |
| JIRITSU_MODE | 0x4001 | |
| **JIRITSU_MOUNT** | 0x4002 | ✅ 实测 `00000000` 成功 |
| JIRITSU_FORMAT | 0x4003 | ⚠️ 危险，勿碰 |
| JIRITSU_START | 0x4004 | |
| **JIRITSU_DEBUG** | 0x4005 | ← A7C2 的 **root 代码执行触发点** |
| JIRITSU_FIRMUP | 0x4006 | |
| JIRITSU_SET_FILE | 0x4007 | |
| **JIRITSU_GET_FILE** | 0x4008 | res=0 但返回错误码（结构未对） |
| JIRITSU_FILE_DELETE | 0x4009 | |
| JIRITSU_GET_FILELIST | 0x400a | res=0 + 错误码 |
| JIRITSU_GET_FILE_HASH | 0x400b | res=0 + 错误码 |
| JIRITSU_COMPARE_MEDIA | 0x400c | |
| JIRITSU_GET_VERSION | 0x400d | res=0 + 0x10000006 |
| JIRITSU_FACTORY | 0x400e | |
| **JIRITSU_SET_FILE_PATH** | 0x400f | ✅ 实测 `00000000` 成功 |
| JIRITSU_SET_FILE2 | 0x4010 | |
| JIRITSU_GET_FILE2 | 0x4011 | |
| JIRITSU_FILE_DELETE2 | 0x4012 | |
| JIRITSU_TEST_UI_VIEW | 0x0013 | |

wire 帧 = PMCA 的 AdjustControl：`PFUNC=0x40` + `{int16 block(0x1001); int16 code}` + payload。

## 2. 已确定的请求结构细节（从 `libJiritsu.so` 反汇编）

- `adj_cmd_set_file_path`：读 `[req+20]` 当长度，要求 `2 ≤ len ≤ 513`，否则回 **0x10000201**
  → 实测 `payload[0]=0` 时 SET_FILE_PATH 回 `00000000`（成功，含 `/tmp/jiritsu/../..` 穿越路径）
- `adj_cmd_get_file`：
  - `[req+28]` = 模式（0/1/2 三分支）
  - `[req+29]` = 区域索引 → `get_jiritsu_area_filename()` 映射成文件名
  - mode1 分支：`r5=[req+20]-3`，然后 `memcpy(buf, req+31, r5)` → **路径从 payload 偏移 3 开始**
  - mode0 分支：`sys_mount(...)`（**ramdisk**）→ 失败即回错误码
- 错误码是**完整 32 位常量**（在函数字面量池里），与实测响应逐字节吻合：
  `0x10000201` 长度/参数错、`0x10000004`、`0x1000000f`、`0x10000006`、`0x10000015`、`0x10000008`、`0x1000000a`、`0x10000020`

### 区域索引 → 文件名（`get_jiritsu_area_filename`）

| idx | 文件 |
| --- | --- |
| 1 | `/tmp/jiritsu/tmmc.jit` |
| 2 | `/tmp/jiritsu/jit.log` |
| 0/3/4 | 其它 `/tmp/jiritsu/*`（字符串未完全解析） |

即 **Jiritsu 的工作区就在 `/tmp/jiritsu/`**，与 A7C2 论文描述一致。

## 3. 还没打通的最后一步

**payload 字节 → 请求结构体字段的映射（由 Senser 核心填装）尚未确定**。
即：我们知道 `[req+20]/[req+28]/[req+29]/[req+31]` 的含义，但不知道我们的 payload[0..N] 对应到 req 的哪个偏移。
（目前"payload[0]=模式"是经验推断，不是定论。）

→ 下一步：反汇编 `libsencore.so` 里把 Senser 包转成插件请求的那段
（`SubsystemCommunicationCommand` / `HostCommunicationCommand` / `CreatePluginCommand`），
拿到确切的偏移映射，然后 `GET_FILE` 就能返回文件内容。

## 3.5 字节位置 oracle（本轮新结果，方法已验证）

对 `GET_FILE` / `SET_FILE_PATH` 各发 20 字节 payload，逐位置置 1：

| 观察 | 结论 |
| --- | --- |
| `GET_FILE`：全 0 → `0x10000006`；**只有 `payload[0]=1` → `0x1000000a`**；其余位置无变化 | **payload[0] 是那个"模式/类型"字节**（可能对应 `[req+28]`） |
| `SET_FILE_PATH`：全 0 → `00000000` 成功；**只有 `payload[0]=1` → `0x10000201`**（长度检查失败） | payload[0] 同时也影响 set 家族读的 `[req+20]` → **两个家族拿到的偏移可能不同**（`libJiritsu.so` 里 set-file / get-file 是两个独立 switch） |
| `GET_FILE` mode1 的错误码随 payload 长度变化（L=2 → `0x0f`；L=20 → `0x0a`） | mode1 分支确实用 `size` 字段做 `memcpy(buf+len, req+31, size-3)` |

⇒ 下一步要么**整段逆 `libJiritsu.so` 的插件入口**（看它从 packet 里按什么偏移取字段），
要么继续用"单字节 oracle + 长度扫描"把 `size/area/path` 三个字段的位置全部标定。

---

## 4. 打通后的路线（都在同一条通道上）


```
GET_FILE  → 读 /dev/nflasha*  → 整机分区备份（配 verify_zve10_backup.py）
SET_FILE  → 写 .so + 写 /etc/ld.so.preload
JIRITSU_DEBUG (0x4005) → root 子进程加载 .so → root 代码执行
```

## 5. 本轮产物（宿主 `<host>:/path/to/your-tools/`，容器内 `<repo>/` 同名）

| 脚本 | 用途 |
| --- | --- |
| `pmca_probe_zve10.py` | 分阶段：devices / senser / ops / run / stop（服务模式进出） |
| `svc_dump_backup.py` | 读 HASP / backup status / CH88701 子芯片镜像（1.18 MB）|
| `adjstart_test.py` | ADJSTART 前后权限对比（结论：不变） |
| `jiritsu_probe*.py` / `jiritsu_matrix.py` / `jiritsu_read.py` | Jiritsu 命令探测与结构矩阵 |
| `updater_mode.py` / `updater_checkguard.py` / `updater_state.py` | 更新模式路线（结论：第 4 代 RSA 签名，自定义 body 不可行）|
| `extcmd_probe.py` / `extcmd_sweep.py` | EXTCMD 通道枚举（组 1/2/4/6 = 信息/日志/备份计数/镜头）|

## 6. 重要事实（避免走回头路）

- **CXD90045 = 第四代**（fwtool：`AesCbcCrypter` = 4th gen；`CXD90014` = 3rd gen）。
  ⇒ FDAT 有 RSA-2048 签名、工具不支持重新加密 ⇒ **PMCA「自定义 updater body → 工厂 shell（PULL/RMEM/SHEL）」对这台机器结构性不可行**（实测 `TEST` 一直 Invalid Command、`checkGuard` 报 Invalid version）。
- 更新模式（`054c:03e2`）只提供**官方写固件协议**，没有任意读。
- 分区是明文（ext2/vfat），**拆机直读 eMMC** 始终是最稳的兜底。
