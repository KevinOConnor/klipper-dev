// RiscV interrupt vector table and initial bootup handling
//
// Copyright (C) 2019-2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_MCU
#include "board/misc.h" // dynmem_start
#include "command.h" // DECL_CONSTANT_STR
#include "riscv_boot.h" // riscv_main
#include "riscv_irq.h" // riscv_irq_configure

// Export MCU type
DECL_CONSTANT_STR("MCU", CONFIG_MCU);

// Symbols created by riscv_link.lds.S linker script
extern uint32_t _data_start, _data_end, _data_flash;
extern uint32_t _bss_start, _bss_end, _stack_start;
extern uint32_t _stack_end;

/****************************************************************
 * Basic interrupt handlers
 ****************************************************************/

// Inlined version of memset (to avoid function calls during intial boot code)
static void __always_inline
boot_memset(void *s, int c, size_t n)
{
    volatile uint32_t *p = s;
    while (n) {
        *p++ = c;
        n -= sizeof(*p);
    }
}

// Inlined version of memcpy (to avoid function calls during intial boot code)
static void __always_inline
boot_memcpy(void *dest, const void *src, size_t n)
{
    const uint32_t *s = src;
    volatile uint32_t *d = dest;
    while (n) {
        *d++ = *s++;
        n -= sizeof(*d);
    }
}

// Main initialization code (called from _start below)
void __visible __noreturn __section(".text.riscv_boot.stage_two")
reset_handler_stage_two(void)
{
    // Copy global variables from flash to ram
    uint32_t count = (&_data_end - &_data_start) * 4;
    boot_memcpy(&_data_start, &_data_flash, count);

    // Clear the bss segment
    boot_memset(&_bss_start, 0, (&_bss_end - &_bss_start) * 4);

    barrier();

    riscv_irq_configure();

    // Run the main board specific code
    riscv_main();

    // The riscv_main() call should not return
    for (;;)
        ;
}

// Initial code entry point - invoked by the processor after a reset
asm(
    ".section .text.riscv_boot.stage_one\n"
    ".global reset_handler_stage_one\n"
    "reset_handler_stage_one:\n"
    "    csrrc x0, mstatus, 0x8\n" // Disable irqs
    "    la sp, _stack_end\n"
    "    call reset_handler_stage_two");


/****************************************************************
 * Dynamic memory range
 ****************************************************************/

// Return the start of memory available for dynamic allocations
void *
dynmem_start(void)
{
    return &_bss_end;
}

// Return the end of memory available for dynamic allocations
void *
dynmem_end(void)
{
    return &_stack_start;
}
