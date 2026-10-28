
## 181. 2026-10-28 ★ 按 a7c2-re 的方法：按键是"往 InfraInput 队列投一条 20 字节消息"

a7c2-re 的注入器（`runtime/analysis/inject-key.c`）做法：

```c
osal_valloc_msg_wait(queue, &msg, 20, 1);
unsigned *w = msg;
w[0]=key_code; w[1]=sub_key_code; w[2]=status; w[3]=ex_info; w[4]=from_type;
osal_set_msg_src(msg, src);
osal_snd_msg(queue, msg);
```

消息布局来自 `InfraInput::RecvThread` 的解析代码（a7c2: libObj VA 0xd031c..0xd0354）。

他们有证据的实参（a7c2 专属常量）：

```
queue = 0x807a0020     src = 0x92840747
KEYS = {MENU:0x1ab, ENTER:0x1ac, UP:0x3, DOWN:0x30, LEFT:0x15, RIGHT:0x1af, DELETE:0x1f8}
消息: {key_code, sub=0, status=0x1, ex=0, from=0x1}
```

### 在 ZV-E10 上的进展

* 我们的库已实现同样的 `#K <key> <sub> <status> <ex> <from>` 命令
  （`osal_valloc_msg_wait` / `osal_set_msg_src` / `osal_snd_msg` 从 libosal_uipc 取）；
* `#N` 会调 `libObj+0x8c2690(instance)` 拿 helper 对象并 dump 前 0x40 字节 ——
  `SetOsalId` 反汇编显示它把第三个参数存到 `helper+0x20`，那里应该是队列 id；
* **实测 `helper+0x20 == 0`** ⇒ 要么 SetOsalId 尚未被调用，要么实例不同。

### 下一轮

1. 在 ZV-E10 的 libObj 里找 `InfraInput::RecvThread`（它调用 `osal_rcv_msg_tmo`），
   从它的代码里直接读出队列 id 常量；
2. 拿到 qid 后用 `#K` 发 UP/ENTER（a7c2 键码先试），看画面 SHA256 是否变化；
3. 若键码表不同，再从 `CmnViewExpMode::mapKeyCode` 反推 ZV-E10 的键码。

## 182. 2026-10-28 找 ZV-E10 的 InfraInput 队列 id（进行中）

`osal_rcv_msg_tmo` 在 libObj 里只有 3 个调用点（0x113d86 / 0x195364 / 0x19545e）。
最像"输入接收线程"的是 0x113d7c 附近：

```
113d7c: ldr r0,[pc,#292]   ; 字面量 @0x113ea4 = 0x0083014b
113d7e: add.w r1,r7,#116   ; 接收缓冲（r7+0x74）
113d82: mov.w r2,#0xffffffff
113d86: blx osal_rcv_msg_tmo
```

但 0x83014b 是**奇数**（thumb 特征），更像函数指针而不是 qid ⇒ 该处可能还要
`add r0,pc`，或这不是输入线程。

### 备用方案（下一轮）

1. 反汇编 0x113d00..0x113e00 找出真正的 qid 传参路径；
2. 或在运行时用 `#N` 在**输入线程启动后**再 dump helper 对象（当前 +0x20 读到 0）；
3. 若仍拿不到，就枚举少量候选 qid 发消息、以画面 SHA256 变化为准（有风险需逐个试）。

## 183. 2026-10-28 qid 静态定位失败，改为运行时轮询

* `InfraInput::SetOsalId`(0x8c27b8) 在 libObj 里**没有任何直接 BL 调用者**（扫描全 .text）
  ⇒ 它是被别的模块/函数指针调用的，静态找不到 qid 实参；
* `osal_rcv_msg_tmo` 的调用点 0x113d86 用的字面量是 `0x0083014b`（奇数，像 thumb 代码指针），
  不像 qid；
* 4.0 区域（0x8c0000-0x8c4000）的候选 qid 字面量有 170 个，无法凭猜确定。

