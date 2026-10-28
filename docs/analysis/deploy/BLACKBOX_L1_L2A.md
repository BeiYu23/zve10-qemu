# L1+L2a 启动黑匣子：不依赖 UI 的取证通道

> 2026-10-28。目标：不管新菜单起不起得来，都能回答
> 「im.elf 跑了吗 / libObj·appFw 映射了吗 / 是 hang 还是死」。

## 一、为什么不用 USB 串口

查过内核配置，三项关键开关都没开：

```
CONFIG_USB_CONFIGFS   无      （没有 configfs 实例化接口）
CONFIG_USB_G_SERIAL   无      （无 g_serial.ko）
CONFIG_USB_F_ACM      无      （无 usb_f_acm.ko）
内核里 ttyGS / gserial 字符串 0 处
```

**⇒ 没有 ACM 就没有 /dev/ttyGS0，做不出真正的 USB 串口控制台。**
（要做只能改内核 config 重编内核并刷 /system，风险和工作量都大，暂不做。）

## 二、改用的两条通道

### 通道 A：内核态黑匣子（L1）

```
blackbox.ko  35,480 B
  vermagic = 3.0.27_nl-rt106+ SMP preempt mod_unload ARMv7
  ⇒ 与相机现有 .ko 逐字一致（构建树 localversion-rt=-rt106，PREEMPT_RT_FULL=y）
  依赖的内核符号已逐个验证被相机自己的模块引用：
    filp_open/filp_close/vfs_write ← bac*/bcmdhd 在用
    kthread_create_on_node/kthread_stop ← bcmdhd/mmc 在用
    scnprintf/printk/init_task/jiffies ← 多个模块在用
    __aeabi_idivmod ← bcmdhd/cdrom/dmm/hdmi 都在用
  做两件事：
    ① 每 500 ms 写一行心跳：T <jiffies> imelf=<im.elf 进程数>
    ② 加载/卸载时各写一条标记
  ⇒ 它活在用户态之外：即使 im.elf 死了、卡住了，心跳停止的时刻就是死亡时刻
```

### 通道 B：用户态取证（L2a）

```
bb.so  5,836 B   0 NEEDED / 0 未定义符号 / 0 外部重定位 / 17 处 svc 全部正确装载 r7
  由 /setting/mode/preload 注入 im.elf，构造函数里：
    ① 从 /proc/partitions 查 nflasha17 的 major:minor，mknod /dev/bb17
    ② 读 /log/blackbox.ko → init_module 加载内核模块（L2a 的"加载器"角色）
    ③ 把自己看到的启动状态写成文本，写到 nflasha17 的 8 个槽位（槽号 = pid%8）
       同时写 /tmp/.bb.log 和 /log/bb.log
  记录内容：
    pid / slot
    /proc/mounts 里 /usr、/tmp、/log 三行   ← 判断挂载是否成功
    libObj / appFw / appFwBoot / libIMDB / libInfraSKD / global.xdb 的**实际大小**
                                             ← 判断 bind 或刷写是否生效
    MemFree
    modload=<返回值>                          ← 0=模块加载成功
```

## 三、数据落点与读回

```
nflasha17 @ 0x49000000（100 MB，全零，未挂载，不与任何分区冲突）
    偏移 0     .. 8192     bb.so 的 8 个实例槽（每槽 1 KiB）
    偏移 8192  .. 20992    blackbox.ko 心跳（200 × 64 B）
    偏移 20992 ..          BBMOD1 头/尾标记

读回（需要 Pico + 相机在 bootloader）：
    python3 /root/fw/oss/tools/blackbox/read_bb.py
  ⇒ 内部执行 oem emmc-init 然后 oem upload:0x248000:64
    （LBA = 0x49000000/512 = 0x248000，64 扇区 = 32 KiB，一条命令拿全）
  ⇒ 完全在 bootloader 里，不依赖内核、不依赖 /usr、不依赖 UI
```

## 四、部署

```bash
cd /root/fw/oss/tools/blackbox
python3 deploy_bb.py            # 推 bb.so + blackbox.ko，preload 变成四项
# 重启相机
python3 read_bb.py              # 插 Pico 后读回
python3 deploy_bb.py --rollback # 不想要了
```

preload 最终形态：
```
/usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so:/log/bb.so
```

## 五、已知局限（诚实标注）

```
① 覆盖范围：从 im.elf 的 preload 构造函数开始。
   如果 /usr 挂不上、连 bootin 都没跑起来，bb.so 不会执行 ⇒ 那时读回只有空数据
   ⇒ 这种情形可以另外用 read_tmp.py / verify_partition.py 直接读分区判断

② preload 里各库的构造函数顺序未经验证：
   bb.so 记录的文件尺寸可能是 bind 生效【之前】的。
   不过它会同时记录 /proc/mounts 里有没有 m2root，据此可以判断顺序
   ⇒ 不影响"im.elf 跑没跑"这个最关键结论

③ blackbox.ko 与 bb.so 谁先写设备没有同步（两者写不同区间，不重叠）：
     bb.so    → 0 .. 8191
     blackbox → 8192 .. 20992
   ⇒ 无冲突

④ 若 /proc/partitions 里没有 nflasha17，bb.so 建不出 /dev/bb17，
   内核模块加载会失败（modload=-1），此时仍能从 /tmp/.bb.log 取数据
   （read_tmp.py 已有 FAT 解析能力）
```

## 六、产物

```
/root/fw/oss/tools/blackbox/
  bb.so          5,836 B   md5 2135b591bca9e4b7
  blackbox.ko   35,480 B   md5 73b56a85b859f5db
  deploy_bb.py   2,595 B
  read_bb.py     2,816 B
  bb.c / blackbox.c / Makefile
```
