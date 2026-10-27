# 内存：最可能的根本阻塞（真机实测数据）

## 一、真机内存实测（老菜单运行中，通过 agent）

```
MemTotal:        128,796 kB   (125.8 MB)
MemFree:           3,324 kB
Buffers:             884 kB
Cached:           39,744 kB
SwapTotal:               0 kB   ← 无 swap
AnonPages:        58,816 kB   ← 不可回收
Mapped:           30,072 kB   ← 当前已映射的库/文件
Slab:              9,444 kB
PageTables:        4,504 kB
VmallocTotal:  1,032,192 kB
VmallocUsed:       1,616 kB

overcommit_memory: 1   ★ 永远允许 overcommit
overcommit_ratio: 50
```

## 二、★ 关键：`overcommit_memory = 1`

**⇒ `mmap` / `dlopen` **不会因为内存不足而失败** —— 它们总是成功返回。
**⇒ 失败发生在**后续触碰页面时**（缺页 → 无内存可回收 → OOM kill 或 SIGBUS）。**

**⇒ 这正好解释现象：**
```
· 没有 UI        ← appFw 的工作集无法全部驻留
· 没有日志       ← 没跑到能写日志的阶段
· 没有崩溃信息   ← overcommit 模式下缺页失败不产生明确的 dlopen 错误
```

## 三、容量核算

### 3.1 相机的可用内存

```
总计           128,796 kB
减 AnonPages   -58,816 kB   （不可回收，无 swap）
减 Slab/PT     -13,948 kB
减 内核自身     约 -8,000 kB
────────────────────────────
理论可给文件页  约 48,000 kB (47 MB)
其中当前已用    Mapped 30,072 kB 中大部分是活工作集
实际可腾出      约 10,000 ~ 18,000 kB (10~18 MB)
```

### 3.2 新菜单需要映射的

```
appFw.so             64,273,528 B  = 61.3 MB   ← ★ 单它就超
libObj.so             3,931,624 B  =  3.7 MB
appFwBoot.so              8,312 B
area_check_data.dat   1,142,255 B  =  1.1 MB
area_check_data_vertical 1,024,593 B = 1.0 MB
image.uxb             2,281,172 B  =  2.2 MB
字体/arena 等         （uxengine arena 还要堆内存）
────────────────────────────────────────────
仅文件映射合计       约 70 MB
```

### 3.3 ⇒ 缺口

```
需要约 70 MB（其中 appFw 单独 61.3 MB）
可腾出 10~18 MB
★ 缺口 50 MB 以上
```

## 四、⚠️ 但不算"证明必死"——一个重要的缓和因素

**共享库的 `mmap` 是**按页按需加载**的：只有**实际执行到的代码页、访问到的数据页**需要驻留，
其余留在 /usr 上（`/usr` 是 ext2 只读文件系统，可直接作为 page cache 回读源）。**

**⇒ 所以真正的判据不是"文件 61.3 MB"，而是"**appFw 的热工作集**能否驻留"。**

**⇒ 若热工作集 < 15 MB，理论上仍可能跑起来（但会持续缺页、极慢）。**
**⇒ 若 > 20 MB，几乎必然被 OOM 打掉。**

**⇒ 目前无法静态判定热工作集大小 —— 只能实测。**

## 五、★★ 一个旁证：ZV-1M2 的分区布局完全不同

**从 M2 的 `partinf.conf`：**
```
ZV-1M2:  nflasha15 偏移 0x2E800000  大小 0x2BC00000 = 700 MB
ZV-E10:  nflasha15 偏移 0x2D800000  大小 0x12C00000 = 300 MB
```

**⇒ 两者的整个分区布局不一样（M2 的 /usr 是 700 MB，是 E10 的 2.3 倍）。**

**⇒ 这提示 ZV-1M2 是**更高配置的硬件**（更大的存储，很可能也有更多可用 RAM）。**

**⇒ 「M2 的 61 MB appFw 在 M2 上能跑」**不能推出**「在 E10 上也能跑」。**

## 六、⇒ 这个假设的解释力

**它能解释此前所有观察：**

| 观察 | 内存假设的解释 |
|---|---|
| 完全无 UI | appFw 工作集无法驻留 |
| 无任何日志 | 没跑到能写日志的阶段 |
| dmesg 无 ext2/VFS 错误 | 挂载确实成功（已确认 ro 挂载） |
| 无明确崩溃信息 | overcommit=1 下缺页失败不产生 dlopen 错误 |
| 刷回老菜单正常 | 老菜单只需 21 MB libObj |

**⇒ 比"参数分叉走错分支"更能解释"完全静默"。**
**（若走错分支加载 AdjChkConfig，理论上仍会有某种 UI 或日志。）**

## 七、⇒ 判决性实验（都很便宜）

```
① 刷 hybrid 后，开机 10 秒内从 agent 读：
     cat /proc/meminfo
     ls /proc/*/maps | 找 appFw.so
     dmesg | grep -i "oom\|Out of memory\|killed process"
   ⇒ 若出现 OOM kill 或 appFw.so 只映射了一部分 ⇒ 证实

② 观察 appFw.so 的 Rss 增长：
     cat /proc/<pid>/smaps | grep -A5 appFw.so
   ⇒ 看 Rss 卡在某个值不动 ⇒ 热工作集上限

③ 对照实验：只放一个 61 MB 的假库，让老菜单 dlopen 它
   ⇒ 若老菜单也死 ⇒ 与内容无关，纯内存
```

## 八、若证实是内存，可能的出路（都很难）

```
· 减小 E10 的 memrsv 预留，把更多 RAM 给 Linux
    ⇒ 需改 bootloader/cmdline，且 CP 可能真的需要那些区域
· 只保留 appFw 的部分功能（不现实，无法拆分）
· 放弃 M2 供体，改回 ZV-1F（appFw 53 MB，仍偏大）
· ★ 接受"新菜单在 E10 上跑不了"这个结论
```

## 九、诚实边界

**以上是**强假设 + 定量核算**，不是证明：**
```
✓ 已证实：MemTotal 128,796 kB、AnonPages 58,816 kB、overcommit=1、无 swap
✓ 已证实：appFw.so = 61.3 MB、资源全部到位
✗ 未证实：appFw 的热工作集有多大（只能实测）
✗ 未证实：ZV-1M2 的实际可用 RAM（需它的 boot 区/cmdline）
```