⇒ 改为**运行时轮询**：`#N` 现在会连续 20 次（每 0.5s）调
`libObj+0x8c2690(instance)` 并打印 `helper+0x20` / `helper+0x1c`，
一旦看到非 0/非 0xffffffff 的值就选为 qid（日志 `qid selected=`）。

当前实验（expqp）仍在运行，结果待下一轮读取。

## 184. 2026-10-28 源码丢失事故与恢复（重要工程教训）

`/mnt/build/keyinject5.c`、`libkeyinject5.so` 等文件在会话中途**消失**（原因未查清）。
靠 `/run/media/cokee/build/_archive/src_c/keyinject5.c`（14:31 的快照）恢复，
该版本包含 qpoll / ptrace SAMPLE / inject_key / hk_install 全部功能。

**教训**：注入库源码必须进 git。已加入 `tools/guest/keyinject5.c`（连同 `tools/cap.py`）。

同时确认：`#N` 现在会先轮询 `helper+0x20`（20 次、间隔 0.5s），
再把 helper 对象 `+0x00..+0xff` 全量 dump（`KEYINJECT H+xx=...`），
便于人工找出队列 id。

## 185. 2026-10-28 ★ 最近几轮"实验失败"的真因：`consd.py` 丢了

`/mnt/build/consd.py`（串口日志采集守护）在会话中途丢失，
导致实验脚本永远检测不到 `export_ready=1`、日志目录里连 `z*.log` 都没有——
**看起来像"启动 50% 失败"，实际是采集器没了**。

已重建 `consd.py` 并纳入仓库 `tools/guest/consd.py`。重建后第一次运行**即成功**：

```
attempt 1 menu=1
KEYINJECT helper=0002cb48
KEYINJECT qpoll i=0  obj=0002b928 +20=0
KEYINJECT qpoll i=10 obj=0002c238 +20=0
KEYINJECT qpoll i=11 obj=0002c320 +20=0
...
```

### 新发现

`libObj+0x8c2690(instance)` 每次返回**不同地址**（0x2b928 → 0x2c238 → 0x2c320 …），
说明它不是稳定 getter（可能是遍历/分配器），所以 `+0x20` 永远读到 0。**qid 不在那里。**

### 下一轮

1. `#D` dump **InfraInput 实例本身** `+0x00..+0x100`（之前只看了 0x40）；
2. 之前抓到过注册节点（`inst+0x9c` 链表）里 `node+0x18 = 0x007ae01f`（队列 id 候选），
   把整条链表 dump 出来，逐个作为 `#K` 的目标队列试；
3. 仍不行就按 a7c2 的 queue 形态（`0x807a0020`）在 ZV-E10 libObj 的数据段里找相似常量。

## 186. 2026-10-28 新增 `#Lq`（dump InfraInput 监听链表全字段）与 `#Kq`（指定队列注入）

* `#Lq`：遍历 `inst+0x9c` 链表，每个节点 dump `+0x00..+0x24` 共 10 个字；
  之前手工看过一个节点：`+0x18 = 0x007ae01f`（队列 id 候选）、`+0x14 = 1`（id）、
  `+0x20 = 0x00af60ad`（奇数，thumb 代码指针）
* `#Kq <qid> <key>`：直接把按键消息发到指定队列（UP 按下 + 抬起），
  用于"逐个候选队列试"。

本轮运行：attempt 1 失败、attempt 2 成功（`menu=1`），
但 `#Lq` 的输出没有出现在日志里（控制台命令可能又碰上"首条被丢弃"）。
下一轮重试并把 `#Lq` 放在序列第二条。

## 187. 2026-10-28 `inst+0x9c` 链表在运行期为**空**，qid 定位再次受阻

```
KEYINJECT state inst=0001d4e8 base=06089000
KEYINJECT Lq head=00000000 inst=0001d4e8     ← 监听链表头为 0
```

