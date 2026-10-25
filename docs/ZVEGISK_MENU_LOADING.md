# zvegisk：新菜单怎么被加载

## 一、核心障碍

**`mount --bind` 只能**替换已存在的文件**，不能**新建文件**。**

**而新菜单需要新增 `/usr/lib/appFw.so`（ZV-E10 上不存在）。**

**⇒ 单文件 bind 不够。**

## 二、★★★ 技术方案：目录 bind + 符号链接农场

**这是 Magisk 的核心技巧（不需要 overlayfs，本机内核也没有）。**

```
① mount tmpfs → /tmp/zovl  （几 MB，RAM）
② /usr/lib 的每个文件在 /tmp/zovl 建符号链接指回原厂
③ 我们的替换/新增文件，在 /tmp/zovl 建符号链接指向 /log 上的实体
④ mount --bind /tmp/zovl /usr/lib
```

**★★★ 关键：第 ③ 步的符号链接指向 `/log`，**不是拷贝**.**

**⇒ tmpfs 只占符号链接的空间（KB 级），大文件（appFw.so 53MB）本体留在 /log。**

**★ 为什么不拷进 tmpfs：相机只有 ~129MB 空闲 RAM，53MB 会吃掉一半。**

**★ 为什么不能直接在 /log 建符号链接：vfat 不支持符号链接。**

## 三、完整加载流程

```
zvegisk.so 子进程：
 ① 读 SD /zvegisk/boot_mode（只读，毫秒级）→ 卸载 SD
 ② if (boot_mode == "new") {
       a. mount tmpfs → /tmp/zovl（size 限制几 MB）
       b. 遍历 /usr/lib/* ：在 /tmp/zovl 建 symlink 指回原厂
       c. 遍历 /log/zvegisk/mods/<name>/system/lib/ ：
            在 /tmp/zovl 建 symlink 指向 /log 上的实体
            （同名则覆盖 b 步建的链接）
       d. mount --bind /tmp/zovl /usr/lib
       e. 对 /usr/share/app 和 /usr/bin 做同样的处理
    }
 ③ fork watchdog（仅 armed 时）
```

## 四、★★★ 要先回答：换哪些文件才叫"新菜单"

| | ZV-E10（老）| ZV-1F（新）|
|---|---|---|
| 入口 | `im.elf` 23KB | `im.elf`？ |
| 主框架 | **单体 `libObj.so` 21MB** | **`appFw.so` 53MB + `libObj.so` 3.9MB** |
| 菜单屏 | **214 个 `view*.uxc`** | **5 个 `master_*.uxc`** |
| 资源索引 | `global.xdb` 12KB | `global.xdb` 13KB |

### 三种策略

**A. 整套替换（激进）**
```
换 im.elf + appFw.so + libObj.so + 全部 share/app
⚠️ ZV-1F 的 appFw 可能依赖 ZV-E10 没有的硬件/驱动
⚠️ 成功率低
```

**B. 只换资源（保守）**
```
只换 .uxc / global.xdb，用 ZV-E10 的引擎读 ZV-1F 的资源
⚠️ 但 view*.uxc vs master_*.uxc 命名都不同 ⇒ 引用关系可能对不上
⚠️ 成功率低（除非格式兼容）
```

**C. 逐步试探（★ 推荐）**
```
第1步  只换 global.xdb（12KB→13KB）
       ⇒ 菜单结构有没有变化
第2步  换 color_cmn.uxc（312B→388B）
       ⇒ 配色有没有变（最安全，只影响颜色）
第3步  换一个 view*.uxc（viewBaseMenu.uxc 只有 84B）
       ⇒ 单个菜单屏有没有变
第4步  才考虑换 viewUnified2.so（14MB）
```

**★ 策略 C 的每步都是最小改动、可观察、崩了只影响当次。**

## 五、模块格式

```
/log/zvegisk/mods/<name>/
├── module.prop              元数据
└── system/                  路径对应 /usr 下的相对路径
    ├── lib/appFw.so
    ├── lib/libObj.so
    ├── bin/im.elf
    └── share/app/global.xdb
```

## 六、必须先做的离线验证

**① tmpfs 里能否建指向 vfat 文件的符号链接**
**② 两边 `global.xdb` / `.uxc` 格式是否兼容**

**★★★ ② 决定一切：**
- **兼容 ⇒ 走策略 C（只换小文件，风险低）**
**- 不兼容 ⇒ 只能策略 A（整套替换 70MB，风险高）**

## 七、内存预算

| 项 | 占用 |
|---|---|
| **tmpfs 覆盖层（符号链接）** | **几十 KB** |
| **大文件（appFw.so 53MB）** | **0（在 /log 上，符号链接指过去）** |
| **小文件（.uxc / global.xdb）** | **0（同上）** |
| **tmpfs 元数据开销** | **可忽略** |

**★ 所以这个方案几乎没有内存代价。**
