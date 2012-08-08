/*
 * AK8973 Compass Emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Junsik.Park <okdear.park@samsung.com>
 */

#include "i2c-addressable.h"


//#define DEBUG

#define AK8973_ST               0xC0
#define AK8973_TMPS             0xC1
#define AK8973_H1X              0xC2
#define AK8973_H1Y              0xC3
#define AK8973_H1Z              0xC4

#define AK8973_MS1              0xE0
#define AK8973_HXDA             0xE1
#define AK8973_HYDA             0xE2
#define AK8973_HZDA             0xE3
#define AK8973_HXGA             0xE4
#define AK8973_HYGA             0xE5
#define AK8973_HZGA             0xE6

#define AK8973_ETS              0x62
#define AK8973_EVIR             0x63
#define AK8973_EIHE             0x64
#define AK8973_ETST             0x65
#define AK8973_EHXGA            0x66
#define AK8973_EHYGA            0x67
#define AK8973_EHZGA            0x68
#define AK8973_WRAL1            0x60


typedef struct AK8973State {
    I2CAddressableState i2c_addressable;

    uint8_t status;
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t ms1;
    uint8_t dac[3];
    uint8_t gain[3];
    qemu_irq irq;
} AK8973State;


static void ak8973_reset(struct AK8973State *s)
{
    s->status  = 0;
    /* Random coordinates */
    s->x       = 10;
    s->y       = 20;
    s->z       = 30;

    /* EEPROM data-write disable / Powerdown mode */
    s->ms1     = 0x3;

    /* TODO: get the defaults */
    s->dac[0]  = 0;
    s->dac[1]  = 0;
    s->dac[2]  = 0;
    s->gain[0] = 0;
    s->gain[1] = 0;
    s->gain[2] = 0;

    return;
}

static uint8_t ak8973_read(void *opaque, uint32_t address, uint8_t offset)
{
    struct AK8973State *s = (struct AK8973State *)opaque;
    uint32_t ret = 0, index = address + offset;

    switch (index) {
    case AK8973_ST:
        ret = s->status;
        break;
    case AK8973_TMPS:
        /* IRQ is lowered when any of data registers are read */
        qemu_irq_lower(s->irq);
        s->status &= ~1;
        /* Always 20 degree Celsius */
        ret = 0x90;
        break;
    case AK8973_H1X:
        qemu_irq_lower(s->irq);
        s->status &= ~1;
        ret = s->x;
        break;
    case AK8973_H1Y:
        qemu_irq_lower(s->irq);
        s->status &= ~1;
        ret = s->y;
        break;
    case AK8973_H1Z:
        qemu_irq_lower(s->irq);
        s->status &= ~1;
        ret = s->z;
        break;
    case AK8973_MS1:
        ret = s->ms1;
        break;
    case AK8973_HXDA:
    case AK8973_HYDA:
    case AK8973_HZDA:
        ret = s->dac[index - AK8973_HXDA];
        break;
    case AK8973_HXGA:
    case AK8973_HYGA:
    case AK8973_HZGA:
        ret = s->gain[index - AK8973_HXGA];
        break;
    case AK8973_ETS:
    case AK8973_EVIR:
    case AK8973_EIHE:
    case AK8973_ETST:
    case AK8973_EHXGA:
    case AK8973_EHYGA:
    case AK8973_EHZGA:
    case AK8973_WRAL1:
        if ((s->ms1 & 3) == 2 && (s->ms1 & 0xF8) != 0xA8) {
            /* TODO: implement EEPROM reading */
            ret = 0;
        } else {
            ret = 0;
        }
        break;
    default:
        hw_error("ak8973: bad read offset 0x%x\n", index);
    }

#ifdef DEBUG
    printf("ak8973_read IDX = 0x%x, Data = 0x%x\n", index, ret);
#endif

    return ret;
}

static void ak8973_write(void *opaque, uint32_t address, uint8_t offset,
                         uint8_t val)
{
    struct AK8973State *s = (struct AK8973State *)opaque;
    uint32_t index = address + offset;

#ifdef DEBUG
    printf("ak8973_write IDX = 0x%x, Data = 0x%x\n", index, val);
#endif

    switch (index) {
    case AK8973_MS1:
        if ((val & 3) == 2) {
            /* EEPROM access mode */
            /* TODO: implement this mode */
            s->ms1 = val;
        } else {
            /* All other mode setting finishes in power-down mode */
            s->ms1 |= 3;
        }
        if ((val & 3) == 0) {
            /* Measurement mode */
            qemu_irq_raise(s->irq);
            s->status |= 1;
            /* TODO: change measurement registers accordingly */
        }
        break;
    case AK8973_HXDA:
    case AK8973_HYDA:
    case AK8973_HZDA:
        /* TODO: implement DAC changes influence on reported data */
        s->dac[index - AK8973_HXDA] = val;
        break;
    case AK8973_HXGA:
    case AK8973_HYGA:
    case AK8973_HZGA:
        /* TODO: implement gain changes influence on reported data */
        s->gain[index - AK8973_HXGA] = val;
        break;
    case AK8973_ETS:
    case AK8973_EVIR:
    case AK8973_EIHE:
    case AK8973_ETST:
    case AK8973_EHXGA:
    case AK8973_EHYGA:
    case AK8973_EHZGA:
    case AK8973_WRAL1:
        if ((s->ms1 & 3) == 2 && (s->ms1 & 0xF8) == 0xA8) {
            /* TODO: implement EEPROM writing */
        }
        break;
    default:
        hw_error("ak8973: bad write offset 0x%x\n", index);
    }
}

static int ak8973_init(I2CAddressableState *s)
{
    AK8973State *t = FROM_I2CADDR_SLAVE(AK8973State, s);

    /* Set irq address */
    qdev_init_gpio_out(&s->i2c.qdev, &t->irq, 1);

    ak8973_reset(t);

    return 0;
}

static I2CAddressableDeviceInfo ak8973_info = {
    .i2c.qdev.name = "ak8973",
    .i2c.qdev.size = sizeof(AK8973State),
    .init = ak8973_init,
    .read = ak8973_read,
    .write = ak8973_write,
    .size = 1,
    .rev = 0
};

static void ak8973_register_devices(void)
{
    i2c_addressable_register_device(&ak8973_info);
}

device_init(ak8973_register_devices)
