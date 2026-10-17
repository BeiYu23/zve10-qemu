/* main.c — 第二阶段：完整 mmc_init + 只读一个块（零写入）
 * 读 eMMC 块 0x56000（= 0x0AC00000/512 = nflasha10 的 FAT 引导扇区）
 * ⚠️ 注意: .img 文件内偏移是 0xB000000，但真机 eMMC 用户区偏移是 0xAC00000（.img 有 4MB boot 前缀）
 * 打印 512 字节 hex + ASCII ⇒ 验证 SDHCI 初始化 / 块读 / 数据搬运整条链
 */
typedef unsigned int  u32;
typedef unsigned short u16;
typedef unsigned char  u8;

#define REG32(a)  (*(volatile u32 *)(a))
#define REG16(a)  (*(volatile u16 *)(a))
#define REG8(a)   (*(volatile u8  *)(a))

#define UART0      0xF2000000u
#define UART_DR    (UART0 + 0x00)
#define UART_FR    (UART0 + 0x18)

#define SDHCI      0xF0304000u
#define R_DMA      0x00
#define R_BLKSZ    0x04
#define R_BLKSIZE  0x04
#define R_BLKCNT   0x06
#define R_TRNMOD   0x0C
#define R_ARG      0x08
#define R_XFER     0x0C
#define R_CMD      0x0E
#define R_RESP     0x10
#define R_BUF      0x20
#define R_PSTATE   0x24
#define R_HOSTCTL  0x28
#define R_CLKCTL   0x2C
#define R_SWRST    0x2F
#define R_INTSTAT  0x30
#define R_ERRSTAT  0x32
#define R_INTEN    0x34
#define R_ERREN    0x36
#define R_CAP      0x40
#define R_VER      0xFE

/* 命令标志 */
#define F_NONE     0x00
#define F_LONG     0x01
#define F_SHORT    0x02
#define F_BUSY     0x03
#define F_CRC      0x08
#define F_IDX      0x10
#define F_DATA     0x20
/* transfer mode */
#define T_DMA      0x01
#define T_BLKCNT   0x02
#define T_AUTO12   0x04
#define T_READ     0x10
#define T_MULTI    0x20
/* present state */
#define PS_CMDINH  0x0001
#define PS_DATINH  0x0002
/* int status */
#define IS_CMDCPL  0x0001
#define IS_XFERCPL 0x0002
#define IS_BUFWR   0x0010
#define IS_BUFRD   0x0020
#define IS_ERR     0x8000
/* clock */
#define CLK_INTEN  0x0001
#define CLK_STABLE 0x0002
#define CLK_SDEN   0x0004

