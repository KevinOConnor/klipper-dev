#ifndef __LITEX_RISCV_CSR_H
#define __LITEX_RISCV_CSR_H

#include "stdint.h" // uint32_t

#define riscv_csrr(REG) ({                                      \
    uint32_t _value;                                            \
    asm volatile ("csrr %0, %1" : "=r"(_value) : "i"(REG));     \
    _value; })

#define riscv_csrw(REG, VAL) do {                               \
        if (__builtin_constant_p(VAL) && (uint32_t)(VAL) < 32)  \
            asm volatile ("csrw %0, %1" :: "i"(REG), "i"(VAL)); \
        else                                                    \
            asm volatile ("csrw %0, %1" :: "i"(REG), "r"(VAL)); \
    } while (0)

#define riscv_csrrc(REG, VAL) do {                                      \
        if (__builtin_constant_p(VAL) && (VAL) < 32)                    \
            asm volatile ("csrrc x0, %0, %1" :: "i"(REG), "i"(VAL));    \
        else                                                            \
            asm volatile ("csrrc x0, %0, %1" :: "i"(REG), "r"(VAL));    \
    } while (0)

#define riscv_csrrs(REG, VAL) do {                                      \
        if (__builtin_constant_p(VAL) && (VAL) < 32)                    \
            asm volatile ("csrrs x0, %0, %1" :: "i"(REG), "i"(VAL));    \
        else                                                            \
            asm volatile ("csrrs x0, %0, %1" :: "i"(REG), "r"(VAL));    \
    } while (0)

#define RISCV_CSR_MSTATUS 0x300
#define RISCV_CSR_MIE 0x304
#define RISCV_CSR_MTVEC 0x305
#define RISCV_CSR_MEPC 0x341
#define RISCV_CSR_IRQ_MASK 0xBC0
#define RISCV_CSR_IRQ_PENDING 0xFC0

#endif // riscv_csr.h
