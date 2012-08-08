/*
 * Interface to support I2C devices with control registers accessed by I2C
 *
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 *                Vladimir Monakhov <vmonakhov@ispras.ru>
 */

#include "i2c-addressable.h"


static int i2c_addressable_rx(i2c_slave *s)
{
    I2CAddressableDeviceInfo *t =
        DO_UPCAST(I2CAddressableDeviceInfo, i2c, s->info);
    I2CAddressableState *dev = FROM_I2C_SLAVE(I2CAddressableState, s);

    /* Get next byte of the value and transfer it */
    if (t->read) {
        return t->read(dev, dev->addr, dev->num++);
    }
    return 0;
}

static int i2c_addressable_tx(i2c_slave *s, uint8_t data)
{
    I2CAddressableDeviceInfo *t =
        DO_UPCAST(I2CAddressableDeviceInfo, i2c, s->info);
    I2CAddressableState *dev = FROM_I2C_SLAVE(I2CAddressableState, s);

    if (dev->num < t->size) {
        /* First fully get control register address byte after byte */
        if (!t->rev) {
            /* Less significant bytes come first */
            dev->addr |= data << (dev->num * 8);
        } else {
            /* More significant bytes come first */
            dev->addr |= data << ((t->size - dev->num - 1) * 8);
        }
    } else {
        /* Then get corresponding data byte after byte */
        if (t->write) {
            t->write(dev, dev->addr, dev->num - t->size, data);
        }
    }

    dev->num++;
    return 1;
}

static void i2c_addressable_event(i2c_slave *s, enum i2c_event event)
{
    I2CAddressableState *dev = FROM_I2C_SLAVE(I2CAddressableState, s);

    switch (event) {
    case I2C_START_SEND:
        dev->addr = 0;
        /* fallthrough */
    case I2C_START_RECV:
        /* Save address from the previous send */
        dev->num = 0;
        break;
    case I2C_FINISH:
    default:
        break;
    }
}

static int i2c_addressable_device_init(i2c_slave *s)
{
    I2CAddressableDeviceInfo *t =
        DO_UPCAST(I2CAddressableDeviceInfo, i2c, s->info);
    I2CAddressableState *dev = FROM_I2C_SLAVE(I2CAddressableState, s);

    return t->init(dev);
}

void i2c_addressable_register_device(I2CAddressableDeviceInfo *info)
{
    assert(info->i2c.qdev.size >= sizeof(I2CAddressableState));
    info->i2c.init  = i2c_addressable_device_init;
    info->i2c.event = i2c_addressable_event;
    info->i2c.recv  = i2c_addressable_rx;
    info->i2c.send  = i2c_addressable_tx;
    i2c_register_slave(&info->i2c);
}
