/*
 * Real Time Clock for Samsung S5C110-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

//#include "s5pc1xx.h"
#include "qemu-common.h"
#include "qemu-timer.h"
#include "sysbus.h"

/* Registers addresses */

#define INT_PEND    0x30    /* R/W Interrupt Pending Register */
#define RTC_CON     0x40    /* R/W RTC Control Register */
#define TIC_CNT     0x44    /* R/W Tick Time Count Register */
#define RTC_ALM     0x50    /* R/W RTC Alarm Control Register */
#define ALM_SEC     0x54    /* R/W Alarm Second Data Register */
#define ALM_MIN     0x58    /* R/W Alarm Minute Data Register */
#define ALM_HOUR    0x5C    /* R/W Alarm Hour Data Register */
#define ALM_DAY     0x60    /* R/W Alarm Day of the month Data Register */
#define ALM_MON     0x64    /* R/W Alarm Month Data Register */
#define ALM_YEAR    0x68    /* R/W Alarm Year Data Register */
#define BCD_SEC     0x70    /* R/W BCD Second Register */
#define BCD_MIN     0x74    /* R/W BCD Minute Register */
#define BCD_HOUR    0x78    /* R/W BCD Hour Register */
#define BCD_DAY     0x7C    /* R/W BCD value for a day of the month (1-31) */
#define BCD_WEEKDAY 0x80    /* R/W BCD value for a day of the week (1-7) */
#define BCD_MON     0x84    /* R/W BCD Month Register */
#define BCD_YEAR    0x88    /* R/W BCD Year Register */
#define CUR_TIC_CNT 0x90    /* R Current Tick Time Counter Register */

/* Bit mapping for INT_PEND register */

#define INT_ALM     0x02    /* Alarm interrupt pending bit */
#define INT_TIC     0x01    /* Time TIC interrupt pending bit */

/* Bit mapping for RTC_CON register */

#define TIC_EN      0x100   /* Tick timer enable */
#define TIC_CK_SEL_SHIFT (4)/* Tick timer sub clock selection */
#define CLK_RST     0x08    /* RTC clock count reset */
#define CNT_SEL     0x04    /* BCD count select */
#define CLK_SEL     0x02    /* BCD clock select */
#define RTC_EN      0x01    /* RTC control enable */

/* Bit mapping for RTC_ALM register */

#define ALM_EN      0x40    /* Alarm global enable */
#define YEAR_EN     0x20    /* Year alarm enable */
#define MON_EN      0x10    /* Month alarm enable */
#define DAY_EN      0x08    /* Day of the month alarm enable */
#define HOUR_EN     0x04    /* Hour alarm enable */
#define MIN_EN      0x02    /* Minute alarm enable */
#define SEC_EN      0x01    /* Second alarm enable */

#define S5PC1XX_RTC_REG_MEM_SIZE 0x94


typedef struct S5pc1xxRTCState {
    SysBusDevice busdev;

    uint32_t  regs[BCD_YEAR + 1];

    /* periodic timer */
    QEMUTimer *periodic_timer;
    uint32_t  freq_out;
    uint64_t  last_tick;
    uint64_t  cur_tic_cnt;
    qemu_irq  tick_irq;

    /* second update */
    QEMUTimer *second_timer;
    struct tm current_tm;
    qemu_irq  alm_irq;

} S5pc1xxRTCState;


static void s5pc1xx_rtc_periodic_tick(void *opaque);
static void s5pc1xx_rtc_second_update(void *opaque);

static void s5pc1xx_rtc_get_date(S5pc1xxRTCState *s)
{
    struct tm *tm = &(s->current_tm);

    tm->tm_sec = from_bcd(s->regs[BCD_SEC] & 0x7F);
    tm->tm_min = from_bcd(s->regs[BCD_MIN] & 0x7F);
    tm->tm_hour = from_bcd(s->regs[BCD_HOUR] & 0x3F);
    /* TODO: check if wday value in qemu is between 1 and 7 */
    tm->tm_wday = from_bcd(s->regs[BCD_WEEKDAY] & 0x07);
    tm->tm_mday = from_bcd(s->regs[BCD_DAY] & 0x3F);
    /* month value in qemu is between 0 and 11 */
    tm->tm_mon = from_bcd(s->regs[BCD_MON] & 0x1F) - 1;
    /* year value is with base_year = 2000 (not 1900 as in qemu) */
    tm->tm_year = 100 + from_bcd(s->regs[BCD_YEAR] & 0xFF) +
        from_bcd((s->regs[BCD_YEAR] >> 8) & 0xF) * 100;
}

