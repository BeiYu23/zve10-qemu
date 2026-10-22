# 2026-10-22 日期末状态（ZV-E10 救砖）

## ✅ 已完全打通（真机实测确认）
| 能力 | 证据 |
|---|---|
| 串口注入链路（sense 同步） | 脚本打印「检测到相机激活 1.9s」，多次复现 |
| 记录协议（SET_UNLOCK 帧） | payload 稳定执行 |
| 二级加载器（接收器 v6 + CRC） | 1.5s 自检 + CRC 长闪 + 跳转 |
| 调用 ROM 的 eMMC 初始化函数 0xFFFF2279 | 返回 0（成功），t6 验证 |
| 自己实现完整 SDHCI 初始化 | t9 四阶段全过 |
| **CMD0** | t13 段2=1 成功 |
| **CMD1（OCR 就绪）** | t13 段3=1 成功 |
| 卡存在且供电正常 | CMD1 就绪已证明 |

## ❌ 未解决
```
CMD2（R2 136 位响应）：0x0209 / 0x0219 失败
  → 最新方案：0x0201（t15/t17 已包含）
CMD3 / CMD7 / CMD13 未通过（因为 CMD2 没过）
数据读取未验证
```

## 🔑 今天挖到的硬约束（重要）
```
1. eMMC = PEF1G4，1.98GB，按【非高容量】访问
   ⇒ CMD17/24/25 的 ARG 必须是【字节地址】不是块号
   ⇒ nflasha10 分区起点 = 0x0AC00000（字节）
   ⇒ ld.so.preload size 字段 = 0x0AC0107C（若布局 A）/ 0x0AC010BC（若布局 B）
2. 看门狗 ~500ms（cmdline wdt.timeout=500）
   ⇒ 整分区写 12MB 必被复位 ✗
   ⇒ 单块写 1 字节安全 ✓
3. R1 状态在 RESP0(0x10)，掩码 0xE0F82000
4. 完成判据：PRESENT_STATE(0x24) & 3 == 0（不是 INT_STATUS 位）
5. ROM 初始化函数 0xFFFF2278 只返回 0 或 0x80000004
6. 载波：翻转载波；记录组帧首末必须同 type（SET_UNLOCK）

## 📋 下次继续的步骤
```
1) 充满电或换新电池
2) 接 Pico（A6700 固件 a6700_pio_od 2），相机彻底断电
3) 依次测试（每个都需彻底断电）：
   t17（CMD2=0x0201 + 字节寻址 0x0AC00000）← 首选
   t15（CMD2 四种 flags 扫描）
   t16（跳过 CMD2 直接 CMD3）
4) 段4=1 即读通 ⇒ 进入写回：
   CMD24 单块写，字节地址 0x0AC0107C，把 ld.so.preload 的 size 清 0
```

## 🧰 可用载荷清单（/root/fw/）
```
aux_fix_t17  1324B  CMD2=0x0201 + 字节寻址 + CMD1清HCS   ← 首选
aux_fix_t15  1128B  CMD2 四种 flags 扫描
aux_fix_t16  1408B  跳过 CMD2 直接 CMD3
aux_fix_t13  1100B  CMD0/CMD1 验证（已验证 1,1,1,2）
aux_fix_t11   748B  ROM init + CMD13
aux_fix_t9   1132B  分阶段 SDHCI 初始化（全过）
aux_fix_t7    388B  只读 PHY 地址
aux_fix_rx   1684B  接收器 v6（CRC + 进度灯）
```

## 🔧 注入命令
```
cd /root/fw/aux_fix
python3 -u inject_sense.py <payload路径> 0xFE020000
（看到 ★ ARMED 后装电池；需先彻底断电=拔电池+断VBUS）
```
