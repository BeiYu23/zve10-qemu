#include "sdhci.h"
#include "io.h"
#include "timer.h"

/*
 * Ported from the ILCE-7M4-RE payload's src/sdhci.c (CXD90057). Base and
 * register offsets are the same; the divider arithmetic and PIO details are
 * unchanged. This build identifies the card and then moves data: CMD17 read
 * and (mode=1) CMD24 write, both one 512-byte block at a time in PIO.
 */

#define CMD_TIMEOUT_TICKS   (TIMER0_HZ / 2)     /* 500 ms */
#define BUSY_TIMEOUT_TICKS  (TIMER0_HZ)         /* 1 s    */

static void sdhci_reset(u8 mask)
{
    u32 start = timer_ticks();

    write8(SDHCI_SOFTWARE_RESET, mask);
    while (read8(SDHCI_SOFTWARE_RESET) & mask) {
        if (timer_ticks() - start > CMD_TIMEOUT_TICKS)
            return;
    }
}

static void sdhci_recover(void)
{
    sdhci_reset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    write16(SDHCI_INT_STATUS, read16(SDHCI_INT_STATUS));
    write16(SDHCI_ERR_INT_STATUS, read16(SDHCI_ERR_INT_STATUS));
}

static int wait_inhibit(u32 mask)
{
    u32 start = timer_ticks();

    while (read32(SDHCI_PRESENT_STATE) & mask) {
        if (timer_ticks() - start > CMD_TIMEOUT_TICKS)
            return MMC_ERR_INHIBIT;
    }
    return MMC_OK;
}

static int wait_int(u16 mask, u32 timeout, int err)
{
    u32 start = timer_ticks();
    u16 sts;

    for (;;) {
        sts = read16(SDHCI_INT_STATUS);

        if (sts & SDHCI_INT_ERROR) {
            write16(SDHCI_ERR_INT_STATUS, read16(SDHCI_ERR_INT_STATUS));
            write16(SDHCI_INT_STATUS, sts);
            sdhci_recover();
            return MMC_ERR_CMD_ERROR;
        }
        if (sts & mask) {
            write16(SDHCI_INT_STATUS, mask);
            return MMC_OK;
        }
        if (timer_ticks() - start > timeout) {
            sdhci_recover();
            return err;
        }
    }
}

static int send_cmd(u8 index, u32 arg, u16 flags)
{
    int ret;

    ret = wait_inhibit(SDHCI_CMD_INHIBIT);
    if (ret) {
        sdhci_recover();
        ret = wait_inhibit(SDHCI_CMD_INHIBIT);
        if (ret)
            return ret;
    }

    write16(SDHCI_INT_STATUS, read16(SDHCI_INT_STATUS));
    write16(SDHCI_ERR_INT_STATUS, read16(SDHCI_ERR_INT_STATUS));

    write32(SDHCI_ARGUMENT, arg);
    dsb();
    write16(SDHCI_COMMAND, SDHCI_MAKE_CMD(index, flags));
    dsb();

    return wait_int(SDHCI_INT_CMD_COMPLETE, CMD_TIMEOUT_TICKS,
                    MMC_ERR_CMD_TIMEOUT);
}

/* R1b commands (CMD7) hold DAT0 low until the card finishes. */
static int mmc_raw_cmd(u8 index, u32 arg, u16 flags, u32 *resp)
{
    int ret = send_cmd(index, arg, flags);

    if (resp)
        *resp = read32(SDHCI_RESPONSE);

    if (ret == MMC_OK && (flags & 0x3) == SDHCI_CMD_RESP_BUSY)
        ret = wait_int(SDHCI_INT_XFER_COMPLETE, BUSY_TIMEOUT_TICKS,
                       MMC_ERR_XFER);
    return ret;
}

/* div is the SDHCI divider N: card clock = base / (2*N). */
static void sdhci_set_clock(u32 div)
{
    u32 start;
    u16 clk;

    clk = read16(SDHCI_CLOCK_CONTROL);
    write16(SDHCI_CLOCK_CONTROL, clk & (u16)~SDHCI_CLK_SD_EN);

    clk = (u16)(((div & 0xff) << SDHCI_CLK_DIV_SHIFT) |
                (((div >> 8) & 0x3) << SDHCI_CLK_DIV_HI_SHIFT) |
                SDHCI_CLK_INT_EN);
    write16(SDHCI_CLOCK_CONTROL, clk);

    start = timer_ticks();
    while (!(read16(SDHCI_CLOCK_CONTROL) & SDHCI_CLK_INT_STABLE)) {
        if (timer_ticks() - start > CMD_TIMEOUT_TICKS)
            break;
    }

    write16(SDHCI_CLOCK_CONTROL, (u16)(clk | SDHCI_CLK_SD_EN));
    udelay(100);
}

