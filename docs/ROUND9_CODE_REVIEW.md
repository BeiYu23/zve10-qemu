# 轮次 9：bindm2 代码审查——修掉一个必败 bug 和两个隐患

## 一、★ 致命 bug：bind 挂载的 flags 传错

```c
/* 错误写法（我原来的） */
sy5(SYS_mount, "/tmp/m2root/lib", "/usr/lib", 0, (long)"bind", 0);
                                                  ↑ 第 4 参数是 mountflags
/* 正确写法 */
sy5(SYS_mount, "/tmp/m2root/lib", "/usr/lib", 0, MS_BIND /*0x1000*/, 0);
```

**`mount(2)` 的签名：`mount(source, target, fstype, mountflags, data)`**

```
bind 挂载的正确形式是：
    mount(src, dst, NULL, MS_BIND, NULL)
    即 fstype=NULL、mountflags=MS_BIND(0x1000)

我原来把字符串 "bind" 的【指针】当成 mountflags 传进去
⇒ 0x4xxxxxxx 这样的非法 flags ⇒ 内核返回 EINVAL
⇒ 两个 bind 调用必然失败
⇒ 结果会是：/usr/lib 仍是 E10 的，appFwBoot.so 依然找不到 → 无 UI
```

**如果不是这一轮复查，明天真机测试会直接失败，而且日志会显示 `bind /usr/lib=-22`（EINVAL），
还得再花一轮去查。**

## 二、另两个隐患

### 2.1 `/proc/self/cmdline` 的日志处理

```
cmdline 是 \0 分隔、没有换行
原实现：找到 "im.elf" 后拷到 '\n' 为止 ⇒ 找不到换行 ⇒ 拷到缓冲区末尾并写入 NUL
改：专用 log_cmdline()，整段读取后把 \0 换成空格
```

### 2.2 幂等保护的缓冲太小

```
原：file_has("/proc/mounts","m2root") 只读 4096 字节
风险：若 m2root 出现在 4096 之后，保护失效 ⇒ im.elf 的 7 个实例各挂一次
    ⇒ 7 个 loop 设备 + 7 层叠加 bind
改：缓冲 4096 → 8192
```

### 2.3 新增一项关键诊断

```
ext2 挂载失败时，读 loop 设备偏移 1080 的 2 字节，确认超块 magic：
  "53 ef"  ⇒ 镜像有效，失败另有原因（权限/loop 状态）
  其他     ⇒ ★镜像无效（上传不完整/文件损坏）
⇒ 把「镜像坏了」和「权限问题」彻底分开
```

## 三、修复后

```
bindm2.so  10,104 B   NEEDED=0   未定义符号=0   .init_array 正常
```

## 四、现在 /log/bindm2.log 的完整格式

```
=== bindm2 start ===
loopno=N
LOOP_SET_FD=0
mount ext2=0
ext2 mounted
bind /usr/lib=0
bind /usr/share=0
-- after bind --
libObj.so      = 3931624
appFw.so       = 64273528
appFwBoot.so   = 8312
libIMDB.so     = 40596
libInfraSKD.so = 7259708
global.xdb     = 16112
argv: /usr/bin/im.elf debug s g r u l i a lazy-global
MemFree: NNNN kB
=== bindm2 done ===

（若 ext2 挂载失败，中间会多一行）
ext2_magic: 53ef  => 是 ext2 ✓
```

## 五、教训

```
凡是用裸 syscall 直接对接内核接口的代码，【参数顺序】必须逐个对照
man 手册核验，不能凭印象。本轮 mount(2) 的 flags 位置就是一个典型：
  mount(source, target, fstype, flags, data)  ← flags 是第 4 个
而 bind 的 "bind" 只是【调用方式】的说明，不是参数值。
```