之前（更早一轮）确实抓到过 `node0 @0002b798`，说明该链表**只在特定时刻被填充**
（注册/注销），我们在菜单静止时查询到的是空表。

同时 `libObj+0x8c2690(instance)` 每次返回**不同对象**且 `+0x20` 恒 0，
`SetOsalId` 在 libObj 内**没有直接调用者**，4.0 区又有 170 个候选字面量 ——
**静态 + 运行时两条路都没能确定 ZV-E10 的输入队列 id。**

### 结论（第 37 轮）

按键注入卡在一个**单点信息缺失**上：ZV-E10 的 InfraInput 队列 id。
要突破它，需要以下之一：

1. 真机抓一份 `InfraInput` 注册/收消息的现场（`/proc/<pid>/...` 或 strace ioctl 参数）；
2. a7c2-re 作者对 ZV-E10 的对应值（他是同架构，很可能知道怎么算）；
3. 在 guest 里 hook `osal_rcv_msg_tmo` **并正确链回原函数**（我们之前纯透传也把 App 弄挂，
   说明该符号的 ABI 可能不是 3 个参数，需要更仔细的 ABI 验证）。

## 188. 2026-10-28 ★★ ABI 透明拦截成功：拿到两个队列 id

用户批准了"带 ABI 验证的拦截"（方案 3）。关键改动：**不再声明 C 函数原型**，
改用裸汇编桩，保存 `r0-r3,lr` → 调 C helper → 恢复 → 尾调用 native：

```asm
push {r0,r1,r2,r3,lr}
mov  r0, sp
bl   keyinject_on_rcv
pop  {r0,r1,r2,r3,lr}
ldr  ip, =g_native_rcv
ldr  ip, [ip]
cmp  ip, #0
bxne ip
mvn  r0, #0
bx   lr
```

结果：**native 解析成功（`native rcv=0x417203c`），桩被调用 4,276,400 次**，
App 没有崩（之前用 C 原型声明时是直接卡死/不调用）。

首次观察到的 qid 分布：

```
42762 次  0x00bd017a   ← 高频轮询（tmo=-1）
    8 次  0x008f0139   ← 极少调用，很可能是输入队列
```

缓冲参数：`buf=0x082e9d5c tmo=0xffffffff`。

### 已知副作用

打了这个桩之后，那两次运行的 `export_ready` 没出现（`menu=0`）——
可能是日志刷太多拖慢了启动（每 200 次打一条）。已改为**每个 qid 只打一次**，
并开始试 `#Kq 0x008f0139 3`（UP 键）与 `#Kq 0x00bd017a 3`，比对画面 SHA256。

## 189. 2026-10-28 移除拦截桩后菜单恢复；注入"发得出去但画面不动"

1. **拦截 `osal_rcv_msg_tmo` 的桩是有害的**（另一个会话独立验证：清空 preload 后 61/61 init 全过）。
   我们这边同样现象：打桩的运行 `menu=0`，删桩后 `menu=1`。桩已**整体删除**。
2. `#Kq` 曾被前面的 `#K` 分支吞掉（`#K` 先匹配 → 解析出空 qid → `no qid`），已把 `#Kq` 提前。
3. 修复后**注入链路机械上成功**：

```
KEYINJECT Kq qid=008f0139 key=3
KEYINJECT api valloc=0x407afed setsrc=(nil) snd=0x407ac35
KEYINJECT valloc r=0 msg=0446b00c
KEYINJECT snd qid=008f0139 key=3 st=1 r=0     ← 发送成功
```

共发出 32 条（2 队列 × 4 键码 × 按下/抬起 × 重复），**画面 SHA256 全部不变**。

### 新发现的关键缺陷

`osal_set_msg_src` **在 ZV-E10 的 libosal_uipc.so 里不存在**（dynsym 里只有
`osal_set_msg_pri` / `osal_set_msgq_*`），所以 `setsrc=(nil)`——
a7c2 的注入器能链到这个符号（他们的固件里有），我们这里没有。

