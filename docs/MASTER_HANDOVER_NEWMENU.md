# ZV-1M2 新菜单移植 —— 权威交接书

> 生成于 goal round 15（2026-10-28 03:xx）。
> 汇总 15 轮静态分析的全部结论、工具、执行路径与失败判据。
> **只看这一份就能开工。**

---

## 一、目标与现状

**目标**：把 ZV-1M2（M2）的完整新菜单移植到 ZV-E10 上，真机出现新 UI。

**现状**：
```
✅ 静态分析全部完成，9 个"硬墙"假设已排除，5 个工具 bug 已修
✅ 交付物就绪：镜像 nflasha15_v9.ext2 + 可逆测试工具 bindm2
❌ 尚未在真机上跑过一次（这是唯一的未完成项）
```

---

## 二、两条执行路径（先做 A）

### 路径 A：loop + bind（推荐，零风险，不刷机）

```bash
# 0) 前提：相机正常开机、老菜单可用、WiFi 已连、agent 在跑
cd /root/fw/oss/tools/payload && python3 cam.py ping        # 期望 PONG

# 1) 看当前状态
python3 /root/fw/oss/tools/bindm2/deploy_bindm2.py --status

# 2) 部署（237.6 MB，1 MB 分块 + 三重试 + 断点续传，约 4~8 分钟）
python3 /root/fw/oss/tools/bindm2/deploy_bindm2.py \
        --image /mnt/build/nflasha15_v9.ext2

# 3) 重启相机 → 看屏幕
#    成功：出现 M2 新菜单
#    失败：再重启一次即完全恢复（bind 不持久，不写任何 flash）
```

**原理**：preload 里放三个东西，靠 SONAME 顶替 + 构造函数里的 loop+bind，
让 im.elf 在装载阶段就拿到 M2 的模块表和库，而 `/usr` 分区一个字节没改。

```
preload = /usr/tool/libefence.so : /log/libIMDB_m2.so : /log/bindm2.so
                      ↑白名单占位        ↑SONAME 顶替      ↑loop+bind
```

**若有 SD 卡**：把 `nflasha15_v9.ext2` 拷成卡上 `/sd/m2usr.img`，省掉 WiFi 上传（bindm2 会自动优先用它）。

### 路径 B：fastboot 刷 /usr（A 验证 bind 链路后再做）

```bash
cd /root/fw/oss/fastboot
python3 pico/inject_payload.py --port /dev/ttyACM0 --file prebuilt/stage1.bin --duration 600
python3 host/flash_partition.py --part nflasha15 \
  --image /mnt/build/nflasha15_v9.ext2 \
  --part-start 0x2D800000 --chunk-kb 768 --full --verify-every 30
```

**恢复**：刷回 `/root/fw/zve10_backup/nflasha15.img`
**判据**：`ls -l /usr/share/app/global.xdb` → `12252`=没刷进去 / `16112`=刷进去了

**★ 两方案最终生效的文件集合完全相同** ⇒ A 的结果可预测 B。

---

## 三、失败时怎么读（取证）

### 3.1 首选：/log/bindm2.log（一次开机给全部判据）

```
=== bindm2 start ===
image: /log/m2usr.img          ← 用了哪个镜像路径
loopno=N
LOOP_SET_FD=0                  ← 0 成功；负=errno
mount ext2=0                   ← -19=镜像不是 ext2；-1=权限
ext2 mounted
bind /usr/lib=0                ← -22=EINVAL（flags 错，已修）
bind /usr/share=0
-- after bind --               ← ★ 这段能证明 bind 是否真的生效
libObj.so      = 3931624       期望 M2 值（E10 是 21305528）
appFw.so       = 64273528      期望（E10 不存在→-1）
appFwBoot.so   = 8312
libIMDB.so     = 40596
libInfraSKD.so = 7259708
global.xdb     = 16112
argv: /usr/bin/im.elf debug s g r u l i a lazy-global
MemFree: NNNN kB
=== bindm2 done ===
```

**判读：**
| 现象 | 结论 | 下一步 |
|---|---|---|
| `appFw.so = 64273528` | **bind 完全生效** | 若仍无 UI ⇒ app 层运行问题，看 3.2 |
| `appFw.so = -1` | bind 没生效 | 看上面 mount/bind 的 errno |
| `mount ext2 = -19` | 镜像无效 | 重新上传（校验超块 magic） |
| 日志里连 start 都没有 | im.elf 没跑到 preload | 看 /tmp/.bindm2.log；再不行查 im.elf 是否被 exec |
| 有 `ext2_magic: 53ef` | 镜像完好，失败另有原因 | 查权限/loop 状态 |

### 3.2 深入：im.elf 映射了哪些库

```bash
for p in /proc/[0-9]*; do c=$(cat $p/comm 2>/dev/null); [ "$c" = "im.elf" ] && {
  echo "PID ${p#/proc/}"; busybox cat $p/maps | busybox grep -E "libObj|appFwBoot|appFw|libIMDB"; }; done
```
| maps 里有 | 结论 |
|---|---|
| libObj.so | 选项 Is 的 dlopen 成功 |
| + appFwBoot.so | M2 libIMDB 装载成功并调用了 init |
| + appFw.so | Application_System_Init 跑通 |
| 没有 appFw.so | 卡在 Application_System_Init（同步消息/符号解析） |

