/*
 * SIO transport to Darwin, the power-supervisor MCU.
 *
 * Transcribed from ILCE-7M4-RE (payloads/fastboot/src/darwin.c), which in turn
 * came from the 7M4 AP loader's transaction primitive at 0xFE04DD3C.
 * SIO+0x100 is one 0x80-byte buffer used in both directions.
 *
 * Retrying is the protocol, not error handling: Darwin answers a command in a
 * later exchange and returns a CRC of 0xFFFF for "nothing ready yet".
 *
 * ZV-E10 DIFFERENCES
 *   - GPIO block is 0xF2910000 with a 0x100 per-port stride (7M4: 0xF101D000,
 *     0x1000).
 *   - The XCS port/bit is not known, so it is a runtime value and nothing
 *     touches GPIO until darwin_set_pin() + darwin_init() are called
 *     explicitly. A payload built with this file behaves exactly like one
 *     without it until a command asks for Darwin.
 */
#include "io.h"
#include "timer.h"
#include "darwin.h"

#define SIO_CTRL        (SIO_BASE + 0x00)
#define SIO_CTRL2       (SIO_BASE + 0x04)
#define SIO_REG08       (SIO_BASE + 0x08)
#define SIO_LEN         (SIO_BASE + 0x0C)
#define SIO_CFG         (SIO_BASE + 0x10)
#define SIO_RATE_LO     (SIO_BASE + 0x14)
#define SIO_RATE_HI     (SIO_BASE + 0x18)
#define SIO_DATA        (SIO_BASE + 0x100)

#define SIO_CTRL_SETUP  0xD8000000u
#define SIO_CTRL_GO     0x20000000u
#define SIO_CTRL_BUSY   BIT(29)
#define SIO_CTRL_STOP   0xC0000000u
#define SIO_CTRL2_VAL   0x30000000u
#define SIO_CFG_ON      0x01000000u
#define SIO_LEN_VAL     ((u32)(DARWIN_PKT - 1) << 24)
#define SIO_RATE        3u

/* Runtime chip-select pin. -1 = not configured, GPIO untouched. */
static int g_pin_port = -1;
static int g_pin_bit  = -1;

void darwin_set_pin(u32 port, u32 bit)
{
    g_pin_port = (int)port;
    g_pin_bit  = (int)bit;
}

u32 darwin_pin_port(void) { return g_pin_port < 0 ? 0xFFFFFFFFu : (u32)g_pin_port; }
u32 darwin_pin_bit(void)  { return g_pin_bit  < 0 ? 0xFFFFFFFFu : (u32)g_pin_bit; }
int darwin_pin_set(void)  { return g_pin_port >= 0 && g_pin_bit >= 0 && g_pin_bit < 32; }

static inline u32 xcs_reg(u32 off)
{
    return XCS_GPIO_BASE + XCS_GPIO_STRIDE * (u32)g_pin_port + off;
}

