# bootin.elf 的模式字母：静态结论与实测捷径

## 静态分析结果（`bootin.elf`，15,336 B）

### dmode 解析：字符 → 标志位 / target

`0x948e` 起的解析循环，`r3` 是调试标志掩码，`r1` 是 target 字符串：

```
'p'(0x70) / 'P'(0x50) → r3 |= 0x40     自定义 LD_PRELOAD
'l'(0x6c) / 'L'(0x4c) → r3 |= 0x42     (= 0x40|0x02, LeakTracer + preload)
'w'(0x77) / 'W'(0x57) → r3 |= 0x08     ssllibc write check
'g'(0x67) / 'G'(0x47) → r3 |= 0x10     target GDB
's'(0x73) / 'S'(0x53) → r3 |= 0x20     gdbserver
'0'(0x30) → r1 = "0"    '1'(0x31) → r1 = "1"
'2'(0x32) → r1 = "2"    '3'(0x33) → r1 = "3"
最后: [r4+0] = r3 (flags), [r4+12] = r1 (target)
```

### 七条命令模板

| 字母 | 命令串 |
|---|---|
| s | `/usr/bin/im.elf s &` |
| g | `/usr/bin/im.elf g &` |
| **r** | **`/usr/bin/im.elf r &`** |
| u | `/usr/bin/im.elf u &` |
| l | `/usr/bin/im.elf l &` |
| i | `/usr/bin/im.elf i &` |
| a | `/usr/bin/im.elf a &` |

另有裸串 `/usr/bin/im.elf `（无字母）与 `/usr/bin/im.elf`。

### 推断：`r` = 常规启动

- dmode 解析只产生 **5 个调试标志**（p/l/w/g/s），而命令模板有 **7 个字母**
- `l`/`g`/`s` 与标志一一对应（LeakTracer / gdb / gdbserver）
- **`r` 是唯一没有对应调试标志的字母** ⇒ 最可能是 **regular（常规拍照界面）**
- `u`/`i`/`a` 在本构建里似乎无法由 dmode 触发（无对应标志位）
  ⇒ 疑为其它调试器残留（DUMA / Electric Fence / ICE 之类，见外部资料
  的 dmode 表 d/e/9 项），**不建议**优先试

## ★ 实测捷径：不需要 7 次穷举

你们的 **EXECVE 追踪已经抓到了那次 exec**（日志里有 `EXECVE /usr/bin/im.elf`）。
只要把 **argv 也打出来**，一次 run 就能读到字母：

```
EXECVE /usr/bin/im.elf   argv = ["/usr/bin/im.elf", "r"]
```

⇒ 建议在 execve hook 里记录 `argv[0..n]`（或直接 `ptrace` 读用户栈上的
argc/argv 指针数组），设置 `dmode=3`（无后缀）跑一次即可确认。

## 顺带：`im.elf` 本身是二级启动器（23,240 B）

它不是 UI，而是一个装载器：

```
解析参数: boot=  target=  app_argv=
读:   /proc/udm/upm_bootmode     /setting/sen/smode
用:   libIMDB.so 的 IMDB_find_entry / IMDB_get_entries / IMDB_find_target_bit
      然后 dlopen("/usr/lib/libObj.so")
其它: libSTMcmdexecute.so / libjemalloc_ds.so.1 / libbackup.so
```

`libIMDB.so` 里的 profile 名只有 6 个：`IMDB:default` / `IMDB:qemu` /
`IMDB:nfs` / `IMDB:set` / `IMDB:USBJ` / `IMDB:RE-SUB` —— **没有单字母**，
所以那个字母不是 IMDB profile，而是 im.elf 自己的启动模式选择。

⇒ 若补服务/环境仍不解决，下一步值得看的是 `IMDB:default` 这条 profile 里
`libObj.so` 的入口记录（外部资料称 idx=140 记录 `appFw.so` +
`Application_System_Init`；ZV-E10 的 libObj 自己就导出
`Application_System_Init`，所以这条记录在 ZV-E10 上指向 libObj 本身）。
