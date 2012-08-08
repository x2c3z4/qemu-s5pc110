/*
 * I2C bus through GPIO pins
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 */

#include "i2c.h"
#include "sysbus.h"
#include "s5pc1xx.h"
#include "s5pc1xx_gpio_regs.h"
#include "bitbang_i2c.h"


//#define DEBUG
#define SDA_PIN_NUM GPIO_IDX_I2C_SDA


typedef struct S5pc1xxI2CGPIOState {
    SysBusDevice busdev;

    bitbang_i2c_interface *bitbang;
    i2c_bus *bus;

    int sda;
    int scl;
    uint32_t instance;
} S5pc1xxI2CGPIOState;


static void s5pc1xx_i2c_gpio_reset(S5pc1xxI2CGPIOState *s)
{
    s->sda = 1;
    s->scl = 1;
}

static void s5pc1xx_i2c_bitbang_set_conf(void *opaque, int io_index,
                                         uint32_t conf)
{
    S5pc1xxI2CGPIOState *s = (S5pc1xxI2CGPIOState *)opaque;

#ifdef DEBUG
    fprintf(stderr, "QEMU BITBANG I2C set configuration: io_index = %s, "
                    "conf = 0x%02X\n",
            io_index == SDA_PIN_NUM ? "sda" : "scl", conf == GIPIO_CONF_INPUT);
#endif

    if (io_index == SDA_PIN_NUM) {
        s->sda = bitbang_i2c_set(s->bitbang, BITBANG_I2C_SDA, conf == GIPIO_CONF_INPUT);
    } else {
        s->sda = bitbang_i2c_set(s->bitbang, BITBANG_I2C_SCL, conf == GIPIO_CONF_INPUT);
        s->scl = (conf == GIPIO_CONF_INPUT);
    }
}

static uint32_t s5pc1xx_i2c_bitbang_read(void *opaque, int io_index)
{
    S5pc1xxI2CGPIOState *s = (S5pc1xxI2CGPIOState *)opaque;

    uint32_t ret = io_index == SDA_PIN_NUM ? s->sda : s->scl;

#ifdef DEBUG
    fprintf(stderr, "QEMU BITBANG I2C read: io_index = %s, value = %d\n",
            io_index == SDA_PIN_NUM ? "sda" : "scl", ret);
#endif

    return ret;
}

static void s5pc1xx_i2c_bitbang_write(void *opaque, int io_index, uint32_t value)
{
#ifdef DEBUG
    fprintf(stderr, "QEMU BITBANG I2C write: io_index = %s, value = %u\n",
            io_index == SDA_PIN_NUM ? "sda" : "scl", value);
#endif
}

DeviceState *s5pc1xx_i2c_gpio_init(int instance)
{
    DeviceState *dev = qdev_create(NULL, "s5pc1xx,i2c,gpio");
    qdev_prop_set_uint32(dev, "instance", instance);
    qdev_init_nofail(dev);
    return dev;

}

/* I2C init */
static int s5pc1xx_i2c_gpio_init1(SysBusDevice *dev)
{
    S5pc1xxI2CGPIOState *s = FROM_SYSBUS(S5pc1xxI2CGPIOState, dev);

    s->bus = i2c_init_bus(&dev->qdev, "i2c");
    s->bitbang = bitbang_i2c_init(s->bus);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_I2C, s->instance,
                                    s5pc1xx_i2c_bitbang_read,
                                    s5pc1xx_i2c_bitbang_write,
                                    s5pc1xx_i2c_bitbang_set_conf, s);

    s5pc1xx_i2c_gpio_reset(s);

    return 0;
}

static SysBusDeviceInfo s5pc1xx_i2c_gpio_info = {
    .init = s5pc1xx_i2c_gpio_init1,
    .qdev.name  = "s5pc1xx,i2c,gpio",
    .qdev.size  = sizeof(S5pc1xxI2CGPIOState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("instance",   S5pc1xxI2CGPIOState, instance, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5pc1xx_i2c_gpio_register(void)
{
    sysbus_register_withprop(&s5pc1xx_i2c_gpio_info);
}

device_init(s5pc1xx_i2c_gpio_register)
