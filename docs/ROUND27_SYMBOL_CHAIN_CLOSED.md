# 轮次 27：符号解析链彻底闭合（最后一个逻辑缺口补上）

## 一、此前的疑问

```
appFwBoot.so 的 .text 里调用 uxengine_init2 / UXEngine::gfxeng() 等一大堆 UXEngine 符号，
但 appFwBoot 自己只是 8 KB 的薄壳，不可能实现它们。
这些符号由谁提供？如果没人提供，dlopen(appFwBoot.so) 会成功，
但调用 Application_System_Init 时会在 uxengine_init2 处崩溃。
```

## 二、符号链验证（逐个核对）

`appFwBoot.so` 的未定义符号中，UXEngine/libra/ux 相关的全部来自 M2 的 `libObj.so`：

```
✓ UXEngine::gfxeng()
✓ UXEngine::resmgr()
✓ libra::ArenaParam::ArenaParam(unsigned
✓ libra::ArenaParam::setAcceptableType(unsigned
✓ libra::ArenaParam::setMaximumSize(unsigned
✓ libra::ArenaParam::setMinimumSize(unsigned
✓ libra::ArenaParam::setPriority(unsigned
✓ stella_print_log
✓ ux::core::ID::ZERO
✓ ux::core::Kernel::sys_setCurrentDirectory(char
✓ ux::gfxeng::AreaCheckExtensionManager::setCheckDataPath(char
✓ ux::gfxeng::AreaCheckExtensionManager::setLogPath(char
✓ ux::gfxeng::GraphicsEngine::getAreaCheckExtensionManager()
✓ ux::gfxeng::GraphicsEngine::setOverflowResizeFontAttribute(unsigned
✓ ux::gfxeng::GraphicsEngine::setOverflowResizeLayoutAttribute(unsigned
✓ ux::gfxeng::GraphicsEngine::sys_getInstance()
✓ ux::resmgr::ResourceManager::addArena(unsigned
✓ ux::resmgr::ResourceManager::setDatabase(char
✓ ux::resmgr::ResourceManager::sys_setFontNameWithNamespace(unsigned
✓ uxengine_init2
```

**libObj.so 导出的 uxengine 系列：`uxengine_init` / `init2` / `exit` / `res` / `sus` / `app_fw_init`**

## 三、这些符号怎么进入全局作用域（关键）

```
im.elf 的 .rodata 里有一张「选项名 → 默认值」表，含：
    /usr/lib/libObj.so
⇒ im.elf 在启动时 dlopen 它，flags 来自 argv 的 "lazy-global" = 0x101 = RTLD_LAZY|RTLD_GLOBAL
⇒ libObj 的符号进入【全局命名空间】
⇒ 之后 dlopen 的 appFwBoot.so 的未定义符号可以在全局作用域解析
```

## 四、决定性证据：M2 自己就是这么干的

```
E10 的 im.elf ：23,240 B，字符串表含 /usr/lib/libObj.so
M2  的 im.elf ：23,240 B，字符串表含 /usr/lib/libObj.so（完全一致）

选项表（两版一致）: td FS oM vl gV hZ BE qX ii ...
路径表（两版一致）: /setting/sen/smode
                    /usr/bin/im.elf
                    /usr/bin/e2fsck -y %s
                    /usr/bin/sen.elf &
                    /usr/upgrade
                    /usr/lib/libObj.so

⇒ M2 的 im.elf 也是通过同一个机制加载 libObj.so
⇒ 我们保留 E10 的 im.elf 不影响这条逻辑（这部分两版完全相同）
⇒ 也就是说：**我们复现的正是 M2 原厂的行为路径**
```

## 五、为什么决定不把 libObj 加进 preload

```
M2 的 libObj.so：.init_array 428 B = 107 个构造函数
⇒ 预加载会在每个进程里跑 107 个 C++ 全局构造
⇒ 而 im.elf 本来就会 dlopen 它（原厂路径）
⇒ 结论：走原厂路径，不画蛇添足

（补充：dlopen 是共享映射，物理内存只占一份；
  但 107 个构造函数会在每个进程中各执行一遍，这没有意义且有风险）
```

## 六、完整符号解析链（最终形态）

```
① im.elf (E10) 解析选项 → dlopen("/usr/lib/libObj.so", RTLD_GLOBAL)
   bind 已生效 ⇒ M2 的 3.9 MB 图形引擎 ⇒ uxengine 符号进全局
② im.elf 调 IMDB_get_entries（M2 的 libIMDB）拿到 228 条表
③ 逐条：dlopen(路径) → dlsym(符号名) → 调用
   · appFwBoot.so 条目：dlopen ✓ → dlsym("Application_System_Init") ✓ → 调用
     → 里面的 uxengine_* 从 ① 的全局作用域解析 ✓
④ appFwBoot 再 dlopen("appFw.so") → dlsym("Application_Start") → 调用
⑤ appFw 里的 UXEngine 调用同样走 ① 的全局符号 ✓

⇒ 符号层面已无缺口，且与 M2 原厂路径一致
```

## 七、轮次 27 结论

```
在 11 个"硬墙假设"排除、6 个真实 bug 修复之后，
本轮补上了最后一个逻辑缺口：UXEngine 符号的来源与加载次序。

关键是"两版 im.elf 的选项表完全相同"这一证据：
它说明这套加载机制不是我们臆想的，而是 M2 原厂就在用的。
```
