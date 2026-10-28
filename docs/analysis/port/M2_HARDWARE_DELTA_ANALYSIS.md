# ZV-1M2 → ZV-E10 移植的硬件差异静态分析（2026-10-28）

> 范围：五个维度——USB 芯片、电源开关、ND 滤镜、settings 需求、镜头系统。
> 方法：纯静态（strings/nm/objdump/IMDB 表解析），不碰相机。

---

## 总判定表

| 维度 | 差异性质 | 对移植的影响 | 层归属 |
|---|---|---|---|
| USB 芯片 | **驱动不同**（darwin vs MAX14648） | 低——库接口相同 | 内核模块 |
| **电源开关** | **形态不同：E10 拨动滑块 vs M2 按钮**；但 M2 保留 SLIDE 语义（双形态兼容） | 低——滑块事件 M2 能理解；upm.ko 新接口缺失仅影响省电/唤醒（非启动路径） | 内核+app |
| ND 滤镜 | **语义不同**（E10 外接3档 vs M2 内置转盘+AUTO） | 中——UI 会显示 ND 菜单但硬件不存在 | app 层 |
| settings 需求 | **M2 多 4 个文件**（全部可自动创建） | 低 | /setting |
| 镜头系统 | **形态不同**（E 卡口可换 vs 内置定焦） | 高——M2 无镜头通信库 | 平台+app |
| **入口 ABI** | **一致**（两版 Application_System_Init 都只吃 R0 指针） | **无** ✓ | app 层 |
| **模块覆盖** | **hybrid == M2 原厂**（多缺 0 个） | **无** ✓ | /usr |

---

## ① USB 芯片（已定位，影响低）

```
E10: usb_darwin.ko    (28,584 B)  "USB Darwin IF Driver" v0.9.0.0
     · 通过电源 MCU 管理充电电流: 0/100/500/900/1500/2000mA
     · 命令: /proc/udm/usb_darwin  1=挂起 2=释放 3=读适配器类型 4=读限流 5=设限流
M2 : usb_max14648.ko  (39,996 B)  "USB MAX14648 Driver" v0.9.1.0
     · MAX14648 = Maxim USB 型号检测/充电开关芯片
     · 命令: /proc/udm/usb_max14648  1=限100mA 2=限500mA 3=挂起 4=释放 5=读CHG_TYPE寄存器

用户态接口: 两个机型都由 libUsbBusPower.so 统一封装（各 6 处引用）
⇒ 内核模块不同，但用户态库同名同接口 ⇒ 换 kmod 即可，app 层不用动 ✓
（我们的 hybrid 的 kmod 全取 E10 ✓）
```

## ② 电源开关（E10 = 拨动滑块，M2 = 按钮；用户指正后已核实）

```
★ 事实（已修正，最初分析有误）:
  ZV-E10 = 拨动滑块:  KEY_POWER_SLIDE / OFF_BY_USBJ_POWER_SLIDE
                       view/ROOT_POWER_SW、ActionChangeSlideLightPowerKeyStatus
                       MDT_0028_00_POWER_SW / MDT_0257_00_POWER_SWITCH
  ZV-1M2 = 按键:      TACT_POWER_KEY_ON/OFF × 4 种 MONITOR_POWER_LINKAGE
                       is_tact_power_key_by_power_off() / set_tact_power_key_by_power_off()

M2 的 appFw 同时保留了两套语义:
  SLIDE_POWER_SWITCH_ON_OPEN / ON_CLOSE / ON_ALL / OFF
  TACT_POWER_KEY_ON_OPEN  / ON_CLOSE / ON_ALL / OFF
  ⇒ 双形态兼容；在 E10 上 input.ko(E10) 按 E10 的 key_tbl 上报滑块事件
  ⇒ M2 appFw 能理解 SLIDE 语义 ⇒ 大概率兼容

内核侧通路（两边同构，含拨动开关的底层检测）:
  input.ko: getkey_front / getkey_vgrip / getkey_gpio / getkey_adc
  按键位图经 UIPC 上报; key_tbl 是 insmod 参数（机型按键表，随 rootfs 走，未动）

★ 底层开关检测在 sencore（sen.elf 进程）—— 证据:
  E10 libsencore.so: PwrSlideSW / PowerSW / XPWR_OFF_DISABLE_PORT_{INFO,SET}
                     GPIO 读写原语 CXS_READ_GPIO / /dev/gpio
  M2  libsencore.so: 完全相同的四个串都在
  sen.elf 两版【字节相同】（19,208 B）
  ⇒ 开关的电平/脉冲采集在 CP 侧完成，与 app 层无关
  ⇒ hybrid 保留 E10 的 sen.elf + sencore ⇒ 检测层完好
  ⇒ M2 appFw 收到的是上层事件，且双形态语义都在 ⇒ 链路兼容 ✓

upm.ko（电源管理驱动）: E10 137,260 B / M2 149,240 B，内容不同
  M2 独有能力: k_upm_aps_apokey_enable、port_bit/grp_xpwr_off_disable、
               PanelOpen_WakeOn、电源状态谓词集（_is_usb_power_supply 等）
  注意: appFw 里大量 upm_* 串只是调试日志格式串（nm 无定义无引用），不是接口
  ⇒ hybrid 用 E10 的 upm.ko ⇒ 新能力缺失，但均为运行时功能（省电/唤醒/热告警），
    不在启动必经路径 ⇒ 与 ND 同级：低风险 ⚠

启动期电源链: init → upm.ko → /proc/udm/upm_bootmode → bootin → im.elf
  两版 im.elf 都读同一 /proc/udm/upm_bootmode 节点 ⇒ 启动期兼容 ✓
```

