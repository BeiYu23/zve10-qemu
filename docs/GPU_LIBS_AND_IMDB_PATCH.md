# GPU 库核查 + 补丁版 libIMDB

## 一、★ `libgrm` / `libgpe` 在三个固件里都不存在

```
             libgrm*:  libgpe*:
ZV-E10          0         0
ZV-1M2          0         0
hybrid          0         0
```

**⇒ 用户态没有 GPU 库。`libIMDB` 注册表里的 `libgrm.so`/`libgpe.so` 只是
"尝试加载"清单（同 `libuxengine.so`、`libBizInitializer.so`），文件并不存在；
`uxengine` 实现实际在 `libObj.so` 内。**

**GPU 只有内核模块：**

| 模块 | ZV-E10 | ZV-1M2 | hybrid |
|---|---|---|---|
| `grm_gles.ko` | `a7f1258b` | `8f743810` | **`a7f1258b` ✅ 保留 E10** |
| `grm_ma.ko` | `5c6a7de3` | `39ecd6e7` | **`5c6a7de3` ✅ 保留 E10** |

**⇒ 两版 .ko 不同，hybrid 正确保留 E10 版（须匹配 E10 内核）。
⇒ 且因用户态无 libgrm，不存在"库版本 vs 内核模块版本"耦合问题。**

## 二、补丁版 libIMDB（备用方案 B）

```
/mnt/build/fw/imdbpatch/libIMDB_patched.so    40,596 B（大小未变）
```

### 2.1 原 ELF 结构

```
LOAD#1  R E   off=0x000000 vaddr=0x0      filesz=0x6998
LOAD#2  RW    off=0x007000 vaddr=0xmzf000 filesz=0x2904
.init_array     vaddr=0xf000 off=0x7000  size=4  ← 只有 1 个条目
.init_array[0] = 0x000036DD（Thumb）
★ 代码段尾 0x6998 到下一段 0x7000 之间有 1,640 字节空隙
```

### 2.2 补丁（只改 3 处，共 316 字节）

```
① 0x0064-0x0069   PT_LOAD#1 filesz/memsz: 0x6998 → 0x6ACC
② 0x6998-0x6ACB   308 字节 bind 蹦床
③ 0x7000-0x7001   .init_array[0]: 0x36DD → 0x6999
```

**全文件 diff 验证：除这 3 处外字节完全未改；7 个 UND 导入、SONAME 保持不变。**

### 2.3 蹦床逻辑（全裸 syscall）

```
① mknod("/tmp/zdev/loop0", S_IFBLK|0600, makedev(7,0))   __NR=14
② open("/log/zvegisk/m2usr.img", O_RDONLY)               __NR=5
③ open("/tmp/zdev/loop0", O_RDWR)
④ ioctl(lfd, LOOP_SET_FD=0x4C00, fd)                     __NR=54
⑤ mount(loop0, "/tmp/m2usr", "ext2", MS_RDONLY, 0)       __NR=21
⑥ bind "/tmp/m2usr/lib"       → "/usr/lib"      (MS_BIND=4096)
⑦ bind "/tmp/m2usr/share/app" → "/usr/share/app"
⑧ close fds
⑨ 尾调用原 init (0x36DD)

任一步 <0 → fail：unlink("/setting/mode/preload") 自愈 → 尾调用原 init
```

### 2.4 ⚠️ 补丁不解决白名单问题

**preload 内容仍须以白名单路径开头，仍需冒号：**
```
/usr/tool/libefence.so:/setting/zvegisk/libIMDB.so
```

**⇒ 补丁的价值：三条目减为两条 + 消除 fork race（bind 在 libIMDB 构造函数里同步完成）。
⇒ 冒号若不支持，补丁也救不了 —— 那时只能刷 nflasha15 或找 init 钩子。**

## 三、`bootin` preload 白名单（对端反汇编结论）

```
白名单 4 项（前缀比较 strncmp）：
  /usr/tool/LeakTracer.so
  /usr/tool/libefence.so      ← ZV-E10 上【不存在】⇒ ld.so 只警告后忽略
  /usr/tool/libjemalloc_tsh.so
  /usr/tool/libmcheckb.so

读取逻辑：read(fd, buf+11, 498) 写入 "LD_PRELOAD=" 之后
比较：strncmp(白名单项, buf+11, strlen(白名单项))  ⇒ 前缀匹配
```

**⇒ 可用 `/usr/tool/libefence.so:<真实库>` 的形式带上追加项。**

### 3.1 三个注意点

1. `read` 只读 498 字节且不保证一次读完；preload 行应远短于此（三条目约 120 字节）
2. `.init_array` 条目是 Thumb 指针（奇数），补丁保持 0x6999 即符合 ARM EABI
3. 白名单第一项不存在时 glibc 打印 `cannot be preloaded: ignored` 后继续
   —— 前提是 `LD_PRELOAD` 里其余项都能正常加载

### 3.2 验证冒号是否生效的建议组合

```
/usr/tool/libefence.so:/usr/lib/libm.so.6
```

**比用 `/bin/sh` 安全：第二条是真实存在、必然可加载、无副作用的库，判读更干净。**
