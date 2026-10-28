# hybrid 镜像的安全性评估：会不会造成不可逆改变

> 用户问题：镜像传上去之前，有没有确认它不会对相机造成不可逆改变
> （写 ZV-1M2 专有设置、导致回退老菜单开不了机、改 RTOS/MCU/flash 内容）？

## 一、直接回答：**镜像本身不会写任何 flash**

**hybrid 镜像 = `nflasha15`（/usr）分区的内容，纯粹是用户态文件（库 + 资源 + 少量二进制）。
它不含：**

```
✗ init / initrd        → 在 nflasha3，我们不碰
✗ 启动脚本             → 在 nflasha7 (rootfs) 和 initrd 里，我们不碰
✗ 内核 / 内核模块加载规则 → /rootfs 里，不碰
✗ RTOS / MCU 固件      → av-cam.bin / bonobo.bin 等在 nflasha3(/system)、
                          nflasha5(/wbi)，只由 updater 写
✗ eMMC boot area       → 只有 bootloader 会写
✗ 自动执行钩子         → 已验证：镜像里没有开机自动运行的脚本
```

**已验证：混合镜像 vs 原厂 /usr 只多 3 个可执行文件**
```
+ bin/app_monio.elf       （ZV-1M2 的监控工具，由服务调用，不自动运行）
+ bin/checkServerCert
+ bin/mpr_monio2.elf
```

**⇒ 单纯"把文件放进去"不会改变任何 flash 状态。**

---

## 二、⚠️ 真正的风险在【跑起来之后】：新菜单会写可写分区

**新菜单访问 `/usr` 以外的路径（实测字符串）vs 老菜单对照：**

| 路径 | 老菜单 | M2 appFw | M2 libObj | 判断 |
|---|:--:|:--:|:--:|---|
| `/dev/nflasha23`（/PMBP）| ✅ | ✅ | ✅ | **两边都用 ⇒ 非新增** |
| `/dev/nflasha30` | ✅ | ✅ | ✅ | 共享 |
| `/setting/facedmy.bin` | ✅ | ✅ | — | 共享 |
| `/setting/sen/smode` | ✅ | ✅ | ✅ | 共享 |
| **`/setting/acctime1.bin`** | ❌ | **✅** | — | **★ 仅新菜单** |
| **`/setting/acctime2.bin`** | ❌ | **✅** | — | **★ 仅新菜单** |
| **`/setting/sig.txt`** | ❌ | **✅** | — | **★ 仅新菜单** |
| `/dev/mem` `/dev/fb0` `/dev/dmpgles2` | ✅ | ✅ | ✅ | 正常硬件访问，共享 |

**⇒ 绝大多数路径是**两版共用**的；只有 3 个 /setting 文件是新菜单独有。**

### 2.1 风险评级

| 风险 | 可能性 | 说明 |
|---|---|---|
| **变砖（完全开不了机）** | **极低** | /usr 只是用户态；kernel/initrd/boot 区都不动。变砖需要写坏 boot 区或 kernel |
| **回退老菜单后启动异常** | **中** | 若新菜单把 /setting 写成新格式，老菜单可能解析失败 |
| **黑屏 / 死机 / 卡启动** | 中 | 新菜单依赖的硬件/服务不满足时的预期表现 |
| **RTOS / MCU 被改** | **极低** | 这些固件在 nflasha3/nflasha5，只由 updater 写入 |
| **eMMC boot 区被改** | **极低** | 只有 bootloader 写 |

---

## 三、✅ 完整回退能力已在手

**我们已有全部分区的原厂 dump（`/root/fw/zve10_delivery/`）：**

```
nflasha1.img      8,323,072    （dat2/model 等）
nflasha2.img     20,971,520    ★ /setting
nflasha3.img     50,331,648    /system（RTOS/MCU 固件）
nflasha4.img     12,582,912
nflasha5.img     62,914,560    /wbi
nflasha6.img     16,777,216
nflasha7.img      8,388,608    /rootfs
nflasha10.img    12,582,912    /tmp
nflasha12.img    41,943,040    /cert
nflasha13.img     4,194,304
nflasha15.img   314,572,800    ★ /usr（回退目标）
nflasha16.img   146,800,640    /android/system
nflasha17.img   104,857,600    /android/data
nflasha18.img    20,971,520    /lens
nflasha23.img    67,108,864    ★ /PMBP
+ nflashaB0/B1（eMMC boot area）+ 各分区头
+ ZVE10_EMMC_FULL.img（1,983,905,792 B 整盘）
```

**⇒ 任何分区都能恢复原厂状态。**

---

## 四、★ 必须做的额外备份：**可写状态分区**

**原厂 dump 恢复的是"出厂状态"，但相机的当前状态（用户的设置、对焦校准等）会丢。
所以测试前应现场备份：**

| 分区 | 挂载点 | 大小 | 为什么 |
|---|---|---:|---|
| **nflasha2** | `/setting` | 20 MB | **★★★ 新菜单可能改写格式** |
| **nflasha1** | （dat2=model/dat3=region/dat4=version）| 8 MB | **★★★ 机型/版本记录** |
| **nflasha23** | `/PMBP` | 64 MB | 新老都用 |
| **nflasha12** | `/cert` | 40 MB | 证书 |
| **nflasha18** | `/lens` | 20 MB | 镜头数据 |
| **nflasha15** | `/usr` | 300 MB | **★★★ 回退目标（原厂 dump 已有）** |

**⇒ 最小备份集：nflasha1 + nflasha2（约 29 MB）。
   稳妥起见再加 nflasha12/23/18（约 124 MB）。**

**备份方式：**
```sh
# 相机上（需 shell）
dd if=/dev/nflasha2 of=/log/bak/nflasha2.bin bs=1M
# 或整个 /setting 目录 tar 到 /log
tar czf /log/bak/setting.tgz -C / setting
```

---

## 五、结论与建议流程

**结论：**
1. **镜像本身不可能造成不可逆改变** —— 它只是 /usr 文件，不含任何 flash 写入逻辑
2. **变砖风险极低** —— kernel/initrd/boot 区/RTOS 全都不碰
3. **唯一真实风险是 `/setting` 等可写分区被写成新格式**，导致回退老菜单后行为异常
4. **该风险完全可逆** —— 恢复 nflasha2 即可

**建议流程：**
```
0. 相机上现场 dd 备份 nflasha1 + nflasha2（+ nflasha12/23/18）
1. 部署 hybrid（bind 或 fastboot 写 nflasha15）
2. 测试
3. 回退：恢复 nflasha15 原厂 → 若设置异常再恢复 nflasha2/nflasha1
```

**⚠️ 特别注意：如果采用 bind 方式（不写 flash），重启即恢复，
   连 nflasha15 都不用动 ⇒ 这是最安全的测试形态。**
