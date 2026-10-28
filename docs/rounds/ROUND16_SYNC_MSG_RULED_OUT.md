# 轮次 16：`0x008F013A` 同步消息不再可疑（三重证据）

## 背景

此前把 `appFwBoot.Application_System_Init` 里那条
`osal_snd_sync_msg(0x008F013A, msg)` 列为"最可疑的启动阻塞点"，
理由是"若同步语义是等应答、而无人应答，就会永久阻塞"。

本轮用三重证据把这个嫌疑排除。

## 一、`0x008F013A` 是系统级公共 id，不是私有队列

扫描两机型全部 `.so` / `.elf` 的 4 字节对齐常量（模拟字面量池），
落在 `0x008F0000–0x008FFFFF` 段的 id：

```
0x008F013A 出现在约 50 个模块：
  im.elf  bootin.elf  sen.elf  netblog.elf  ulogio.elf
  libEMG  libObj  appFw  libmpr  libMWF  libptp  libsencore
  libInfra{NetConnectionManager,NetDeviceInfo,HttpClient,Hdmi,UsbCmn,
           MscServer,ExtCmdSvc,Crypt,IPsec,PtpControl,BluetoothCore,…}
  libUsb{ChargingApp,Control,BusPower}  libWlan{Adjust,Resource,ControlCommon}
  libGOConnectionManager  libNetCommon  dataflowInfraFramework …
几乎都与 0x008F0313 成对出现
```

**⇒ 这是"系统通知/管理"类的公共消息族，不是某个组件的私有队列。**

## 二、接收方（系统守护）已就位且早于 appFwBoot

```
libEMG.so（v9 里是 M2 版，5,872 B，md5 58f50a70…）
  导出：emg_init / emg_notice_init / emg_notice_thread /
        emg_start_notice / emg_start_observe /
        emg_check_memory_shortage / emg_shortly_reboot
  ⇒ 它就是系统的通知/守护组件
  IMDB 表位置：idx=130  enable=0x01
  appFwBoot  ：idx=145  enable=0x01
  ⇒ libEMG 先于 appFwBoot 装载 ✓
```

## 三、"sync" 不等于"等应答"（反汇编确认）

```asm
; 调用方（appFwBoot）
12a4: movs r2, #52
12a8: blx  osal_valloc_msg_wait
...
12d8: blx  osal_snd_sync_msg

; 实现 __osal_snd_sync_msg 的入口逻辑
3db0: tst.w r0,#0x8000
3dba: mov  r4,r0
3dbe: mov  r5,r1
3dc0: bne.n 3dd0
3dc2: ... 尾调 u_osal_snd_sync_msg        ← 常规路径
3dd0: cmp  r0,#0
...
3e1c: cbz  r4, 3e4c
3e1e: ldrb r0,[r4,#13]
3e22: ubfx r1,r0,#3,#1
3e26: cmp  r2,#0
3e28: ite eq
3e2a: moveq r1,#0                          ← r2==0 ⇒ 清掉"需要回调"标志
3e2c: andne.w r1,r1,#1
3e30: cbz  r1, 3e54                         ← 直接走"无回调"分支
3e32: ... blx r2                            ← 只有 r2!=0 才回调
```

**appFwBoot 调用时 r2 = 0 ⇒ 走"无回调"分支 ⇒ 发送后立即返回，
不存在"等对方回复"的等待。**

**⇒ 即使真的无人接收，也只是返回一个错误码，不会卡住启动。**

## 四、结论

```
✗ "同步消息导致启动卡死"  —— 排除
✓ 该消息是系统通知族的一员，接收方（libEMG 等）比 appFwBoot 更早装载
✓ 调用方式是"同步投递 + 无回调"，不会阻塞

⇒ 嫌疑名单上又划掉一项。剩余未知收缩到：
   · appFw 与 E10 CP 的启动握手
   · UXEngine 初始化后是否真的产生绘制
   · /dev/nflasha30 不存在（预计非致命）
```