static void s5pc1xx_rtc_set_date(S5pc1xxRTCState *s, const struct tm *tm)
{
    s->current_tm = *tm;

    s->regs[BCD_SEC] = to_bcd(tm->tm_sec);
    s->regs[BCD_MIN] = to_bcd(tm->tm_min);
    s->regs[BCD_HOUR] = to_bcd(tm->tm_hour);
    /* TODO: check if wday value in qemu is between 1 and 7 */
    s->regs[BCD_WEEKDAY] = to_bcd(tm->tm_wday);
    s->regs[BCD_DAY] = to_bcd(tm->tm_mday);
    /* month value in qemu is between 0 and 11 */
    s->regs[BCD_MON] = to_bcd(tm->tm_mon + 1);
    /* year value is with base_year = 2000 (not 1900 as in qemu) */
    s->regs[BCD_YEAR] = to_bcd((tm->tm_year - 100) % 100) |
        (to_bcd((tm->tm_year - 100) % 1000 / 100) << 8);
}

/* set default values for all fields */
static void s5pc1xx_rtc_reset(S5pc1xxRTCState *s)
{
    short i;

    /* stop tick timer */
    s->regs[RTC_CON] &= ~TIC_EN;
    s5pc1xx_rtc_periodic_tick(s);

    /* stop second timer */
    s->regs[RTC_CON] &= ~RTC_EN;
    s5pc1xx_rtc_second_update(s);

    for (i = 0x30; i < 0x90; i += 0x4)
        s->regs[i] = 0;

    s->last_tick = 0;
    s->freq_out  = 0;
}

/* Setup next timer tick */
static void s5pc1xx_rtc_set_timer(S5pc1xxRTCState *s)
{
    uint64_t next_periodic_time, last = qemu_get_clock(vm_clock);
    s->last_tick = last;
    next_periodic_time = last +
        muldiv64(s->regs[TIC_CNT], get_ticks_per_sec(), s->freq_out);
    qemu_mod_timer(s->periodic_timer, next_periodic_time);
}

/* Send periodic interrupt */
static void s5pc1xx_rtc_periodic_tick(void *opaque)
{
    S5pc1xxRTCState *s = (S5pc1xxRTCState *)opaque;

    /* tick actually happens not every CUR_TIC_CNT but rather at irq time;
     * if current ticnto value is needed it is calculated in s5pc1xx_rtc_read */
    if (s->regs[RTC_CON] & TIC_EN) {
        qemu_irq_raise(s->tick_irq);
        s->regs[INT_PEND] |= INT_TIC;
        s5pc1xx_rtc_set_timer(s);
    } else {
        s->last_tick = 0;
        qemu_del_timer(s->periodic_timer);
    }
}

/* Update tick timer frequency */
static void s5pc1xx_rtc_periodic_update(S5pc1xxRTCState *s)
{
    short freq_code;

    /* get four binary digits to determine the frequency */
    freq_code = (s->regs[RTC_CON] >> TIC_CK_SEL_SHIFT) & 0xF;

    /* tick timer frequency */
    s->freq_out = 32768 / (1 << freq_code);
}