## ③ ND 滤镜（语义不同，影响中等）

```
E10（外接 ND，走 EXT 扩展协议）:
  VALUEID_EXT_ND_OFF / ON / 1 / 2 / 3      ← 只有 4 个值
  PARAMID_EXT_ND_FILTER_MODE               ← 5 处引用
  老菜单里 256 处 ND 串在 CautionConfig.so（帮助文档）
M2（内置 ND 转盘）:
  auto_nd_filter_t、setting_nd_filter_position_display_t
  ND_FILTER_POSITION_DISPLAY_OFF / ON
  ASSIGNABLE_SWITCH_AUTO_ND_FILTER / PUSH_AUTO_ND
  BKID_APP_auto_nd_filter / clear_nd_filter / nd_filter_position
  appFw.so 109 处；PARAMID_EXT_ND_FILTER_MODE 0 处（旧协议彻底不用）

对移植的影响:
  · M2 UI 会显示"ND 滤镜"设置项（AUTO/位置档），但 E10 没有这块硬件
  · sequence_set_nd_filter 等 model 层调用最终落到 CP 侧（av-cam.bin）
  · CP 是 E10 的 ⇒ 收到 ND 命令要么被忽略要么报错 ⇒ 预期行为: 设置无效但不崩
  · 风险: 若 model 层在启动时查询 ND 状态而 CP 无应答且【阻塞等待】⇒ 卡死
  · 缓解: 这是"启动后按需"的路径，不是启动必经路径 ⇒ 低概率致命
```

## ④ Settings 分区需求（M2 多 4 个文件，全部可自建）

```
M2 引用 27 条 /setting 路径，E10 引用 24 条。
M2 新增且 E10 代码从不引用的 4 条:
  /setting/acctime1.bin    累计使用时间（应用自己写）
  /setting/acctime2.bin    （同上，第二计数器）
  /setting/facerec         人脸识别（E10 用 facerec0.bin / facerec_.bin 命名）
  /setting/sig.txt         签名
⇒ 全部是"记录/缓存"性质，应用打开失败后自行创建 ⇒ 不阻塞启动
⇒ 唯一注意: facerec 与 E10 的 facerec0.bin 命名冲突不冲突取决于打开模式
  （M2 大概率 O_CREAT，若 E10 已有 facerec0.bin 不会干扰）
```

## ⑤ 镜头系统（形态差异最大的一块）

