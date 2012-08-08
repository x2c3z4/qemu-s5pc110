/*
 * PWM Timer for Samsung S5PC110-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "s5pc1xx.h"
#include "sysbus.h"
#include "s5pc1xx_gpio_regs.h"


#define S5C_TCFG0                   (0x00)  /* R/W Timer Configuration
                                             * Register 0 that configures two
                                             * 8-bit Prescaler and DeadZone
                                             * Length */
#define S5C_TCFG1                   (0x04)  /* R/W Timer Configuration
                                             * Register 1 that controls
                                             * 5 MUX Select Bit */
#define S5C_TCON                    (0x08)  /* R/W Timer Control Register */
#define S5C_TINT_CSTAT              (0x44)  /* R/W Timer Interrupt Control and
                                             * Status Register */
/* TCFG0 */
#define S5C_TCFG0_PRESCALER0_SHIFT  (0)
#define S5C_TCFG0_PRESCALER1_SHIFT  (8)

/* TCFG1 */
#define S5C_TCFG1_DIV_SHIFT(tmr)    (tmr * 0x04)
#define TCLK                        0x5

/* TINT_CSTAT */
#define INT_EN(tmr)                 (1 << tmr)
#define INT_STAT(tmr)               (1 << (tmr+5))

#define S5C_TIMERREG(tmr,reg)       (0x0c + reg + (0x0c * tmr))

/* R/W Timer 0..4 Count Buffer Register */
#define S5C_TCNTB(tmr)              S5C_TIMERREG(tmr, 0x00)

/* R/W Timer 0..4 Compare Buffer Register */
#define S5C_TCMPB(tmr)              S5C_TIMERREG(tmr, 0x04)

/* R Timer 0..4 Count Observation Register */
#define S5C_TCNTO(tmr)              S5C_TIMERREG(tmr, ((tmr == 4) ? 0x04 : 0x08))
#define ALL_STAT                    (INT_STAT(0) | INT_STAT(1) | INT_STAT(2) | \
                                     INT_STAT(3) | INT_STAT(4))

#define S5PC1XX_PWM_REG_MEM_SIZE    0x50

static const uint32_t S5C_TCON_RELOAD[5]    = {(1 << 3), (1 << 11), (1 << 15), (1 << 19), (1 << 22)};
static const uint32_t S5C_TCON_INVERT[5]    = {(1 << 2), (1 << 10), (1 << 14), (1 << 18),    (0)   };
static const uint32_t S5C_TCON_MANUALUPD[5] = {(1 << 1), (1 << 9),  (1 << 13), (1 << 17), (1 << 21)};
static const uint32_t S5C_TCON_START[5]     = {(1 << 0), (1 << 8),  (1 << 12), (1 << 16), (1 << 20)};


struct S5pc1xxPWMState;

typedef struct S5pc1xxPWMTimerState {
    qemu_irq  irq_inst;         /* irq instance */
    uint8_t   tag;              /* control tag and timer number */

    QEMUTimer *pwm_timer;
    uint32_t  freq_out;
    uint64_t  last_pwm_time;

    uint32_t  tcntb;            /* count buffer value */
    uint64_t  tcntb_in_qemu;    /* tcntb measured in 1/(9GHz) units */
    uint32_t  tcnt;             /* count initial value */
    uint64_t  tcnt_in_qemu;     /* tcnt measured in 1/(9GHz) units */
    uint32_t  tcmpb;            /* comparison buffer value */
    uint32_t  tcmp;             /* comparison current value */

    struct S5pc1xxPWMState *regs;
} S5pc1xxPWMTimerState;

typedef struct S5pc1xxPWMState {
    SysBusDevice busdev;
    uint8_t      tag;           /* control tag */
    /* control regs */
    uint32_t     tcfg0;
    uint32_t     tcfg1;
    uint32_t     tcon;
    uint32_t     tint_cstat;
    S5pc1xxPWMTimerState tmr[5]; /* timers settings */
} S5pc1xxPWMState;


/* Convert term to be measured in (1/pwm_timer_freq) units */
static uint32_t s5pc1xx_pwm_convert_term(uint64_t term, uint32_t timer_freq)
{
    return muldiv64(term, timer_freq, get_ticks_per_sec());
}

/* Get buffer values */
static void s5pc1xx_pwm_timer_renew(S5pc1xxPWMTimerState *t)
{
    t->tcmp = t->tcmpb;
    t->tcnt = t->tcntb;
    t->tcnt_in_qemu = t->tcntb_in_qemu;
}

/* Set timer */
static void s5pc1xx_pwm_timer_start(S5pc1xxPWMTimerState *t)
{
    t->last_pwm_time = qemu_get_clock(vm_clock);
    qemu_mod_timer(t->pwm_timer, t->last_pwm_time + t->tcnt_in_qemu);
}

/* Stop timer */
static void s5pc1xx_pwm_timer_stop(S5pc1xxPWMTimerState *t)
{
    qemu_del_timer(t->pwm_timer);
}

