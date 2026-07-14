#ifndef __GENERIC_RISCV_IRQ_H
#define __GENERIC_RISCV_IRQ_H

#include <stdint.h> // uint32_t

void riscv_irq_configure(void);
typedef void (*irqhandler_t)(void);
void riscv_irq_enable(irqhandler_t func, uint32_t num);

void UART_IRQHandler(void);
void Timer_IRQHandler(void);

#endif // armcm_irq.h
