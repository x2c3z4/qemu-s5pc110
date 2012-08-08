/*
 * S5PC1XX ADC & TOUCH SCREEN INTERFACE
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 */

#include "sysbus.h"
#include "console.h"
#include "s5pc1xx.h"


#define QEMUMAXX 0x7FFF
#define QEMUMAXY 0x7FFF

#define WAIT_FOR_INT 3

#define S5PC1XX_TSADC_REG_MEM_SIZE 0x24


typedef union {
    /* raw register data */
    uint32_t v;

    /* register bits */
    struct tsadccon_bits {
        unsigned enable_start : 1;
        unsigned read_start   : 1;
        unsigned standby      : 1;
        unsigned reserved3_5  : 3;
        unsigned prscvl       : 8;
        unsigned prscen       : 1;
        unsigned ecflg        : 1; /* (Read only) */
        unsigned res          : 1;
        unsigned tssel        : 1;
    } b;
} tsadccon_s;

typedef union {
   /* raw register data */
   uint32_t v;

   /* register bits */
   struct tsdat_bits {
       /*XPDATA or YPDATA*/
       unsigned pdata        : 12;
       unsigned xy_pst_val   : 2;
       unsigned auto_pst_val : 1;
       unsigned updown       : 1;
   } b;
} tsdat_s;

typedef union {
    /* raw register data */
    uint32_t v;

    /* register bits */
    struct tscon_bits {
        unsigned xy_pst   : 2;
        unsigned auto_pst : 1;
        unsigned pull_up  : 1;
        unsigned xp_sen   : 1;
        unsigned xm_sen   : 1;
        unsigned yp_sen   : 1;
        unsigned ym_sen   : 1;
        unsigned ud_sen   : 1;
    } b;
} tscon_s;

typedef union {
    /* raw register data */
    uint32_t v;

    /* register bits */
    struct tspenstat_bits {
        unsigned tsc_dn   : 1;
        unsigned tsc_up   : 1;
    } b;
} tspenstat_s;

typedef struct S5pc1xxTSADCState {
    SysBusDevice busdev;

    /* R/W Specifies the TSn - ADC Control Register  */
    tsadccon_s tsadccon;

    /* R/W Specifies the TSn - Touch Screen Control Register */
    tscon_s tscon;

    /* R/W Specifies the TSn - ADC Start or Interval Delay Register */
    uint32_t tsdly;

    /* R Specifies the TSn - ADC Conversion Data X Register */
    tsdat_s tsdat0;

    /* R Specifies the TSn - ADC Conversion Data Y Register */
    tsdat_s tsdat1;

    /* R/W Specifies the TSn - Penn Up or Down Status Register */
    tspenstat_s tspenstat;

    /* R/W Specifies the Analog input channel selection */
    uint32_t adcmux;

    /* Internal data */

    /* Current pointer coordinates ({0,0} < {x,y} < {QEMUMAXX,QEMUMAXY}) */
    uint32_t x, y;
    /* Boundary reported coordinates */
    uint32_t minx, maxx, miny, maxy;
    /* Is it a 'new' touchscreen version? */
    int new;
    /* Touchscreen resolution, max 12 bit */
    uint32_t resolution;
    /* Current mouse buttons state */
    int pressure;
    /* Currently reported touchscreen touch state */
    int report_pressure;

    /* Interrupts */
    qemu_irq irq_adc;
    qemu_irq irq_pennd;

    /* Timer to report conversion data periodically */
    QEMUTimer *timer;
    unsigned int conversion_time;
} S5pc1xxTSADCState;


/* Current number of S3C Touchscreen controllers */
static int s5pc1xx_tsadc_number = 0;


static void s5pc1xx_tsadc_reset(DeviceState *d)
{
    S5pc1xxTSADCState *s =
        FROM_SYSBUS(S5pc1xxTSADCState, sysbus_from_qdev(d));

    s->tsadccon.v  = 0x00003FC4;
    s->tscon.v     = 0x00000058;
    s->tsdly       = 0x000000FF;
    s->tsdat0.v    = 0x00000000;
    s->tsdat1.v    = 0x00000000;
    s->tspenstat.v = 0x00000000;
    s->adcmux      = 0x00000000;

    s->x = s->y = 0;
    s->pressure = 0;
    s->report_pressure = 0;
    s->conversion_time = 0;
}

static void s5pc1xx_tsadc_conversion_start(S5pc1xxTSADCState *s,
                                           unsigned int time)
{
    /* Do not do anything in standby mode */
    if (!s->tsadccon.b.standby) {
        /* Conversion is going... */
        s->tsadccon.b.ecflg = 0;
        /* ... and will finish in 'time' QEMU vm_clock ticks */
        qemu_mod_timer(s->timer,
                       qemu_get_clock(vm_clock) + time);
    }
}