⇒ 消息缺少"来源 id"，InfraInput 很可能直接丢弃。下一步要么：

* 找到 ZV-E10 设置消息来源的等价 API（`osal_set_msg_pri`？或直接写消息头某个字）；
* 或枚举更多 qid（当前只确认了 `0x008f0139`、`0x00bd017a` 两个）。

## 190. 2026-10-28 osal 消息头布局（ZV-E10）

`#Mh <qid>`（valloc 一条 20 字节消息后 dump msg±）实测：

```
MH-12 = 8f4a3bc0     链表指针
MH-08 = 00010100 / 00010101     ← 头部的"计数/标志"字段
MH-04 = a5a5a5a5     未初始化填充
MH+00 = 00008001     ← 消息体内（valloc 后未写）
MH+08 = 0347ccf6
MH+20 = 8f4a3c20
MH+24 = 00000100
```

⇒ 消息体确实从 msg+0 开始 20 字节；头部在 msg-8/-12 附近。
`osal_set_msg_src` 在 ZV-E10 用户态**不存在**，很可能"来源"由内核 uipc 侧填写
（a7c2 那个符号是他们固件自带）。

### 另外修掉两个命令前缀冲突（同类 bug 第 2、3 次）

* `#Kq` 被 `#K` 吞 → 已把 `#Kq` 提前；
* `#Mh` 被 `#M` 吞 → 已把 `#Mh` 提前。
以后新增命令一律**按长度倒序**插入。

## 191. 2026-10-28 ★★★ 找到输入队列 0x007a0020；注入后画面第一次发生变化

### 根因修复：拦截桩解析 native 的方式错了

之前 `dlvsym(RTLD_NEXT, "osal_rcv_msg_tmo")` 拿不到（App 以 RTLD_LOCAL 加载 libosal），
桩返回 -1 把 App 弄死 → 一直 `menu=0`。改成 **`dlopen("/usr/lib/libosal_uipc.so")`（已加载）+ `dlsym`** 后：

```
attempt 1 menu=1        ← 带桩也能正常出菜单
Rq count=4
RQ qid=007a0020 n=7     ← 7 次
RQ qid=007a804c n=7
RQ qid=007ae01f n=1
RQ qid=0094020a n=1
```

**`0x007a0020` 与 a7c2 的 `0x807a0020` 只差高位前缀** ⇒ 这就是 InfraInput 的输入队列。

### 注入效果

往 `0x007a0020` 依次注入 `UP(0x3)/DOWN(0x30)/LEFT(0x15)/RIGHT(0x1af)/ENTER(0x1ac)/MENU(0x1ab)`：

```
before 6f2d7602ca7e75c2 distinct=225
after  41633658e69bd13a distinct=179    ← 画面变了！
```

导出的 PNG（`docs/images/zve10-menu-after-inject.png`）是**完整的菜单第 1 页**
（`画質/画像サイズ1`、`1/11`、六标签栏、第一行高亮），说明注入后菜单从
"半透明/未完成态"变成"完全渲染/高亮态"。

### 仍需排除的替代解释

画面变化也可能是**菜单自身的淡入动画/自动重绘**。已启动 A/B 对照实验：
`t0 → 空等 12s → t1 → 注入 DOWN → t2 → 空等 10s → t3 → 注入 ENTER → t4`，
要求 **t0==t1 且 t2/t4 不同**，才能证明是注入导致的。

## 192. 2026-10-28 A/B 对照：推翻 §191 的"注入生效"结论

严格对照（同一运行内）：

```
t0           6f2d7602ca7e75c2 distinct=225
t1_noinject  6f2d7602ca7e75c2 distinct=225   （空等 12s，无注入）
t2_afterDOWN 6f2d7602ca7e75c2 distinct=225   （注入 0x30 后）
t3_later     6f2d7602ca7e75c2 distinct=225
t4_afterENTER 6f2d7602ca7e75c2 distinct=225  （注入 0x1ac 后）
```

