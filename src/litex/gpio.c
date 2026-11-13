// GPIO functions on simulator.
//
// Copyright (C) 2026  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/gpio.h" // gpio_out_write
#include "board/io.h" // readl
#include "board/irq.h" // irq_save
#include "command.h" // DECL_ENUMERATION_RANGE
#include "sched.h" // sched_shutdown


/****************************************************************
 * GPIO definitions
 ****************************************************************/

#define GPIO(PORT, NUM) ((PORT) * 32 + (NUM))
#define GPIO2PORT(PIN) ((PIN) / 32)
#define GPIO2BIT(PIN) (1 << ((PIN) % 32))

struct gpiodef {
    void *out;
    void *in;
    void *oe;
};

// The digital_regs[] array is built by scripts/litex/parse_csr.py tool
#include "litex_gpio.h"

// Verify that a gpio is a valid pin and return its definition
struct gpiodef *
gpio_pin_to_def(uint32_t pin)
{
    uint32_t port = GPIO2PORT(pin);
    if (port >= ARRAY_SIZE(digital_regs))
        shutdown("Not a valid pin");
    return &digital_regs[port];
}


/****************************************************************
 * GPIO output
 ****************************************************************/

struct gpio_out
gpio_out_setup(uint32_t pin, uint32_t val)
{
    struct gpiodef *def = gpio_pin_to_def(pin);
    if (! def->out)
        shutdown("Not a valid output pin");
    struct gpio_out g = { .out=def->out, .bit=GPIO2BIT(pin) };
    gpio_out_reset(g, val);
    return g;
}

void
gpio_out_reset(struct gpio_out g, uint32_t val)
{
    int i;
    for (i=0; i<ARRAY_SIZE(digital_regs); i++) {
        struct gpiodef *def = &digital_regs[i];
        if (def->out == g.out) {
            irqstatus_t flag = irq_save();
            uint32_t oldval = readl(g.out);
            if (val)
                writel(g.out, oldval | g.bit);
            else
                writel(g.out, oldval & ~g.bit);
            if (def->oe)
                writel(def->oe, readl(def->oe) | g.bit);
            irq_restore(flag);
            return;
        }
    }
}

void
gpio_out_toggle_noirq(struct gpio_out g)
{
    writel(g.out, readl(g.out) ^ g.bit);
}

void gpio_out_toggle(struct gpio_out g)
{
    irqstatus_t flag = irq_save();
    gpio_out_toggle_noirq(g);
    irq_restore(flag);
}

void
gpio_out_write(struct gpio_out g, uint32_t val)
{
    irqstatus_t flag = irq_save();
    uint32_t oldval = readl(g.out);
    if (val)
        writel(g.out, oldval | g.bit);
    else
        writel(g.out, oldval & ~g.bit);
    irq_restore(flag);
}


/****************************************************************
 * GPIO input
 ****************************************************************/

struct gpio_in
gpio_in_setup(uint32_t pin, int32_t pull_up)
{
    struct gpiodef *def = gpio_pin_to_def(pin);
    if (! def->in)
        shutdown("Not a valid input pin");
    struct gpio_in g = { .in=def->in, .bit=GPIO2BIT(pin) };
    gpio_in_reset(g, pull_up);
    return g;
}

void
gpio_in_reset(struct gpio_in g, int32_t pull_up)
{
    int i;
    for (i=0; i<ARRAY_SIZE(digital_regs); i++) {
        struct gpiodef *def = &digital_regs[i];
        if (def->in == g.in) {
            if (def->oe) {
                irqstatus_t flag = irq_save();
                writel(def->oe, readl(def->oe) & ~g.bit);
                irq_restore(flag);
            }
            return;
        }
    }
}

uint8_t
gpio_in_read(struct gpio_in g)
{
    return !!(readl(g.in) & g.bit);
}


/****************************************************************
 * Dummy SPI and I2C wrappers (needed for software SPI and I2C)
 ****************************************************************/

struct spi_config
spi_setup(uint32_t bus, uint8_t mode, uint32_t rate)
{
    shutdown("Only software SPI supported on LiteX");
}
void
spi_prepare(struct spi_config config)
{
}
void
spi_transfer(struct spi_config config, uint8_t receive_data
             , uint8_t len, uint8_t *data)
{
}

struct i2c_config
i2c_setup(uint32_t bus, uint32_t rate, uint8_t addr)
{
    shutdown("Only software I2C supported on LiteX");
}
int
i2c_write(struct i2c_config config, uint8_t write_len, uint8_t *write)
{
    return 1;
}
int i2c_read(struct i2c_config config, uint8_t reg_len, uint8_t *reg
             , uint8_t read_len, uint8_t *read)
{
    return 1;
}