### 3.3 最底层：内核与挂载

```bash
cat /proc/uptime
dd if=/dev/nflasha15 bs=1024 skip=1     count=1 | busybox md5sum   # 超块
ls -l /usr/share/app/global.xdb                                    # 刷写是否生效
dmesg | busybox grep -iE "oom|undefined symbol|EXT2|VFS|segfault"
```

---

## 四、已确认的事实（不用再查）

```
① 文件系统：e2fsck 干净；几何/UUID/特性位与原厂一致
② /usr 一直能 ro 挂载（s_mnt_count 判据在 ro 下无效，别再用）
③ 符号闭包：im.elf/bootin/sen 只缺 2 个 CRT 弱符号；
             appFwBoot 缺 0；appFw 缺 2（与 M2 原版相同）
④ 传递依赖：全镜像 224 个二进制【零缺失】
⑤ 版本正确性：v9 里业务库全部 M2 版；只有 kmod/sabin/启动锚点/libosal_uipc 保留 E10
⑤b ★ 工具正确性：bindm2/beacon2 的 syscall 包装器已修复 r7 装载 bug（见 ROUND23）
⑥ im.elf 的依赖：除 libIMDB(M2) 与 libosal_uipc(E10) 外，其余全部两机型字节相同
⑦ 内存：两机型 Linux 可用均 141.7 MB（kemco.txt 实算）⇒ 内存不是瓶颈
⑧ 资源格式：global.xdb 头部两版相同（uxa v7）；area_check_data.dat 同为 UXAC v2
⑨ 显示通路：M2 的图形引擎用 /dev/dmpgles2，与 E10 同一个设备
⑩ loop：内核有 loop 驱动；E10 自己的 libObj 就在用 /dev/loop6、/dev/loop7
⑪ CP 固件：两版 av-cam.bin 差 0.54%，ADF/AIP 子系统字符串完全一致 ⇒ 同代同协议族
⑫ IMDB 装载器：dlopen(路径) → dlsym(符号) → 调用，失败【容忍】
⑬ libIMDB 两版 SONAME 相同、无版本冲突、构造函数无副作用 ⇒ SONAME 顶替成立
⑭ im.elf 按路径 dlopen /usr/lib/libObj.so（选项 "Is" 的默认值）
```

## 五、已知的未知（只能真机验证）

```
· M2 的 appFw 能否与 E10 的 CP 完成启动握手
· UXEngine 初始化后是否会真的产生绘制
· 那条 type=9 / id=0x008F013A 的同步消息是否有接收方
· M2 的 appFw 引用 /dev/nflasha30（E10 无此分区，预计非致命）
```

---

## 六、文件清单

### 镜像
```
/mnt/build/nflasha15_v9.ext2        249,102,336 B  md5 f6ae9f576821b35ce33d172a7cab2859  ★待用
/mnt/build/nflasha15_v8.ext2        （v9 的前一版，差 13 个库）
/mnt/build/nflasha15_v7.ext2        （再前一版）
/root/fw/zve10_backup/nflasha15.img 原厂镜像（回滚用）
/mnt/build/nflasha2_factory.img     原厂 /setting（回滚用）
```

### 工具
```
/root/fw/oss/tools/payload/          agent 通道：cam.py / send_payload.py
/root/fw/oss/tools/bindm2/           bindm2.so（loop+bind+自诊断）/ libIMDB_m2.so
                                     deploy_bindm2.py（分块+续传+校验）/ make_tools.sh
/root/fw/oss/tools/beacon/           beacon2.so（环境取证，写到 /tmp 和 /log）
/root/fw/oss/fastboot/               Pico 注入 + fastboot 刷写/校验
```

### 文档（按重要性）
```
MASTER_HANDOVER_NEWMENU.md            ← 本文
TOMORROW_ONE_PAGER.md                 一页纸操作
BIND_M2_NOFLASH_APPROACH.md           bind 方案原理（含时序证明）
BLOCKER_FOUND_LIBMPRCTRL.md           阻塞点 1：第二级依赖缺失
BLOCKER_8_LIBS_WRONG_VERSION.md       阻塞点 2：8 个库版本错配
ROUND4_FULL_VERSION_AUDIT.md          97 个版本错配项的处理表
ROUND9_CODE_REVIEW.md                 mount flags 致命 bug
ROUND13_DISPLAY_AND_LOOP.md           显示与 loop 的双重确认
ROUND14_IMELF_STABILITY.md            启动锚点依赖稳定性
MEMORY_HYPOTHESIS_REFUTED.md          内存假说的证伪
STATIC_ANALYSIS_NO_UI_VERDICT.md      15 项逐项排除表
```

---

## 七、下一步（唯一未完成项）

```
开机 → 跑 deploy_bindm2.py → 重启 → 看屏幕 → 按第三节判读
把 /log/bindm2.log 发我，我就能定位到具体环节
```
