# `bootin.elf` 的 preload 白名单（历史方案，禁止直接部署）

> **安全审计 NO-GO（2026-10-26）：** 前缀比较分支有离线依据，但下文构造顺序、fork race 无害、备用补丁可用和恢复保证已撤回。冒号支持不能证明部署安全；备用 libIMDB 补丁有错误 ABI 参数和绝对跳转。详见 [开机部署审计](ZVEGISK_PREDEPLOY_AUDIT.md)。

> 反汇编 `bootin.elf`（15,336 B，`vaddr - file_off = 0x8000`）得到的结论。
> 这是把 M2 的 `libIMDB.so` 塞进 `im.elf` 启动链的**唯一入口**。

---

## 一、白名单：只有 4 个路径能通过

```
@vaddr 0xA879  '/usr/tool/LeakTracer.so'          ← 3L 模式
@vaddr 0xA891  '/usr/tool/libefence.so'           ← 3e 模式
@vaddr 0xA8A8  '/usr/tool/libjemalloc_tsh.so'     ← 3p 模式
@vaddr 0xA8C5  '/usr/tool/libmcheckb.so'          ← 3d 模式
```

**对应 `change_mode.sh` 里的 `LIB_LT` / `LIB_EFENCE` / `LIB_JEMALLOC` / `LIB_MCHECKB`。**

### 逻辑

```asm
9996: strncpy(buf, "/setting/mode/preload", 512)
99a4: open("/setting/mode/preload", 0)
99b0: r2 = 498                      ; 最多读 498 字节
99b4: read(fd, buf+11, 498)         ; ★ buf+11（11 = "LD_PRELOAD=" 的长度）
99d0: r9 = [whitelist++, #4]
99e2: strncmp(r9, buf+11, strlen(r9))   ; ★★ 前缀比较，不是全等
99ea: cmp r5, #4 ; bne 99d0
99ee: cmp r5, #3 ; bls 9a00             ; 匹配到任一 ⇒ 放行
99f2: (不匹配 ⇒ 错误路径)
```

**⇒ 任意路径 preload **不成立** —— 必须以上述 4 个之一开头。**

---

## 二、★★★ 但 `strncmp` 只比 `strlen(白名单)` 字节 ⇒ 可以冒号追加

**内容只要**以白名单路径开头**就算匹配，后面的字符随便。**

### ZV-E10 的 `/usr/tool/` 现状

```
LeakCheck              689 B
LeakTracer.so       31,128 B   ← 存在，hook malloc/free/realloc
libjemalloc.so          20 B   ← 符号链接
libjemalloc_tsh.so      20 B   ← 符号链接 → .so.1
libjemalloc_tsh.so.1 115,504 B
libefence.so                    ★ 不存在
libmcheckb.so                   ★ 不存在
```

### 最优写法

```
/setting/mode/preload =
  /usr/tool/libefence.so:/setting/zvegisk/zvegisk.so:/log/zvegisk/m2/libIMDB.so
```

| 段 | 作用 |
|---|---|
| **`/usr/tool/libefence.so`** | **前 23 字节匹配白名单 ⇒ 通过校验；但文件不存在 ⇒ ld.so 只打警告后忽略** |
| **`/setting/zvegisk/zvegisk.so`** | **★ 我们的工具，构造函数里做 loop mount + bind** |
| **`/log/zvegisk/m2/libIMDB.so`** | **★ SONAME 覆盖 im.elf 的 NEEDED ⇒ 老版本不被映射** |

**★ 为什么选 `libefence.so` 而不是 `LeakTracer.so`：
前者在 ZV-E10 上**不存在**，所以不会真的加载（LeakTracer/jemalloc 都会 hook `malloc`，
能避免就避免）。**

---

## 三、★ 顺序：zvegisk 必须在 libIMDB **之前**

**glibc 大致按加载顺序跑构造函数。
若 `libIMDB` 在前，它的构造函数会在 bind 之前执行。**

```
✓ /usr/tool/libefence.so : /setting/zvegisk/zvegisk.so : /log/zvegisk/m2/libIMDB.so
✗ /usr/tool/libefence.so : /log/zvegisk/m2/libIMDB.so : /setting/zvegisk/zvegisk.so
```

---

## 四、★ 为什么"bind 晚于启动"是可以接受的

**关键实测：启动链上的 `/usr/lib` 库，E10 与 M2 **字节完全相同****

```
libbackup.so         9,384   ★ 相同
libosal_utm.so       4,900   ★ 相同
libosal_uipc.so     26,692   ★ 相同
libssl.so.3        412,204   ★ 相同
libcrypto.so.3   2,656,568   ★ 相同
libjemalloc_ds.so.1  2,936   ★ 相同
────────────────────────────────
libIMDB.so          37,204 → 40,596   ← 只有它不同
```

**⇒ 启动期只有 `libIMDB` 需要换，而它由 preload 直接提供。
其他库本来就一样 ⇒ bind 晚一点毫无影响。
bind 只需在 `libObj` / `appFw` / 资源 **dlopen 之前**完成，而那是很久以后。**

**⚠️ 仍存在一个 race：zvegisk 的构造函数是 `fork()` → 父进程立刻返回，
子进程才做 bind。父进程返回后 ld.so 继续跑 libIMDB 的构造函数 —— bind 可能还没完成。**

**★ 但由上表可知：libIMDB 的构造函数不需要 /usr/lib 里的新内容（那些库本来就一样），
所以这个 race 无害。**

---

## 五、完整部署流程

```
1. hybrid 镜像（300 MB）→ /log/zvegisk/m2usr.img
2. /setting/mode/preload = 三段的冒号串（见上）
3. /setting/mode/dmode   = 3p
4. 重启

zvegisk.so 构造函数（子进程）：
   mknod /tmp/zdev/loop0 b 7 0
   open(m2usr.img) → ioctl(LOOP_SET_FD=0x4C00)
   mount("/tmp/zdev/loop0", "/tmp/m2usr", "ext2", MS_RDONLY, 0)
   bind /tmp/m2usr/lib        → /usr/lib
   bind /tmp/m2usr/share/app  → /usr/share/app
```

**★ 只 bind 两个子树，不 bind 整个 /usr ⇒ `share/screenreader`、
`usr/bin`、`usr/sbin` 保持原厂，少一个变量。**

---

## 六、⚠️ 待实测（第 0 步）

```
① loop 可用性：mknod b 7 0 → LOOP_SET_FD → mount
② ★ 白名单前缀匹配是否真如静态分析
   （写成 /usr/tool/libefence.so_X:/bin/sh 之类无害组合，
     看 ld.so 是否只警告第一个、正常加载第二个）
③ mknod 在 bootin 上下文有权限吗
```

**★★★ 第 ② 条是整个方案的门槛。若前缀匹配不成立，退回"补丁 libIMDB 的 .init_array"方案。**

---

## 七、备选方案（若白名单不支持追加）

**在 M2 的 `libIMDB.so` 里加一个 `.init_array` 构造函数做 bind：**

```
1. 复制 M2 的 libIMDB.so（40,596 B）
2. 文件末尾追加 do_bind() 代码（纯 syscall，不能依赖 libc）
3. .init_array 里插入指向 do_bind 的指针
4. 存为 /setting/zvegisk/libIMDB.so
5. preload = /usr/tool/libefence.so:/setting/zvegisk/libIMDB.so
```

**★ 补丁里要加失败自愈：bind 失败（loop 打不开等）就 `unlink /setting/mode/preload` 再返回。**
