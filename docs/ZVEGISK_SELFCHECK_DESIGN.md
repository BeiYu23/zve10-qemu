# zvegisk 自检设计建议（供真机侧）

## 一、`/log` 可用性时序

**分区对应（来自 ZV-1F `mount.conf`，同平台）：**

```
/dev/nflasha2  → /setting  vfat  20,971,520 B
/dev/nflasha10 → /tmp      vfat  12,582,912 B
/dev/nflasha11 → /log      vfat 524,288,000 B   (500 MB)
/dev/nflasha15 → /usr      raw
```

**推断链：**
```
bootin.elf 读 /setting/mode/preload
  ⇒ /setting 在 bootin 之前必须已挂载
  ⇒ 挂载是 initrd 的 init 里按 mount.conf 一次完成
  ⇒ /log 与 /setting 同批次 ⇒ preload 构造函数时 /log 应可写
```

**`/proc` 必然可用**（glibc 启动即需 auxv / `/proc/self/exe`）
⇒ 用 `/proc/self/maps` 做自检数据源安全。

### ⚠️ 建议降级链

```c
const char *paths[] = { "/log/zvegisk.log", "/tmp/zvegisk.log",
                        "/setting/zvegisk.log", NULL };
/* 逐个 O_WRONLY|O_CREAT|O_APPEND，第一个成功即可 */
/* 全失败静默返回 —— 绝不能因日志失败影响启动 */
```

**理由**：vfat 若以 `ro` 挂载、或早期挂载点未建好，写日志会失败。
降级链成本极低，能排除"看不到日志"这种最难诊断的情况。

## 二、四项自检（建议全做）

| # | 检查 | 证明什么 |
|---|---|---|
| ① | `/proc/self/maps` 里 `libIMDB.so` 的**路径** | **preload 是否生效** |
| ② | loop 自检（mknod + LOOP_SET_FD + mount）| **loop 是否可用** |
| ③ | `/proc/mounts` 里 `/usr/lib`、`/usr/share/app` 两行 | **bind 是否做成** |
| ④ | `/proc/self/maps` 里 `libObj.so` 的**路径** | **bind 是否对 dlopen 生效** |

### 2.1 ★ 为什么 ① 和 ④ 必须一起看

**① 只看 preload（与 bind 无关）；④ 才反映 bind 对 dlopen 的效果。**

| ① libIMDB 路径 | ④ libObj 路径 | 结论 |
|---|---|---|
| `/log/.../libIMDB.so` | `/usr/lib/libObj.so` | preload 生效，**bind 未生效** |
| `/log/.../libIMDB.so` | `/tmp/m2usr/...libObj.so` | **两者都生效 ✅** |
| `/usr/lib/libIMDB.so` | `/usr/lib/libObj.so` | preload 完全没生效 |

**⇒ 一张表覆盖所有情况，一次开机即可定位。**

### 2.2 ⚠️ ④ 的时机问题

**`libObj.so` 是运行时 dlopen 的，构造函数执行时尚未加载 ⇒ maps 里看不到。**

**⇒ 两个办法：**
- 构造函数里打第一份 maps 快照；**30 秒后**再打第二份，diff 即知后来加载了什么
- 或把 ④ 放到延迟线程里

## 三、日志命名

**vfat 上时间戳可能不可靠（RTC 未必同步）⇒ 用递增序号：**

```
/log/zvegisk.0001.log
/log/zvegisk.0002.log
```

## 四、两阶段开机方案（变量隔离）

```
第 1 次（手动 dlopen zvegisk.so，不设 preload）
   ⇒ 拿到 ② loop 自检结果
第 2 次（preload 三条目 + dmode）
   ⇒ 拿到 ① preload 是否生效 + ③④ bind 是否生效
```

**⇒ 每个答案有独立判据，失败能直接定位环节。**

## 五、glibc 2.11.2 preload 行为（已从固件确认）

```
.rodata @0x14718  " :"        ← 空格与冒号都是分隔符
.rodata @0x1471C  ": \t\n"    ← 字符集式分割（LD_LIBRARY_PATH 一类）
.rodata @0x13AE8  "ERROR: ld.so: object '%s' from %s cannot be preloaded: ignored.\n"
                  ⇒ 单条失败只警告，不中止
```

**⇒ 三条目 preload 可行；单条失败无害；最坏结果是"新菜单未生效、相机正常启动"。**