/* Events when timer riches zero */
static void s5pc1xx_pwm_timer_expiry(void *opaque)
{
    S5pc1xxPWMTimerState *t = (S5pc1xxPWMTimerState *)opaque;
    uint8_t num = t->tag;

    if (t->tag > 4)
        hw_error("s5pc1xx_pwm_timer_expiry: wrong param (tag = %u)\n",
                 t->tag);

    /* Interrupt raising */
    t->regs->tint_cstat |= INT_STAT(num);
    if (t->regs->tint_cstat & INT_EN(num))
        qemu_irq_raise(t->irq_inst);

    if (t->regs->tcon & S5C_TCON_RELOAD[num]) {
        s5pc1xx_pwm_timer_renew(t);
        s5pc1xx_pwm_timer_start(t);
    }
}

/* Update timer frequency */
static void s5pc1xx_pwm_timer_freq(S5pc1xxPWMTimerState *t)
{
    S5pc1xxClk clk;
    uint8_t divisor;
    uint8_t prescaler = 0;

    if (t->tag > 4)
        hw_error("s5pc1xx_pwm_timer_freq: wrong param (tag = %u)\n",
                 t->tag);

    switch (t->regs->tcfg1 >> S5C_TCFG1_DIV_SHIFT(t->tag) & 0xf) {
        case TCLK:
            clk = s5pc1xx_findclk("sclk_pwm");
            prescaler = 0;
            divisor = 1;
            break;
        default:
            clk = s5pc1xx_findclk("pclk_66");
            switch (t->tag) {
                case 0 ... 1:
                    prescaler =
                        t->regs->tcfg0 >> S5C_TCFG0_PRESCALER0_SHIFT & 0xFF;
                    break;
                case 2 ... 4:
                    prescaler =
                        t->regs->tcfg0 >> S5C_TCFG0_PRESCALER1_SHIFT & 0xFF;
                    break;
            }
        divisor = 1 << (t->regs->tcfg1 >> S5C_TCFG1_DIV_SHIFT(t->tag) & 0xF);
    }
    t->freq_out = s5pc1xx_clk_getrate(clk) / (prescaler + 1) / divisor;
}

/* Read PWM by GPIO */
static uint32_t s5pc1xx_pwm_gpio_read(void *opaque,
                                      int io_index)
{
    S5pc1xxPWMState *s = (S5pc1xxPWMState *)opaque;
    uint8_t i, tout = 0;

    if (s->tag < 255)
        hw_error("s5pc1xx_pwm_gpio_read: wrong param (tag = %u)\n",
                 s->tag);

    for (i = 0; i < 4; i++)
        if (io_index == GPIO_PWM_TOUT(i)) {
            tout = (s->tcon & S5C_TCON_INVERT[i]) ? 0 : 1;

            if (s5pc1xx_pwm_convert_term(qemu_get_clock(vm_clock) - s->tmr[i].last_pwm_time,
                s->tmr[i].freq_out) < s->tmr[i].tcmp)
                tout = (s->tcon & S5C_TCON_INVERT[i]) ? 1 : 0;
        }
    return tout;
}

static GPIOReadMemoryFunc *s5pc1xx_pwm_gpio_readfn   = s5pc1xx_pwm_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_pwm_gpio_writefn = s5pc1xx_empty_gpio_write;    /* a plug */

/* Read PWM by OS */
static uint32_t s5pc1xx_pwm_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxPWMState *s        = (S5pc1xxPWMState *)opaque;
    S5pc1xxPWMTimerState *tmr = s->tmr;
    uint32_t tcnto;
    uint8_t  num;

    if (s->tag < 255)
        hw_error("s5pc1xx_pwm_read: wrong param (tag = %u)\n",
                 s->tag);

    switch (offset) {
        case S5C_TCFG0:
            return s->tcfg0;

        case S5C_TCFG1:
            return s->tcfg1;

        case S5C_TCON:
            return s->tcon;

        case S5C_TINT_CSTAT:
            return s->tint_cstat;

        case S5C_TCNTB(0):
        case S5C_TCNTB(1):
        case S5C_TCNTB(2):
        case S5C_TCNTB(3):
        case S5C_TCNTB(4):
            return tmr[(offset - S5C_TCNTB(0)) / 0x0C].tcntb;

        case S5C_TCMPB(0):
        case S5C_TCMPB(1):
        case S5C_TCMPB(2):
        case S5C_TCMPB(3):
            return tmr[(offset - S5C_TCMPB(0)) / 0x0C].tcmpb;

        case S5C_TCNTO(0):
        case S5C_TCNTO(1):
        case S5C_TCNTO(2):
        case S5C_TCNTO(3):
        case S5C_TCNTO(4):
            num =
                (offset == S5C_TCNTO(4)) ? 4 : ((offset - S5C_TCNTO(0)) / 0x0C);

            if (!(s->tcon & S5C_TCON_START[num]))
                return tmr[num].tcnt;

            if (qemu_get_clock(vm_clock) - tmr[num].last_pwm_time > tmr[num].tcnt_in_qemu)
                return 0;

            tcnto =
                tmr[num].tcnt -
                s5pc1xx_pwm_convert_term(qemu_get_clock(vm_clock) -
                                         tmr[num].last_pwm_time,
                                         tmr[num].freq_out);
            return tcnto;

        default:
            hw_error("s5pc1xx_pwm: bad read offset " TARGET_FMT_plx "\n",
                     offset);
    }
}