/* Month is between 0 and 11 */
static int get_days_in_month(int month, int year)
{
    short d;
    static const int days_tab[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if ((unsigned)month >= 12)
        return 31;
    d = days_tab[month];
    if (month == 1) {
        if ((year % 4) == 0)
            d++;
    }
    return d;
}

/* Update 'tm' to the next second */
static void s5pc1xx_rtc_next_second(struct tm *tm)
{
    int days_in_month;

    tm->tm_sec++;
    if ((unsigned)tm->tm_sec >= 60) {
        tm->tm_sec = 0;
        tm->tm_min++;
        if ((unsigned)tm->tm_min >= 60) {
            tm->tm_min = 0;
            tm->tm_hour++;
            if ((unsigned)tm->tm_hour >= 24) {
                tm->tm_hour = 0;
                /* next day */
                tm->tm_wday++;
                /* TODO: check if wday value in qemu is between 1 and 7 */
                if ((unsigned)tm->tm_wday >= 8)
                    tm->tm_wday = 1;
                days_in_month = get_days_in_month(tm->tm_mon, tm->tm_year);
                tm->tm_mday++;
                if (tm->tm_mday < 1) {
                    tm->tm_mday = 1;
                } else if (tm->tm_mday > days_in_month) {
                    tm->tm_mday = 1;
                    tm->tm_mon++;
                    if (tm->tm_mon >= 12) {
                        tm->tm_mon = 0;
                        tm->tm_year++;
                    }
                }
            }
        }
    }
}

/* Working with up-to-date time and check alarm */
static void s5pc1xx_rtc_second_update(void *opaque)
{
    S5pc1xxRTCState *s = (S5pc1xxRTCState *)opaque;
    uint64_t next_second_time;

    if (s->regs[RTC_CON] & RTC_EN) {
        /* check if the alarm is generally enabled */
        /* then check if an alarm of at least one kind is enabled */
        if (s->regs[RTC_ALM] & ALM_EN &&
            (s->regs[RTC_ALM] & SEC_EN ||
             s->regs[RTC_ALM] & MIN_EN ||
             s->regs[RTC_ALM] & HOUR_EN ||
             s->regs[RTC_ALM] & DAY_EN ||
             s->regs[RTC_ALM] & MON_EN ||
             s->regs[RTC_ALM] & YEAR_EN)) {

            /* check alarm values together with corresponding permissive bits */
            if (((s->regs[ALM_SEC] & 0x7F) ==
                    to_bcd(s->current_tm.tm_sec) ||
                 !(s->regs[RTC_ALM] & SEC_EN)) &&

                ((s->regs[ALM_MIN] & 0x7F) ==
                    to_bcd(s->current_tm.tm_min) ||
                 !(s->regs[RTC_ALM] & MIN_EN)) &&

                ((s->regs[ALM_HOUR] & 0x3F) ==
                    to_bcd(s->current_tm.tm_hour) ||
                 !(s->regs[RTC_ALM] & HOUR_EN)) &&

                ((s->regs[ALM_DAY] & 0x3F) ==
                    to_bcd(s->current_tm.tm_mday) ||
                 !(s->regs[RTC_ALM] & DAY_EN)) &&

                ((s->regs[ALM_MON]  & 0x1F) ==
                    to_bcd(s->current_tm.tm_mon) ||
                 !(s->regs[RTC_ALM] & MON_EN)) &&

                (((s->regs[ALM_YEAR] & 0xFF) ==
                    to_bcd((s->current_tm.tm_year - 100) % 100) &&
                  ((s->regs[ALM_YEAR] >> 8) & 0xF) ==
                    to_bcd((s->current_tm.tm_year - 100) % 1000 / 100)) ||
                 !(s->regs[RTC_ALM] & YEAR_EN))) {

                qemu_irq_raise(s->alm_irq);
                s->regs[INT_PEND] |= INT_ALM;
            }
        }

        s5pc1xx_rtc_next_second(&(s->current_tm));
        s5pc1xx_rtc_set_date(s, &(s->current_tm));

        next_second_time = qemu_get_clock(vm_clock) + get_ticks_per_sec();
        qemu_mod_timer(s->second_timer, next_second_time);
    } else {
        qemu_del_timer(s->second_timer);
    }
}

/* Host RTC mappings */
static void s5pc1xx_rtc_get_date_from_host(S5pc1xxRTCState *s)
{
    struct tm tm;

    /* set the RTC date */
    qemu_get_timedate(&tm, 0);
    s5pc1xx_rtc_set_date(s, &tm);
}

static uint32_t s5pc1xx_rtc_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxRTCState *s = (S5pc1xxRTCState *)opaque;

    switch (offset) {
        case INT_PEND:
        case RTC_CON:
        case TIC_CNT:
        case RTC_ALM:
        case ALM_SEC:
        case ALM_MIN:
        case ALM_HOUR:
        case ALM_DAY:
        case ALM_MON:
        case ALM_YEAR:
        case BCD_SEC:
        case BCD_MIN:
        case BCD_HOUR:
        case BCD_WEEKDAY:
        case BCD_DAY:
        case BCD_MON:
        case BCD_YEAR:
            return s->regs[offset];
        case CUR_TIC_CNT:
            if (s->freq_out && s->last_tick && s->regs[TIC_CNT]) {
                s->cur_tic_cnt = s->regs[TIC_CNT] -
                    muldiv64(qemu_get_clock(vm_clock) - s->last_tick,
                             s->freq_out, get_ticks_per_sec()) %
                        s->regs[TIC_CNT];
            } else {
                s->cur_tic_cnt = 0;
            }

            return s->cur_tic_cnt;
        default:
            hw_error("s5pc1xx_rtc: bad read offset " TARGET_FMT_plx "\n",
                     offset);
    }
}