/* Poll CMD13 until the card is idle (TRAN + READY_FOR_DATA). */
static int wait_tran(u32 rca, u32 timeout)
{
    u32 start = timer_ticks(), resp = 0;
    int ret;

    for (;;) {
        ret = mmc_raw_cmd(MMC_CMD_SEND_STATUS, rca << 16,
                          SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                          SDHCI_CMD_INDEX_CHECK, &resp);
        if (ret)
            return ret;
        if (MMC_R1_STATE(resp) == MMC_STATE_TRAN &&
            (resp & MMC_R1_READY_FOR_DATA))
            return MMC_OK;
        if (timer_ticks() - start > timeout)
            return MMC_ERR_XFER;
    }
}

/* One-block PIO read. DMA Enable stays clear, as in the CXD90057 driver. */
static int mmc_read_block(u32 block, void *buf)
{
    u32 *out = (u32 *)buf;
    u32 w;
    int ret;

    ret = wait_inhibit(SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT);
    if (ret)
        return ret;

    write16(SDHCI_BLOCK_SIZE, 512);
    write16(SDHCI_BLOCK_COUNT, 1);
    write16(SDHCI_TRANSFER_MODE, SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);
    dsb();

    ret = send_cmd(MMC_CMD_READ_SINGLE_BLOCK, block,
                   SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                   SDHCI_CMD_INDEX_CHECK | SDHCI_CMD_DATA_PRESENT);
    if (ret)
        return ret;

    ret = wait_int(SDHCI_INT_BUF_READ_RDY, CMD_TIMEOUT_TICKS,
                   MMC_ERR_DATA_TIMEOUT);
    if (ret)
        return ret;

    for (w = 0; w < 512 / 4; w++)
        out[w] = read32(SDHCI_BUFFER);

    return wait_int(SDHCI_INT_XFER_COMPLETE, CMD_TIMEOUT_TICKS, MMC_ERR_XFER);
}

/* One-block PIO write, mirroring mmc_read_block. DMA Enable stays clear.
 * After XFER_COMPLETE the card may still be programming (PRG state), so poll
 * CMD13 until it reports TRAN + READY_FOR_DATA. */
static int mmc_write_block(u32 block, const void *buf, u32 rca)
{
    const u32 *in = (const u32 *)buf;
    u32 w;
    int ret;

    ret = wait_inhibit(SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT);
    if (ret)
        return ret;

    write16(SDHCI_BLOCK_SIZE, 512);
    write16(SDHCI_BLOCK_COUNT, 1);
    write16(SDHCI_TRANSFER_MODE, SDHCI_TRNS_BLK_CNT_EN);   /* write: no READ bit */
    dsb();

    ret = send_cmd(MMC_CMD_WRITE_SINGLE_BLOCK, block,
                   SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                   SDHCI_CMD_INDEX_CHECK | SDHCI_CMD_DATA_PRESENT);
    if (ret)
        return ret;

    ret = wait_int(SDHCI_INT_BUF_WRITE_RDY, CMD_TIMEOUT_TICKS,
                   MMC_ERR_DATA_TIMEOUT);
    if (ret)
        return ret;

    for (w = 0; w < 512 / 4; w++)
        write32(SDHCI_BUFFER, in[w]);

    ret = wait_int(SDHCI_INT_XFER_COMPLETE, CMD_TIMEOUT_TICKS, MMC_ERR_XFER);
    if (ret)
        return ret;

    /* R1b-ish: wait for the card to finish programming. */
    return wait_tran(rca, BUSY_TIMEOUT_TICKS);
}

/* CMD12 + wait for TRAN: terminates a multi-block transfer. */
static int mmc_stop(u32 rca)
{
    int ret = send_cmd(MMC_CMD_STOP_TRANSMISSION, rca << 16,
                       SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                       SDHCI_CMD_INDEX_CHECK);
    if (ret)
        return ret;
    return wait_tran(rca, BUSY_TIMEOUT_TICKS);
}

