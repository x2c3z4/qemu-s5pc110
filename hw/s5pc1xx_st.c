/*
 * System Timer for Samsung S5C110-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 */

#include "sysbus.h"
#include "qemu-timer.h"
#include "s5pc1xx.h"


#define TCFG            0x00        /* R/W Configures 8-bit-Prescaler and Clock MUX 0x0000_0000 */
#define TCON            0x04        /* R/W Timer Control Register 0x0000_0000 */
#define TICNTB          0x08        /* R/W Tick Integer Count Buffer Register 0x0000_0000 */
#define TICNTO          0x0C        /* R Tick Integer Count Observation Register 0x0000_0000 */
#define TFCNTB          0x10        /* R/W Tick Fractional Count Buffer Register 0x0000_0000 */
#define ICNTB           0x18        /* R/W Interrupt Count Buffer Register 0x0000_0000 */
#define ICNTO           0x1C        /* R Interrupt Count Observation Register 0x0000_0000 */
#define INT_CSTAT       0x20        /* R/W Interrupt Control and Status Register 0x0000_0000 */

/* TCFG */
#define TICK_SWRST      (1 << 16)   /* SW reset of TICK generation logic */
#define FDIV_SEL        (1 << 15)   /* Fractional divider select */
#define TICKGEN_SEL     (1 << 14)   /* 0 = Integer divider 1 = Fractional divider */
#define TCLKB_MUX       (3 << 12)   /* Selects clock input for TCLKB */
#define DIV_MUX         (7 << 8)    /* Selects Mux input for Timer */
#define PRESCALER       (0xFF << 0) /* Prescaler value for timer 0x00 */

/* TCON */
#define INT_AUTO_RELOAD (1 << 5)    /* 0 = No operation 1 = Interval mode(auto-reload) */
#define INT_MAN_UPD     (1 << 4)    /* 0 = No operation 1 = Update ICNTB and One-shot mode */
#define INT_RUN         (1 << 3)    /* 0 = Stop 1 = Start timer */
#define TIMER_RUN       (1 << 0)    /* 0 = Stop 1 = Start timer */

/* INT_CSTAT */
#define TWIE            (1 << 10)   /* TCON Write Interrupt Enable / 0: Disable, 1: Enable 0x0 */
#define IWIE            (1 << 9)    /* ICNTB write Interrupt Enable / 0: Disable, 1: Enable 0x0 */
#define TFWIE           (1 << 8)    /* TFCNTB write Interrupt Enable / 0: Disable, 1: Enable 0x0 */
#define TIWIE           (1 << 7)    /* TICNTB write Interrupt Enable / 0: Disable, 1: Enable 0x0 */
#define ICNTEIE         (1 << 6)    /* Interrupt counter expired (INTCNT=0) Interrupt Enable */
#define TCON_W_STAT     (1 << 5)    /* TCON Write Interrupt Status Bit */
#define ICNTB_W_STAT    (1 << 4)    /* ICNTB Write Interrupt Status Bit */
#define TFCNTB_W_STAT   (1 << 3)    /* TFCTNB Write Interrupt Status Bit */
#define TICNTB_W_STAT   (1 << 2)    /* TICTNB Write Interrupt Status Bit */
#define INTCNT_EXP_STAT (1 << 1)    /* Interrupt counter expired (INTCNT=0) Interrupt Status Bit */
#define INT_ENABLE      (1 << 0)    /* Enables Interrupt */
#define ALL_STAT        (TCON_W_STAT | ICNTB_W_STAT | TFCNTB_W_STAT | \
                         TICNTB_W_STAT | INTCNT_EXP_STAT)

#define S5PC1XX_ST_REG_MEM_SIZE 0x24


typedef struct {
    SysBusDevice busdev;

    uint32_t tcfg;
    uint32_t tcon;
    uint32_t ticntb;
    uint32_t ticnto;
    uint32_t tfcntb;
    uint32_t icntb;
    int32_t  icnto;     /* has signed type */
    uint32_t int_cstat;

    qemu_irq irq;

    uint8_t  divider;
    uint8_t  prescaler;

    QEMUTimer *st_timer;
    uint32_t freq_out;
    uint64_t tick_interval;
    uint64_t last_tick;
    uint64_t next_planned_tick;
    uint64_t base_time;
} S5pc1xxSTState;


const char *st_clks[] = { "XXTI", "XrtcXTI", "XusbXTI", "pclk_66" };

static void s5pc1xx_st_tick(void *opaque);

