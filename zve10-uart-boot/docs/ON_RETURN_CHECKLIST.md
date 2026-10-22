# 用户回来后的一步到位清单

## 状态
- Pico 当前离线（用户带走）
- 已备好：A6700 固件（带 sense）、pico_a6700.py、inject_sense.py、stage4.py
- 已备好载荷：
  - t3 v3（1,044 B）= PHY→SDHCI→CMD13→CMD7→CMD18
  - t2 v2（1,216 B）= 完整识别版
  - 接收器 v6（1,684 B）= CRC + 进度灯

## t3 灯码
```
1 = CMD13 通且无错误位
2 = CMD13 无响应
3 = CMD18 超时
4 = ★ 读到 FAT 引导扇区
5 = 数据不对
6 = RESP0 报错
7 = 命令抑制位卡住
8 = RESP3 报错
```

## 步骤
1) 接 Pico；`python3 -u -c` 验证 PING（刷新固件到 uart_boot_dual 或 A6700 固件）
2) 注 t3：
   `python3 -u inject_sense.py /root/fw/aux_fix_t3/payload.bin 0xFE020000`
   或备选：
   `python3 -u stage4.py /root/fw/aux_fix_t3/payload.bin 0xFE020000 0xFE020000 96000`
3) 看长闪次数，回报

## 后续分支
- 出 1 或 4 → 继续 CMD18 读、再改 CMD25 写
- 出 2 → 完整识别版 t2；仍失败 → 请协助挖 SuGi 时钟
- 出 6/8 → R1 报错，需读具体错误位（改 payload 拆位报告）
- 出 7 → 控制器命令通路卡死 → 查复位/时钟

## 写盘参数（待验证）
```
CMD24 单块写 = 0x181A, TM = 0x21
CMD25 多块写 = 0x193A, TM = 0x27
CMD17 单块读 = 0x111A, TM = 0x31
描述符: desc[0]=(len+511)&0xFE00<<16 | 0x21 |(last?2), desc[1]=addr
```
