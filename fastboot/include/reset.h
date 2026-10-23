#ifndef ZVE10_RESET_H
#define ZVE10_RESET_H

#include "io.h"

/* Assert XRESET_REQ through the MISC block. Nothing after the call runs. */
__attribute__((noreturn)) void system_reset(void);

/* Disable the app watchdog that survives the XRESET_REQ boot pulse.
 * Call once before anything slow: it is what keeps the session alive
 * past ~3m40s instead of dying to a stale RSTREQ. */
void wdt_kill(void);

#endif /* ZVE10_RESET_H */
