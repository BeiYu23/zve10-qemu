# ★ 找到"刷入后无 UI"的静态原因：传递依赖缺 libmprctrl.so

> 2026-10-28 goal round 2。这是**第一个被静态分析证实的、确凿的启动阻塞点**。

## 一、因果链（完整可验证）

```
① M2 的 appFw.so
      DT_NEEDED → libNetCommon.so

② 我们 hybrid 里的 libNetCommon.so 是【E10 版】
   （它属于构建时保留为 E10 版的 16 个库之一，大小 25,116 vs M2 的 25,164）

③ E10 版的 libNetCommon.so
      DT_NEEDED → libmprctrl.so

④ libmprctrl.so 是 E10 专属库（263,812 B）
      M2 的 /usr/lib 里【没有】⇒ 我们的 hybrid 里也没有

⑤ ⇒ dlopen("appFw.so") 在解析第二级依赖时失败
   ⇒ appFw 的 Application_Start 永远不会被调用
   ⇒ 无 UI、无日志（`/log` 又在 `/usr` 之后挂载，所以连日志目录都没有）
```

**为什么之前没发现**：所有检查都只做了 `appFw.so` 的**直接** `DT_NEEDED`（14 个全齐），
没有做**传递闭包**。这个缺口在第二级。

## 二、修复与验证

```
修复：把 E10 的 libmprctrl.so (263,812 B, mode 555) 放入 hybrid 的 /lib
产物：/mnt/build/nflasha15_v7.ext2
      249,102,336 B
      md5 c6f3b4ea46f9cdb755c7ba737925f422
      e2fsck 通过；985 文件；228,908/243,264 blocks
```

### 复验 1：全镜像传递依赖闭包

```
根 = 镜像内全部 224 个 .so / .elf
遍历到 185 个唯一库
结果：★ 零缺失
```

### 复验 2：与 M2 原厂 /usr/lib 对照

```
M2 /usr/lib : 190 个文件
v7  /lib    : 197 个文件
M2 有而 v7 缺 = 0 个   ← 完整覆盖
v7 多出 7 个 = githash + 6 个 E10 专属库（含本次补的 libmprctrl.so）
```

### 复验 3：运行时 dlopen 名字核对

```
libObj.so 运行时会 dlopen：libInfra.so / libgrm.so / libgpe.so /
  libDMPGlslc.so / libdmpNative.so / gpslogdiag.so / libSnr.so …
这些名字在 M2 自己 /usr/lib 里【也不存在】
⇒ 是遗留/可选名字，M2 上同样装载失败并被容忍
⇒ 不构成缺口
```

## 三、同时修复的另一处缺陷

```
share/avsystemsound/beeplist.txt
  hybrid 原为 5,239 B（E10 版） → 已换成 M2 版 15,586 B
```

## 四、结论

```
v7 是目前最完备的待刷镜像：
  ✅ 传递依赖零缺失（224 个二进制全查）
  ✅ 完整覆盖 M2 的 /usr/lib
  ✅ 符号闭包与 M2 原版等价
  ✅ 资源全为 M2 版
  ✅ e2fsck 干净、几何与原厂一致

⇒ 如果 v7 刷入后仍无 UI，那么"缺文件/缺依赖"这条已可完全排除，
  剩下的就只能是刷写是否生效、或运行时（CP 协议/同步消息）层面。
```

## 五、方法论教训（值得记住）

```
本次错误模式：只检查了「直接依赖」，没检查「传递依赖」
同类风险：符号闭包做了全量，但 DT_NEEDED 只做了第一层
⇒ 以后凡"能否 dlopen 成功"的判定，必须做【传递闭包】
```