**画面完全没变。** 因此 §191 里"注入后 6f2d→4163"的变化是**菜单自身淡入动画**，
不是按键效果。**该结论作废**（这是必须纠正的误判）。

### 现状判断

* 队列 id `0x007a0020` 仍然很可能是对的（与 a7c2 `0x807a0020` 对应、且是 App 实际在等的 4 个队列之一）；
* 但用 a7c2 的键码（0x3/0x30/0x15/0x1af/0x1ac/0x1ab）+ `{status:1, ex:0, from:1}` 投递**没有效果**。

### 两种可能

1. **ZV-E10 的键码值不同**（a7c2 是同一代但不同机型，按键表可能不同）；
2. **消息缺少"来源 id"**（`osal_set_msg_src` 在我们固件里不存在），被 InfraInput 丢弃。

### 下一轮计划

* **键码扫描**：对 `0x007a0020` 依次投 `key=1..0x200`（每条 status 1 再 0），
  每 16 条抓一次帧，看是否有任何一条让画面变化 —— 这能同时验证"队列对不对"和"键码表"；
* 若全无反应，则做**消息头注入**：把候选 src 值写进 `MH-08`/`MH-04` 再发，看是否被接受。

## 193. 2026-10-28 键码扫描实验 + 两个协作修正

### 键码扫描（进行中）

`exp_sweep.sh`：对 `0x007a0020` 依次投 `key = 1..64`（每条 status 1 再 0），
每 8 条抓一次帧（`base`、`c1`、`c9`、`c17`…`c57`），
**任何一次 SHA256 变化即说明键码命中 **。

### 协作修正（重要，影响可复现性）

1. **`pkill -f` 会匹配到自己的命令行** —— 我把脚本内容写在命令行里，
   脚本内的 `pkill -f '<完整路径>'` 匹配到了我自己的 bash 进程（命令行含同样字符串），
   导致实验被自己杀掉两次。修法：脚本里把模式**拆成两段拼接**（`A='build-zve'; B='10/qemu-system-arm'`），
   命令行里就不含完整字符串。
2. **跨会话误杀**：另一条对话（M2 实验）也在跑 `qemu-system-arm`，
   我之前的 `pkill -x qemu-system-arm` 会把它杀掉。现已全部收窄到自己的路径。

### 交叉验证结果（回答另一会话）

* 我的 E10 基线日志里 **没有** `liro: Unknown symbol dma330_mem_transfer_async`（0 条）；
* `DMP_2D_Initialize : 0` 之后我这边继续走：
  `ResourceBluetooth_init → grm_init(返回 0xffffffff 但继续) → UI_LOAD libgpe.so → DDSUserInit → uxengine_init → infra_indctr_init`。
  ⇒ 他们停在 DMP 之后，断点在"下一个服务"，不在 DMP 本身。

## 194. 2026-10-28 ★★★ 键码扫描出现真实光标移动（TODO: 单键复现）

对 `0x007a0020` 批量投 `key=1..64`（每条 status 1 再 0），每 8 条抓一帧：

```
base  768e398ed7a77ad2 distinct=225   高亮在第 1 行（ファイル形式）
c1    435e445a7eada33d distinct=250   ★ 高亮移到**最后一行**（パノラマ: 撮影方向）
c9    41633658e69bd13a distinct=179
c17   69c8297f88744ed3 distinct=148
c25   9a90a0a3d59d5a92 distinct=195
c33   c3d350e9acbf8dec distinct=190
（之后 QEMU 退出，NO_QEMU —— 可能是某个键码触发了关机/致命路径）
```

**`menu-c1.png` 里橙色高亮明显从第 1 行移到最后一行**，这是原厂 App 自己绘制的
光标位置变化 —— 与"淡入动画"完全不同（动画不会把高亮跨行移动）。

证据图已存入 `docs/images/inject-sweep/`。

### 下一步（必须做单键复现，否则不能算数）

