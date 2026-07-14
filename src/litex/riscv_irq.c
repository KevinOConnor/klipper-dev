// Definitions for irq enable/disable on LiteX RiscV
//
// Copyright (C) 2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/irq.h" // irqstatus_t
#include "command.h" // shutdown
#include "litex_defs.h" // LDEF_TIMER_interrupt
#include "riscv_csr.h" // riscv_csrr
#include "riscv_irq.h" // riscv_irq_enable
#include "sched.h" // DECL_SHUTDOWN

#define CSR_MSTATUS_MIE 0x08

void
irq_disable(void)
{
    riscv_csrrc(RISCV_CSR_MSTATUS, CSR_MSTATUS_MIE);
    barrier();
}

void
irq_enable(void)
{
    barrier();
    riscv_csrrs(RISCV_CSR_MSTATUS, CSR_MSTATUS_MIE);
}

irqstatus_t
irq_save(void)
{
    irqstatus_t flag = riscv_csrr(RISCV_CSR_MSTATUS);
    irq_disable();
    return flag;
}

void
irq_restore(irqstatus_t flag)
{
    if (flag & CSR_MSTATUS_MIE)
        irq_enable();
}

void __attribute__((interrupt))
main_irq_handler(void)
{
    uint32_t irqs = (riscv_csrr(RISCV_CSR_IRQ_PENDING)
                     & riscv_csrr(RISCV_CSR_IRQ_MASK));
    if (irqs & (1<<LDEF_UART_interrupt))
        UART_IRQHandler();
    if (irqs & (1<<LDEF_TIMER_interrupt))
        Timer_IRQHandler();
}

void
riscv_irq_enable(irqhandler_t func, uint32_t num)
{
    riscv_csrrs(RISCV_CSR_IRQ_MASK, 1 << num);
}

void
riscv_irq_configure(void)
{
    riscv_csrw(RISCV_CSR_IRQ_MASK, 0);
    riscv_csrw(RISCV_CSR_MTVEC, main_irq_handler);
    riscv_csrw(RISCV_CSR_MIE, 0x880);
    irq_enable();
}

void
irq_wait(void)
{
    asm volatile("wfi");
    irq_enable();
    asm volatile("nop\n nop");
    irq_disable();
}

void
irq_poll(void)
{
}
