# 轮次 22：diagnose.py 判决逻辑的五场景验证

> 明天要靠它把真机日志翻译成结论。如果判决逻辑有误，我会误判结果。
> 本轮用 mock agent 造五种典型状态，逐个检验。

## 一、五个场景与判决

### 场景 A：一切成功
```
mock 提供：bind 6/6 都是 M2 尺寸 + maps 里有 appFw.so + /usr 显示为 loop 设备
判决：
  ✓ bind 完全生效（6/6 关键文件都是 M2 尺寸）
  ✓ appFw.so 已被映射 ⇒ 新 UI 代码已装载
  → 若屏幕仍无 UI，问题在绘制/CP 层，不在装载层
```
**✅ 正确**

### 场景 B：镜像无效
```
mock 提供：mount ext2=-19, ext2_magic: 0000（不是 ext2）
判决：
  ✗ ext2 挂载失败 ⇒ 看日志里的 ext2_magic 行
  ! 镜像不是有效 ext2 ⇒ 上传不完整，重新上传
  → mount 已失败 ⇒ bind 必然不生效；先解决镜像/权限问题
```
**✅ 正确**（修复前会多出一句误导性的"bind 没生效"，现已改为指向根因）

### 场景 C：完全没有日志
```
mock 提供：/log/bindm2.log 和 /tmp/.bindm2.log 都是空
判决：
  ✗ bindm2 完全没有输出 ⇒ im.elf 没跑到 preload 阶段
  → 说明问题在 im.elf 被 exec 之前（挂载链 / bootin）
  → 下一步：核对 /usr 是否挂载、bootin.elf 首字节是 ELF 还是 SCRIPT
```
**✅ 正确**

### 场景 D：bind 成功但 appFw 未装载
```
mock 提供：bind 6/6 成功，maps 里只有 libObj/libIMDB/appFwBoot，没有 appFw.so
判决：
  ✓ bind 完全生效（6/6 关键文件都是 M2 尺寸）
  ✗ appFwBoot 装载了但 appFw 没有 ⇒ 卡在 Application_System_Init
```
**✅ 正确** —— 这正是最可能出现的真实情形，判决直接指向 `Application_System_Init`

### 场景 E：bind 没生效
```
mock 提供：mount 成功但 bind=-22，文件尺寸全是工厂值
判决：
  ✗ bind 没生效（看到的还是工厂尺寸）
  → 原因可能：mount/bind 返回负值；或 im.elf 在 bind 前已读了库
```
**✅ 正确**

## 二、本轮修掉的问题

```
① diagnose.py 只接受 --ip，端口写死 7777
   → 加 --port 参数（测试需要，实际使用时也有用）
② badimg 场景会输出两条互相矛盾的结论
   （既然 mount 失败，再说"bind 没生效"就是噪声）
   → 改为：mount 已失败 ⇒ 明确指向"先解决镜像/权限"
```

## 三、价值

```
明天无论真机出现哪种状态，diagnose.py 都能给出：
  · 是哪一层的失败（preload / mount / bind / Application_System_Init / 绘制）
  · 下一步该做什么

而不是像今晚前面几轮那样，需要我人肉读日志再推理。
```

## 四、累计已验证的五个层面

```
轮次 18  ld.so 语义（SONAME 顶替 / 路径 dlopen）          本机复现实验
轮次 19  内核 syscall 序列（loop/mount/bind + 反向验证）   真实 ext2 实跑
轮次 20  ABI 零风险（硬浮点可加载 / 零外部调用）           属性核对
轮次 21  部署脚本端到端（分块/拼接/续传/校验）             mock agent 实跑
轮次 22  诊断判决逻辑（五种失败场景）                       mock 场景验证
```