/* Multi-block PIO read (CMD18 + manual CMD12). buf must hold count*512. */
static int mmc_read_multi(u32 block, u32 count, void *buf)
{
    u32 *out = (u32 *)buf;
    u32 b, w;
    int ret;

    ret = wait_inhibit(SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT);
    if (ret)
        return ret;

    write16(SDHCI_BLOCK_SIZE, 512);
    write16(SDHCI_BLOCK_COUNT, count);
    write16(SDHCI_TRANSFER_MODE,
            SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI);
    dsb();

    ret = send_cmd(MMC_CMD_READ_MULTIPLE_BLOCK, block,
                   SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                   SDHCI_CMD_INDEX_CHECK | SDHCI_CMD_DATA_PRESENT);
    if (ret)
        return ret;

    for (b = 0; b < count; b++) {
        ret = wait_int(SDHCI_INT_BUF_READ_RDY, CMD_TIMEOUT_TICKS,
                       MMC_ERR_DATA_TIMEOUT);
        if (ret) {
            mmc_stop(1);
            return ret;
        }
        for (w = 0; w < 512 / 4; w++)
            out[b * (512 / 4) + w] = read32(SDHCI_BUFFER);
    }

    ret = wait_int(SDHCI_INT_XFER_COMPLETE, CMD_TIMEOUT_TICKS, MMC_ERR_XFER);
    if (ret) {
        mmc_stop(1);
        return ret;
    }
    return mmc_stop(1);
}

/* Multi-block PIO write (CMD25 + manual CMD12). */
static int mmc_write_multi(u32 block, u32 count, const void *buf, u32 rca)
{
    const u32 *in = (const u32 *)buf;
    u32 b, w;
    int ret;

    ret = wait_inhibit(SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT);
    if (ret)
        return ret;

    write16(SDHCI_BLOCK_SIZE, 512);
    write16(SDHCI_BLOCK_COUNT, count);
    write16(SDHCI_TRANSFER_MODE, SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI);
    dsb();

    ret = send_cmd(MMC_CMD_WRITE_MULTIPLE_BLOCK, block,
                   SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                   SDHCI_CMD_INDEX_CHECK | SDHCI_CMD_DATA_PRESENT);
    if (ret)
        return ret;

    for (b = 0; b < count; b++) {
        ret = wait_int(SDHCI_INT_BUF_WRITE_RDY, CMD_TIMEOUT_TICKS,
                       MMC_ERR_DATA_TIMEOUT);
        if (ret) {
            mmc_stop(rca);
            return ret;
        }
        for (w = 0; w < 512 / 4; w++)
            write32(SDHCI_BUFFER, in[b * (512 / 4) + w]);
    }

    ret = wait_int(SDHCI_INT_XFER_COMPLETE, CMD_TIMEOUT_TICKS, MMC_ERR_XFER);
    if (ret) {
        mmc_stop(rca);
        return ret;
    }
    return mmc_stop(rca);
}

