# 挂载是误判 + 两版镜像闭包对比

## 一、❌ 我的整条文件系统分析建立在错误前提上

**真机实测（Jiritsu root shell）：**
```
/dev/nflasha15 /usr ext2 ro,relatime,errors=continue 0 0
                              ↑↑ 只读挂载 —— /usr 一直是挂上的
```

**⇒ 判据 `s_mnt_count` 在 `ro` 挂载下【永远不变】，因为超块写不进去。**
**⇒ "文件系统挂不上去"这个前提从一开始就不成立。**

**dmesg 里 ext2 / VFS / mount 相关零条错误，也印证这一点。**

### 1.1 三处"差异"全部与挂载无关（已实测）

| 字段 | 结论 | 依据 |
|---|---|---|
| `s_inode_size` 256 | ❌ 不影响（128 更保险） | 256 那版也能挂载 |
| `s_blocks_count` 307,200 | ❌ 不影响 | ro 挂载不关心 fs 占多少 |
| `s_volume_name = usr` | ❌ 不影响 | 纯信息字段 |

**⇒ inode_size / 几何对齐 / resize2fs 全部是无用功。**

### 1.2 ★ 这是一条值得记住的方法论

**`s_mnt_count` 是【写】超块，而 `ro` 挂载禁止写超块。**
**⇒ 用"需要写入才能更新的计数器"作为"是否挂载"的证据，在 ro 场景下结构性无效。**

**同类错误在本项目已出现多次：**
```
· grep 前导零恒返回 0（假阴性）
· 手写 Thumb 扫描器返回 0（工具本身错）
· 子进程 /proc/self/maps 看不到父进程 dlopen（概念错）
· 本次 s_mnt_count 在 ro 下不变（判据结构性无效）
```
**⇒ 共同点：用一个"在目标场景下必然给出该值"的观测，去证明一个与它无关的结论。**

---

## 二、✅ 符号闭包：两版镜像完全一致，且与 ZV-1M2 原版一致

**实测（/lib 全库 + E10 rootfs 的 glibc 作为提供者，含版本化符号取基名）：**

| 库 | UND | 缺失 | 真实缺 | 缺什么 |
|---|---:|---:|---:|---|
| `appFw.so` | 753 | 4 | **2** | `infra_wrapper_bb::adf_ext_type`<br>`infra_wrapper_imaging::set_user_matrix` |
| `appFwBoot.so` | 36 | 2 | **0** | ✅ |
| `libObj.so` | 688 | 3 | **1** | `arch_phys_to_cache` |
| `libIMDB.so` | 6 | 2 | **0** | ✅ |
| `libMWF.so` | 103 | 2 | **0** | ✅ |
| `libmpr.so` | 304 | 5 | **3** | `GpsMgr::SetAllInvalidDefStrGpsInfo`<br>`std::basic_regex::_M_compile`<br>`std::regex_search<…>` |

**`n15_final.ext2` 与 `nflasha15_geom.ext2` 的上述结果【完全相同】。**

### 2.1 这 5 个缺口的性质（关键）

**全部是 `GLOBAL DEFAULT UND`（不是 WEAK）。但：**

| 符号 | 判断 |
|---|---|
| `arch_phys_to_cache` | **内核导出符号**，运行时由内核解析，不是库缺失 |
| `infra_wrapper_bb::adf_ext_type`<br>`infra_wrapper_imaging::set_user_matrix` | 无库提供；**在 ZV-1M2 原版里同样缺失** |
| `std::basic_regex::_M_compile` / `std::regex_search` | **两版 libstdc++ 字节完全相同（`cbb2534f…`，6.0.14 / GLIBCXX 到 3.4.14），都没有 std::regex** ⇒ **在 ZV-1M2 原版里同样缺失** |
| `GpsMgr::SetAllInvalidDefStrGpsInfo` | 同上，原版同样缺 |

**⇒ ⇒ 结论：这 5 个缺口在 ZV-1M2 原厂固件里【一模一样存在】。**
**⇒ 混合镜像没有引入任何新缺口。**
**⇒ 它们能工作靠的是懒绑定：只要那几条代码路径不被执行，就无害。**

---

## 三、两版候选镜像的差异

| 项 | `n15_final.ext2`（你的） | `nflasha15_geom.ext2`（我的） |
|---|---|---|
| 大小 | 314,572,800 B (300 MB) | **249,102,336 B (237.56 MiB)** |
| blocks | 307,200（占满分区） | **243,264（= 原厂）** |
| inodes | 77,216 | **60,960（= 原厂）** |
| inode_size | 128 ✅ | **128 ✅** |
| 卷标 | 空 ✅ | **空 ✅** |
| `/lib` 库数 | 191 | **195**（多 4 个 E10 专属库）|
| `share/app` | 224 | 224 |
| md5 | `41321cbc…` | `8a36ab7d…` |

**多出的 4 个（我的版本）：**
```
libInfraCameraAllSetting.so   libInfraCameraProfile.so
libInfraExtTransferApp.so     libclema.so
```
**⇒ 都是 ZV-E10 机型专属库，M2 的 libIMDB 不注册它们 ⇒ 保留无害、丢弃也无害。**

**⇒ 两版闭包结果完全相同，都可挂载（本机 loop 测试均通过）。**
**⇒ 既然挂载误判已澄清，几何差异不影响选择。**

---

## 四、⇒ 现在真正的未知

**/usr 能挂载、闭包与厂商一致 ⇒ 剩下的是【应用层为什么不出 UI】：**

```
· libIMDB 的 init 是否走到了 appFwBoot
· appFwBoot 的 application_System_Init 是否被 dlsym 到
· appFw 是否被 dlopen 成功（懒绑定下加载成功不代表功能可用）
· 五个 GLOBAL UND 里，是否正好有在启动路径上被调用的
  （尤其 infra_wrapper 那两个 —— 无任何库提供）
```

**⇒ 下一步需要的是运行时证据（agent 日志 / proc maps / dlopen 返回值），
不是再做静态分析。**
