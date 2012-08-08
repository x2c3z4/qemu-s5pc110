#ifndef QEMU_I2C_ADDRESSABLE_H
#define QEMU_I2C_ADDRESSABLE_H

/*
 * Interface to support I2C devices with control registers accessed by I2C
 *
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 *                Vladimir Monakhov <vmonakhov@ispras.ru>
 *
 * Many I2C devices have common logic which closely corresponds to control
 * registers.  These devices are first passed with an ID of the needed command
 * to be performed.  This ID may be considered as control register address.
 * Then the device is either passed with some data or is awaited to return
 * some data.  This may be considered as control register write and read
 * respectively.  This interface provides a generic way to implement such
 * I2C devices emulation by writing two functions for reading and writing of
 * control registers similar to iomem interface.  One difference is that
 * control registers may not have fixed sizes so along with the register
 * address these functions get another parameter - offset inside this register
 * data.
 */

#include "i2c.h"

#define I2CADDR_SLAVE_FROM_QDEV(dev) DO_UPCAST(I2CAddressableState, i2c.qdev, dev)
#define FROM_I2CADDR_SLAVE(type, dev) DO_UPCAST(type, i2c_addressable, dev)

typedef struct I2CAddressableState {
    /* I2C slave for the device */
    i2c_slave i2c;
    /* Address of currently processing data */
    uint32_t addr;
    /* Number of transferred bytes */
    uint8_t num;
} I2CAddressableState;

typedef uint8_t (*i2c_addressable_read)  (void *opaque, uint32_t address,
                                          uint8_t offset);
typedef void    (*i2c_addressable_write) (void *opaque, uint32_t address,
                                          uint8_t offset, uint8_t val);
typedef int     (*i2c_addressable_init)  (I2CAddressableState *i2c);

typedef struct I2CAddressableDeviceInfo {
    I2CSlaveInfo i2c;

    /* Read, write and init handlers */
    i2c_addressable_read  read;
    i2c_addressable_write write;
    i2c_addressable_init  init;

    /* Size of passed addresses in bytes */
    int size;
    /* Byte order: reversed is big-endian (more significant bytes come before
     * less significant ones) */
    int rev;
} I2CAddressableDeviceInfo;

void i2c_addressable_register_device(I2CAddressableDeviceInfo *info);

#endif
