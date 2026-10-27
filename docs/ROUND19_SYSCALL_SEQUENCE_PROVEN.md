# 轮次 19：bindm2 的完整 syscall 序列在本机实跑验证通过

> 上一轮验证了 ld.so 语义（SONAME 顶替 / 路径 dlopen）。
> 本轮验证**内核侧**：loop 设备 + ext2 挂载 + bind 挂载，用真实 ext2 镜像跑通。

## 一、实验设置

```
真实 ext2 镜像 4 MB（mke2fs -t ext2 -d src），内含:
    lib/libObj.so            = "M2-LIB-MARKER-value-3931624"
    share/app/global.xdb     = "M2-SHARE-MARKER-globalxdb"
目标目录:
    target/lib/libObj.so     = "E10-OLD-libObj"     ← 待覆盖
程序: test.c —— 完全照搬 bindm2.c 的 syscall 序列
```

## 二、实跑结果（全部成功）

```
LOOP_CTL_GET_FREE                                              -> 15 (errno=0)
LOOP_SET_FD                                                    -> 0  (errno=0)
mount(/dev/loop15, target/mnt, "ext2", MS_RDONLY, NULL)         -> 0  (errno=0)
bind(target/mnt/lib,   target/lib,   NULL, MS_BIND, NULL)       -> 0  (errno=0)
bind(target/mnt/share, target/share, NULL, MS_BIND, NULL)       -> 0  (errno=0)

验证:
    target/lib/libObj.so          = "M2-LIB-MARKER-value-3931624"   ← 覆盖成功
    target/share/app/global.xdb   = "M2-SHARE-MARKER-globalxdb"     ← 覆盖成功

挂载表（★ 这是 bind 生效后的特征签名）:
    /dev/loop15 /tmp/bindtest/target/mnt   ext2 ro
    /dev/loop15 /tmp/bindtest/target/lib   ext2 ro
    /dev/loop15 /tmp/bindtest/target/share ext2 ro
    ⇒ bind 之后 /usr/lib 会显示为【同一个 loop 设备】
```

## 三、反向验证：证明第 9 轮修的 bug 确实致命

```
错误写法  mount(src, dst, NULL, (unsigned long)"bind", NULL)
              -> -1, errno=22 (EINVAL)      ★ 必然失败
正确写法  mount(src, dst, NULL, MS_BIND /*0x1000*/, NULL)
              -> 0                          ✓
```

**⇒ 若不修这个 bug，明天真机上两个 bind 都会 EINVAL，
   日志只会显示 `bind /usr/lib=-22`，还得再花一轮排查。**

## 四、diagnose.py 新增判读规则

```
若 /proc/mounts 里出现 m2root            ⇒ loop 挂载成功
若 /usr 或 /usr/lib 显示为 /dev/loopN    ⇒ bind 已生效（本机实测特征）
若 m2root 有但 /usr 无 loop 设备          ⇒ bind 可能没成功
```

## 五、两轮实证合起来覆盖了什么

```
轮次 18（用户态 / ld.so）:
  ✓ SONAME 相同的 preload 顶替 DT_NEEDED
  ✓ 按路径 dlopen 走文件系统，拿到 bind 后的新文件
  ✓ 两者共存不干扰

轮次 19（内核侧）:
  ✓ LOOP_CTL_GET_FREE / LOOP_SET_FD
  ✓ mount ext2 只读
  ✓ bind 挂载（含正确 flags —— 反向验证了错误写法必失败）
  ✓ 覆盖后的内容正确

⇒ bindm2 的每一行关键操作都已被实证跑通
⇒ 剩余未知只有：真机的权限环境、老 glibc 差异、appFw 与 CP 的运行时握手
```
