typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;
#define REG32(a) (*(volatile u32*)(a))
#define REG16(a) (*(volatile u16*)(a))
#define REG8(a)  (*(volatile u8*)(a))
#define LED_DIR   0xF2910004u
#define LED_SET   0xF2910044u
#define LED_CLR   0xF2910048u
#define TIMER     0xF240310Cu
#define SDHCI     0xF0304000u
#define R_BLKSIZE 0x04
#define R_ARG     0x08
#define R_CMD     0x0E
#define R_RESP0   0x10
#define R_INTSTAT 0x30
#define R_INTEN   0x34
#define R_ERREN   0x36
#define R_CAP     0x40
#define R_CLKCTL  0x2C
#define R_PWRCTL  0x29
#define R_SWRST   0x2F

static void mdelay(u32 ms)
{
    volatile u32 *t = (volatile u32 *)TIMER;
    u32 s = *t, tgt = ms * 4032u, g = 0;
    while ((u32)(*t - s) < tgt) { if (++g > 20000000u) break; }
    if ((u32)(*t - s) < tgt) { for (volatile u32 i = 0; i < ms * 2000u; i++) {} }
}
static void led(u32 on) { REG32(on ? LED_CLR : LED_SET) = 1u; }
static void blinks(u32 n) { for (u32 i = 0; i < n; i++) { led(1); mdelay(100); led(0); mdelay(160); } }
static void stage(u32 n) { blinks(n); led(1); mdelay(450); led(0); mdelay(250); }

static int wait_int(u32 mask, u32 ms)
{
    for (u32 t = 0; t < ms * 4u; t++) {
        if (REG32(SDHCI + R_INTSTAT) & mask) return 0;
        mdelay(1);
    }
    return -1;
}
static int sd_cmd(u32 idx, u32 arg, u32 flags, u32 ms)
{
    REG32(SDHCI + R_INTSTAT) = 0xFFFFFFFFu;
    REG32(SDHCI + R_ARG) = arg;
    REG16(SDHCI + R_CMD) = (u16)((idx << 8) | (flags & 0x3F));
    return wait_int(0x0001u, ms);
}

int main(void)
{
    u32 rca = 0x00010000u;
    REG32(0xF2402000u) = 0;
    REG32(0xF2402004u) = 0x11u;
    REG32(LED_DIR) = 1u;

    led(1); mdelay(300); led(0); mdelay(150);
    (void)REG32(SDHCI + R_CAP);
    stage(1);

    REG8(SDHCI + R_SWRST) = 0x06; mdelay(5);
    stage(2);

    { u16 ck = REG16(SDHCI + R_CLKCTL);
      REG16(SDHCI + R_CLKCTL) = (u16)(ck & ~0x0004u);
      mdelay(3);
      { u32 d = 400000u / (2 * 400u); if (!d) d = 1;
        REG16(SDHCI + R_CLKCTL) = (u16)(((d & 0xFF) << 8) | 0x01u);
        for (int w = 0; w < 200; w++) { if (REG16(SDHCI + R_CLKCTL) & 0x0002u) break; mdelay(1); }
        REG16(SDHCI + R_CLKCTL) = (u16)((((d & 0xFF) << 8) | 0x01u) | 0x0004u); }
      mdelay(5); }
    stage(3);

    REG8(SDHCI + R_PWRCTL) = 0x0F; mdelay(3);
    stage(4);

    REG16(SDHCI + R_INTEN) = 0x0033;
    REG16(SDHCI + R_ERREN) = 0x03FF;

    sd_cmd(0, 0, 0x00, 100);
    stage(5);

    for (int t = 0; t < 1000; t++) {
        if (sd_cmd(1, 0x40FF8080u, 0x02, 50) == 0
            && (REG32(SDHCI + R_RESP0) & 0x80000000u)) break;
        mdelay(1);
    }
    stage(6);

    sd_cmd(2, 0, 0x05, 200);
    stage(7);

    sd_cmd(3, 0x00010000u, 0x0E, 200);
    { u32 r = REG32(SDHCI + R_RESP0); if (r & 0xFFFF0000u) rca = r & 0xFFFF0000u; }
    stage(8);

    sd_cmd(7, rca, 0x0F, 200);
    stage(9);

    REG16(SDHCI + R_BLKSIZE) = 512;
    sd_cmd(17, 0x00056000u, 0x1E, 500);
    wait_int(0x0020u, 500);
    blinks(10);

    for (;;) { led(1); mdelay(2000); led(0); mdelay(500); }   /* 独特签名：亮2秒/灭0.5秒 */
}