/* Write PWM by OS */
static void s5pc1xx_pwm_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5pc1xxPWMState *s        = (S5pc1xxPWMState *)opaque;
    S5pc1xxPWMTimerState *tmr = s->tmr;
    uint8_t num;

    if (s->tag < 255)
        hw_error("s5pc1xx_pwm_write: wrong param (tag = %u)\n",
                 s->tag);

    switch (offset) {
        case S5C_TCFG0:
            s->tcfg0 = value;
            for (num = 0; num < 5; num++)
                s5pc1xx_pwm_timer_freq(&tmr[num]);
            break;

        case S5C_TCFG1:
            s->tcfg1 = value;
            for (num = 0; num < 5; num++)
                s5pc1xx_pwm_timer_freq(&tmr[num]);
            break;

        case S5C_TCON:
            /* start timer if it was stopped */
            for (num = 0; num < 5; num++) {
                if ((value & S5C_TCON_START[num]) >
                    (s->tcon & S5C_TCON_START[num]))
                    s5pc1xx_pwm_timer_start(&tmr[num]);

                if ((value & S5C_TCON_START[num]) <
                    (s->tcon & S5C_TCON_START[num]))
                    s5pc1xx_pwm_timer_stop(&tmr[num]);

                if (value & S5C_TCON_MANUALUPD[num]) {
                    s5pc1xx_pwm_timer_stop(&tmr[num]);
                    s5pc1xx_pwm_timer_renew(&tmr[num]);
                }
            }
            s->tcon = value;
            break;

        case S5C_TINT_CSTAT:
            for (num = 0; num < 5; num++)
                if (value & INT_STAT(num))
                    qemu_irq_lower(tmr[num].irq_inst);
            /* set TINT_CSTAT as value except *_STAT bits */
            s->tint_cstat =
                (s->tint_cstat & ALL_STAT) | (value & ~ALL_STAT);
            /* clear *_STAT bits if they are set in value */
            s->tint_cstat &= ~(value & ALL_STAT);
            break;

        case S5C_TCNTB(0):
        case S5C_TCNTB(1):
        case S5C_TCNTB(2):
        case S5C_TCNTB(3):
        case S5C_TCNTB(4):
            num = (offset - S5C_TCNTB(0)) / 0x0C;
            /* count buffer */
            tmr[num].tcntb = value;
            tmr[num].tcntb_in_qemu = muldiv64(value,
                                              get_ticks_per_sec(),
                                              tmr[num].freq_out);
            break;

        case S5C_TCMPB(0):
        case S5C_TCMPB(1):
        case S5C_TCMPB(2):
        case S5C_TCMPB(3):
            /* comparison buffer */
            tmr[(offset - S5C_TCMPB(0)) / 0x0C].tcmpb = value;
            break;

        default:
            hw_error("s5pc1xx_pwm: bad write offset " TARGET_FMT_plx "\n",
                     offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_pwm_readfn[] = {
   s5pc1xx_pwm_read,
   s5pc1xx_pwm_read,
   s5pc1xx_pwm_read
};

static CPUWriteMemoryFunc * const s5pc1xx_pwm_writefn[] = {
   s5pc1xx_pwm_write,
   s5pc1xx_pwm_write,
   s5pc1xx_pwm_write
};

/* PWM Init */
static int s5pc1xx_pwm_init(SysBusDevice *dev)
{
    int i, iomemtype;
    S5pc1xxPWMState *s        = FROM_SYSBUS(S5pc1xxPWMState, dev);
    S5pc1xxPWMTimerState *tmr = s->tmr;

    s->tag = 255;

    for (i = 0; i < 5; i++) {
        tmr[i].tag      = i;
        tmr[i].regs     = s;
        sysbus_init_irq(dev, &tmr[i].irq_inst);
        tmr[i].pwm_timer = qemu_new_timer(vm_clock, s5pc1xx_pwm_timer_expiry, &tmr[i]);
    }

    s5pc1xx_pwm_write(s, S5C_TCFG0, 0x00000101);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_pwm_readfn, s5pc1xx_pwm_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_PWM_REG_MEM_SIZE, iomemtype);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_PWM, 0, s5pc1xx_pwm_gpio_readfn,
                                    s5pc1xx_pwm_gpio_writefn, NULL, s);
    return 0;
}

static void s5pc1xx_pwm_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,pwm", sizeof(S5pc1xxPWMState),
                        s5pc1xx_pwm_init);
}

device_init(s5pc1xx_pwm_register_devices)