static void s5pc1xx_rtc_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5pc1xxRTCState *s = (S5pc1xxRTCState *)opaque;

    switch (offset) {
        case INT_PEND:
            /* lower interrupts if any */
            if (value & INT_TIC)
                qemu_irq_lower(s->tick_irq);
            if (value & INT_ALM)
                qemu_irq_lower(s->alm_irq);

            /* clear INT_* bits if they are set in value */
            s->regs[INT_PEND] &= ~(value & (INT_TIC | INT_ALM));
            break;
        case RTC_CON:
            /* reset tick counter */
            if (value & CLK_RST) {
                s5pc1xx_rtc_reset(s);
                value &= ~CLK_RST;
            }
            /* start second timer */
            if ((value & RTC_EN) > (s->regs[RTC_CON] & RTC_EN)) {
                s->regs[RTC_CON] |= RTC_EN;
                s5pc1xx_rtc_get_date_from_host(s);
                s5pc1xx_rtc_second_update(s);
            }
            /* start tick timer */
            if ((value & TIC_EN) > (s->regs[RTC_CON] & TIC_EN)) {
                s->regs[RTC_CON] = value;
                s5pc1xx_rtc_periodic_update(s);
                s5pc1xx_rtc_set_timer(s);
                break;
            } else if ((value & TIC_EN) < (s->regs[RTC_CON] & TIC_EN)) {
                qemu_del_timer(s->periodic_timer);
            }
            s->regs[RTC_CON] = value;
            s5pc1xx_rtc_periodic_update(s);
            break;
        case TIC_CNT:
        case RTC_ALM:
        case ALM_SEC:
        case ALM_MIN:
        case ALM_HOUR:
        case ALM_DAY:
        case ALM_MON:
        case ALM_YEAR:
            s->regs[offset] = value;
            break;
        case BCD_SEC:
        case BCD_MIN:
        case BCD_HOUR:
        case BCD_WEEKDAY:
        case BCD_DAY:
        case BCD_MON:
        case BCD_YEAR:
            s->regs[offset] = value;
            /* if in disabled mode, do not update the time */
            if (s->regs[RTC_CON] & RTC_EN)
                s5pc1xx_rtc_get_date(s);
            break;
        default:
            hw_error("s5pc1xx_rtc: bad write offset " TARGET_FMT_plx "\n",
                     offset);
    }
}

/* Memory mapped interface */
static CPUReadMemoryFunc * const s5pc1xx_rtc_mm_read[] = {
    s5pc1xx_rtc_read,
    s5pc1xx_rtc_read,
    s5pc1xx_rtc_read
};

static CPUWriteMemoryFunc * const s5pc1xx_rtc_mm_write[] = {
    s5pc1xx_rtc_write,
    s5pc1xx_rtc_write,
    s5pc1xx_rtc_write
};

/* initialize and start timers */
static int s5pc1xx_rtc_init(SysBusDevice *dev)
{
    int iomemory;
    S5pc1xxRTCState *s = FROM_SYSBUS(S5pc1xxRTCState, dev);

    sysbus_init_irq(dev, &s->alm_irq);
    sysbus_init_irq(dev, &s->tick_irq);

    s->periodic_timer =
        qemu_new_timer(vm_clock, s5pc1xx_rtc_periodic_tick, s);
    s->second_timer =
        qemu_new_timer(vm_clock, s5pc1xx_rtc_second_update, s);

    iomemory =
        cpu_register_io_memory(s5pc1xx_rtc_mm_read, s5pc1xx_rtc_mm_write, s);
    sysbus_init_mmio(dev, S5PC1XX_RTC_REG_MEM_SIZE, iomemory);

    s5pc1xx_rtc_reset(s);

    return 0;
}

static void s5pc1xx_rtc_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,rtc", sizeof(S5pc1xxRTCState),
                        s5pc1xx_rtc_init);
}

device_init(s5pc1xx_rtc_register_devices)
