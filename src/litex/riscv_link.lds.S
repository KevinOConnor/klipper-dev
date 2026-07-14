// LiteX RiscV linker script
//
// Copyright (C) 2019-2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_STACK_SIZE
#include "litex_defs.h" // LMEM_ADDR_CODE

MEMORY
{
  rom (rx) : ORIGIN = LMEM_ADDR_CODE , LENGTH = LMEM_SIZE_CODE
  ram (rwx) : ORIGIN = LMEM_ADDR_RAM , LENGTH = LMEM_SIZE_RAM
}

SECTIONS
{
    .text : {
        . = ALIGN(4);
        KEEP(*(.text.riscv_boot.stage_one))
        *(.text .text.*)
        *(.srodata .srodata*)
        *(.rodata .rodata*)
    } > rom

    . = ALIGN(4);
    _data_flash = .;

    .data : AT (_data_flash)
    {
        . = ALIGN(4);
        _data_start = .;
        *(.ramfunc .ramfunc.*);
        *(.sdata .sdata.*);
        *(.data .data.*);
        . = ALIGN(4);
        _data_end = .;
    } > ram

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    _stack_start = LMEM_ADDR_RAM + LMEM_SIZE_RAM - CONFIG_STACK_SIZE ;
    .stack _stack_start (NOLOAD) :
    {
        . = . + CONFIG_STACK_SIZE;
        _stack_end = .;
    } > ram

    /DISCARD/ : {
    }
}
