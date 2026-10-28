# 第一个 overlay 验证试验（改一个颜色，看屏幕变不变）

> 依据：zv-e10-re 已**硬件验证**——把 `color_cmn.uxc` 的 `0x400c` 改成品红，
> 实时取景的框线会变品红；且该文件**没有校验和**，改色不会被完整性拒绝。

---

## 一、测试件（已生成）

```
tools/zvegisk/testmod/
├── mkcolor.py                     生成工具
├── color_cmn_magenta.uxc          修改后的调色板（312 B）
└── newmenu_test/
    ├── module.prop                模块元数据
    └── system/share/app/color_cmn.uxc
```

**改动内容（已校验）：**
```
原始: 0xBC..0xBE = 33 33 33    (灰)
修改: 0xBC..0xBE = ff 00 ff    (品红)
偏移 = 0x58 + 12*8 + 4 = 0xBC   ⇒ 记录 12（id 0x400c）的 R/G/B
alpha 0x80 保持不变（半透明）
文件大小 312 → 312（不变）
```

**★ 为什么选它做第一个试验：**
| 理由 | 说明 |
|---|---|
| **无校验和** | 改了不会被拒绝 |
| **只动 3 字节** | 出错面积最小 |
| **语义风险最低** | 最坏就是框线颜色难看 |
| **效果肉眼可见** | 不需要读日志 |
| **别人验证过** | 知道预期结果：框线变品红 |

---

## 二、部署步骤

```
① 上传到相机：
     /log/zvegisk/mods/color-test/module.prop
     /log/zvegisk/mods/color-test/system/share/app/color_cmn.uxc

② 写 SD 卡（或 /setting）的 boot_mode = "new"

③ 确认 zvegisk.so 的 Phase 2 实现（mount --bind）

④ 设 dmode=3p + preload

⑤ 重启

⑥ ★ 观察：实时取景的框线应该变品红色
```

**⚠️ ③ 是当前还没实现的部分（Phase 1 只写日志，不 bind）。**

---

## 三、如果框线变品红 ⇒ 证明了什么

**一次性证明四件事：**

1. **preload 钩子在真机上工作**（我们的 .so 被加载了）
2. **构造函数在 UI 读资源之前运行**（时序对）
3. **`mount --bind` 覆盖 /usr 生效**（覆盖机制通）
4. **目录级覆盖（符号链接农场）可用**（要用到的话）

**⇒ 之后所有"换资源"的实验都有了可靠基础。**

## 四、如果没变 ⇒ 可能的原因（按优先级）

| 可能 | 怎么查 |
|---|---|
| **.so 没被加载** | `/log/zvegisk.log` 有没有 "zvegisk start" |
| **bind 时机太晚**（UI 已读走） | 看 im.elf 的构造顺序 |
| **`/usr/share/app` 的 bind 没做** | Phase 2 还没实现 |
| **颜色 0x400c 不控制取景框线** | 换 `04`（蓝）或其它 id 试 |
| **引擎有缓存** | 需要触发一次 UI 重绘 |

---

## 五、回退

```
① 拔 SD 卡 / safemode 文件     → 下次开机不 bind
② 改 dmode=3              → 不加载 preload
③ 断电重启               → bind 全消失（本来就不持久）
```

**★ 这个试验**没有任何持久风险** —— `color_cmn.uxc` 在原厂分区里，我们只是内存覆盖。**
