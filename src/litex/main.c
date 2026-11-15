// Main starting point for LiteX system-on-chip
//
// Copyright (C) 2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/io.h" // writel
#include "board/riscv_boot.h" // riscv_main
#include "command.h" // DECL_COMMAND_FLAGS
#include "litex_defs.h" // LREG_SOC_reset
#include "sched.h" // sched_main

// Implement standard "reset" handler
void
command_reset(uint32_t *args)
{
    writel(LREG_SOC_reset, 1);
}
DECL_COMMAND_FLAGS(command_reset, HF_IN_SHUTDOWN, "reset");

// Startup code
void
riscv_main(void)
{
    sched_main();
}
