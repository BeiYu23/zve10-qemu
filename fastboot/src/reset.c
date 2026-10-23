#include "reset.h"
#include "io.h"

/*
 * cxd900x0_reset() in arch/arm/mach-cxd900x0/core.c:245 asserts XRESET_REQ by
 * writing MISC_RSTREQ_EN | MISC_RSTREQ_ASSERT to VA_MISC_RSTREQ_SET.
 *
 *   MISC base        = CXD900X0_MACRO_BASE + 0x5000 = 0xF2915000
 *                      (include/mach/platform.h:87)
 *   MISC_RSTREQ_SET  = 0x0044   (regs-misc.h:63)
 *   MISC_RSTREQ_EN   = 0x010    (regs-misc.h:61)
 *   MISC_RSTREQ_ASSERT = 0x001  (regs-misc.h:62)
 */
#define MISC_RSTREQ_SET  0xF2915044u
#define MISC_RSTREQ_EN   0x010u
#define MISC_RSTREQ_ASSERT 0x001u

/*
 * The surviving app watchdog.
 *
 * The camera's userland arms the TIMER-IP watchdog (CXD900X0_WDT_TIMER,
 * platform.h:57 = 0xF2402000) through /proc/wdt (WDT_SET_START: RSTREQ_EN +
 * a raw uS count in TIMERCMP; the WD clock is 1 MHz) and re-arms it
 * periodically. The GP27 XRESET_REQ pulse only resets the AP: the
 * peripheral-domain MISC_RSTREQ_EN bit and the running timer survive, so an
 * armed watchdog kills the payload ~3m40s after the last app re-arm --
 * exactly the observed "fastboot exits by itself".
 *
 * wdt_kill() is the kernel wdt driver's WDT_SET_STOP (wdt.c:379):
 * clear the RSTREQ enable, stop the timer, clear counter + interrupt.
 */
#define MISC_RSTREQ_CLR  0xF2915048u
#define WDT_TIMERCTL     0xF2402000u    /* CXD90014_TIMERCTL   */
#define WDT_TIMERCLR     0xF2402004u    /* CXD90014_TIMERCLR   */
#define WDT_TIMERCMP     0xF2402008u    /* CXD90014_TIMERCMP   */
#define TMCLR            0x0010u
#define TMINTCLR         0x0001u

void wdt_kill(void)
{
    write32(MISC_RSTREQ_CLR, MISC_RSTREQ_EN);
    write32(WDT_TIMERCTL, 0);
    write32(WDT_TIMERCLR, TMCLR | TMINTCLR);
    dsb();
}

void system_reset(void)
{
    __asm__ volatile("cpsid aif" ::: "memory");

    write32(MISC_RSTREQ_SET, MISC_RSTREQ_EN | MISC_RSTREQ_ASSERT);
    dsb();

    for (;;)
        __asm__ volatile("wfi");
}
