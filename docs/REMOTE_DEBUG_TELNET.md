# 远程调试通道：ZV-E10 自带 telnet（不需要 SSH）

> 问题：新机型用 SSH 快捷调试，ZV-E10 可以吗？
> **答案：不需要 SSH —— ZV-E10 出厂就带一套 telnet 远程日志/终端设施，只是默认关闭。**

---

## 一、事实：固件里没有 sshd

| 检查 | 结果 |
|---|---|
| `/usr/bin`、`/sbin` 里 `sshd` / `dropbear` | ❌ 全部没有 |
| `libssh2.so.1` / `libssl.so.3` / `libcrypto.so.3` | ✅ 有，但 `libssh2` 是**客户端**库（被 `libInfraRemote` 等用于 SFTP/云） |
| `dropbear` 字符串 | 0 次 |

⇒ 想用真 SSH 得自己交叉编译一个静态 sshd 塞进去（可做但没必要）。

## 二、★ 出厂自带的 telnet 设施（这才是正解）

### 2.1 `ulogio.elf` 是一个带监视器的终端多路复用器

`/usr/bin/ulogio.elf`（19,200 B）内建监视器命令：

```
help                    -- this help message
tlist                   -- list terminals
it                      -- set input terminal
ot                      -- set output terminal by index
rewind                  -- rewind terminal output
term on / term off      -- enable/disable terminal output
telnet up               -- enable telnet output, must be reboot system
telnet down             -- disable telnet output, must be reboot system
```

`it`(输入终端) + `ot`(输出终端) ⇒ **不只是日志流，可以把一个 telnet 会话设成
输入+输出终端 ⇒ 交互式远程控制台**。

### 2.2 服务端在内核模块 + 库

```
/kmod/osal_ulogio.ko        38,992 B   模块参数: ulogio_initfile / ulogio_initdata_size / ulogio_initdata
/lib/libosal_ulogio.so      27,712 B   导入 accept、持有 listener_thread_handle，含
                                       ulogio_telnetd_init / mainloop / print_termlist /
                                       select_io_term_by_id / select_io_term_by_index
                                       ulogio_filter_get_telnet / set_telnet
```
（socket 创建走 osal 封装，所以 libc 层看不到 `socket/bind/listen`。）

### 2.3 ★ 开关就在 `/setting/ULOGIO.BIN`（3796 B）

模块参数 `ulogio_initfile` 指向它；已完整解出格式：

```
[u32 header_size=0x20][name "LGNM191H_Dslr_Astra-Diadem"]
之后是一串记录: [u32 rec_len][4 字节 tag][data]
```

实测记录（真机原始值）：

| 偏移 | tag | 值 | 含义 |
|---|---|---|---|
| 0x20 | `RECO` | `01 000000` | 记录开启 |
| 0x2c | `LOGB` | `00 50 08 03 00 80 47 00` | 环形缓冲地址/大小 |
| 0x3c | `PRTB` | `00 d0 4f 03 00 80 00 00` | 打印缓冲 |
| 0x4c | `CHAN` | `00000000` | |
| 0x58 | `ISYS` | `00000000` | |
| 0x64 | `DBGL` | `01 000000` | 调试级 |
| 0x70 | `DBCL` | `02 06 03 00` | |
| 0x7c | `FLTA` | `00000000` | |
| 0x88 | `TERM` | **`01 000000`** | **终端输出：开** |
| **0x94** | **`TELN`** | **`00 000000`** | **★ telnet：关（当前）** |
| 0xa0 | `FBOX` | 嵌套 SFLT/CFLT | |
| 0xc0 | `FLTF` | `/setting/filter.bin` | |
| 0xdc | `RCVF` | `/log/escapelog.bin` | |
| 0xf8 | `ALGP` | `/log/` | |
| 0x108.. | `ALGB/ALGD/ALGK/ALGT` | `ulogio_blog.dat` / `dlog.dat` / `klog.txt` / `tlog.txt` | |
| 0x168.. | `SUBS` × N | 各子系统的日志配置 | |

**`TELN` 的数据字节在文件偏移 `0x9C`（u32，当前 0）；把它改成 1 即启用。**

## 三、怎么启用（两种方式）

### 方式 A：官方命令（推荐，不用改文件）

从服务 shell 执行：

```sh
# ulogio 监视器（通用 UIPC 消息发送工具，--ulogio 是预置目标）
/usr/bin/sndcmd.elf --ulogio "telnet up"
# 看回复用
/usr/bin/rcvcmd.elf --ulogio
# 之后重启生效（命令本身写明 must be reboot system）
reboot -f    # 或 /usr/bin/change_mode.sh 等
```

（`sndcmd.elf` / `rcvcmd.elf` 是**通用** UIPC 发送/接收工具：
`sndcmd.elf <options> [osal_id] [size]:[data]`，可以给**任意** service 发消息，
这对调试本身也非常有用。）

### 方式 B：直接改配置文件

`/setting/ULOGIO.BIN` 偏移 `0x9C` 的 u32：`0x00000000` → `0x00000001`，然后重启。
`/setting` 是 vfat rw，且是普通文件区（非 LZP），改起来没有风险；改前备份即可。

### 启用后

- 相机联网（WiFi / USB 网卡）后，用 telnet 客户端连相机 IP
- 端口未在静态分析中确定（最可能是标准 **23**）；启用后 `netstat -ltn`
  或从 PC 扫描即可确认
- 用 `tlist` 看终端列表、`it`/`ot` 选输入/输出终端 ⇒ 可把 telnet 变成交互控制台

## 四、其他已有的调试通道（备选/补充）

