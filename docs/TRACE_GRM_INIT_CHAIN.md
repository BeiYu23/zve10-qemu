# App 初始化卡点的完整调用链（GDB 实证）与 0x3E014D 服务应答

**日期**：2026-10-26
**方法**：a7c2-re 路线——不伪造内部完成，而是**跟着真实调用链往下走**，找到"本该由谁回答"。
**结果**：卡点已从"unknown cond_wait"定位到 **`Backup_nread` → `backup_sync_msg(qid 0x003E014D)` 无人应答**；
补上应答后 App 越过该点、连续运行 118s 无异常，但**尚未产生一次 GRM 绘制**（`GRM2 = 0`）。

---

## 1. 结论先说

```
App init
 └ libObj+0x7D7D30   请求等待器（入队 q2=0x072E5C28，等 q2+0x68）
    └ q2 worker (libObj+0x7D7C10)  取到 type=0x8001
       └ cb2 libObj+0x7D7870       按 P[0] 分派（本例 P[0]=1）
          └ libObj+0x7D7940        opcode 1 handler
             └ libObj+0x7D5200
                └ libObj+0x7D8560
                   └ libObj+0x78C560        请求分发器（码 0x30xx）
                      └ libObj+0x790B30
                         └ libObj+0x7DCF70   生命周期/引用
                            └ libObj+0x6F925E 进入临界段 + 虚调用
                               └ vtbl+8 = libObj+0x6F534E   ★ port 对象 init()
                                  └ … singletons / rsrc_port_init / vdf_if_port_init …
                                  └ libObj+0x6F4C74
                                     └ libObj+0x6F4C82  blx Backup_nread@plt
                                        └ libbackup.so Backup_nread (0x169C)
                                           └ backup_sync_msg(qid=0x003E014D, cmd=3, 3 words)
                                              └ osal_snd_sync_msg(0x3E014D, …)   ★ 永远不返回
```

**⇒ 缺的不是"完成信号"，是 Backup 同步队列 `0x003E014D` 的应答方。**

---

## 2. 怎么查到的（可复现）

工具：`/mnt/build/tb_gdb_trace.sh` ~ `tb_gdb_trace5.sh`（逐层下降）、`tb_stackscan.sh`（栈扫描）

- QEMU 正常启动，**只在 libObj 被 mmap 之后**才连 GDB（`-gdb tcp::123x`，连上即暂停），
  在 `0x05E82000 + off` 处下**软件断点**，命中时打印寄存器+关键内存后自动 continue；
- 每层只改"下一层断点"，**一轮 2.5 分钟**决定一次下降；
- 阻塞判定：**"进入某函数"的断点命中、但"它返回后"的断点永不命中** ⇒ 就在该函数内阻塞。

关键实测（`/mnt/build/tmp/gt5/gdbtrace.log`）：

```
2.54 CB2_ENTER       r0=09458c34 r4=072e5c28 lr_off=7d7cbb
2.55 L_8560          r0=072e5ac8 lr_off=7d520f
2.57 L_C560          r0=00000002 r1=072e006c lr_off=7d85fd
2.65 PORT_VTBL_CALL  r0=072df5f8 r3=0657734f        ← vtable+8 = libObj+0x6F534E
2.65 INIT_METHOD     pc=0657734e r0=072df5f8
2.68 … INIT_C11C / INIT_SING_FA0 / INIT_SING_09C / INIT_SING_010
3.01 RSRC_PORT_INIT  pc=065774d4
3.10 VDF_IF_PORT_INIT pc=0657d4f8
3.19 INIT_SING_1B0 / 3.20 INIT_SING_DB0 / 3.24 INIT_SING_10C
3.25 BACKUP_NREAD_W  pc=06576c74 lr_off=6f53b5     ← 进去了……
（此后 45 秒无任何断点命中 ⇒ 卡在此函数内部）
```

被阻塞线程的栈扫描（`tb_stackscan.sh`，同一实例内 re-attach，纯返回地址过滤）：

