// Main starting point for LiteX system-on-chip
//
// Copyright (C) 2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/riscv_boot.h" // riscv_main
#include "sched.h" // sched_main

void
riscv_main(void)
{
    sched_main();
}
