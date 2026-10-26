#ifndef ZVE10_DARWIN_H
#define ZVE10_DARWIN_H

#include "io.h"

/*
 * Link to Darwin, the power-supervisor MCU, over SIO channel 0.
 *
 * Ported from ILCE-7M4-RE (payloads/fastboot/src/darwin.c). The transport,
 * the packet format, the CRC and the Virtual-WDT addresses carry over
 * unchanged; only the GPIO block differs between CXD90057 and CXD90045.
 *
 * WHY THIS EXISTS
 *   Darwin counts four one-byte down-counters at 0x200026FC and kills the
 *   system when one reaches zero. Nothing in bootrom context re-arms them, so
 *   a fastboot session dies after 5-9 minutes -- and much sooner when the AP
 *   never got to populate Darwin's config this power cycle. Any SIO exchange
 *   feeds the counters, so a periodic read keeps the session alive.
 *
 * DIFFERENCE FROM 7M4
 *   The 7M4 hard-codes XCS to GPIO port 4 bit 2. On ZV-E10 that pin has not
 *   been identified yet, so the port and bit are RUNTIME values set with
 *   darwin_set_pin(). Nothing touches GPIO until that is called, which keeps
 *   the payload's start-up behaviour identical to the build without darwin
 *   support.
 */

#define DARWIN_PKT      0x80    /* packet size, both directions */

/* Darwin RAM. Command 0x16 bounds writes to [0x20000000, 0x20004000]. */
#define DARWIN_RAM_BASE 0x20000000u

/* The Virtual WDT counter table: 4 bytes, 0xFF = disabled. */
#define VWDT_TABLE      0x200026FCu

/* Per-channel reload bytes, loaded by every vwdt_set call site. */
#define VWDT_RELOAD     0x2000003Cu

#define PKT_HDR         6       /* opcode + 4-byte address + length */
#define PKT_MAX_PAYLOAD 0x78
#define DARWIN_ATTEMPTS 8       /* ~11 ms; short enough never to starve USB */

/* SIO channel 0. Same base on CXD90045 and CXD90057. */
#define SIO_BASE        0xF1005000u

/*
 * GPIO block. CXD90045 uses 0xF2910000 with a 0x100 stride per port; the
 * 7M4's 0xF101D000 / 0x1000 does not apply here.
 */
#define XCS_GPIO_BASE   0xF2910000u
#define XCS_GPIO_STRIDE 0x100u

/* Register offsets inside one GPIO port (same on both SoCs). */
#define GPIO_DIR_SET    0x04u
#define GPIO_FUNC_SET   0x34u
#define GPIO_FUNC_CLR   0x38u
#define GPIO_SET        0x44u   /* idle high   */
#define GPIO_CLR        0x48u   /* assert low  */

/*
 * The SIO clock and data pads share a port with XCS on the 7M4 (bits 0/1/3
 * muxed to the SIO function, XCS on bit 2). That relationship is the basis of
 * the pin search: find the port whose bits 0/1/3 are muxed away from GPIO and
 * bit 2 of that port is the chip select.
 */
#define SIO_PINS        (BIT(0) | BIT(1) | BIT(3))

#define CS_SETUP_US     150u
#define CS_HOLD_US      1200u
#define BUSY_TIMEOUT_US 20000u

/* Set the chip-select GPIO. Call before darwin_init(). */
void darwin_set_pin(u32 port, u32 bit);
u32  darwin_pin_port(void);
u32  darwin_pin_bit(void);
int  darwin_pin_set(void);

/* Idle the chip select and hand the clock/data pads to SIO. Sends nothing. */
void darwin_init(void);

/* One raw 0x80-byte transaction; seals tx's CRC in place. */
int darwin_xfer(u8 *tx, u8 *rx, u32 match_len, u32 attempts);

/* Command 0x16: read up to 0x78 bytes from any Darwin address. */
int darwin_read(u32 addr, u8 *buf, u32 len);

/* Read the four live counters and the three reload bytes. Reading feeds it. */
int darwin_vwdt_state(u8 counters[4], u8 reloads[3]);

#endif /* ZVE10_DARWIN_H */