```
098d8dbc L+07d7c11  worker 循环
098d8dac L+07d7cbb  worker 调 cb2 之后
098d8d84 L+07d7947  cb2 opcode1 调 7D5200 之后
098d8d24 L+07d520f  7D5200 调 7D8560 之后
098d8cb4 L+07d85fd  7D8560 调 78C560 之后
098d8c54 L+078c709  78C560 内
098d8c14 L+0790cb9 / 098d8c04 L+07dcfaf / 098d8bfc L+06f9275
```

**踩过的坑**：`tb_stackscan.sh` 第一版取"日志里最后一个 sp"，那次最后打印的是**另一个线程**（sp=0x0ee6…），
扫了无关内存；改成**从指定断点名字取 sp** 或固定窗口才正确。

---

## 3. 协议（反汇编 libbackup.so 得到，非猜测）

`backup_sync_msg(out, qid, size, wordcount, words…)`（`libbackup.so+0xF64`）构造成员：

| 消息偏移 | 含义 |
|---:|---|
| +0x00 | 声明长度（nread=0x1E，read_4Byte=4） |
| +0x04 | **应答值（原地写回）** |
| +0x08 | 负载字数 |
| +0x0C | 目标 qid 回显 |
| +0x10 | 0 |
| +0x18 | 负载字（nread: off,buf,len；read_4Byte: key） |

返回后 `backup_sync_msg` 把 `msg+4` 的 4 字节写进调用者的 out；`Backup_nread` 再把它当返回值。
所以 **`Backup_nread` 的"读到几个字节"就是 `msg+4`**，`0x6F4C74` 断言它必须等于 4。

`osal_snd_sync_msg` 的**真实参数顺序是 `(qid, msg, …)`**（`libosal_uipc.so+0x3EC8` → `__osal_snd_sync_msg`）。

> ⚠️ **旧实验为什么无效**：早期探针（`menuc95/96/97…`）把钩子写成 `osal_snd_sync_msg(void *msg, unsigned qid, …)`，
> 参数**顺序反了**，`if (qid == 0x3E014D)` 实际比较的是指针 ⇒ 从来没应答过。
> 于是当年"我们应答了 0x3E014D，没变化"这个结论**作废**。

---

## 4. 补上应答之后

探针 `menuc1100.c`（应答 v1，全零）→ 镜像 `ab_v1600`：

- 备份请求被应答 17 次，**越过原卡点**；
- 随后 App 通过 `Backup_read_4Byte` 询问 **MonDrv** 的两个键：
  `0x00FA0028`（mmap offset）与 `0x00FA005D`（mmap size）；我们回 0 ⇒
  `libMWF` 跳空指针：`pc=058DD372, trap_no=0x0E, address=0` ⇒ 进 `Exception>`。

探针 `menuc1101.c`（应答 v2：按已核键值表回答）→ `ab_v1610`：

- **无异常**，连续跑 118 s，应答 **5,000+ 条**（k1=357 个 4 字节键、k2=4,631 个双字请求、k3=12 个数据读）；
- 期间 guest 继续正常加载模块（`MODLOAD read ok len=238788`、多份 `MAPD`），说明**初始化不再卡死**。

探针 `menuc1102.c`（应答 v2 + 软件绘制 shim）→ `ab_v1620`：

- 同样无异常、`GRM2` 只有一行 `GRM2 init fb=0x4722000`；
- **绘制接口 0 次命中** ⇒ App 还没走到绘制；截图为彩色噪声（未初始化显存），
  **按纪律不算进展**（`GRM2=0`）。

---

## 5. 当前状态与下一步

```
已完成：卡点消除（App 不再阻塞在 Backup 服务）
未完成：App 仍未开始绘制（GRM2=0）
```

下一步（按优先级，全部沿用本轮方法）：

1. **看它现在停在哪**：用 `tb_gdb_trace*.sh` 把断点换成"App 主循环 / create_screen / 资源加载"，
   回答"越过 Backup 之后的下一个等待点是什么"；
