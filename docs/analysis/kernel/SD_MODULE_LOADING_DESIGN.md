# 从 SD 卡加载模块资源 —— 设计（含冲突解决）

## 一、问题：不能直接从 SD 卡 bind

**vfat 挂载限制：**
- **多重读写挂载：第二个直接 EINVAL**
- **一个读写 + 一个只读：读写那个 EBUSY**

**⇒ 我们若持续挂着 SD，**相机自己就无法挂载 ⇒ 无法录制/回放**。**

**实测依据：**
- **早期 `mount(dev, mnt, "vfat", 0, "rw,uid=0,gid=0,fmask=0000,dmask=0000")` → EINVAL**
- **改造后 `mount(dev, mnt, "vfat", 0, 0)` 可读写成功**
- **只读挂载可成功（多挂载需要全只读）**

## 二、★ 解决方案：拷到内部再 bind

```
开机时（zvegisk.so 子进程）：
  ① 挂 SD 卡（只读，毫秒级窗口）
  ② 读 /zsd/zvegisk/boot_mode
  ③ 若 = "new"：
       · 把 /zsd/zvegisk/mods/<name>/system/... 拷到 /log/zvegisk_mod/
       · 记录拷贝清单
  ④ ★ 立刻卸载 SD（把卡还给相机）
  ⑤ 从 /log/zvegisk_mod/ 逐文件 mount --bind 到 /usr/...
  ⑥ fork watchdog
```

**★ 关键：SD 只在步骤 ①-④ 被占用，之后完全释放。**

## 三、空间与时间

| 项 | 数值 |
|---|---|
| **/log 可用空间** | **500 MB（nflasha11）** |
| **典型模块大小** | viewUnified2.so 14MB + global.xdb 12KB + 布局 3KB |
| **拷贝耗时** | 约 1-2 秒（1.9 MB/s 实测速率） |
| **⇒ 可接受** | 开机多 1-2 秒 |

## 四、★★★ 恢复方式在拷贝方案下是否仍有效

| 恢复方式 | 是否有效 | 说明 |
|---|---|---|
| **拔 SD 卡** | ✅ **有效** | 下次开机找不到 SD ⇒ 不拷贝 ⇒ 不 bind ⇒ 原厂 |
| **卡上放 safemode** | ✅ 有效 | 跳过一切 |
| **改 `/setting/mode/dmode`** | ✅ 有效 | 不加载 preload |
| **`/setting/zvegisk/safemode`** | ✅ 有效 | 内部标志，优先级最高 |

**★ 注意差别：拔卡只影响**下次开机**，当次已拷贝的仍在 /log 里（但不会重新 bind）。**

## 五、为什么不用"挂载后不卸载"

**如果保持 SD 挂载以直接 bind：**
- **bind 的源必须在挂载期间一直可访问**
- **⇒ SD 永久被我们占着**
- **⇒ 相机无法录制/回放**
- **⇒ 不可接受**

**⇒ 必须先拷贝到内部。**

## 六、备选方案（如果拷贝太慢）

**方案 B：模块文件直接放 `/setting`**
- **优点：不需要挂 SD，无冲突**
- **缺点：/setting 只有 20 MB，装不下 14 MB 的 viewUnified2.so 加上其它**

**方案 C：模块文件放 `/log`，用 agent 或 SD 卡上传**
- **优点：无冲突、空间充足**
- **缺点：不能用"拔卡"恢复（但可用 safemode / dmode）**

**★★★ 推荐：配置（boot_mode）放 SD（便于电脑改），
模块文件放 `/log`（用 agent 上传一次即可，不需要每次开机拷）。**

**⇒ 这样开机时连 SD 都不用挂 —— 彻底无冲突。**

## 七、推荐架构（最终版）

```
SD 卡（可选，只放配置）          内部（模块与状态）
/zvegisk/                        /log/zvegisk/
 ├─ boot_mode  "old"/"new"        ├─ mods/<name>/system/...
 └─ safemode                      └─ （模块文件，用 agent 上传一次）
                                  
                                 /setting/zvegisk/
                                  ├─ boot_count
                                  ├─ armed
                                  └─ safemode
```

**开机流程：**
```
挂 SD（只读）→ 读 boot_mode → 卸载 SD → 
若 new：从 /log/zvegisk/mods/ 做 bind
```

**★★★ 优点：**
- **SD 只读一个文件（毫秒级）**
- **模块在内部，不占 SD**
- **拔卡 = 下次开机回原厂**
- **完全不影响相机自己用 SD**

## 八、对照：A6700 的做法（供参考）

**a6700-powerful 是**持久化改 flash**：**
```
修改 appFw.so 的固定文件偏移（功能权限字节）
修改 WBI2 分区的字段
修改 CP 分区 1 字节
```
**⇒ 重启保留，但需要完整备份 + 镜像校验 + 写入 flash。**

**★★★ zvegisk 选择不写 flash，代价是改动不持久（重启恢复）——
这是有意的安全取舍。**
