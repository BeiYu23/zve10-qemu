# ZV-E10 防变砖兜底体系（含实测验证）

> 结论先行：**上游 sony-a7m4-cf / ILCE-7M4-RE 的兜底全部依赖 fastboot（Multi 口 + RP2040 启动桥 + 串口启动 payload）**，
> ZV-E10 无 Multi 口、无 ARM32 payload、BootROM 读不出 ⇒ **拿不到 fastboot**。
> 但**我们有 root**，可以做到他们靠 fastboot 才能做的绝大部分事 —— 且已在真机验证。

---

## 一、上游的兜底是什么（已逐份核对）

| 上游能力 | 实现方式 | 依赖 |
| --- | --- | --- |
| 备份全部分区 / 恢复已备份分区 | `maintain --action backup/restore`（在 fastboot 里读写 eMMC） | **fastboot** |
| 刷入"跳过验签"的更新器 | `maintain --action unlock` | **fastboot** |
| 查看/设置报告的固件版本 | 改 `nflasha2` FAT 里的 `updater/dat4`（2 字节 BCD） | **fastboot 读回 nflasha2** |
| 取代官方更新公钥（自签固件） | 重建 SquashFS 更新器镜像 | fastboot + 自签工具 |
| FL2/FL3 移植刷入 | AP/CP 补丁 + eMMC/NOR 写入 | **fastboot** |

README 原文：*"刷机需要一条自制刷机线：相机 Multi 接口引出线 + RP2040-Zero 启动桥…RP2040 的串行启动传输仍是进入 fastboot 必需的硬件协议"*。
并且他们强制：**每次实机写入前必须先做完整分区备份**，写后要做**完整读回 SHA-256 校验** ✓（这个纪律我们照搬）

---

## 二、ZV-E10 的等价手段（有 root，无需任何硬件）

| 上游能力 | 我们的等价实现 | 状态 |
| --- | --- | --- |
| 备份全部分区 | `dd` 读 17 个分区 + `nflashaB0/B1` + 整盘 1.85 GiB，全部 SHA-256 校验 | ✅ **已完成** |
| 恢复已备份分区 | root + `dd` 写回（保留在 `/log` 或 PC 上） | ✅ 已具备 |
| **设置报告的固件版本** | 直接写 `/setting/updater/dat4`（2 字节 BCD） | ✅ **真机实测通过** |
| 跳过版本检查的更新器 | 建 `/setting/updater/mode6`（VersionSkip）+ `mode2` | ✅ 机制已定位 |
| 强制重刷整机 | 官方 2.03 包 + 版本杠杆 + PMCA 更新器流程（INIT/CHK_GUARD/SWITCH_MODE/WRITE_FIRM） | ⏳ 待整机演练 |
| 刷跳过验签的更新器 | root 可直接改相机内的验签/更新器（等价于 unlock） | ⚠️ 高风险，暂不做 |

### 实测记录（本轮）

```
/setting/updater/dat4   2 字节，权限 -r--------（root 可写）
原始值 "03 02"  → 次版本 3、主版本 2 ⇒ 相机报告 2.03   ✓ 与上游 BCD 方案一致
写入   "00 00"  → 读回 "00 00"        ⇒ 相机报告 0.00   ✓ 杠杆可用
已还原 "03 02"                          ⇒ 相机恢复原状态 ✓
备份：/log/dat4.bak（相机内）+ /root/dat4.orig.bin（PC）
```

---

## 三、分层兜底（每层都能单独救回来）

```
L0  铁律：永不触碰 boot 链
      nflashaB0/B1（eMMC boot）、nflasha1（更新器）、内核 vmlinux、/sbin/init、libc/ld.so
      ⇒ 这一层被破坏 = 没有 USB = 真没救（无 UART/fastboot）

L1  旁路安装（不写 /usr）
      新 UI 放 /log/zv1f_ui/，用 mount --bind 覆盖到 /usr/… 的目标文件
      ⇒ /usr 分区上一个字节都没改；原版永远在磁盘上

L2  看门狗自动回滚（agent v11）
      开机 90 秒内没检测到 UI 进程（maps 无 viewUnified*/appFw.so）
      ⇒ 自动 umount + 删开关 + 重启回原版（不需要 USB，不需要人工）

L3  dd 还原
      用已校验的分区镜像 / 整盘镜像写回（root 直接做）

L4  官方更新器强制重刷  ← 最硬的一条，纯软件
      ① 写 /setting/updater/dat4 = 低版本（或建 mode6 VersionSkip）
      ② 相机进海量存储模式，PC 跑 PMCA 更新器流程灌官方 2.03 包
      ③ 更新器会重写系统分区 ⇒ 相机回到出厂状态
```

**L4 为什么成立**：更新器拒绝的只是"版本不比现在新"；我们能把报告版本改成 0.00 ⇒ 任何官方包都能灌进去。
官方包是**索尼签名**的 ⇒ 相机侧验签自然通过 ⇒ 不需要解锁验签、不需要 fastboot ✓✓

---

## 四、恢复操作（照抄即可）

```bash
# 0) 相机开机、USB = 海量存储器；确认能操作
python3 zve10_net.py <相机IP> ping            # WiFi 通道
# 或 utilsb:  zve10_run.py status

# 1) 先备份（若还没备份）
python3 zve10_net.py <IP> run "dd if=/dev/nflasha15 of=/log/nflasha15.bak bs=1M"

# 2) 放低报告版本（强制重刷的前提）
python3 zve10_net.py <IP> run "cp -a /setting/updater/dat4 /log/dat4.bak; printf '\000\000' > /setting/updater/dat4; sync"

# 3) PC 端跑官方更新器流程（PMCA）
python3 zve10_official_update.py <build>/fw/FirmwareData_ZVE10V203.dat

# 4) 恢复原版本号
python3 zve10_net.py <IP> run "cp -f /log/dat4.bak /setting/updater/dat4; sync"
```

---

## 五、诚实的边界

- **没有硬件级兜底**（无 UART/fastboot）⇒ L0 一旦破坏就真的没救。所以策略是"**让写入本身不具破坏性**"，而不是"坏了能救"。
- 上游明确声明：即便刷新成功、菜单出现，也**不等于**功能完整或绝对安全 —— 我们持同样态度。
- 官方更新器会**重写系统分区**（等于把机器恢复出厂）⇒ 我们做的所有改造（含 agent 本身）都会被抹掉，需要重装 ✓ 这是可接受的代价。

---

## 六、下一步（等你确认）

1. **L4 整机演练**（约 10 分钟）：把 dat4 设 0.00 → 用官方包完整刷一次 → 验证相机恢复 2.03 且功能正常
   ⇒ 这一步做完，"最坏情况 = 跑一次官方刷机"才算**实证**
2. 然后才动 UI（L1 + L2 旁路方案）—— 此时风险已经可控