static void s5pc1xx_tsadc_conversion(void *opaque)
{
    S5pc1xxTSADCState *s = opaque;

    s->tsadccon.b.ecflg = 1;

    /* Generate IRQ_ADC for any mode except 'Waiting for interrupt' */
    if (s->tscon.b.xy_pst != WAIT_FOR_INT) {
        qemu_irq_raise(s->irq_adc);
    } else {
        /* If mouse buttons state changed recently - report it */
        if (s->report_pressure != s->pressure &&
            ((s->pressure == 1 && s->tscon.b.ud_sen == 0) ||
             (s->pressure == 0 && s->tscon.b.ud_sen == 1))) {
            qemu_irq_raise(s->irq_pennd);
            s->tspenstat.b.tsc_dn |= s->pressure;
            s->tspenstat.b.tsc_up |= !s->pressure;
        }
        s->report_pressure = s->pressure;
    }
}

static uint32_t s5pc1xx_tsadc_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxTSADCState *s = (S5pc1xxTSADCState *)opaque;

    switch (offset) {
    case 0x00:
        return s->tsadccon.v;
    case 0x04:
        return s->tscon.v;
    case 0x08:
        return s->tsdly;
    case 0x0C:
        if (s->new) {
            s->tsdat0.b.pdata =
                ((1 << s->resolution) - 1) -
                (s->miny + s->y * (s->maxy - s->miny) / QEMUMAXY);
        } else {
            s->tsdat0.b.pdata = s->minx + s->x * (s->maxx - s->minx) / QEMUMAXX;
        }
        s->tsdat0.b.updown = s->report_pressure == 0;
        s->tsdat0.b.auto_pst_val = s->tscon.b.auto_pst;
        s->tsdat0.b.xy_pst_val = s->tscon.b.xy_pst;
        if (s->tsadccon.b.read_start)
            s5pc1xx_tsadc_conversion_start(s, s->conversion_time);
        return s->tsdat0.v;
    case 0x10:
        if (s->new) {
            s->tsdat1.b.pdata =
                ((1 << s->resolution) - 1) -
                (s->minx + s->x * (s->maxx - s->minx) / QEMUMAXX);
        } else {
            s->tsdat1.b.pdata = s->miny + s->y * (s->maxy - s->miny) / QEMUMAXY;
        }
        s->tsdat1.b.updown = s->report_pressure == 0;
        s->tsdat1.b.auto_pst_val = s->tscon.b.auto_pst;
        s->tsdat1.b.xy_pst_val = s->tscon.b.xy_pst;
        if (s->tsadccon.b.read_start)
            s5pc1xx_tsadc_conversion_start(s, s->conversion_time);
        return s->tsdat1.v;
    case 0x14:
        return s->tspenstat.v;
    case 0x18:
        return 0x0;
    case 0x1C:
        return s->adcmux;
    case 0x20:
        return 0x0;
    default:
        hw_error("s5pc1xx_tsadc: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static void s5pc1xx_tsadc_write(void *opaque, target_phys_addr_t offset,
                                uint32_t val)
{
    S5pc1xxTSADCState *s = (S5pc1xxTSADCState *)opaque;

    switch (offset) {
    case 0x00:
        s->tsadccon.v = val;
        if (!s->tsadccon.b.read_start && s->tsadccon.b.enable_start)
            s5pc1xx_tsadc_conversion_start(s, s->conversion_time);
        s->tsadccon.b.enable_start = 0;
        /* FIXME: choose the correct clock depending on a register value */
        s->conversion_time =
            muldiv64(s->tsdly & 0xFFFF, get_ticks_per_sec(),
                     s5pc1xx_clk_getrate(s5pc1xx_findclk("pclk_66")) /
                         (s->tsadccon.b.prscen ? s->tsadccon.b.prscvl + 1: 1));
        break;
    case 0x04:
        s->tscon.v = val;
        if (s->report_pressure != s->pressure &&
            s->tscon.b.xy_pst == WAIT_FOR_INT) {
            /* Raise next IRQ for touch-up in one ms */
            s5pc1xx_tsadc_conversion_start(s, get_ticks_per_sec() / 1000);
        }
        break;
    case 0x08:
        s->tsdly = val;
        /* FIXME: choose the correct clock depending on a register value */
        s->conversion_time =
            muldiv64(s->tsdly & 0xFFFF, get_ticks_per_sec(),
                     s5pc1xx_clk_getrate(s5pc1xx_findclk("pclk_66")) /
                         (s->tsadccon.b.prscen ? s->tsadccon.b.prscvl + 1: 1));
        break;
    case 0x14:
        s->tspenstat.v = val;
        break;
    case 0x18:
        qemu_irq_lower(s->irq_adc);
        break;
    case 0x1C:
        s->adcmux = val;
        break;
    case 0x20:
        qemu_irq_lower(s->irq_pennd);
        break;
    default:
        hw_error("s5pc1xx_tsadc: bad write offset " TARGET_FMT_plx "\n",
                 offset);
        break;
    }
}

static void s5pc1xx_touchscreen_event(void *opaque,
                                      int x, int y, int z, int buttons_state)
{
    S5pc1xxTSADCState *s = opaque;

    if (buttons_state) {
        s->x = x;
        s->y = y;
    }

    if (s->pressure == !buttons_state) {
        s->pressure = !!buttons_state;

        /* Report button state change momentarily if it happens in
         * 'Waiting for interrupt' mode */
        if (s->tscon.b.xy_pst == WAIT_FOR_INT && !s->tsadccon.b.standby) {
            s5pc1xx_tsadc_conversion(s);
        }
    }
}

static CPUReadMemoryFunc * const s5pc1xx_tsadc_mm_read[] = {
    s5pc1xx_tsadc_read,
    s5pc1xx_tsadc_read,
    s5pc1xx_tsadc_read
};

static CPUWriteMemoryFunc * const s5pc1xx_tsadc_mm_write[] = {
    s5pc1xx_tsadc_write,
    s5pc1xx_tsadc_write,
    s5pc1xx_tsadc_write
};

DeviceState *s5pc1xx_tsadc_init(target_phys_addr_t base, qemu_irq irq_adc,
                                qemu_irq irq_pennd, int new, int resolution,
                                int minx, int maxx, int miny, int maxy)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, new ? "s5pc1xx,tsadc,new" : "s5pc1xx,tsadc");
    qdev_prop_set_uint32(dev, "resolution", resolution);
    qdev_prop_set_uint32(dev, "minx", minx);
    qdev_prop_set_uint32(dev, "miny", miny);
    qdev_prop_set_uint32(dev, "maxx", maxx);
    qdev_prop_set_uint32(dev, "maxy", maxy);

    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_connect_irq(s, 0, irq_adc);
    sysbus_connect_irq(s, 1, irq_pennd);
    sysbus_mmio_map(s, 0, base);

    return dev;
}

