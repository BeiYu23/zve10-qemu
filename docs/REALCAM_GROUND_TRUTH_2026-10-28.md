
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
