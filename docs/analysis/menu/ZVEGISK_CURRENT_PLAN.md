# zvegisk 当前架构规划（截至本轮）

> 这份取代 `ZVEGISK_ARCHITECTURE.md` 里的实施细节 —— 那份讲的是**为什么能成**，
> 这份讲的是**现在怎么做**，含今天所有修正。

---

## 一、一句话

**用 Sony 官方的 `dmode=3p` + `/setting/mode/preload` 钩子，在 `im.elf` 启动时
加载我们的 `.so`，由它做"目录覆盖层"把新资源/新库喂给 UI，并自带崩溃回退。**

---

## 二、机制（已验证）

```
/sbin/init → 读 /setting/mode/dmode
            含 'p' ⇒ bootin.elf 读 /setting/mode/preload
                        ↓
                  putenv("LD_PRELOAD=<该路径>")
                        ↓
                  execve("/usr/bin/im.elf")
                        ↓
        ★ ld.so 先加载我们的 .so ⇒ 构造函数运行
                        ↓
        ★ im.elf 的 main() 才开始
```

**★ 构造函数在 UI 打开任何资源之前 ⇒ 在里面做 bind，UI mmap 到的就是我们的文件。**

**★★★ 与当初变砖的 `/etc/ld.so.preload` 的本质区别：那个对所有进程生效，
这个只影响 `im.elf`。**

---

## 三、★★★ 覆盖层技术（今天修正后的正确做法）

### `mount --bind` 不能新建文件 ⇒ 需要目录级覆盖

**用符号链接农场（Magisk 的做法，不需要 overlayfs）：**

```sh
# ① 先暴露原厂（★ 关键：避免循环引用）
mount --bind /usr/share/app /tmp/stock

# ② 建覆盖层（★ 必须是 2 层深，见下）
mount -t tmpfs tmpfs /tmp/zovl

# ③ 原厂文件：链到【暴露路径】，不是链到自身
ln -s /tmp/stock/color_cmn.uxc /tmp/zovl/color_cmn.uxc

# ④ 我们的文件：链到 /log 上的实体（不拷贝，零 RAM）
ln -sf /log/zvegisk/mods/<name>/system/share/app/global.xdb /tmp/zovl/global.xdb

# ⑤ 最后 bind（★ 必须最后）
mount --bind /tmp/zovl /usr/share/app
```

### ⚠️ 两个坑（都是实测/分析出来的）

**坑 1：循环引用**
```
若写成 /tmp/zovl/x → /usr/share/app/x，bind 后：
  /usr/share/app/x → /tmp/zovl/x → /usr/share/app/x → ∞
⇒ 必须先 mount --bind 到 /tmp/stock，链到 /tmp/stock/x
```

**坑 2：相对符号链接的深度**
```
/usr/lib 里有 58 个相对符号链接，其中 8 个跨到 rootfs：
  libanl.so → ../../lib/libanl.so.1
⇒ 暂存目录必须和 /usr/lib 同层数（2 层）：/tmp/stock、/tmp/zovl ✓
   /tmp/a/b/stock（3 层）✗ —— ../../ 会解析到错误位置
```

### 为什么用符号链接而不是拷贝

**tmpfs 在 RAM 里。`appFw.so` 53 MB、`viewUnified2.so` 14 MB —— 拷进去会吃掉一半空闲内存。
符号链接只占几十字节，实体留在 `/log` 上。**

**★ vfat 不支持符号链接 ⇒ 所以必须建在 tmpfs 里。**

---

## 四、模块存放（修正：不直接从 SD bind）

### ⚠️ 问题

**vfat 不支持多重挂载：我们若持续挂着 SD，**相机自己就挂不上 ⇒ 无法录制/回放**。

### ✅ 方案：拷到内部再 bind

```
① 挂 SD（只读）→ 读 boot_mode
② 若 "new"：把模块文件拷到 /log/zvegisk/mods/（★ 首次用 agent 上传即可）
③ ★ 立刻卸载 SD（把卡还给相机）
④ 从 /log/zvegisk/mods/ 做覆盖层 bind
```

**★ SD 只被占用毫秒级（读一个 4 字节文件），之后完全释放。**

### 存储分工

```
SD 卡（只放配置）              内部（模块与状态）
/zvegisk/boot_mode             /log/zvegisk/mods/<name>/system/...   ← 模块
/zvegisk/safemode              /setting/zvegisk/boot_count            ← 计数
                               /setting/zvegisk/armed                ← 看门狗开关
                               /setting/zvegisk/safemode             ← 停用标志
```

**★ `/log` = 500 MB（nflasha11），已验证可写。**

---

## 五、★ 第一个验证试验（已备好，未执行）

**这是我强烈建议的第一步 —— 用最小改动验证整个机制。**

```
测试件：tools/zvegisk/testmod/newmenu_test/
  system/share/app/color_cmn.uxc   ← 只改 3 字节（0xBC/0xBD/0xBE）
  module.prop

改动：color 记录 12（id 0x400c）的 RGB 33 33 33 → ff 00 ff（品红），alpha 0x80 不变
文件大小 312 → 312（不变）
```

**为什么它是最佳第一个试验：**

| 理由 | 说明 |
|---|---|
| **无校验和** | zv-e10-re 确认：`color_cmn.uxc` 没有 checksum，改色不会被完整性拒绝 |
| **只动 3 字节** | 出错面积最小 |
| **语义风险最低** | 最坏就是框线颜色难看 |
| **效果肉眼可见** | 不需要读日志 |
| **别人已验证过** | 他们改 0x400c 让实时取景框线变品红，硬件确认 |

**⇒ 如果框线变品红，一次性证明：preload 钩子工作 + 构造时序对 + bind 生效 + 覆盖层可用。**

---

