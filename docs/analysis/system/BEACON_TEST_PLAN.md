# beacon 信标测试方案（把"启动链断在哪"变成二值事实）

## 为什么需要它

```
已确定静态无碍: bootin.elf / im.elf 符号闭包完整（UND 47/64，真实缺失 0）
未知:          im.elf 到底执行了没有
混淆因素:      hybrid 里 WiFi 是混搭的（见下），agent 不可达 ≠ agent 没运行
⇒ beacon 不依赖网络，直接回答"im.elf 有没有跑"
```

## ★ hybrid 里发现的真实 WLAN 版本错配

```
文件                                 hybrid   E10   M2
libWlanControlCommon.so              M2       ≠     M2    ← 错配
libInfraNetConnectionManager.so      M2       ≠     M2    ← 错配
libWlanAdjust.so                     E10      E10   ≠     ← 错配
libWlanResource.so                   E10      E10   ≠     ← 错配
kmod/bcmdhd.ko                       E10      E10   ≠     ✓ 正确
/usr/wlan/bcm/bcm4339*.bin           两版相同              ✓ 正确
```

**⇒ WLAN 栈是「M2 的 ControlCommon/NetConnMgr + E10 的 Adjust/Resource」混搭。
`libWlanAdjust` 依赖 `libWlanControlCommon`，版本错配足以让 WiFi 起不来。
⇒ 所以「agent 连不上」有两层可能：a) im.elf 没跑；b) 跑了但 WiFi 没起来。
⇒ beacon 专门排除 b。**

## beacon.so（已构建）

```
/mnt/build/beacon/beacon.so        5,640 B
  NEEDED            0      ← 不依赖任何库
  未定义符号        0
  .init_array       1 项   ← 构造函数注册正确
  只做裸 syscall: open/write/read/close/getpid，不 hook 不 exit
  输出:
    /tmp/.beacon.txt      pid + argv（/tmp 最早挂载）
    /log/.beacon.txt      同上（持久，重启后可读）
    /log/.beacon.count    每次构造追加一个 'x'（计数）
```

## 两阶段测试（第一阶段完全不用刷机）

### 阶段 1：验证 beacon 机制（原厂 /usr，只改 /setting 一个文件）

```sh
/root/fw/oss/tools/beacon/beaconctl.sh deploy
# 重启相机
/root/fw/oss/tools/beacon/beaconctl.sh read
```

**预期：** `/log/.beacon.txt` 出现多条（bootin 会用单字母参数起 7 个 im.elf 实例）
```
BEACON pid=157 argv=/usr/bin/im.elf debug s g r u l i a lazy-global
BEACON pid=1xx argv=...
...
```
**⇒ 这一步同时把「真机 im.elf 的完整 argv」拿到手（之前只能从 QEMU 推）。**

### 阶段 2：hybrid 上跑同一个 beacon（需要刷一次 /usr）

```
preload 保持不变（/usr/tool/libefence.so:/log/beacon.so）
刷 hybrid 的 /usr → 开机（预期无 UI）→ 刷回原厂 → 读 beacon
```

| `/log/.beacon.txt` | 结论 |
|---|---|
| **有多条记录** | `im.elf` 正常启动 ⇒ 断点在 appFwBoot / appFw / CP 协议，与启动链无关 |
| **文件不存在** | `im.elf` 从未执行 ⇒ 断点在 bootin → im.elf（挂载/exec 失败） |

## 回滚

```sh
/root/fw/oss/tools/beacon/beaconctl.sh stock     # 恢复 3e + agent 自启
/root/fw/oss/tools/beacon/beaconctl.sh clean     # 删掉 beacon 文件
```

**全程只动：`/setting/mode/{dmode,preload}` 和 `/log` 下的文件。
不碰 /usr（阶段 1）、不碰 kmod、不碰 /system。断电即回到可用状态。**
