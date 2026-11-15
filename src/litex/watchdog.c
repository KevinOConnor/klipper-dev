// Watchdog handler on LiteX
//
// Copyright (C) 2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/io.h" // writel
#include "board/misc.h" // timer_from_us
#include "litex_defs.h" // LREG_WATCHDOG_control
#include "sched.h" // DECL_TASK

#define WATCHDOG_TIMEOUT_US 200000 // 200ms
#define WATCHDOG_KEY ((1<<16) | (1<<8) | 1) // reset, enable, feed bits

void
watchdog_reset(void)
{
    writel(LREG_WATCHDOG_control, WATCHDOG_KEY);
}
DECL_TASK(watchdog_reset);

void
watchdog_init(void)
{
    writel(LREG_WATCHDOG_cycles, timer_from_us(WATCHDOG_TIMEOUT_US));
    watchdog_reset();
}
DECL_INIT(watchdog_init);
