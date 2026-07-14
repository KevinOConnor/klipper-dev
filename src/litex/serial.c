// Litex uart handling
//
// Copyright (C) 2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/io.h" // readl
#include "board/riscv_irq.h" // riscv_irq_enable
#include "board/serial_irq.h" // serial_get_tx_byte
#include "litex_defs.h" // LDEF_UART_interrupt
#include "sched.h" // DECL_INIT

#define UART_EV_TX    0x1
#define UART_EV_RX    0x2

void
UART_IRQHandler(void)
{
    while (!readl(LREG_UART_rxempty)) {
        serial_rx_byte(readl(LREG_UART_rxtx));
        writel(LREG_UART_ev_pending, UART_EV_RX);
    }

    while (!readl(LREG_UART_txfull)) {
        uint8_t data;
        int ret = serial_get_tx_byte(&data);
        if (ret) {
            writel(LREG_UART_ev_enable, UART_EV_RX);
            break;
        }
        writel(LREG_UART_rxtx, data);
    }
}

void
serial_enable_tx_irq(void)
{
    writel(LREG_UART_ev_enable, UART_EV_TX | UART_EV_RX);
}

void
serial_init(void)
{
    serial_enable_tx_irq();
    riscv_irq_enable(UART_IRQHandler, LDEF_UART_interrupt);
}
DECL_INIT(serial_init);
