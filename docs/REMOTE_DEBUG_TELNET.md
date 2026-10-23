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
