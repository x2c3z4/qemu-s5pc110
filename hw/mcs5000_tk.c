/*
 * Melfas MCS-5000 Touchkey
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Dmitry Zhurikhin <zhur@ispras.ru>
 *                Vladimir Monakhov <vmonakhov@ispras.ru>
 *
 * NB: Only features used in the kernel driver is implemented currently.
 * NB2: This device may also be used in touchscreen mode.  Not supported now.
 */

#include "console.h"
#include "i2c-addressable.h"


#define MCS5000_TK_LED_ONOFF            0x01
#define MCS5000_TK_LED_DIMMING          0x02
#define MCS5000_TK_RESERVED             0x03
#define MCS5000_TK_VALUE_STATUS         0x04
#define MCS5000_TK_RESERVED1            0x05
#define MCS5000_TK_HW_VERSION           0x06
#define MCS5000_TK_FW_VERSION           0x0A
#define MCS5000_TK_MI_VERSION           0x0B

#define MCS5000_TK_FW_ADDR              0x7e


typedef struct MCS5000State {

    I2CAddressableState i2c_addressable;

    uint8_t hw_version;
    uint8_t fw_version;
    uint8_t mi_version;

    uint8_t status;

    int download_fw_state;
    int download_fw_size;
    qemu_irq irq;
} MCS5000State;


static int map[0x80];

static int universal_tk_keymap[0x80] = {
    [0x61] = 16, /* Q is KEY_PHONE / KEY_SEND */
    [0x62] = 17, /* W is KEY_FRONT / KEY_HOME */
    [0x63] = 18, /* E is KEY_EXIT  / KEY_END  */
};



static void mcs5000_reset(MCS5000State *s)
{
    s->hw_version = 0x9;
    s->fw_version = 0x9;
    s->mi_version = 0x0;

    s->status = 0;

    s->download_fw_state = 0;
    s->download_fw_size = 0;
}

static uint8_t mcs5000_read(void *opaque, uint32_t address, uint8_t offset)
{
    MCS5000State *s = (MCS5000State *)opaque;

    if (offset > 0)
        hw_error("mcs5000: bad read size\n");

    switch (address) {
    case MCS5000_TK_VALUE_STATUS:
        return s->status;
    case MCS5000_TK_HW_VERSION:
        return s->hw_version;
    case MCS5000_TK_FW_VERSION:
        return s->fw_version;
    case MCS5000_TK_MI_VERSION:
        return s->mi_version;
    case MCS5000_TK_FW_ADDR:
        s->download_fw_state++;
        switch (s->download_fw_state - 1) {
        case 0: /* Request for firmware download */
            return 0x55;
        case 1: /* Prepare erase done */
            return 0x8F;
        case 2: /* Erase done */
            return 0x82;
        case 3: /* Read flash */
            return 0x84;
        case 4: /* Check firmware */
            if (s->download_fw_size > 0) {
                s->download_fw_size--;
                s->download_fw_state = 4;
            }
            return 0xFF;
        case 5: /* Prepare program */
            return 0x8F;
        case 6: /* Program flash */
            return 0x83;
        case 7: /* Read flash */
            return 0x84;
        case 8: /* Verify data */
            /* TODO */
            if (s->download_fw_size > 0) {
                s->download_fw_size--;
                s->download_fw_state = 8;
            }
            return 0xFF;
        default:
            s->download_fw_state = 0;
            return 0;
        }
    default:
        hw_error("mcs5000: bad read offset 0x%x\n", address);
    }
}


static void mcs5000_write(void *opaque, uint32_t address, uint8_t offset,
                          uint8_t val)
{
    MCS5000State *s = (MCS5000State *)opaque;

    if (offset > 0)
        hw_error("mcs5000: bad write size\n");

    switch (address) {
    case MCS5000_TK_LED_ONOFF:
        break;
    case MCS5000_TK_LED_DIMMING:
        break;
    case MCS5000_TK_FW_ADDR:
        if (s->download_fw_state == 3 || s->download_fw_state == 7) {
            s->download_fw_size = val;
        } else if (s->download_fw_state == 6) {
            /* TODO */
            /* Get firmware image */
        }
        break;
    default:
        hw_error("mcs5000: bad write offset 0x%x\n", address);
    }
}

static void mcs5000_tk_event(void *opaque, int keycode)
{
    MCS5000State *s = (MCS5000State *)opaque;
    int k, push = 0;

    push = (keycode & 0x80) ? 0 : 1; /* Key push from qemu */
    keycode &= ~(0x80); /* Strip qemu key release bit */

    k = map[keycode];

    /* Don't report unknown keypress */
    if (k < 0)
        return;

    s->status = k | (push << 7);
    qemu_irq_raise(s->irq);
}

static void mcs5000_init_keymap(int keycodes[])
{
    int i;

    for (i = 0; i < 0x80; i++)
        map[i] = -1;
    for (i = 0; i < 0x80; i++)
        map[keycodes[i]] = i;
}

static int mcs5000_init(I2CAddressableState *s, int keycodes[])
{
    MCS5000State *t = FROM_I2CADDR_SLAVE(MCS5000State, s);

    qdev_init_gpio_out(&s->i2c.qdev, &t->irq, 1);
    qemu_add_kbd_event_handler(mcs5000_tk_event, t);

    mcs5000_reset(t);
    mcs5000_init_keymap(keycodes);

    return 0;
}

static int mcs5000_universal_init(I2CAddressableState *i2c)
{
    return mcs5000_init(i2c, universal_tk_keymap);
}

static I2CAddressableDeviceInfo mcs5000_universal_info = {
    .i2c.qdev.name  = "mcs5000,universal",
    .i2c.qdev.size  = sizeof(MCS5000State),
    .init  = mcs5000_universal_init,
    .read  = mcs5000_read,
    .write = mcs5000_write,
    .size  = 1,
    .rev   = 0
};

static void mcs5000_register_devices(void)
{
    i2c_addressable_register_device(&mcs5000_universal_info);
}

device_init(mcs5000_register_devices)