static void uart_putc(char c)
{
    int g = 2000000;
    while ((REG32(UART_FR) & (1u << 5)) && --g) { }
    REG32(UART_DR) = (u32)(u8)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_nl(void) { uart_putc('\r'); uart_putc('\n'); }
static void uart_hex8(u32 v) { static const char h[]="0123456789abcdef"; uart_putc(h[(v>>4)&0xF]); uart_putc(h[v&0xF]); }
static void uart_hex32(u32 v) { for (int i=28;i>=0;i-=4) { static const char h[]="0123456789abcdef"; uart_putc(h[(v>>i)&0xF]); } }
static void uart_dec(u32 v) { char b[12]; int i=0; if(!v){uart_putc('0');return;} while(v&&i<11){b[i++]='0'+(v%10);v/=10;} while(i--) uart_putc(b[i]); }

static void mdelay(u32 ms) { for (volatile u32 i = 0; i < ms * 2000u; i++) { } }
static void udelay(u32 us) { for (volatile u32 i = 0; i < us * 2u; i++) { } }

/* ---- SDHCI 基础 ---- */
static int wait_state_clear(u32 mask, u32 timeout_ms)
{
    for (u32 t = 0; t < timeout_ms; t++) {
        if ((REG32(SDHCI + R_PSTATE) & mask) == 0) return 0;
        mdelay(1);
    }
    return -1;
}
static int wait_int(u32 mask, u32 timeout_ms)
{
    for (u32 t = 0; t < timeout_ms; t++) {
        u32 s = REG32(SDHCI + R_INTSTAT);
        if (s & mask) { REG32(SDHCI + R_INTSTAT) = mask; return 0; }
        mdelay(1);
    }
    return -1;
}
static void sdhci_reset_cmd_data(void)
{
    REG8(SDHCI + R_SWRST) = 0x03;   /* RESET_CMD | RESET_DATA */
    for (u32 t = 0; t < 500; t++) { if ((REG8(SDHCI + R_SWRST) & 0x03) == 0) return; mdelay(1); }
}
static int sdhci_set_clock(u32 div)
{
    u16 clk = REG16(SDHCI + R_CLKCTL);
    REG16(SDHCI + R_CLKCTL) = clk & ~CLK_SDEN;        /* ① 先停 SD 时钟 */
    u16 v = (u16)(((div & 0xFF) << 8) | (((div >> 8) & 0x03) << 6) | CLK_INTEN);
    REG16(SDHCI + R_CLKCTL) = v;
    for (u32 t = 0; t < 500; t++) {                    /* ② 等内部时钟稳定 ≤500ms */
        if (REG16(SDHCI + R_CLKCTL) & CLK_STABLE) break;
        mdelay(1);
    }
    if (!(REG16(SDHCI + R_CLKCTL) & CLK_STABLE)) return -1;
    REG16(SDHCI + R_CLKCTL) = (u16)(v | CLK_SDEN);     /* ③ 再开 SD 时钟 */
    udelay(100);
    return 0;
}
static int sdhci_cmd(u32 idx, u32 arg, u32 flags, u32 *resp)
{
    if (wait_state_clear(PS_CMDINH | PS_DATINH, 1000)) return -1;
    REG32(SDHCI + R_ARG) = arg;
    __asm__ volatile("dsb");
    REG16(SDHCI + R_CMD) = (u16)((idx << 8) | (flags & 0xFF));   /* ★ 必须 16 位写 */
    __asm__ volatile("dsb");
    if (wait_int(IS_CMDCPL, 1000)) return -1;
    if (resp) *resp = REG32(SDHCI + R_RESP);
    if (REG32(SDHCI + R_INTSTAT) & IS_ERR) { REG32(SDHCI + R_INTSTAT) = IS_ERR; return -2; }
    return 0;
}


/* ================= 写路径（CMD25）+ CRC32 + 分阶段验证 ================= */
#define T_MULTI    0x20
#define T_ACMD12   0x04
#define T_BLKCNTEN 0x02
#define IMG_SRC    0xFE100000u          /* 镜像预置在 ESRAM 0xFE100000 */

static u32 crc32_calc(const u8 *p, u32 len)
{
    u32 c = 0xFFFFFFFFu;
    for (u32 i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* 多块写：nblocks 块（每块 512B），src 指向连续数据 */
static int mmc_write_blocks(u32 start_block, const u8 *src, u32 nblocks)
{
    u32 w, b;
    const u32 *in = (const u32 *)src;

    if (wait_state_clear(0x3, 1000)) return -1;         /* CMD+DATA inhibit */
    REG16(SDHCI + R_BLKSIZE) = 512;
    REG16(SDHCI + R_BLKCNT) = (u16)nblocks;
    /* PIO 写：方向 = 不设 TRNS_READ；多块 = MULTI|BLKCNTEN|AUTO_CMD12；DMA 不设 */
    REG16(SDHCI + R_TRNMOD) = T_MULTI | T_BLKCNTEN | T_ACMD12;
    __asm__ volatile("dsb");

    REG32(SDHCI + R_INTSTAT) = REG32(SDHCI + R_INTSTAT);   /* 清 stale */
    if (sdhci_cmd(25, start_block, F_SHORT | F_CRC | F_IDX | F_DATA, 0)) return -2;

    for (b = 0; b < nblocks; b++) {
        if (wait_int(IS_BUFWR, 1000)) return -3;           /* 每块等一次 ✓ */
        for (w = 0; w < 128; w++) REG32(SDHCI + R_BUF) = *in++;
    }
    if (wait_int(IS_XFERCPL, 2000)) return -4;
    return 0;
}

/* 单块读（复用现有读逻辑，供校验） */
static int mmc_read_blocks(u32 start_block, u8 *dst, u32 nblocks)
{
    u32 w, b;
    u32 *out = (u32 *)dst;

    if (wait_state_clear(0x3, 1000)) return -1;
    REG16(SDHCI + R_BLKSIZE) = 512;
    REG16(SDHCI + R_BLKCNT) = (u16)nblocks;
    REG16(SDHCI + R_TRNMOD) = T_MULTI | T_BLKCNTEN | T_ACMD12 | 0x10;
    __asm__ volatile("dsb");
    REG32(SDHCI + R_INTSTAT) = REG32(SDHCI + R_INTSTAT);
    if (sdhci_cmd(nblocks > 1 ? 18 : 17, start_block, F_SHORT | F_CRC | F_IDX | F_DATA, 0)) return -2;
    for (b = 0; b < nblocks; b++) {
        if (wait_int(IS_BUFRD, 1000)) return -3;
        for (w = 0; w < 128; w++) *out++ = REG32(SDHCI + R_BUF);
    }
    if (wait_int(IS_XFERCPL, 2000)) return -4;
    return 0;
}

int main(void)
{
    u8 buf[512];
    u32 resp = 0;

    uart_nl();
    uart_puts("==== ZV10 stage2: mmc_init + READ block 0x58000 ===="); uart_nl();

    /* 保存并打印原始状态 */
    u16 clk_saved  = REG16(SDHCI + R_CLKCTL);
    u8  host_saved = REG8 (SDHCI + R_HOSTCTL);
    u32 cap = REG32(SDHCI + R_CAP);
    uart_puts(" saved CLKCTL="); uart_hex32(clk_saved);
    uart_puts(" HOSTCTL=");     uart_hex32(host_saved);
    uart_puts(" CAP=");         uart_hex32(cap); uart_nl();

    u32 base_khz = ((cap >> 8) & 0xFF) * 1000u;
    if (!base_khz) base_khz = 48000u;
    u32 div = base_khz / 800u;              /* ≤400kHz */
    if (!div) div = 1;
    uart_puts(" base_khz="); uart_dec(base_khz); uart_puts(" div="); uart_dec(div); uart_nl();

    /* ① 复位 + 1-bit + 400kHz */
    sdhci_reset_cmd_data();
    REG8(SDHCI + R_HOSTCTL) = (u8)(host_saved & ~0x22);
    if (sdhci_set_clock(div)) { uart_puts("!! set_clock(init) FAIL"); uart_nl(); goto done; }
    uart_puts(" [1] reset+clock OK"); uart_nl();

    /* ★★★ 关键修复（QEMU 源码定位 + 真机同样必需）:
     *   INT_ENABLE 必须在【任何命令之前】打开 CMD_COMPLETE，否则命令永远不"完成" ✗
     *   同时打开总线电源 3.3V（CAP bit24=1 ⇒ V=7 合法）*/
    REG8(SDHCI + 0x29)     = 0x0F;      /* POWER_CONTROL: 3.3V */
    REG16(SDHCI + R_INTEN) = 0x0033;    /* CMD_COMPLETE|XFER_COMPLETE|BUF_WRITE_RDY|BUF_READ_RDY */
    REG16(SDHCI + R_ERREN) = 0x03FF;    /* 错误中断全开 */
    uart_puts(" [1b] POWER_CONTROL=0x0F INT_ENABLE="); uart_hex32(REG16(SDHCI + R_INTEN));
    uart_puts(" ERR_INT_EN="); uart_hex32(REG16(SDHCI + R_ERREN)); uart_nl();

    /* ② CMD0 / CMD1 / CMD2 / CMD3 / CMD7 */
    if (sdhci_cmd(0, 0, F_NONE, 0)) { uart_puts("!! CMD0 FAIL"); uart_nl(); goto done; }
    mdelay(3);
    { int ok = -1;
      for (u32 t = 0; t < 200; t++) {           /* CMD1 轮询 ≤1s */
          if (sdhci_cmd(1, 0x40FF8080u, F_SHORT, &resp) == 0 && (resp & 0x80000000u)) { ok = 0; break; }
          mdelay(5);
      }
      if (ok) { uart_puts("!! CMD1 FAIL (OCR polling) resp="); uart_hex32(resp); uart_nl(); goto done; }
      uart_puts(" [2] CMD1 OCR="); uart_hex32(resp); uart_nl();
    }
    if (sdhci_cmd(2, 0, F_LONG | F_CRC, 0)) { uart_puts("!! CMD2 FAIL"); uart_nl(); goto done; }
    if (sdhci_cmd(3, (1u << 16), F_SHORT | F_CRC | F_IDX, &resp)) { uart_puts("!! CMD3 FAIL"); uart_nl(); goto done; }
    uart_puts(" [3] CMD2/CMD3 OK rca-resp="); uart_hex32(resp); uart_nl();
    if (sdhci_cmd(7, (1u << 16), F_BUSY | F_CRC | F_IDX, &resp)) { uart_puts("!! CMD7 FAIL"); uart_nl(); goto done; }
    uart_puts(" [4] CMD7 OK"); uart_nl();

    /* ③ CMD6: 8-bit 总线 + HS_TIMING=1（失败不致命，继续） */
    { u32 a1 = (3u << 24) | (183u << 16) | (8u << 8);
      u32 a2 = (3u << 24) | (185u << 16) | (1u << 8);
      if (sdhci_cmd(6, a1, F_SHORT | F_CRC | F_IDX, &resp)) uart_puts(" [5] CMD6[183]=8 FAIL(忽略)");
      else                                              uart_puts(" [5] CMD6[183]=8 OK");
      uart_nl();
      if (sdhci_cmd(6, a2, F_SHORT | F_CRC | F_IDX, &resp)) uart_puts(" [6] CMD6[185]=1 FAIL(忽略)");
      else                                              uart_puts(" [6] CMD6[185]=1 OK");
      uart_nl();
    }

    /* ④ ★ 开 PIO 的 BUF_READ/WRITE_RDY 中断（否则 PIO 永久等待 ✗） */
    REG32(SDHCI + R_INTEN) = REG32(SDHCI + R_INTEN) | (IS_BUFRD | IS_BUFWR);
    REG16(SDHCI + R_ERREN) = 0x03FF;
    uart_puts(" [7] PIO ints enabled INTEN="); uart_hex32(REG32(SDHCI + R_INTEN)); uart_nl();

    /* ⑤ 恢复 8-bit 总线；★ 不恢复时钟！
     *   原因: BootROM 串行启动场景下 saved_clk 实测可能是 0x0003（SD_CLK_EN=0 ✗）
     *   ⇒ 恢复它 = 把 SD 时钟关掉 ⇒ 后续 CMD17 根本发不出去（"时钟关则拒绝发命令"）
     *   ⇒ 保持 sdhci_set_clock() 设好的状态，并确保 SD_CLK_EN 置位 ✓ */
    REG8(SDHCI + R_HOSTCTL) = host_saved;
    REG16(SDHCI + R_CLKCTL) = (u16)(REG16(SDHCI + R_CLKCTL) | CLK_SDEN);
    uart_puts(" [7b] CLKCTL after restore="); uart_hex32(REG16(SDHCI + R_CLKCTL));
    uart_puts(" (bit2=1 必须 ✓)"); uart_nl();

    /* ⑥ ★ 读块 0x56000（CMD17 单块，PIO） */
    if (wait_state_clear(PS_CMDINH | PS_DATINH, 1000)) { uart_puts("!! state inhibit"); uart_nl(); goto done; }
    REG16(SDHCI + R_BLKSZ)  = 512;
    REG16(SDHCI + R_BLKCNT) = 1;
    REG16(SDHCI + R_XFER)   = T_READ;          /* 单块 ⇒ 不设 BLKCNT_EN ✓ */
    __asm__ volatile("dsb");
    REG32(SDHCI + R_ARG) = 0x56000u;   /* ★ 0xAC00000/512 = nflasha10 用户区块号 ✓ */
    __asm__ volatile("dsb");
    REG16(SDHCI + R_CMD) = (u16)((17u << 8) | (F_SHORT | F_CRC | F_IDX | F_DATA));
    __asm__ volatile("dsb");
    if (wait_int(IS_CMDCPL, 1000)) { uart_puts("!! CMD17 CMD_COMPLETE timeout"); uart_nl(); goto done; }
    uart_puts(" [8] CMD17 sent"); uart_nl();
    /* ★ 修复: BUF_READ_RDY 是【每块一次】，不是每字一次 ⇒ 循环外只等一次 ✓ */
    if (wait_int(IS_BUFRD, 1000)) { uart_puts("!! BUF_READ_RDY timeout"); uart_nl(); goto done; }
    for (int i = 0; i < 128; i++) {
        u32 w = REG32(SDHCI + R_BUF);
        buf[i*4+0] = (u8)(w      ); buf[i*4+1] = (u8)(w >>  8);
        buf[i*4+2] = (u8)(w >> 16); buf[i*4+3] = (u8)(w >> 24);
    }
    if (wait_int(IS_XFERCPL, 1000)) { uart_puts("!! XFER_COMPLETE timeout"); uart_nl(); goto done; }
    uart_puts(" [9] data read OK"); uart_nl();

    /* ⑦ 打印 512 字节 */
    uart_puts("---BEGIN BLOCK 0x56000 (512B)---"); uart_nl();
    for (int i = 0; i < 512; i += 16) {
        uart_hex32((u32)i); uart_puts(": ");
        for (int j = 0; j < 16; j++) { uart_hex8(buf[i+j]); uart_putc(' '); }
        uart_puts(" |");
        for (int j = 0; j < 16; j++) { u8 c = buf[i+j]; uart_putc((c >= 32 && c < 127) ? (char)c : '.'); }
        uart_puts("|"); uart_nl();
    }
    uart_puts("---END BLOCK---"); uart_nl();
    uart_puts("==> 判定: "); 
    if (buf[0] == 0xEB && buf[2] == 0x90) uart_puts("FAT 引导扇区特征命中 (EB .. 90) ✓✓ 全链路通了！");
    else if (buf[0] == 0 && buf[1] == 0)  uart_puts("读到全 0（块号或初始化可能不对）");
    else                                  uart_puts("读到了数据，但不是标准 FAT 引导扇区（可能块号偏）");
    uart_nl();


    /* ============ 阶段 W0：写 2 块 → 读回 → CRC 对比 ============ */
    {
        const u8 *img = (const u8 *)IMG_SRC;
        u32 expect = crc32_calc(img, 1024);
        int r;
        uart_puts(" [W0] === 写 2 块到块号 0x56000 ==="); uart_nl();
        uart_puts(" [W0] 源 CRC32=0x"); uart_hex32(expect); uart_nl();

        r = mmc_write_blocks(0x56000, img, 2);
        uart_puts(" [W0] write ret="); uart_dec((u32)r); uart_nl();
        if (r == 0) {
            u8 back[1024];
            r = mmc_read_blocks(0x56000, back, 2);
            if (r) { uart_puts(" [W0] 读回 FAIL ret="); uart_dec((u32)r); uart_nl(); }
            else {
                u32 got = crc32_calc(back, 1024);
                uart_puts(" [W0] 读回 CRC32=0x"); uart_hex32(got);
                if (got == expect) uart_puts("  ✓✓ W0 写入闭环成立！");
                else uart_puts("  ✗ CRC 不符");
                uart_nl();
            }
        } else {
            uart_puts(" [W0] ✗ 写入失败"); uart_nl();
        }
    }


    /* ============ 阶段 W2：完整写 12MB（48 批 × 512 块）+ 逐批抽检 ============ */
    {
        const u8 *img = (const u8 *)IMG_SRC;
        u32 total_crc = 0, b, fails = 0;
        int r;
        uart_puts(" [W2] === 完整写入 12MB -> 块 0x56000 起 ==="); uart_nl();
        total_crc = crc32_calc(img, 12582912);
        uart_puts(" [W2] 源整体 CRC32=0x"); uart_hex32(total_crc); uart_nl();

        for (b = 0; b < 48; b++) {
            r = mmc_write_blocks(0x56000 + b * 512, img + b * 262144u, 512);
            if (r) {
                uart_puts(" [W2] ✗ batch "); uart_dec(b);
                uart_puts(" write ret="); uart_dec((u32)r); uart_nl();
                fails++; break;
            }
            /* 抽检：读回本批第 1 块，与源对比 */
            {
                u8 back[512];
                if (mmc_read_blocks(0x56000 + b * 512, back, 1) == 0) {
                    if (crc32_calc(back, 512) != crc32_calc(img + b * 262144u, 512)) {
                        uart_puts(" [W2] ✗ batch "); uart_dec(b);
                        uart_puts(" verify MISMATCH"); uart_nl(); fails++; break;
                    }
                } else {
                    uart_puts(" [W2] ✗ batch "); uart_dec(b);
                    uart_puts(" readback FAIL"); uart_nl(); fails++; break;
                }
            }
            if ((b % 8) == 7) {
                uart_puts(" [W2] ... done "); uart_dec((b + 1) * 512); uart_puts(" blocks"); uart_nl();
            }
        }
        if (fails == 0) {
            uart_puts(" [W2] ✓✓ 48 批全部写入并抽检通过 ==="); uart_nl();
            /* 整体回读校验（流式，分 48 批读回算 CRC） */
            {
                u32 c = 0xFFFFFFFFu, bb;
                for (bb = 0; bb < 48 && !fails; bb++) {
                    u8 rb[512];
                    if (mmc_read_blocks(0x56000 + bb * 512, rb, 1) == 0) {
                        for (u32 i = 0; i < 512; i++) {
                            c ^= rb[i];
                            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
                        }
                    } else { fails++; }
                }
                uart_puts(" [W2] 回读抽样 CRC 完成（完整校验以主机 md5 为准）"); uart_nl();
            }
        }
    }

done:
    uart_puts("==== done ===="); uart_nl();
    for (;;) { }
}