/* work with interrupts depending on permissive bits */
static void s5pc1xx_st_irq(S5pc1xxSTState *s, uint32_t enab_mask,
                           uint32_t stat_mask)
{
    /* stop tick timer */
    if ((stat_mask == TICNTB_W_STAT) || (stat_mask == TFCNTB_W_STAT)) {
        s->tcon &= ~TIMER_RUN;
        s5pc1xx_st_tick(s);
    }

    /* reload ICNT after manual update */
    if ((stat_mask == ICNTB_W_STAT) && (s->tcon & INT_MAN_UPD))
        s->icnto = s->icntb;

    /* raise irq */
    if ((s->int_cstat & INT_ENABLE) && (s->int_cstat & enab_mask)) {
        qemu_irq_raise(s->irq);
    }

    s->int_cstat |= stat_mask;
}

static void s5pc1xx_st_set_timer(S5pc1xxSTState *s)
{
    uint64_t last = qemu_get_clock(vm_clock) - s->base_time;
    /* make a tick each tick_interval'th QEMU timer cycle - this way
     * system timer is working consistently (1 second for emulated machine
     * corresponds to 1 second for host); otherwise due to QEMU timer interrupt
     * handling overhead it will slowly drift towards the past */
    s->next_planned_tick = last + (s->tick_interval - last % s->tick_interval);
    qemu_mod_timer(s->st_timer, s->next_planned_tick + s->base_time);
    s->last_tick = last;
}

/* counter step */
static void s5pc1xx_st_tick(void *opaque)
{
    S5pc1xxSTState *s = (S5pc1xxSTState *)opaque;

    /* tick actually happens not every ticnto but rather at icnto update;
     * if current ticnto value is needed it is calculated in s5pc1xx_st_read */
    if (s->tcon & TIMER_RUN) {
        if (s->tcon & INT_RUN) {
            /* reload count */
            if (s->icnto == 0 && s->tcon & INT_AUTO_RELOAD)
                s->icnto = s->icntb;

            s->icnto--;

            /* time for interrupt */
            if (s->icnto <= 0) {
                s->icnto = 0;
                s5pc1xx_st_irq(s, ICNTEIE, INTCNT_EXP_STAT);
            }
        }

        /* schedule next interrupt */
        s5pc1xx_st_set_timer(s);
    } else {
        s->next_planned_tick = 0;
        s->last_tick = 0;
        qemu_del_timer(s->st_timer);
    }
}

/* set default values for all fields */
static void s5pc1xx_st_reset(S5pc1xxSTState *s)
{
    /* TODO: Check if reseting all counters is needed */
    s->tcfg      = 0;
    s->tcon      = 0;
    s->ticntb    = 0;
    s->ticnto    = 0;
    s->tfcntb    = 0;
    s->icntb     = 1;
    s->icnto     = 0;
    s->int_cstat = 0;

    s->last_tick = 0;
    s->next_planned_tick = 0;
    s->freq_out  = 0;
    s->tick_interval = 0;
    s->divider   = 1;
    s->prescaler = 0;
    s->base_time = qemu_get_clock(vm_clock);

    qemu_del_timer(s->st_timer);
}

/* update timer frequency */
static void s5pc1xx_st_update(S5pc1xxSTState *s)
{
    S5pc1xxClk clk;

    s->divider = 1 << ((s->tcfg & DIV_MUX) >> 8);
    s->prescaler = s->tcfg & PRESCALER;

    clk = s5pc1xx_findclk(st_clks[(s->tcfg & TCLKB_MUX) >> 12]);
    s->freq_out = s5pc1xx_clk_getrate(clk) / (s->prescaler + 1) / s->divider;
    s->tick_interval =
        muldiv64(s->ticntb, get_ticks_per_sec(), s->freq_out) +
        (muldiv64(s->tfcntb, get_ticks_per_sec(), s->freq_out) >> 16);
    s->next_planned_tick = 0;

    if (!s->freq_out)
        hw_error("s5pc1xx_st: timer update input frequency is zero\n");
}