int stage2_main(void)
{
    volatile struct stage2_result *res =
        (volatile struct stage2_result *)STAGE2_RESULT;

    u16 saved_clk = read16(SDHCI_CLOCK_CONTROL);
    u8  saved_host = read8(SDHCI_HOST_CONTROL);
    u16 r1flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC_CHECK |
                  SDHCI_CMD_INDEX_CHECK;
    u32 base_khz, div, start, resp = 0;
    int ret = MMC_OK;
    int i;

    for (i = 0; i < 16; i++)
        ((volatile u32 *)res)[i] = 0;

    res->magic = STAGE2_MAGIC;
    res->caps = read32(SDHCI_CAPABILITIES);
    res->host_version = read16(SDHCI_HOST_VERSION);
    res->host_control = saved_host;
    res->clock_control = saved_clk;

    /* Identification runs at <= 400 kHz. */
    base_khz = ((read32(SDHCI_CAPABILITIES) >> 8) & 0xff) * 1000;
    if (base_khz == 0)
        base_khz = 192000;
    div = base_khz / (2 * 400);
    if (div == 0)
        div = 1;

    /* The controller gates the status bits themselves, so polling needs the
     * enables set. Interrupts stay masked at the CPU. */
    write16(SDHCI_INT_ENABLE, SDHCI_INT_CMD_COMPLETE |
                              SDHCI_INT_XFER_COMPLETE |
                              SDHCI_INT_BUF_WRITE_RDY |
                              SDHCI_INT_BUF_READ_RDY);
    write16(SDHCI_ERR_INT_ENABLE, 0xffff);

    sdhci_reset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    write8(SDHCI_HOST_CONTROL, (u8)(saved_host & ~0x22));   /* 1-bit bus */
    sdhci_set_clock(div);

    res->step = 1;
    ret = send_cmd(MMC_CMD_GO_IDLE_STATE, 0, SDHCI_CMD_RESP_NONE);
    if (ret)
        goto out;
    mdelay(2);

    /* CMD1: bit 30 asks for sector addressing; poll until bit 31 sets. */
    res->step = 2;
    start = timer_ticks();
    for (;;) {
        ret = mmc_raw_cmd(MMC_CMD_SEND_OP_COND, 0x40FF8080,
                          SDHCI_CMD_RESP_SHORT, &resp);
        if (ret)
            goto out;
        res->ocr = resp;
        if (resp & 0x80000000u)
            break;
        if (timer_ticks() - start > TIMER0_HZ) {
            ret = MMC_ERR_CMD_TIMEOUT;
            goto out;
        }
        mdelay(5);
    }

    /* CMD2 ALL_SEND_CID, 136-bit. */
    res->step = 3;
    ret = mmc_raw_cmd(MMC_CMD_ALL_SEND_CID, 0,
                      SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC_CHECK, 0);
    if (ret)
        goto out;
    res->cid[0] = read32(SDHCI_RESPONSE + 0x0);
    res->cid[1] = read32(SDHCI_RESPONSE + 0x4);
    res->cid[2] = read32(SDHCI_RESPONSE + 0x8);
    res->cid[3] = read32(SDHCI_RESPONSE + 0xC);

    /* CMD3 SET_RELATIVE_ADDR, host-assigned RCA. */
    res->step = 4;
    res->rca = 1;
    ret = mmc_raw_cmd(MMC_CMD_SET_RELATIVE_ADDR, res->rca << 16, r1flags, 0);
    if (ret)
        goto out;

    /* CMD9 SEND_CSD, 136-bit; the card is still in stand-by. */
    res->step = 5;
    ret = mmc_raw_cmd(MMC_CMD_SEND_CSD, res->rca << 16,
                      SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC_CHECK, 0);
    if (ret)
        goto out;
    res->csd[0] = read32(SDHCI_RESPONSE + 0x0);
    res->csd[1] = read32(SDHCI_RESPONSE + 0x4);
    res->csd[2] = read32(SDHCI_RESPONSE + 0x8);
    res->csd[3] = read32(SDHCI_RESPONSE + 0xC);

    /* CMD7 SELECT_CARD, R1b. */
    res->step = 6;
    ret = mmc_raw_cmd(MMC_CMD_SELECT_CARD, res->rca << 16,
                      SDHCI_CMD_RESP_BUSY | SDHCI_CMD_CRC_CHECK |
                      SDHCI_CMD_INDEX_CHECK, 0);
    if (ret)
        goto out;

    /* CMD13 SEND_STATUS, read-only sanity check: state should be TRAN(4). */
    res->step = 7;
    ret = mmc_raw_cmd(MMC_CMD_SEND_STATUS, res->rca << 16, r1flags, &resp);
    res->r1 = resp;
    if (ret)
        goto out;

    if (MMC_R1_STATE(resp) != MMC_STATE_TRAN ||
        !(resp & MMC_R1_READY_FOR_DATA)) {
        ret = MMC_ERR_XFER;
        goto out;
    }

    /* Identification is done at 400 kHz; raise the bus for the data phase.
     * Backward-compatible eMMC timing allows up to 26 MHz without CMD6, so
     * target 25 MHz and let sdhci_set_clock round to a legal divider. */
    if (ret == MMC_OK) {
        u32 div_fast = base_khz / (2 * 25000);
        if (div_fast == 0)
            div_fast = 1;
        sdhci_set_clock(div_fast);
        res->data_clk_div = div_fast;
    }

out:;
    /* Read what the host poked into the parameter block: block address,
     * count and mode (see sdhci.h). */
    u32 block = *(volatile u32 *)(STAGE2_PARAMS + 0);
    u32 count = *(volatile u32 *)(STAGE2_PARAMS + 4);
    u32 mode = *(volatile u32 *)(STAGE2_PARAMS + 8);

    if (count == 0)
        count = 1;
    if (count > STAGE2_DATA_MAX / 512)
        count = STAGE2_DATA_MAX / 512;    /* the host iterates in 64 KiB chunks */

    if (mode == 1) {
        res->write_block = block;
        res->write_status = (u32)MMC_ERR_CMD_ERROR;    /* not attempted */
        if (ret == MMC_OK) {                            /* only in TRAN state */
            res->write_status = 0;
            if (count == 1)
                res->write_status = (u32)mmc_write_block(
                    block, (const void *)STAGE2_DATA, res->rca);
            else
                res->write_status = (u32)mmc_write_multi(
                    block, count, (const void *)STAGE2_DATA, res->rca);
            ret = (int)res->write_status;
        }
    } else {
        res->read_block = block;
        res->read_status = 0;
        if (count == 1)
            res->read_status = (u32)mmc_read_block(
                block, (void *)STAGE2_DATA);
        else
            res->read_status = (u32)mmc_read_multi(
                block, count, (void *)STAGE2_DATA);
        if (!ret)
            ret = (int)res->read_status;
    }

    res->status = (u32)ret;
    write8(SDHCI_HOST_CONTROL, saved_host);
    write16(SDHCI_CLOCK_CONTROL, saved_clk);
    udelay(100);
    res->step = res->step | 0x80000000u;   /* finished */
    return ret;
}
