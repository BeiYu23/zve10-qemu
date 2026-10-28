# 轮次 21：deploy_bindm2.py 端到端跑通（mock agent 实测）

> 部署脚本逻辑复杂（1 MB 分块、断点续传、超块校验、preload 设置），
> 但从未被实际执行过。本轮用 mock agent 做了完整的端到端验证。

## 一、mock agent 设计

```
在 127.0.0.1:7777 实现 agent 的三种协议：
  PING                              → PONG
  RUN <n>\n<cmd>                    → OUT <m>\n<输出>（模拟 busybox 行为）
  PUSH <n>\n<path\0data>            → OK（写入内存文件系统）

模拟的命令（脚本实际会发的）：
  rm -f / ls -l / awk 取大小 / cat / tail / dd|md5sum / df -k
  cat P >> DST && rm -f P && echo N > SZ; echo ok     ← 分块追加
  含 printf 写 preload/dmode 的长命令
```

## 二、首次完整部署：通过

```
镜像 4 MB → 分 4 块 × 1 MB 上传
  bindm2.so / libIMDB_m2.so 推送 OK
  块 1~4 全部 OK
  超块 magic 校验: md5 完全一致 ✓
  preload 设为: /usr/tool/libefence.so:/log/libIMDB_m2.so:/log/bindm2.so
  dmode 设为: 3e

拼接内容验证（从 mock 读回）：
  前 4 KB md5  = f7cdbb0ee1f59d5a3fb5e984fdea7b56  = 源镜像 ✓
  末 4 KB md5  = f9ccc74a1683f645f1eb3575fffdb485  = 源镜像 ✓
  /log/m2usr.size 已按设计删除 ✓
```

## 三、断点续传：通过

```
制造"已上传 2 MB + size 记录 2 MB"的中断状态 → 重新运行脚本
  ★ 断点续传：已提交 2097152 B (2.0 MB)，从第 3 块继续
  只传了块 3、块 4

续传后 4 个采样点全部与源镜像一致：
  偏移 0        ✓
  偏移 1048576  ✓
  偏移 2097152  ✓
  偏移 3145728  ✓
```

## 四、测试中发现并修掉的问题

```
问题：脚本最后的"最终文件大小"用的是裸 ls（无兜底），
      某些情况下输出为空，导致无法确认上传是否完整。
修复：改用 awk 取大小 + 与期望值做一致性断言：
      最终文件: 4194304 B  (期望 4194304 B)  ✓ 一致
      若不一致会明确提示"上传不完整，请重新运行（会自动重传）"
```

## 五、这条测试的价值

```
如果没有它，明天第一次真机部署可能出现：
  · 分块追加顺序错（拼出错误镜像 → mount 失败 → 白折腾一轮）
  · 续传逻辑有 bug（中断后重跑拼错 → 同上）
  · 最终文件不完整但脚本报告成功（更糟：静默错误）

现在这三类问题都被实测排除了。
```

## 六、累计已实证的项目

```
轮次 18  ld.so 语义（SONAME 顶替 / 路径 dlopen）
轮次 19  内核 syscall 序列（loop / mount / bind，含反向验证）
轮次 20  ABI 零风险（硬浮点可加载 + bindm2 零外部调用）
轮次 21  部署脚本端到端（分块 / 拼接 / 续传 / 校验 / 配置）

⇒ 从"镜像内容"到"传输过程"到"运行时加载"全链路都有实证
```
