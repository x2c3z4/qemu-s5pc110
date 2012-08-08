/*
 * Watchdog Timer for Samsung S5C110-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "sysbus.h"
#include "s5pc1xx.h"
#include "qemu-timer.h"


#define WTCON       0x00    /* R/W Watchdog Timer Control Register */
#define WTDAT       0x04    /* R/W Watchdog Timer Data Register */
#define WTCNT       0x08    /* R/W Watchdog Timer Count Register */
#define WTCLRINT    0x0C    /* W Watchdog Timer Interrupt Clear Register */

#define PRESCALER_SHIFT (8)
#define WDT_EN          (1 << 5)
#define CLK_SEL_SHIFT   (3)
#define INT_EN          (1 << 2)
#define RESET_EN        (1 << 0)

#define S5PC1XX_WDT_REG_MEM_SIZE 0x14


typedef struct S5pc1xxWDTState {
    SysBusDevice busdev;

    qemu_irq  irq;
    uint32_t  regs[WTCNT + 1];

    QEMUTimer *wdt_timer;
    uint32_t  freq_out;
    uint64_t  ticnto_last_tick;
} S5pc1xxWDTState;


/* Timer step */
static void s5pc1xx_wdt_tick(void *opaque)
{
    S5pc1xxWDTState *s = (S5pc1xxWDTState *)opaque;
    uint64_t next_wdt_time;

    if (s->regs[WTCON] & WDT_EN) {
        if (s->regs[WTCON] & INT_EN)
            qemu_irq_raise(s->irq);

        if (s->regs[WTCON] & RESET_EN)
            qemu_system_reset();
            /* ?? or qemu_system_reset_request() */

        s->ticnto_last_tick = qemu_get_clock(vm_clock);
        next_wdt_time = s->ticnto_last_tick +
            muldiv64(s->regs[WTDAT], get_ticks_per_sec(), s->freq_out);
        qemu_mod_timer(s->wdt_timer, next_wdt_time);
    } else {
        s->ticnto_last_tick = 0;
        qemu_del_timer(s->wdt_timer);
    }
}

/* Perform timer step, update frequency and compute next time for update */
static void s5pc1xx_wdt_update(S5pc1xxWDTState *s)
{
    short div_fac;
    short prescaler;
    S5pc1xxClk clk;

    clk = s5pc1xx_findclk("pclk_66");
    div_fac = 16 << (s->regs[WTCON] >> CLK_SEL_SHIFT & 0x3);
    prescaler = s->regs[WTCON] >> PRESCALER_SHIFT & 0xff;

    s->freq_out = s5pc1xx_clk_getrate(clk) / (prescaler + 1) / div_fac;

    if (!s->freq_out)
        hw_error("s5pc1xx_wdt: timer update input frequency is zero\n");
}

/* WDT read */
static uint32_t s5pc1xx_wdt_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxWDTState *s = (S5pc1xxWDTState *)opaque;

    /* check if offset is correct, WTCLRINT is not for read */
    if (offset <= WTCNT) {
        if (offset == WTCNT) {
            if (s->freq_out && s->ticnto_last_tick && s->regs[WTDAT]) {
                s->regs[WTCNT] = s->regs[WTDAT] -
                    muldiv64(qemu_get_clock(vm_clock) - s->ticnto_last_tick,
                             s->freq_out, get_ticks_per_sec()) %
                        s->regs[WTDAT];
            } else
                s->regs[WTCNT] = 0;
        }
        return s->regs[offset];
    } else {
        hw_error("s5pc1xx_wdt: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

/* WDT write */
static void s5pc1xx_wdt_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5pc1xxWDTState *s = (S5pc1xxWDTState *)opaque;

    switch (offset) {
    case WTCON:
        if ((value & WDT_EN) > (s->regs[WTCON] & WDT_EN)) {
            s->regs[WTCON] = value;
            s5pc1xx_wdt_update(s);
            s5pc1xx_wdt_tick(s);
        }
        s->regs[WTCON] = value;
        s5pc1xx_wdt_update(s);
        break;
    case WTDAT:
    case WTCNT:
        s->regs[offset] = value;
        break;
    case WTCLRINT:
        qemu_irq_lower(s->irq);
        break;
    default:
        hw_error("s5pc1xx_wdt: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_wdt_readfn[] = {
    s5pc1xx_wdt_read,
    s5pc1xx_wdt_read,
    s5pc1xx_wdt_read
};

static CPUWriteMemoryFunc * const s5pc1xx_wdt_writefn[] = {
    s5pc1xx_wdt_write,
    s5pc1xx_wdt_write,
    s5pc1xx_wdt_write
};

/* WDT init */
static int s5pc1xx_wdt_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxWDTState *s = FROM_SYSBUS(S5pc1xxWDTState, dev);

    sysbus_init_irq(dev, &s->irq);
    iomemtype =
        cpu_register_io_memory(s5pc1xx_wdt_readfn, s5pc1xx_wdt_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_WDT_REG_MEM_SIZE, iomemtype);

    s->wdt_timer = qemu_new_timer(vm_clock, s5pc1xx_wdt_tick, s);

    s->regs[WTDAT] = 0x00008000;
    s->regs[WTCNT] = 0x00008000;
    /* initially WDT is stopped */
    s5pc1xx_wdt_write(s, WTCON, 0x00008001);

    return 0;
}

static void s5pc1xx_wdt_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,wdt", sizeof(S5pc1xxWDTState),
                        s5pc1xx_wdt_init);
}

device_init(s5pc1xx_wdt_register_devices)
