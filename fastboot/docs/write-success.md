# eMMC 写路径打通（CMD24，2026-10-22）

> 真机验证。本轮对 eMMC 的净数据变化为 **零**；唯一短暂改动是 `.orig`
> 文件松弛区（offset ≥ 20，文件不可见）写入后又还原的测试图案。

## stage2 写支持

`stage2/src/sdhci.c` 新增 `mmc_write_block()`，与已验证的 `mmc_read_block()`
镜像：PIO、512 字节、`TRANSFER_MODE` 不带 READ 位即写方向，等
`BUF_WRITE_RDY` 后把数据写入 buffer 端口，等 `XFER_COMPLETE`，再轮询
CMD13 直到卡回到 TRAN + READY_FOR_DATA（编程完成）。

参数块 `0xFE320000` 扩展：`[0]=block`、`[1]=count(≤64)`、
**`[2]=mode：1=写，其它任何值=读`**。老脚本不 poke `[2]`，永远不会触发写。
写数据源是 `oem load:0xfe310000` 复制进去的主机 download 暂存。
保险：识别未成功（非 TRAN 态）时 `write_status` 直接置错、不发出 CMD24。

结果区新增 `write_status`（0xFE308050）、`write_block`（0xFE308054）。
注意 payload 的 `oem peek` 按整 16 字节行输出，peek 长度必须是 16 的倍数。

## 构建（无 clang 环境的 GNU 工具链）

stage2 与主 payload 都可以用 GNU 交叉工具链构建（stage2 无需 libgcc；
主 payload 需要 libgcc.a 追加在目标文件之后，`libgcc_shim.c` 提供 raise()）：

```sh
export PATH=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin:$PATH
export LD_LIBRARY_PATH=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/lib
TC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1
CFLAGS="-march=armv7-a -mthumb -mfloat-abi=soft -mno-unaligned-access -ffreestanding \
  -fno-builtin -fno-stack-protector -fno-pic -fno-common \
  -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections \
  -fdata-sections -Os -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Iinclude"

make -C fastboot/stage2 \
  CC=arm-buildroot-linux-gnueabihf-gcc LD=arm-buildroot-linux-gnueabihf-ld \
  OBJCOPY=arm-buildroot-linux-gnueabihf-objcopy SIZE=arm-buildroot-linux-gnueabihf-size \
  CFLAGS="$CFLAGS"

make -C fastboot \
  CC=arm-buildroot-linux-gnueabihf-gcc LD=arm-buildroot-linux-gnueabihf-ld \
  OBJCOPY=arm-buildroot-linux-gnueabihf-objcopy SIZE=arm-buildroot-linux-gnueabihf-size \
  CFLAGS="$CFLAGS" \
  LDPOST="$TC/lib/gcc/arm-buildroot-linux-gnueabihf/6.4.0/libgcc.a"
```

产物：stage2 1,860 B（v3 多块）；payload 6,516 B（含看门狗修复，预算 8,192 B）。

## 验证序列（host/emmc_write_test.py）

1. **读回归**：新二进制读 block `0x56000`，与旧 stage2 捕获逐字节相同 ✓
2. **Ground truth**：`0x56005/0x56006/0x58DC4/0x59104` 四块存到
   `build/emmc-groundtruth/`；size 字段确认均为 20 ✓
3. **TEST A**：`0x58DC4`（`.orig` 内容扇区）原样写回，`write_status=0`，
   读回逐字节一致 ✓
4. **TEST B**：`0x58DC4` 松弛区 offset 100..115 写入 `ZVE10RW-TEST-01!`，
   读回观察到图案、其余字节不变 ✓；随后还原原块，读回一致 ✓
   ——图案真实落盘又被抹除，证明 CMD24 确实在写
5. **TEST C**：`0x56006`（ld.so.preload size 字段所在块）原样写回，
   `write_status=0`，读回一致，size 字段仍为 20 ✓
6. **终检**：四块再次读回，全部与 ground truth 逐字节相同 ✓

## 现状与边界

- `ld.so.preload` 一切正常：size=20 与内容一致，FAT 链健康，**无需修复**。
  TEST C 只是把它的目录块原样写了一遍，值未变。
- 此后如需真正的修复写入，位置已精确标定：
  `ld.so.preload` size 字段 = 块 `0x56006` 偏移 `0x7C`（绝对 `0x0AC00C7C`）。
- 写路径目前每次调用最多 64 块、逐块 CMD24；识别（CMD0/1/2/3/9/7/13）
  在每次调用时重跑，与读路径一致。
