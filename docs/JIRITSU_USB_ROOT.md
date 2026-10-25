# ZV-E10 的 Jiritsu USB Root 路径（对照 a6700-powerful / a7c2-re）

> 参考：`xyzhangxin/sony-a6700-powerful`（USB进入root教程）、`YuuLuo/a7c2-service-preload`（协议文档）、
> `chocotan/a7c2-re`（libJit 机制）
> 本文仅做静态核实：**ZV-E10 是否具备同一条链**。未在真机执行任何写操作。

## 一、结论

**ZV-E10 的 `libJiritsu.so`（175,092 B）与 A7C2 结构同构，具备相同的安全 root 路径。**

**关键区别：这条路**不写 `/etc/ld.so.preload`** —— 它是"往 `/tmp/jiritsu/libJit.so` 写一个 .so，
由固件自己 `dlopen`"。这正是 a7c2-re 从"会变砖的 preload 方案"切换到的安全方案。**

## 二、ZV-E10 `libJiritsu.so` 静态事实

### 导出的命令处理函数

```
jiritsu_init / jiritsu_exit / jiritsu_sus / jiritsu_res / jiritsu_inact / jiritsu_act
jiritsu_thread
cmd_mode / cmd_format / cmd_start / cmd_debug / cmd_test_ui_view / cmd_factory
adj_cmd_get_version
adj_cmd_set_file_path
adj_cmd_set_file      @ 0x56C0
adj_cmd_set_file2     @ 0x5B00      ← 与 A7C2 同名同职责
get_jiritsu_area_filename
```

### 导入的关键 API

```
dlopen / dlsym                        ← JIT 加载
sys_mount                             ← 挂载工作分区
DmmMgr::CheckUnitType / DmmMgr::dmm_alloc   ← 用 DMM 分配缓冲
sys_file_copy                         ← 复制载荷
```

### 相关字符串

```
/tmp/jiritsu/libJit.so                ← 载荷目标路径
/tmp/jiritsu/jit.log
/tmp/jiritsu/history.txt
```

### 三个 `dlopen` 调用点

| 位置 | 所属函数 | 上下文 |
|---|---|---|
| `0x5864` | `adj_cmd_set_file` | 路径 `strcmp` 之后 |
| **`0x5DA0`** | **`adj_cmd_set_file2`** | 挂载 + DMM 分配 + `get_access_filename` 之后 |
| **`0x76AC`** | **`IM_JIRITSU::CImJiritsu::jit_thread`** | **`sys_file_copy` 之后 `dlopen`** ← JIT 主路径 |

**`adj_cmd_set_file2` 里对 `jit.log` / `history.txt` 做 `strcmp` 是**排除项**（这两个路径跳过特殊处理），
不是 `libJit.so` 的触发条件 —— 触发在随后的 `dlopen` 分支。**

## 三、对照 a6700-powerful 的方法

其教程描述的链条：

```
MSC 身份发现相机
  → Senser 两阶段认证（054C:0E8A → 重新枚举 → 054C:0336 → 再次认证）
  → Jiritsu 文件命令发送载荷
  → SET_FILE 的 file_id=2 路径加载 /tmp/jiritsu/libJit.so
  → 构造函数以维护进程权限执行
  → 固定范围读取并经维护协议返回
  → STOP_SENSER 并释放接口
```

**✗ 不使用 `/etc/ld.so.preload`；✓ 普通 USB 数据线即可；✓ 不需要 Pico / Fastboot 硬件。**

## 四、安全对比（重要）

| 方案 | 是否写 `/etc/ld.so.preload` | 变砖风险 |
|---|---|---|
| a7c2-re 旧方案 | **是** | **高** —— `/etc` 与 `/tmp`/`/var` 同属 12 MiB 分区，写满后连"中和 preload"都会失败；**库本身损坏则 init 都起不来** |
| **libJit / Jiritsu** | **否** | 低 —— 只写 `/tmp/jiritsu`，不触碰 `/etc` |
| a7c2-service-preload 文档 | 描述了 preload 变体（路径穿越 → `/etc/ld.so.preload`）| 高，同第一条 |

**⇒ ZV-E10 应走 libJit 方案。**

## 五、前置条件与待核实项

**已知：**
- ZV-E10 Senser 服务身份预计同为 `054c:0336`（该 ID 属协议级，跨机型一致；A7C2 与 FX30 已验证）
- `/setting`、`/tmp/jiritsu` 等路径已在固件中存在

**未核实（需真机或进一步静态分析）：**
- ZV-E10 的 Senser 命令表中，`SET_FILE` 的 `file_id=2` 是否同样对应 `adj_cmd_set_file2`
- 载荷是否需要特定导出符号（`jit_thread` 路径可能要求 `CreateScriptI` 等，而 `set_file2` 路径不要求）
- 分区/挂载点是否同名（`/dev/nflasha6` → `/tmp/jiritsu`）

## 六、风险提示（照抄 a6700 / a7m4 项目的硬性要求）

- 刷写/写入前必须完整备份（全部分区 + SHA-256 + 离线副本）
- 更新类操作必须使用原厂电池
- 不要在写入过程中拔线或断电
- **不要把未校验的库留在武装状态**
- 有效签名、脚本成功、菜单出现，都**不等于**安全或功能完整