2. **补齐 k2/k1 的真实语义**：k2（双字，4,631 次，如 `0x005C00xx`、`0x00BD00xx`）很可能是
   "写/设置属性"，我们现在一律回 0；k1 的 4 字节键需要一个**键值表**（`docs/BACKUP_KEY_TABLE.md`）
   从真实 BK4 取值，而不是猜；
3. **数据读（k3，12 次）**：`(key, buf, len)` 形状，需要按 BK4 表把真实字节写进 `buf`，
   这是"从真实数据服务"与"假成功"的分界线；
4. 保持 `GRM2>0` 作为唯一绘制判据；噪声截图不计。

---

## 6. 附：本轮新增/变更的文件

| 文件 | 作用 |
|---|---|
| `/mnt/build/tb_gdb_trace{2..5}.sh` | 逐层下降的 GDB 断点脚本（自动等到 libObj mmap 再挂） |
| `/mnt/build/tb_stackscan.sh` | 同一实例内 re-attach + 线程栈返回地址扫描 |
| `/mnt/build/menuc1100.c` | 0x3E014D 应答 v1（全零，用于确认卡点） |
| `/mnt/build/menuc1101.c` | 0x3E014D 应答 v2（MonDrv 等已核键值） |
| `/mnt/build/menuc1102.c` | v2 + 软件绘制 shim 合体 |
| 镜像 | `ab_v1600`(v1) / `ab_v1610`(v2) / `ab_v1620`(v2+shim) |

---

## 7. 追加结果（同一天，合并探针 `ab_v1630`）

把 **观测探针**（`menuc1009.c`：`INITCFG_ENTER/EXIT` + qid 普查 + qdump）与
**备份应答**（`menuc1110.c`）合成一份 preload 后重跑，得到决定性的顺序证据：

```
… BKV n=1..48（0x005C00xx 属性读，成对 size=0/3）…
INITCFG_ENTER tid=188 lr=0x068a6ead
MISSSO //lib/viewUnified2.so (err=-2)
ZZREAD Appframework pid=188 fd=23 count=512 dev=viewUnified2.so
QMMAP … ret=0x0ec63000 name=viewUnified2.so          ← 15 MB 的 viewConfig 模块
QMMAP … len=0x228000 off=0xb78000 name=viewUnified2.so
MISSSO //lib/CautionConfig.so (err=-2)
ZZREAD Appframework pid=188 fd=23 count=512 dev=CautionConfig.so
QMMAP … ret=0x0fab3000 name=CautionConfig.so
QMMAP … anon 0x1218d8
（此后只有 probe 自己的 Y9/PV 轮询行与 App 的 IMNANOSLEEP 轮询，无异常）
```

**含义**：`initializeConfig` 不再卡住——它真的开始 **load `viewUnified2` / `CautionConfig`**
（正是 ctor 的那几次 load）。所以"App 初始化链走不通"这条结论**作废**，
现在状态是：**初始化在推进，App 主循环进入 nanosleep 轮询，但 `GRM2 = 0`（仍未绘制）**。

## 8. 现在的下一个问题

App 已经不死、在轮询，却没有任何绘制/建屏痕迹。按 a7c2 的经验，这一层对应
他们的"`process_power_on → boot_start_seq → model_future::wait`"一类**启动/首设状态机**，
以及"建屏要由原生命周期入口触发"。所以下一步优先做：

1. **确认有没有建屏**：在 preload 里给 `GRM_bitmapCreate`（libObj 通过 PLT 导入，可拦）
   加只记录不改变行为的日志，回答"UI 到底有没有申请过位图"；
2. 若无建屏：找 App 的启动状态机在等什么（沿用本轮的 GDB 逐层下降方法）；
3. 若有建屏但 `GRM2` 仍 0：说明绘制走的是另一条内部路径，需要按 `ROUTE_DIFF_VS_A7C2.md`
   D2 那条把绘制入口(PLT `GRM_gpermFillrect/Rectblit`)接住。

**新镜像**：`ab_v1630`（观测探针 + 备份服务应答，当前最完整基线）。
