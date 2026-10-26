# 在任意笔记本上注入 agent（自包含步骤）

## 需要什么

```
① 笔记本（Linux / macOS / Windows+WSL）
② Python 3.8+ 和 pyusb
③ 相机 USB 线
④ 两个仓库（下面 git clone）
```

## 一次性准备

```bash
# 1) pyusb
pip install pyusb

# 2) 认证协议实现（MIT/PolyForm 的开源项目，只引用不复制）
git clone https://github.com/xyzhangxin/sony-a6700-powerful.git
export A6700_SRC="$PWD/sony-a6700-powerful/应用/src"

# 3) 我们的仓库（含 sender + agent_v14.so）
git clone https://github.com/cokeenet/zve10-qemu.git
cd zve10-qemu/tools/payload
```

## 注入（相机插 USB，进入大容量存储模式）

```bash
# 确认设备在
lsusb | grep 054c          # 应看到 054c:0d95

# 干跑（不碰 USB）
python3 send_payload.py vendor/agent_v14.so --dry-run

# 真发（会触发 libJit 加载 agent）
python3 send_payload.py vendor/agent_v14.so --pid 0x0D95
```

**成功标志：`SET_FILE2 ... status=0x00000000 (OK)`**

## 然后

```bash
# 拔掉 USB（WiFi 才会回来）
# 等 10-30 秒，相机自己重新枚举

# 测试 agent 的 TCP 通道
python3 cam.py ping --ip <相机IP>
# 期望输出：PONG
```

## ⚠️ 注意

- **`status=0` 后相机会在几秒内从 USB 消失，这是正常的**
- **拔线后 WiFi 恢复，agent 的 7777 端口就能连**
- **agent 是 daemon，拔线后继续跑，直到相机重启**
- **不要在写入过程中拔线**

## 如果 status 不是 0

| 返回 | 含义 |
|---|---|
| `0x10000000` | dlopen 失败（库有问题） |
| `0x00000000` | ✅ 成功 |
| 其它 | 服务端拒绝，贴给我看 |