/* System Timer read */
static uint32_t s5pc1xx_st_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxSTState *s = (S5pc1xxSTState *)opaque;
    uint64_t cur_clock, tps;

    switch (offset) {
    case TCFG:
        return s->tcfg;
    case TCON:
        return s->tcon;
    case TICNTB:
        return s->ticntb;
    case TICNTO:
        if (s->freq_out && s->last_tick && s->ticntb && s->next_planned_tick) {
            cur_clock = qemu_get_clock(vm_clock) - s->base_time;
            if (cur_clock < s->next_planned_tick) {
                if (s->tick_interval > 0xFFFFFFFF) {
                    /* very large tick interval; muldiv64 can't be used
                     * in this case directly; avoid 64-bit difference by
                     * knowing the fact that next_planned_tick and last_tick
                     * may be represented as "next_planned_tick =
                     * K * tick_interval" and "last_tick = N * tick_interval"
                     * assuming that last_tick happened in time */
                    tps = get_ticks_per_sec();
                    s->ticnto = muldiv64(s->next_planned_tick - cur_clock,
                                         s->ticntb,
                                         (s->next_planned_tick - s->last_tick +
                                          tps / 2) / tps) / tps;
                } else {
                    /* tick interval is 32-bit so both differences
                     * should be 32-bit too */
                    s->ticnto = muldiv64(s->next_planned_tick - cur_clock, s->ticntb,
                                         s->next_planned_tick - s->last_tick);
                }
            } else {
                s->ticnto = 0;
            }
        } else {
            s->ticnto = 0;
        }
        return s->ticnto;
    case TFCNTB:
        return s->tfcntb;
    case ICNTB:
        return s->icntb - 1;
    case ICNTO:
        return s->icnto;
    case INT_CSTAT:
        return s->int_cstat;
    default:
        hw_error("s5pc1xx_st: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

/* System Timer write */
static void s5pc1xx_st_write(void *opaque, target_phys_addr_t offset,
                             uint32_t value)
{
    S5pc1xxSTState *s = (S5pc1xxSTState *)opaque;

    switch (offset) {
    case TCFG:
        if (value & TICK_SWRST) {
            s5pc1xx_st_reset(s);
            break;
        }
        s->tcfg = value;
        s5pc1xx_st_update(s);
        break;

    case TCON:
        s5pc1xx_st_irq(s, TWIE, TCON_W_STAT);

        if ((value & TIMER_RUN) > (s->tcon & TIMER_RUN)) {
            s->base_time = qemu_get_clock(vm_clock);
            s5pc1xx_st_set_timer(s);
        } else if ((value & TIMER_RUN) < (s->tcon & TIMER_RUN)) {
            qemu_del_timer(s->st_timer);
        }
        s->tcon = value;
        break;

    case TICNTB:
        /* in this case s->ticntb is updated after interrupt raise
         * since the timer must be stopped first */
        s5pc1xx_st_irq(s, TIWIE, TICNTB_W_STAT);
        s->ticntb = value;
        s5pc1xx_st_update(s);
        break;

    case TFCNTB:
        s5pc1xx_st_irq(s, TFWIE, TFCNTB_W_STAT);
        s->tfcntb = value;
        s5pc1xx_st_update(s);
        break;

    case ICNTB:
        /* in this case s->icntb is updated before interrupt raise
         * since the value is needed for manual update */
        s->icntb = value + 1;
        s5pc1xx_st_irq(s, IWIE, ICNTB_W_STAT);
        break;

    case INT_CSTAT:
        /* set INT_CSTAT as value except *_STAT bits */
        s->int_cstat = (s->int_cstat & ALL_STAT) | (value & ~ALL_STAT);
        /* clear *_STAT bits if they are set in value */
        s->int_cstat &= ~(value & ALL_STAT);

        /* lower interrupt */
        /* TODO: check if IRQ should be lowered for all cases or
         * only when there are no more stat bits left */
        if (!(s->int_cstat & ALL_STAT))
            qemu_irq_lower(s->irq);
        break;

    default:
        hw_error("s5pc1xx_st: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_st_readfn[] = {
   s5pc1xx_st_read,
   s5pc1xx_st_read,
   s5pc1xx_st_read
};

static CPUWriteMemoryFunc * const s5pc1xx_st_writefn[] = {
   s5pc1xx_st_write,
   s5pc1xx_st_write,
   s5pc1xx_st_write
};

/* System Timer init */
static int s5pc1xx_st_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxSTState *s = FROM_SYSBUS(S5pc1xxSTState, dev);

    s->st_timer = qemu_new_timer(vm_clock, s5pc1xx_st_tick, s);
    sysbus_init_irq(dev, &s->irq);
    iomemtype =
        cpu_register_io_memory(s5pc1xx_st_readfn, s5pc1xx_st_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_ST_REG_MEM_SIZE, iomemtype);

    s5pc1xx_st_reset(s);

    return 0;
}

static void s5pc1xx_st_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,st", sizeof(S5pc1xxSTState), s5pc1xx_st_init);
}

device_init(s5pc1xx_st_register_devices)