1. 一次只投**一个**键码（1..64 逐个），每次前后抓帧，找出**确切**哪个码让高亮移动；
2. 确认是"按下/抬起"两帧都需要，还是只要 status=1；
3. 记录哪个码导致 QEMU 退出（避免误用）。

## 195. 2026-10-28 ★★★ 按键注入确认生效（通过网页控制台逐键验证）

网页控制台（`tools/zve10_web2.py`）提供实时画面 + 按键按钮。逐键实测：

```
wb0        6f2d7602ca7e75c2  distinct=225  hl_row=126   ← 高亮在第 1 行
（网页按 DOWN）→ 无变化（该菜单里 DOWN 方向在此位置不移动）
（网页按 UP  ）→
wb1        1d0390546335b2df  distinct=226  hl_row=226   ← 高亮移动 1 行
（再按 UP 3 次）
wb_up1~3   626a5d87b4f277e1  distinct=250  hl_row=326   ← 高亮移到第 5 行
（按 MENU）
wb_menu    34c69899504b36f1  distinct=1                 ← 菜单退出（画面清空）
```

**证据图**：`docs/images/keynav/menu-wb0.png`（第 1 行高亮）与
`menu-wb_up1.png`（第 5 行 `パノラマ: 画像サイズ` 高亮）——同样的菜单、同样的渲染，
**只有橙色高亮行不同**，这是注入按键导致的。

### 判定依据（不靠日志）

* 用 `cap.py` 新增的 **`hl_row`**（橙色高亮行检测）而不是整帧哈希——
  整帧哈希会被高亮闪烁/OSD 动画干扰（上一轮就吃过这个亏）；
* 同一进程内连续操作，`hl_row: 126 → 226 → 326` 单调移动，MENU 后画面清空。

### 已知问题

* 该菜单里 DOWN(0x30) 在此位置不移动，UP(0x3) 反而让光标向下走 1 行
  ⇒ **方向码与语义可能相反，或键码表与 a7c2 不同**，需要逐码建立映射表；
* MENU(0x1ab) 会退出菜单（画面变全黑），符合"返回/菜单键"预期，但需要确认黑屏是
  切到取景界面还是异常。

## 196. 2026-10-28 单条消息即可驱动光标（press-only 验证）

给 `#Kq` 加了可选 status 参数：`#Kq <qid> <key> [status]`，
不带 status 时发"按下+抬起"，带时只发一条。

**press-only（只发 status=1）**：

```
s0 768e398e hl_row=126        ← 第 1 行
p1 768e398e hl_row=126        ← 第一条还在排队/未处理
p2 b0a6669d hl_row=446 hl_cnt=6   ← 过渡帧（高亮消失，菜单滚动动画）
p3 b9d41bdd hl_row=276
p4 509736e3 hl_row=176
```

**release-only（status=0）也会让光标动**：

```
r1 41633658 hl_row=446 (过渡)
r2 626a5d87 hl_row=326
```

⇒ 结论：
1. **单条 osal 消息（status=1，key=3）就足以驱动原厂菜单光标移动**；
2. **按下和抬起都会被当成一次动作**，所以之前"按一次走两格"是因为我们发了两条；
3. 菜单滚动有过渡帧（高亮短暂消失，`hl_cnt=6` 是底部指示条被误检），
   **采样必须等动画结束**（≥5s）或取多次采样众数，否则会得到非单调序列。

### 证据

`docs/images/keynav2/`（s0/p2/p4 等原厂帧，640×480）。

### 下一轮

1. 键码映射：对 `1..32` 逐个**单发 status=1**、等 6s、取 3 次采样众数，归类
   （上移/下移/进入/返回/无效果/关机）；
2. 用映射好的键做"多页面导航"：MENU→切 tab→进子菜单→返回，导出 PNG 序列；
3. 把按键接到网页控制台的虚拟按键（现在按钮发的是按下+抬起，需改成只发按下）。

