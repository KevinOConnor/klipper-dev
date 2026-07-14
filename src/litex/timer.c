// LiteX timers
//
// Copyright (C) 2018-2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/io.h" // readl
#include "board/irq.h" // irq_disable
#include "board/misc.h" // timer_read_time
#include "board/riscv_csr.h" // riscv_csrr
#include "board/riscv_irq.h" // riscv_irq_enable
#include "command.h" // DECL_CONSTANT
#include "litex_defs.h" // LREG_TIMER_load
#include "sched.h" // DECL_INIT

DECL_CONSTANT("CLOCK_FREQ", LDEF_config_clock_frequency);

// Return the number of clock ticks for a given number of microseconds
uint32_t
timer_from_us(uint32_t us)
{
    return us * (LDEF_config_clock_frequency / 1000000);
}

// Return true if time1 is before time2.  Always use this function to
// compare times as regular C comparisons can fail if the counter
// rolls over.
uint8_t
timer_is_before(uint32_t time1, uint32_t time2)
{
    return (int32_t)(time1 - time2) < 0;
}

// Set the next irq time
static void
timer_set_diff(uint32_t value)
{
    writel(LREG_TIMER_en, 0);
    writel(LREG_TIMER_load, value);
    writel(LREG_TIMER_en, 1);
}

// Return the current time (in absolute clock ticks).
uint32_t
timer_read_time(void)
{
    writel(LREG_TIMER_uptime_latch, 1);
    return readl(LREG_TIMER_uptime_cycles);
}

// Activate timer dispatch as soon as possible
void
timer_kick(void)
{
    timer_set_diff(1);
}

static uint32_t timer_repeat_until;
#define TIMER_REPEAT_TICKS timer_from_us(100)

#define TIMER_MIN_TRY_TICKS 90
#define TIMER_DEFER_REPEAT_TICKS timer_from_us(5)

// Invoke timers
static uint32_t
timer_dispatch_many(void)
{
    uint32_t tru = timer_repeat_until;
    for (;;) {
        // Run the next software timer
        uint32_t next = sched_timer_dispatch();

        uint32_t now = timer_read_time();
        int32_t diff = next - now;
        if (diff > (int32_t)TIMER_MIN_TRY_TICKS)
            // Schedule next timer normally.
            return diff;

        if (unlikely(timer_is_before(tru, now))) {
            // Check if there are too many repeat timers
            if (diff < (int32_t)(-timer_from_us(1000)))
                try_shutdown("Rescheduled timer in the past");
            if (sched_check_set_tasks_busy()) {
                timer_repeat_until = now + TIMER_REPEAT_TICKS;
                return TIMER_DEFER_REPEAT_TICKS;
            }
            timer_repeat_until = tru = now + TIMER_REPEAT_TICKS;
        }

        // Next timer in the past or near future - wait for it to be ready
        irq_enable();
        while (unlikely(diff > 0))
            diff = next - timer_read_time();
        irq_disable();
    }
}

// Make sure timer_repeat_until doesn't wrap 32bit comparisons
void
timer_task(void)
{
    uint32_t now = timer_read_time();
    irq_disable();
    if (timer_is_before(timer_repeat_until, now))
        timer_repeat_until = now;
    irq_enable();
}
DECL_TASK(timer_task);

// IRQ handler
void
Timer_IRQHandler(void)
{
    // Backup irq state to support reentrant irqs
    uint32_t mepc = riscv_csrr(RISCV_CSR_MEPC);
    writel(LREG_TIMER_ev_pending, 1);
    // Dispatch timers
    uint32_t diff = timer_dispatch_many();
    timer_set_diff(diff);
    // Restore original irq state
    riscv_csrw(RISCV_CSR_MEPC, mepc);
}

void
timer_init(void)
{
    riscv_irq_enable(Timer_IRQHandler, LDEF_TIMER_interrupt);
    timer_kick();
    timer_repeat_until = timer_read_time() + timer_from_us(1000000);
    writel(LREG_TIMER_ev_enable, 1);
}
DECL_INIT(timer_init);
