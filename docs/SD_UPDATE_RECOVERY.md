# ZV-E10 的 SD 卡固件更新路径（恢复方案）

> 2026-10-26。全部结论来自**官方固件包内的脚本与二进制**，不是推测。
> 用途：给 ZV-E10 建立一条不受 fastboot 会话长度限制的恢复通道。

## 一、结论

**ZV-E10 支持从 SD 卡刷固件，只是消费者界面里没有入口 —— 它是靠
`/setting/updater/` 下的文件标志触发的工厂/服务通道。**

**⇒ 刷坏 `/usr` 之后仍然能恢复，因为更新器只要 `/setting` 和 `/system`。**

## 二、证据链（四个独立来源）

### ① `UdtrMain.sh` 的开机模式判定

```sh
getCurrentMode()
{
    test -f /root/IAMLDUDWRT && return $MODE_PROD
    test -f /setting/updater/mode || { echo "Forced Prod Mode"; return $MODE_PROD; }
    ...
    test -f /setting/updater/mode6 && return $MODE_VERSKIP
    test -f /setting/updater/mode5 && return $MODE_MSCRYPT
    ...
}
```

### ② `execute_ufp.sh` 的 MS Crypt 分支

```sh
elif [ $MODE_MSCRYPT -eq $cur_mode ] ; then
    echo "[DETAIL] : exec sdfileinput.elf"
    insmodMediaDriver
    sdfileinput.elf || { ... }
    touch /setting/updater/MediaUpdateCompleteFlag
```

### ③ `sdfileinput.elf` 里的文件名

```
strings sdfileinput.elf:
    BODYDATA.DAT
    GetSDFileInputModeUpdater / getFirmwarePath / burnFirmware / verifyUUDCondition
    [sdfileinput.elf] Finalize error
```

### ④ `common.src` 的常量

```
MODE_MSCRYPT=5
MODE_VERSKIP=6
DATSRC_SENSER=0
DATSRC_MS=1        ← MS = Memory Stick / SD
```

**并且 `common.src` 显示更新器只挂载 `/setting`(nflasha2) 与 `/system`(nflasha3)，
**不挂 `/usr`** ⇒ `/usr` 损坏不影响恢复。**

## 三、触发方式

```bash
# 相机上（agent 或更新器 initrd 里都有 up.sh）
/usr/bin/up.sh modver 0.00     # 版本门设 0.00，允许同版本重装
/usr/bin/up.sh ms              # 创建 /setting/updater/mode + mode5
```

`up.sh` 的完整命令集：

```
user  → mode              UFP 模式
serv  → mode mode3        Service 模式
ms    → mode mode5        MS Crypt（从 SD 读）
ver   → mode mode6        版本跳过
modver <x.xx>             改 dat4
modmod <model>            改 dat2
modreg <region>           改 dat3
```

**注意：`mode` 必须存在，否则 `getCurrentMode` 直接进"强制工厂模式"，不看 SD。**

## 四、固件包命名

`FirmwareData_*.dat` 与 `BODYDATA*.DAT` 是**同一种容器格式**，只是文件名不同：

```
FirmwareData_ZVE10V203.dat  →  SD 卡根目录改名为 BODYDATA.DAT
```

头部结构（三者逐字节同构）：

```
89 55 46 55            魔数
"DATV"                 容器类型
"PROV" / "UDID"        段
```

**（不是 UDF —— 早先的判断有误，此处更正。）**

## 五、相机起不来时的恢复顺序

```
1. Pico 注入 fastboot（BootROM 不依赖任何分区，永远可用）
2. 只写几十字节：
     /setting/updater/mode      （空文件）
     /setting/updater/mode5     （空文件）
     /setting/updater/dat4      = 00 00
3. 断电重启，SD 卡插着
4. 相机进入 MODE_MSCRYPT → sdfileinput.elf → 刷 BODYDATA.DAT
5. 完成后自动 touch MediaUpdateCompleteFlag 并重启
```

**★ 关键优势：fastboot 会话只需存活几秒（写几个小文件），
不再需要一口气写 300 MB —— 彻底绕开 Darwin 会话限制。**

## 六、为什么不为 M2 打包 BODYDATA

```
BODYDATA.DAT 由 Sony 密钥签名
⇒ 塞进 hybrid 的 /usr = 破坏签名 = 更新器拒绝安装
⇒ 除非先做 a7m4 的方法 01（替换 updater 公钥，改 nflasha1）
```

**⇒ 现阶段分工：**
- **恢复原厂**：官方包（无需签名工作）
- **安装 M2**：fastboot 直接写 `nflasha15`（LBA 0x16C000，已有地址证明）

## 七、待实测（下一步）

```
□ fastboot 能否在 nflasha2 的 FAT 里创建两个空文件（需要写 FAT 目录项）
□ dat4 的写入位置与编码（2 字节 packed BCD）
□ 官方包改名后用 SD 通道刷写能否通过机型/版本校验
□ 整个流程能否在"起不来"的相机上跑通
```