/* ---- CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, MSB first, no final xor */
static u16 darwin_crc16(const u8 *p)
{
    u32 crc = 0xFFFF;
    u32 i, b;

    for (i = 0; i < DARWIN_PKT - 2; i++) {
        crc ^= (u32)p[i] << 8;
        for (b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
        crc &= 0xFFFF;
    }
    return (u16)crc;
}

static void sio_load(const u8 *tx)
{
    u32 i;
    for (i = 0; i < DARWIN_PKT; i += 4)
        write32(SIO_DATA + i, (u32)tx[i] | ((u32)tx[i + 1] << 8) |
                              ((u32)tx[i + 2] << 16) | ((u32)tx[i + 3] << 24));
}

static void sio_store(u8 *rx)
{
    u32 i, v;
    for (i = 0; i < DARWIN_PKT; i += 4) {
        v = read32(SIO_DATA + i);
        rx[i]     = (u8)v;
        rx[i + 1] = (u8)(v >> 8);
        rx[i + 2] = (u8)(v >> 16);
        rx[i + 3] = (u8)(v >> 24);
    }
}

static int sio_xfer(const u8 *tx, u8 *rx)
{
    u32 start;

    if (!darwin_pin_set())
        return -1;

    write32(SIO_CFG,     SIO_CFG_ON);
    write32(SIO_RATE_LO, (SIO_RATE & 0xFFu) << 24);
    write32(SIO_RATE_HI, (SIO_RATE << 16) & 0xFF000000u);
    write32(SIO_CTRL,    SIO_CTRL_SETUP);
    write32(SIO_CTRL2,   SIO_CTRL2_VAL);

    write32(xcs_reg(GPIO_CLR), BIT(g_pin_bit));
    udelay(CS_SETUP_US);

    write32(SIO_REG08, 0);
    write32(SIO_LEN,   SIO_LEN_VAL);
    sio_load(tx);

    setbits32(SIO_CTRL, SIO_CTRL_GO);

    start = timer_ticks();
    while (read32(SIO_CTRL) & SIO_CTRL_BUSY) {
        if (timer_ticks() - start > BUSY_TIMEOUT_US) {
            /* Release the bus before giving up, or Darwin sees a stuck chip
             * select and the next attempt starts mid-frame. */
            write32(xcs_reg(GPIO_SET), BIT(g_pin_bit));
            write32(SIO_CTRL, SIO_CTRL_STOP);
            write32(SIO_CFG, 0);
            return -1;
        }
    }

    sio_store(rx);

    write32(xcs_reg(GPIO_SET), BIT(g_pin_bit));
    udelay(CS_HOLD_US);

    write32(SIO_CTRL, SIO_CTRL_STOP);
    clrbits32(SIO_CTRL, SIO_CTRL_GO);
    write32(SIO_CFG, 0);
    return 0;
}

void darwin_init(void)
{
    if (!darwin_pin_set())
        return;

    /* Idle high BEFORE handing the pad to the GPIO block, so switching the
     * mux cannot present a spurious falling edge. */
    write32(xcs_reg(GPIO_SET),      BIT(g_pin_bit));
    write32(xcs_reg(GPIO_DIR_SET),  BIT(g_pin_bit));
    write32(xcs_reg(GPIO_FUNC_SET), BIT(g_pin_bit));

    /* Hand the clock and data pads back to the SIO block. */
    write32(xcs_reg(GPIO_FUNC_CLR), SIO_PINS);
    dsb();
}

int darwin_xfer(u8 *tx, u8 *rx, u32 match_len, u32 attempts)
{
    u32 n;
    u16 crc;

    crc = darwin_crc16(tx);
    tx[DARWIN_PKT - 2] = (u8)crc;
    tx[DARWIN_PKT - 1] = (u8)(crc >> 8);

    for (n = 0; n < attempts; n++) {
        if (sio_xfer(tx, rx) == 0) {
            u16 want = darwin_crc16(rx);
            u16 got  = (u16)(rx[DARWIN_PKT - 2] |
                             ((u16)rx[DARWIN_PKT - 1] << 8));
            u32 i;

            /* A validating reply is not necessarily ours: a call that timed
             * out leaves its reply unconsumed. match_len pins it to this
             * command. */
            if (want == got) {
                for (i = 0; i < match_len; i++)
                    if (rx[i] != tx[i])
                        break;
                if (i == match_len)
                    return 0;
            }
        }
    }
    return -1;
}

#define CMD_MEM         0x16

/* The address field is little-endian. */
static void put_addr(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

int darwin_read(u32 addr, u8 *buf, u32 len)
{
    u8 tx[DARWIN_PKT], rx[DARWIN_PKT];
    u32 i;

    if (len == 0 || len > PKT_MAX_PAYLOAD)
        return -1;

    for (i = 0; i < DARWIN_PKT; i++)
        tx[i] = 0;
    tx[0] = CMD_MEM;
    put_addr(tx + 1, addr);
    tx[5] = (u8)len;            /* bit 7 clear selects the read path */

    if (darwin_xfer(tx, rx, PKT_HDR, DARWIN_ATTEMPTS) != 0)
        return -1;

    /* Darwin answers in place, so the payload comes back at PKT_HDR. */
    for (i = 0; i < len; i++)
        buf[i] = rx[PKT_HDR + i];
    return 0;
}

int darwin_vwdt_state(u8 counters[4], u8 reloads[3])
{
    if (counters && darwin_read(VWDT_TABLE, counters, 4) != 0)
        return -1;
    if (reloads && darwin_read(VWDT_RELOAD, reloads, 3) != 0)
        return -1;
    return 0;
}