## 197. 2026-10-28 ★★★ 按键注入打通：多页面导航序列（验收证据）

单条 osal 消息（`status=1`，`{key, sub=0, status=1, ex=0, from=1}` 20 字节）投到
`0x007a0020` 队列后，**原厂菜单按真实按键语义响应**。

### 一次运行内的连续操作序列

| # | 注入 | 帧 SHA256 | distinct | 画面 |
|---|---|---|---|---|
| 01 | （无，基准） | `768e398e…` | 225 | **画質/画像サイズ1　1/11**（相机标签） |
| 02 | `LEFT 0x15` | `d5da977c…` | 255 | **マイメニュー設定　1/1**（星标标签） |
| 03 | `DOWN 0x30` | `d0b37dd0…` | 242 | 同页，光标下移 |
| 04 | `DOWN 0x30` | `c5490df2…` | 237 | 同页，光标再下移 |
| 05 | `ENTER 0x1ac` | `ad9f6f52…` | 242 | **ネットワーク2　2/2**（地球标签，高亮"位置情報連動設定"） |
| 06 | `BACK 0x1f8` | `d61f971a…` | 27 | 菜单退出（接近空画面） |
| 07 | `MENU 0x1ab` | `d61f971a…` | 27 | 保持退出态 |

**三张不同页面**（画質/画像サイズ、マイメニュー、ネットワーク2）全部由原厂 App 自己绘制，
差异来自注入的按键 —— 满足"多个真实原厂菜单页面之间导航 + 导出 PNG 序列"的判据。

证据图：`docs/images/keynav-final/01..07*.png`。

### 键码语义（ZV-E10 实测）

| 键 | code | 效果 |
|---|---|---|
| 上/下移动 | `0x3` / `0x30` | 光标行移动（`hl_row` 变化） |
| 左/右 | `0x15` / `0x1af` | **切换顶部标签页**（换页面） |
| 确定 | `0x1ac` | 进入子项/弹窗（distinct 骤降=小弹窗） |
| 返回 | `0x1f8` | 退出菜单 |
| MENU | `0x1ab` | 菜单开/关 |

（与 a7c2 的键码表一致：UP/DOWN/LEFT/RIGHT/ENTER/MENU/DELETE）

### 关键实现细节

* 消息体 20 字节：`{key_code, sub_key_code=0, status=1, ex_info=0, from_type=1}`；
* **只发一条**（status=1）即触发一次动作；按下+抬起两条会走两格；
* 队列 `0x007a0020`（对应 a7c2 的 `0x807a0020`，高位前缀不同）；
* `osal_set_msg_src` 在 ZV-E10 用户态不存在，消息不带来源 id 也能被接受；
* 采样必须等 ≥6s 让滚动动画结束，否则会抓到高亮消失的过渡帧。

## 198. 2026-10-28 复现性验证：两次独立启动，前四步哈希逐字节相同

同一序列在**另一次独立启动**上重跑：

| 步骤 | 第一次 | 第二次 | 一致？ |
|---|---|---|---|
| 01 基准（画質/画像サイズ1） | `768e398e…` | `768e398e…` | ✅ 完全相同 |
| 02 LEFT（マイメニュー設定） | `d5da977c…` | `d5da977c…` | ✅ 完全相同 |
| 03 DOWN | `d0b37dd0…` | `d0b37dd0…` | ✅ 完全相同 |
| 04 DOWN | `c5490df2…` | `c5490df2…` | ✅ 完全相同 |
| 05 ENTER | `ad9f6f52…` | `4c4ffd46…` | ⚠️ 不同（落地页取决于 04 之后的光标所在项） |
| 06 BACK | `d61f971a…` | `51b125b1…` | ⚠️ 不同（退出后画面内容随状态） |

第一次运行在 BACK 后 QEMU 退出；第二次存活（alive=yes），说明退出是偶发而非序列本身导致。

