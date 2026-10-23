# Darwin 电源 MCU：从 updater initrd 里挖到的完整接口

> 来源：`nflasha1/boot/INITRD.IMG`（cramfs，4,943,872 B，**updater 的** initrd）。
> 用 `tools/cramfs_extract.py` 解出（736 个 zlib 块 → 2.88 MB 可读内容）。
> **本文件内容是真机固件里的原始脚本，不是推测。**

---

## 一、★ 固件自己怎么初始化 Darwin

updater 启动脚本里（`execute_prod.sh` 一类）原文：

```sh
# Darwin initialization
PWR_IC_DARWIN=7
PWR_IC_PIROSHIKI=5
ud_get_power_ic_type.elf
PWR_IC=$?
echo "Power IC type : $PWR_IC"
if [ $PWR_IC -eq $PWR_IC_DARWIN ] ; then
	us_darwin_init.sh
fi
if [ $PWR_IC -eq $PWR_IC_PIROSHIKI ] ; then
	ud_piroshiki_init.elf
fi
```

⇒ **电源 IC 有两族**：`Darwin`（类型 7）与 `Piroshiki`（类型 5），
用 `ud_get_power_ic_type.elf` 的**退出码**判定。ZV-E10 属哪族，跑一次即知。

## 二、`us_darwin_init.sh` 全文（就这么短）

```sh
#!/bin/sh
ud_darwin_init.elf
echo "Darwin init --- OK"
exit 0
```

⇒ **Darwin 初始化 = 跑一个用户态程序 `ud_darwin_init.elf`**，没有别的。

## 三、`us_darwin_firm.sh` 全文（Darwin 固件更新）

```sh
#!/bin/sh
KEYWORD=$1
TARGET_DIR=$EXTRACT_ROOT/${KEYWORD}

FIRMUP_RESULT_OK=0
updater_log.sh "Darwin update START."
ud_darwin_firmup.elf $TARGET_DIR/darwin_boo.bin $TARGET_DIR/darwin_core.bin
FIRMUP_RESULT=$?
echo "Darwin firmup result : $FIRMUP_RESULT"
if [ $FIRMUP_RESULT -eq $FIRMUP_RESULT_OK ] ; then
	echo "Darwin firmup --- OK"
	updater_log.sh "Darwin update END."
	exit 0
else
	echo "Darwin firmup --- NG"
	getledval.elf 0x63
	exit $?
fi
```

⇒ **Darwin 有两块固件**：`darwin_boo.bin`（boot）+ `darwin_core.bin`（core）。

## 四、★ Darwin 的访问接口（从两个 ELF 的字符串表实测）

访问**不经过寄存器直写**，而是一个字符设备 `/dev/sio`：

```
ud_darwin_init.elf        ud_darwin_firmup.elf
------------------------- -------------------------
/dev/sio                  /dev/sio
/dev/mem                  /dev/mem
darwin_port_initialize    darwin_com_init
device_initialize         darwin_com_firmup
darwin_send_data          darwin_do_update
darwin_recv_data          darwin_send_recv_data
darwin_change_state       darwin_start_update
```

ioctl 与错误串（这两个是**关键**）：

```
SIO driver ioctl(SIO_BAUD) failed,    ERRMSG[%d]
SIO driver ioctl(SIO_CONTROL) failed, ERRMSG[%d]
SIO driver open failed [%s]
Gpio CS port open failed, ERRNO[%d]
GPIO write failed, ERRMSG[%d]
SIO driver write failed, ERRMSG[%d]
com id error : prx_buf->com_id_fld = 0x%x, COM_ID_NOTIFY_REG = 0x%x
darwin_start_update() failed
darwin_send_recv_data() failed
darwin_change_state() failed
error: com_id_fld is not COM_ID_NOTIFY_REG.
error: dwn_stat is not COM_DWN_STAT_UPDATING.
OLD version boo = %04x, core = %04x
NEW boo version = %04x
Boo Firm-Up is not needed.
```

⇒ **SIO 的两个 ioctl：`SIO_BAUD`（波特率）与 `SIO_CONTROL`（模式）**，
外加一个 **GPIO 片选**。这正好对应我们此前在 `0xF1005000` 找到的
SIO 寄存器块（`CFG`/`RATE_LO`/`RATE_HI`/`CTRL`/`CTRL2`）与 XCS 引脚。

⇒ 协议里有 `COM_ID_NOTIFY_REG`、`com_id_fld`、`dwn_stat`、
`COM_DWN_STAT_UPDATING` 等字段名 —— 说明 Darwin 是**命令/状态机**式对话。

## 五、对我们的两条价值

### 5.1 解"会话 5~9 分钟被杀"（payload 线）

现在有了官方的**语义层**参考：SIO 波特率/控制字、片选、命令-状态字段。
配合我们已经定位的 `0xF1005000` 寄存器块，payload 里实现 `oem darwin`
的把握大大增加。

### 5.2 相机上的直接实验（服务 shell 线）

updater initrd 里这些工具**不在零售 `/usr`**（实测 `/usr/bin` 无
`ud_darwin_init.elf` 等）。但既然 `us_darwin_init.sh` 只是调它，
说明**零售系统上 Darwin 的初始化发生在别处**（很可能在 `liro`/`upm` 或
内核模块里，由 `/dev/sio` 驱动完成）。

**值得在真机上确认的三件事**（服务 shell，只读）：
```sh
ls -l /dev/sio                      # 设备节点存在吗
cat /proc/udm/usb_darwin            # usb_darwin 模块的自述（有大量状态）
dmesg | grep -i darwin              # 内核侧 Darwin 交互痕迹
```

## 六、附带发现

- **updater 的 shell 口令**：`/root/IAMLDUDWRT` 存在时要输 `xylitol5` 才给 `/bin/ash --login`
- updater initrd 里还有：`hash_check.sh` `hash_drop.sh` `hash_update.sh`
  `is_valid_boot.sh` `mkramdsk.sh` `prepare-usb.sh` `updater_storage_*.sh` 等
  （`is_valid_boot.sh || halt` 那行被注释掉了，即**验签未强制**）

## 七、提取方法与局限

```sh
python3 tools/cramfs_extract.py nflasha1_..._INITRD.IMG out.bin
```

cramfs 对"压缩无收益"的块**按原样存储**，这类块没有 zlib 头，会被跳过
⇒ **文本（脚本）能完整复原**，但**大二进制会有缺口**，从结果里切出的 ELF
不完整。需要完整 ELF 时必须实现 cramfs 的块指针表解析（未做）。