| 通道 | 入口 | 说明 |
|---|---|---|
| **硬件串口 console** | `launch_shell.elf` → `/bin/ash` on `/dev/ttycons` | Sony 官方 shell 启动器（1 个 ELF，setsid + execv `/bin/ash`）；需 UART 测试点 |
| **服务模式 USB shell** | PMCA 认证后 | 我们一直在用，需要 USB 线 + 认证 |
| **通用 UIPC 命令** | `sndcmd.elf` / `rcvcmd.elf` | 给任意 service 发消息，脚本化调试的关键 |
| **日志环** | `/dev/mem` @ `0x03524FC0`(alog) / `0x035A5000`(klog) | 采集脚本 `tools/z10_capture.sh` |

## 五、结论

- **不建议**走 SSH（要自编译 sshd，收益低）
- **直接用出厂 telnet**：`sndcmd.elf --ulogio "telnet up"` + 重启，1 分钟的事
- 如果目标是"无线的、可脚本化的调试"，telnet + `sndcmd.elf`/`rcvcmd.elf` 已经够用，
  而且比 SSH 更贴合这台机器的体系（服务是 UIPC 的，不是 shell 命令行的）

---

## 六、`/setting/ULOGIO.BIN` 全量清单（128 条记录，110 个子系统）

解析结果：文件 3,796 B 被 128 条记录**恰好用完**（到 0xED4），无残余。

### 6.1 全局配置记录（18 条）

| 偏移 | tag | 原始值 | 解释 |
|---|---|---|---|
| 0x20 | `RECO` | `01000000` | 记录功能开启 |
| 0x2c | `LOGB` | `00500803 00804700` | **日志缓冲：addr=`0x03085000`, size=`0x478000`（4.47 MB）** |
| 0x3c | `PRTB` | `00d04f03 00800000` | **打印缓冲：addr=`0x034FD000`, size=`0x8000`（32 KB）** |
| 0x4c | `CHAN` | `00000000` | 通道 0 |
| 0x58 | `ISYS` | `00000000` | |
| 0x64 | `DBGL` | `01000000` | 调试级 1 |
| 0x70 | `DBCL` | `02060300` | 字节 02/06/03 |
| 0x7c | `FLTA` | `00000000` | 过滤 A 关 |
| 0x88 | `TERM` | `01000000` | 终端输出：开 |
| **0x94** | **`TELN`** | **`00000000`** | **telnet：关（数据在 0x9C）** |
| 0xa0 | `FBOX` | 嵌套 `SFLT`/`CFLT` 两条子记录 | 过滤器盒 |
| 0xc0 | `FLTF` | `/setting/filter.bin` | 过滤规则文件 |
| 0xdc | `RCVF` | `/log/escapelog.bin` | 逃逸日志文件 |
| 0xf8 | `ALGP` | `/log/` | 日志目录 |
| 0x108 | `ALGB` | `ulogio_blog.dat` | 二进制日志 |
| 0x120 | `ALGD` | `ulogio_dlog.dat` | 数据日志 |
| 0x138 | `ALGK` | `ulogio_klog.txt` | 内核日志 |
| 0x150 | `ALGT` | `ulogio_tlog.txt` | 终端（文本）日志 |

**交叉验证**：`LOGB` 的 `0x03085000` 与真机 cmdline 的
`wbimem=0x500000@0x03085000` **完全吻合**（5 MB 预留区里放了 4.47 MB 日志缓冲）；
`PRTB` 的 `0x034FD000+0x8000` 正好落在 `alog.addr=0x03524FC0` 之前，互不重叠。

### 6.2 子系统配置（110 条 `SUBS`）

每条 = `[u32 id][若干 u32 配置字]`，配置字里高位常带 `0x80000000` 标志。

已配置的 110 个 id（十进制）：

```
131 128 129 127 41 42 43 44 45 83 84 85 99 38 2 3 6 86 101 100
89 90 88 105 94 95 103 209 87 104 107 108 122 151 60 197 102 147 189 123
124 125 283 126 97 202 54 225 179 53 154 155 156 157 160 161 162 260 61 62
217 214 196 266 192 191 262 257 82 69 21 146 190 312 310 65 368 364 194 366
93 367 311 345 346 165 319 321 318 336 340 359 385 40 394 393 396 293 406 13
4 414 431 112 606 563 546 255 188 169
```

值得注意的：
- **`0x83`(131) 的配置最长（84 B，12 个配置字）**，是最"话痨"的子系统
- `0x80`(128) / `0x81`(129) / `0x7f`(127) / `0x29..0x2d`(41-45) / `0x53..0x55`(83-85)
  等一批**配置完全相同**（`d8400000 a43f0000 64000080 a4000080`），像是同一组模板
- `0xff`(255) 有 112 B、26 个统一为 `0x80000044` 的配置字（大量同类条目）
- `0xc5`(197)、`0x66`(102)、`0x92`(146)、`0xbe`(190) 也是多字段的大配置

> 各字段的确切语义（阈值/等级/掩码）需要反汇编 `libosal_ulogio.so`
> 的配置装载函数才能定论；目前只确定结构与 id 集合。

### 6.3 由此可推断的用途

这套配置本质是**一个可精细控制的分层日志系统**：

- 4.47 MB 主日志缓冲 + 32 KB 打印缓冲 + 每子系统独立配置
- 4 种落盘格式（二进制/数据/内核/终端文本）到 `/log/`
- 过滤规则文件 `/setting/filter.bin`，逃逸日志 `/log/escapelog.bin`
- 输出目标可选：物理终端（`TERM`）、**telnet（`TELN`）**、以及更多终端（`tlist`）

⇒ 对调试的价值：**它不只是一个 telnet 开关，而是一套可以把任意子系统日志
放大/过滤/导出的仪器**。启用 telnet 后，用 `it`/`ot`/`tlist` 就能实时把
指定子系统的日志流到 PC，不需要改任何代码。
