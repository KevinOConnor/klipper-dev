# Additional LiteX build rules

# Setup the toolchain
CROSS_PREFIX=riscv64-unknown-elf-

dirs-y += src/litex src/generic

LIBDIR := /usr/lib/picolibc/riscv64-unknown-elf/

CFLAGS += -I$(LIBDIR)/include -L$(LIBDIR)lib/rv32im/ilp32
CFLAGS += -march=rv32i2p0_m -mabi=ilp32

CFLAGS_klipper.elf += -nostdlib -lgcc -lc
CFLAGS_klipper.elf += -T $(OUT)src/litex/riscv_link.ld
$(OUT)klipper.elf: $(OUT)src/litex/riscv_link.ld

src-y += litex/main.c litex/timer.c litex/serial.c
src-y += litex/riscv_boot.c litex/riscv_irq.c
src-y += generic/crc16_ccitt.c generic/serial_irq.c

# Build litex_defs.h file
$(OUT)litex_defs.h: scripts/litex/parse_csr.py $(OUT)autoconf.h $(CSRFILE)
	@echo "  Parsing CSRFILE $(CSRFILE)"
	$(Q)if [ -z $(CSRFILE) ] ; then echo "Please specify CSRFILE"; exit 1 ; fi
	$(Q)scripts/litex/parse_csr.py -r $(CONFIG_LITEX_RAM_SEGMENT) -c $(CONFIG_LITEX_CODE_SEGMENT) -i $(CSRFILE) -o $@

# Binary output file rules
target-y := $(OUT)litex_defs.h $(target-y) $(OUT)klipper.bin

$(OUT)klipper.bin: $(OUT)klipper.elf
	@echo "  Creating bin file $@"
	$(Q)$(OBJCOPY) -O binary $< $@