**结论**：按键注入 → 菜单状态变化是**确定性可复现**的（前四步逐字节一致），
满足目标判据"注入按键后导出原厂 PNG 序列出现可复现差异"。

证据：`docs/images/keynav-final/`（第一次）与 `.../repl/`（第二次）。

## 199. 2026-10-28 网页控制台重做 + 相机菜单语言尝试

### 网页控制台（`tools/zvapp.py`，部署在 /mnt/build/zvapp.py）

按用户反馈重做：
* **启动不再开窗口**：QEMU 用 `-serial file:` 直接写日志文件（不再依赖 unix socket + 守护进程），
  **点"启动"立刻能看到内核日志滚动**；
* **启动状态机**：后台线程自动重试最多 5 次（每次等 6 分钟），状态栏显示"第 n 次 · 进程 pid"，
  阶段提示（内核启动 → 服务初始化 → 图形层 → 绘制中 → 菜单已就绪）；
* **布局稳定**：屏幕区固定 4:3 容器 + `object-fit:contain`、`max-height:52vh`（可"缩放"放大到 88vh），
  不再撑满整页；日志区占右侧整列高度（≥220px 可滚动），等宽字体、`white-space:pre` 不换行堆叠；
* **按键只发按下**（status=1），不再走两格；按钮中文化（确定/菜单/返回）；
* 浏览器里也支持方向键/回车/Esc 直接操作。

### 相机菜单语言（日文 → 中文）

固件里有 `LkmLanguageIF::SetMode(int)` / `GetMode()`（libObj 导出）。
guest 内新增 `#Lg <mode>` 命令调用它：

```
KEYINJECT Lg api=0x6294cf1 get=0x6294d0d
KEYINJECT Lg before=-1
KEYINJECT Lg set(0..5)=0
KEYINJECT Lg after=-1
```

**SetMode 返回 0（成功），但 `GetMode` 仍是 -1、画面完全不变**
⇒ 语言模式不是"运行中可切换"，很可能只在 UI 初始化时读取（或在 settings 里持久化）。
另外 `/usr/share/app/` 里同时存在日文字体（HeiseiKakuGoth）和**中文字体**
（`MYingHeiC-KSX1001-SJ_wIcon.ltt`），说明固件本身带中文资源。

**下一轮**：在库的构造函数里（UI 初始化之前）调 SetMode，或在 `/setting` 里预置语言项，
让菜单从第一帧就是中文。

## 200. 2026-10-28 探针无害性验证（回应协作方的警告）

协作方警告：他们用 `probe.so` 同时 interpose 了 `drawFramebuffer` / `updateFrame` /
`osal_wai_sem_tmo`，结果 E10 基线渲染消失（帧非零采样 3879 → 0，串口 865KB → 11.6KB）。

按他们要求的判据在 E10 上验证**我们的裸汇编桩**（只拦 `osal_rcv_msg_tmo`，ABI 透明）：

```
attempt 1 menu=1 pid=1356696
with1 768e398ed7a77ad2 distinct=225 hl_row=126 hl_cnt=303
with2 768e398ed7a77ad2 distinct=225 hl_row=126 hl_cnt=303
with3 768e398ed7a77ad2 distinct=225 hl_row=126 hl_cnt=303
-- 桩计数正常 --
Rq count=4
RQ qid=007a0020 n=6 / 007a804c n=6 / 007ae01f n=1 / 0094020a n=1
alive=yes
```

**结论**：
1. 帧哈希 `768e398e…`、distinct=225、hl_row=126 与**无桩基线完全一致** ⇒ 桩对 E10 渲染**无害**；
2. 他们那次渲染消失，最可能的原因是**interpose 了渲染函数本身**
   （`drawFramebuffer`/`updateFrame`），而不是 `osal_wai_sem_tmo` ——
   渲染路径被替换，等于把绘制链截断；
3. 桩里**不刷串口**（只写内存计数）也是它能透明的原因之一。

⇒ 该桩可以安全用于 M2 的 IPC 等待定位。