static int s5pc1xx_tsadc_init1(SysBusDevice *dev)
{
    S5pc1xxTSADCState *s = FROM_SYSBUS(S5pc1xxTSADCState, dev);
    int iomemtype;
    char name[30];

    s5pc1xx_tsadc_number++;
    snprintf(name, 30, "QEMU s5pc1xx Touchscreen %d", s5pc1xx_tsadc_number);
    iomemtype = cpu_register_io_memory(s5pc1xx_tsadc_mm_read,
                                       s5pc1xx_tsadc_mm_write, s);
    sysbus_init_mmio(dev, S5PC1XX_TSADC_REG_MEM_SIZE, iomemtype);

    s5pc1xx_tsadc_reset(&s->busdev.qdev);

    sysbus_init_irq(dev, &s->irq_adc);
    sysbus_init_irq(dev, &s->irq_pennd);
    s->timer = qemu_new_timer(vm_clock, s5pc1xx_tsadc_conversion, s);

    qemu_add_mouse_event_handler(s5pc1xx_touchscreen_event, s, 1, name);

    return 0;
}

static int s5pc1xx_tsadc_new_init1(SysBusDevice *dev)
{
    S5pc1xxTSADCState *s = FROM_SYSBUS(S5pc1xxTSADCState, dev);

    s->new = 1;
    return s5pc1xx_tsadc_init1(dev);
}

static SysBusDeviceInfo s5pc1xx_tsadc_info = {
    .init = s5pc1xx_tsadc_init1,
    .qdev.name  = "s5pc1xx,tsadc",
    .qdev.size  = sizeof(S5pc1xxTSADCState),
    .qdev.reset = s5pc1xx_tsadc_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("resolution", S5pc1xxTSADCState, resolution, 12),
        DEFINE_PROP_UINT32("minx",       S5pc1xxTSADCState, minx,       0),
        DEFINE_PROP_UINT32("miny",       S5pc1xxTSADCState, miny,       0),
        DEFINE_PROP_UINT32("maxx",       S5pc1xxTSADCState, maxx,       480),
        DEFINE_PROP_UINT32("maxy",       S5pc1xxTSADCState, maxy,       800),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static SysBusDeviceInfo s5pc1xx_tsadc_new_info = {
    .init = s5pc1xx_tsadc_new_init1,
    .qdev.name  = "s5pc1xx,tsadc,new",
    .qdev.size  = sizeof(S5pc1xxTSADCState),
    .qdev.reset = s5pc1xx_tsadc_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("resolution", S5pc1xxTSADCState, resolution, 12),
        DEFINE_PROP_UINT32("minx",       S5pc1xxTSADCState, minx,       0),
        DEFINE_PROP_UINT32("miny",       S5pc1xxTSADCState, miny,       0),
        DEFINE_PROP_UINT32("maxx",       S5pc1xxTSADCState, maxx,       480),
        DEFINE_PROP_UINT32("maxy",       S5pc1xxTSADCState, maxy,       800),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5pc1xx_tsadc_register_devices(void)
{
    sysbus_register_withprop(&s5pc1xx_tsadc_info);
    sysbus_register_withprop(&s5pc1xx_tsadc_new_info);
}

device_init(s5pc1xx_tsadc_register_devices)