## 六、安全模型

### 结构性保证

| 保证 | 实现 |
|---|---|
| **零依赖** | `-nostdlib` + 纯 syscall，未定义符号 0，无 NEEDED |
| **父进程路径只有一个 syscall** | `fork()` → 判断 → 返回，零阻塞 |
| **所有工作都在子进程** | 子进程崩/挂都不影响 im.elf |
| **段对齐正确** | off/vaddr 差 0x10000（已验证） |
| **init_array 正确** | entry=0x494=zvegisk_init（已验证） |

### 失效场景

| 场景 | 后果 |
|---|---|
| **.so 文件损坏** | ld.so 警告 → 相机正常启动 |
| **构造函数崩溃** | 父进程已 fork ⇒ im.elf 继续 |
| **fork 失败** | 走父进程路径 ⇒ 直接返回 |
| **子进程崩溃/hang** | 只死自己 |
| **写 /setting 失败** | 计数不生效（保护失效），无副作用 |
| **watchdog 误触发** | 已加 `armed` 开关，测试时不启动 |

### 三层保护

```
第 1 层  boot_count++ ；> 3 ⇒ 建 safemode ⇒ 永久停止
第 2 层  watchdog 90 秒查 UI ⇒ 没起来就 umount + reboot
第 3 层  armed 开关 ⇒ 测试时绝不启动看门狗
```

### 三种恢复（都不需要 USB）

```
① 拔 SD 卡           → 下次开机找不到配置 → 原厂
② 卡上放 safemode    → 跳过一切
③ echo 3 > dmode     → 不加载 preload
```

---

## 七、⚠️ 契约：preload 钩子是【永久】的

**`dmode` + `preload` 两个文件在 `/setting`（vfat 持久分区）⇒ 重启不恢复。**

**⇒ 所以 .so 必须绝对可靠（上面五条结构性保证就是为此）。**

**★ 但"绑什么"不持久：每次开机由 .so 读 SD 配置重新决定。**

---

## 八、现状与验证进度

### ✅ 已验证（真机）

```
zvegisk start
boot_count=1
sd=250:11              ← 找到 SD 分区
sd_mounted             ← 挂载成功
boot_mode=old          ← 读到配置
mode=OLD -> stock boot ← 分支正确

（连跑 3 次）
boot_count=4
over limit -> safemode ← 自动回退正确触发

（safemode 存在时）
safemode -> do nothing ← 保护生效
```

### ❌ 未实现

| 项 | 说明 |
|---|---|
| **Phase 2：覆盖层 bind** | 代码里还是 TODO，只写日志 |
| **watchdog 实测** | 需 90 秒 + armed 标志 |
| **部署到开机** | 还没设 `dmode=3p` + `preload` |
| **交互选择** | 需输入 RE（未解决） |

### ⚠️ 待处理

```
/log/zvegisk.so   相机上是旧版（watchdog 无开关），需重传
/setting/zvegisk/ boot_count 需重置为 0，删 safemode
```

---

## 九、新菜单资源：格式已确认兼容，但索引方案不同

### ✅ 好消息

**`global.xdb` 前 16 字节相同；`.uxc` 头相同；**76 个同名文件**（含全部框架资源）。**
**`master_camera.uxc` 只差 64 字节（101,804 vs 101,868）⇒ 菜单骨架 99.94% 相同。**

### ⚠️ 坏消息

```
ZV-E10 global.xdb:  313 条引用（含 214 个 view*.uxc + master_* + image_*）
ZV-1F  global.xdb:  154 条引用（只有 image_* + string_*，没有 view 也没有 master）
⇒ 索引方案不同 ⇒ 直接换 global.xdb 会让 ZV-E10 的引擎找不到它的 214 个 view
```

### ⇒ 所以走"叶子资源先行"

**`color_cmn.uxc` / `style_cmn.uxc` 这类**两边都引用**的叶子资源可以单独换，
不涉及索引结构 ⇒ 风险最低。**

**结论：先验 `color_cmn.uxc`，通了这个机制，再谈 global.xdb 和整套替换。**

---

## 十、明确的边界（实测排除）

**zvegisk 做不到"自己画像素"：**

```
✗ /dev/fb0              ENXIO（无驱动）
✗ FBRM 高层 API          空桩
✗ /dev/mem + 物理地址     无可见帧缓存
✗ /dev/stream @0x20312000 描述符 + 压缩数据（四种解码全失败）
✗ ttycons              是串口
✗ SUGILITE ioctl        17 个命令全是 4 字节标量控制，不是显示门
? /dev/dmpgles2 mmap    ★ 唯一未验证 —— 只接受 512B / 128KB / 32MB，用 remap_pfn_range
```

**★ 最后一条（GPU mmap）如果通了，才能谈"自己画"；否则 zvegisk 的边界就是
"换资源 + 换库 + 崩溃回退"。**

---

## 十一、执行顺序（建议）

```
第 0 步【零风险】重传修好的 .so，用 agent 隔离跑一次
        ⇒ 确认日志 "not armed -> no watchdog"，等 2 分钟不重启

第 1 步【零风险】跑 zvegisk_gpumap（GPU mmap 探测）
        ⇒ 决定"能不能自己画"（影响整个方向）

第 2 步【低风险】实现 Phase 2 覆盖层（/tmp/stock + /tmp/zovl + 符号链接）
        ⇒ 先用 color_cmn.uxc 验证

第 3 步【中风险】真部署到开机
        ⇒ 备份原 dmode → 建 armed → 写 dmode=3p + preload → 重启
        ⇒ 出问题：拔卡 / safemode / 改 dmode

第 4 步【高难度】交互选择（需输入 RE）
```

**★★★ 第 1 步最有价值 —— 它决定后面所有工作的形态。**