```
E10（E 卡口可换镜头）:
  libInfraLensCommunicator.so  8,196 B —— 用户态唯一镜头通信封装
  /system/lensbin_lensproperty/LensAberrationData.bin —— 镜头光学矫正表
  libObj 里: PARAMID_MOUNT_ADAPTER_FOCUS_TYPE、CBID_GET_PRE_FIRST_MOUNT_INFO、
             MOUNT_RUNNING —— 卡口在位检测、适配器类型
M2（内置电动变焦镜头）:
  无 libInfraLensCommunicator.so
  appFw 里: lens_zoom_ring / lens_zoom_lever（变焦环+变焦杆两套输入语义）
            CAUTION_ID_FACTOR_CAM_INTERNAL_LENS_ZOOM_STATUS_POWER_MSG
            setting_lens_zoom_position_display_t
  ⇒ 变焦位置是 model 层直接管理的状态，不是通过镜头协议

对移植的影响（按严重度，已逐层追到内核）:
  1. M2 的 libIMDB 表 idx=145 同时引用 AdjChkConfig.so（已放）与 AppConfig.so（两边都没有）
     ⇒ 容忍失败 ✓
  2. ★ 变焦链路已完整追踪（set_zoom_drive 无外部符号，编在 appFw 内部）:
       model::sequence_set_zoom_drive (appFw)
       → infra_wrapper_imaging::set_zoom_drive (appFw 内部)
       → cam_liro_*  (appFw)
       → liro.ko (LIRO 框架内核接口)
       → CP (av-cam.bin)
     ⇒ 变焦马达控制走【LIRO 通用框架】，不是专用镜头协议
     ⇒ E10 与 M2 的 liro.ko 只差 32 字节（65212 vs 65180）⇒ 同源同 ABI
     ⇒ 马达命令最终由 E10 的 CP 执行 ⇒ E10 没有变焦马达 ⇒ 命令被 CP 拒绝/忽略
     ⇒ 预期: 变焦无效但不崩（同 ND 的模式）
  3. 对焦/光圈（E 卡口镜头自带马达）走 libInfraLensCommunicator:
     M2 代码不加载它 ⇒ E10 镜头将由 CP+现有镜头协议驱动（原厂行为保留）✓
  4. 拍照/取景依赖 CP 侧（E10 CP + E10 镜头）⇒ 需要实机验证
```

## ⑥ 入口 ABI（今晚最重要的正面结论）

```
E10 Application_System_Init @0x8a2408 (libObj.so):
  push {r4,r5,r7,lr}; sub sp,#40
  mov r5, r0                      ← R0 = 参数
  memset(栈32B) → snprintf → createRequestModelExecuteEvent
  cbz r5, 默认串; 否则 strncpy(r0串)        ← 参数是名字串，可空
M2 Application_System_Init @0x1270 (appFwBoot.so):
  push {r4-r8,lr}; sub sp,#72
  mov r8, r0                      ← 同样 R0 = 参数
  memset(栈32B) → snprintf → osal_valloc_msg_wait(52B)
⇒ 两版签名一致：void Application_System_Init(const char *cfg)
⇒ im.elf 的调用点传参方式不变 ⇒ ABI 兼容 ✓
```

## ⑦ 模块覆盖（复述收工结论）

```
M2 libIMDB 表: .data.rel.ro@0xf00c，record base 0xf090，228 条 × 44 B
default(0x01) 要装 131 个模块
hybrid 缺 81；M2 自己缺 84；hybrid 比 M2 多缺 = 0
⇒ 文件层面无缺口
```

---

## 综合结论与下一步

```
五个维度里没有发现"致命阻塞"：
  · USB: 换 kmod 即可（hybrid 已用 E10 kmod）✓
  · 电源: 自动适配 ✓
  · settings: 4 个文件可自建 ✓
  · ND: 语义不同但不阻塞启动 ⚠
  · 镜头: 缺 libInfraLensCommunicator 引用方，但 M2 代码不加载它 ✓
  · ABI: 签名一致 ✓
  · 覆盖: 0 额外缺口 ✓

⇒ 静态分析找不出"必死"的原因 ⇒ 失败点在【运行时】某个具体交互
⇒ 下一步（按性价比）:
  1. 对比 E10 libObj 的 Application_System_Init 与 M2 appFwBoot 的【后续行为】：
     E10 版 snprintf 后调 createRequestModelExecuteEvent（往 model 发事件）；
     M2 版 snprintf 后调 osal_valloc_msg_wait（等 UIPC 消息 52 字节）
     ⇒ 两版是【完全不同的启动协议】！E10: 发事件；M2: 等消息。
     若 im.elf 的流程不匹配 M2 版的"等待"，就会停在第一个 wait ⇒ 无 UI、无日志
  2. 反汇编 M2 appFwBoot 的 Main() 与 osal_valloc_msg_wait 的参数（msgid/队列）
  3. 在 E10 上用 agent 抓 im.elf 的 osal 消息队列统计，看它停在哪个 wait
```
