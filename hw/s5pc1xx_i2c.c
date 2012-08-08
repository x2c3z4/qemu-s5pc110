/*
 * I2C controller for Samsung S5PC1XX-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *                Alexey Merkulov <steelart@ispras.ru>
 *
 * Based on SMDK6400 I2C (hw/smdk6400/smdk_i2c.c)
 */

#include "i2c.h"
#include "sysbus.h"


#define I2CCON        0x00      /* I2C Control register */
#define I2CSTAT       0x04      /* I2C Status register */
#define I2CADD        0x08      /* I2C Slave Address register */
#define I2CDS         0x0c      /* I2C Data Shift register */
#define I2CLC         0x10      /* I2C Line Control register */

#define SR_MODE       0x0       /* Slave Receive Mode */
#define ST_MODE       0x1       /* Slave Transmit Mode */
#define MR_MODE       0x2       /* Master Receive Mode */
#define MT_MODE       0x3       /* Master Transmit Mode */


#define S5PC1XX_IICCON_ACKEN        (1<<7)
#define S5PC1XX_IICCON_TXDIV_16     (0<<6)
#define S5PC1XX_IICCON_TXDIV_512    (1<<6)
#define S5PC1XX_IICCON_IRQEN        (1<<5)
#define S5PC1XX_IICCON_IRQPEND      (1<<4)

#define S5PC1XX_IICSTAT_START       (1<<5)
#define S5PC1XX_IICSTAT_BUSBUSY     (1<<5)
#define S5PC1XX_IICSTAT_TXRXEN      (1<<4)
#define S5PC1XX_IICSTAT_ARBITR      (1<<3)
#define S5PC1XX_IICSTAT_ASSLAVE     (1<<2)
#define S5PC1XX_IICSTAT_ADDR0       (1<<1)
#define S5PC1XX_IICSTAT_LASTBIT     (1<<0)

#define S5PC1XX_I2C_REG_MEM_SIZE    0x1000


/* I2C Interface */
typedef struct S5pc1xxI2CState {
    SysBusDevice busdev;

    i2c_bus *bus;
    qemu_irq irq;

    uint8_t control;
    uint8_t status;
    uint8_t address;
    uint8_t datashift;
    uint8_t line_ctrl;

    uint8_t ibmr;
    uint8_t data;
} S5pc1xxI2CState;


static void s5pc1xx_i2c_update(S5pc1xxI2CState *s)
{
    uint16_t level;
    level = (s->status & S5PC1XX_IICSTAT_START) &&
            (s->control & S5PC1XX_IICCON_IRQEN);

    if (s->control & S5PC1XX_IICCON_IRQPEND)
        level = 0;
    qemu_set_irq(s->irq, !!level);
}

static int s5pc1xx_i2c_receive(S5pc1xxI2CState *s)
{
    int r;

    r = i2c_recv(s->bus);
    s5pc1xx_i2c_update(s);
    return r;
}

static int s5pc1xx_i2c_send(S5pc1xxI2CState *s, uint8_t data)
{
    if (!(s->status & S5PC1XX_IICSTAT_LASTBIT)) {
        /*s->status |= 1 << 7;*/
        s->data = data;
        i2c_send(s->bus, s->data);
    }
    s5pc1xx_i2c_update(s);
    return 1;
}

/* I2C read function */
static uint32_t s5pc1xx_i2c_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxI2CState *s = (S5pc1xxI2CState *)opaque;

    switch (offset) {
    case I2CCON:
        return s->control;
    case I2CSTAT:
        return s->status;
    case I2CADD:
        return s->address;
    case I2CDS:
        s->data = s5pc1xx_i2c_receive(s);
        return s->data;
    case I2CLC:
        return s->line_ctrl;
    default:
        hw_error("s5pc1xx_i2c: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
    return 0;
}

/* I2C write function */
static void s5pc1xx_i2c_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5pc1xxI2CState *s = (S5pc1xxI2CState *)opaque;
    int mode;

    qemu_irq_lower(s->irq);

    switch (offset) {
    case I2CCON:
        s->control = value & 0xff;

        if (value & S5PC1XX_IICCON_IRQEN)
            s5pc1xx_i2c_update(s);
        break;

    case I2CSTAT:
        s->status = value & 0xff;
        mode = (s->status >> 6) & 0x3;
        if (value & S5PC1XX_IICSTAT_TXRXEN) {
            /* IIC-bus data output enable/disable bit */
            switch(mode) {
            case SR_MODE:
                s->data = s5pc1xx_i2c_receive(s);
                break;
            case ST_MODE:
                s->data = s5pc1xx_i2c_receive(s);
                break;
            case MR_MODE:
                if (value & (1 << 5)) {
                    /* START condition */
                    s->status &= ~S5PC1XX_IICSTAT_LASTBIT;

                    i2c_start_transfer(s->bus, s->data >> 1, s->data & 1);
                } else {
                    i2c_end_transfer(s->bus);
                    s->status |= S5PC1XX_IICSTAT_TXRXEN;
                }
                break;
            case MT_MODE:
                if (value & (1 << 5)) {
                    /* START condition */
                    s->status &= ~S5PC1XX_IICSTAT_LASTBIT;

                    i2c_start_transfer(s->bus, s->data >> 1, s->data & 1);
                } else {
                    i2c_end_transfer(s->bus);
                    s->status |= S5PC1XX_IICSTAT_TXRXEN;
                }
                break;
            default:
                break;
            }
        }
        s5pc1xx_i2c_update(s);
        break;

    case I2CADD:
        s->address = value & 0xff;
        break;

    case I2CDS:
        s5pc1xx_i2c_send(s, value & 0xff);
        break;

    case I2CLC:
        s->line_ctrl = value & 0xff;
        break;

    default:
        hw_error("s5pc1xx_i2c: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_i2c_readfn[] = {
    s5pc1xx_i2c_read,
    s5pc1xx_i2c_read,
    s5pc1xx_i2c_read
};

static CPUWriteMemoryFunc * const s5pc1xx_i2c_writefn[] = {
    s5pc1xx_i2c_write,
    s5pc1xx_i2c_write,
    s5pc1xx_i2c_write
};

/* I2C init */
static int s5pc1xx_i2c_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxI2CState *s = FROM_SYSBUS(S5pc1xxI2CState, dev);

    sysbus_init_irq(dev, &s->irq);
    s->bus = i2c_init_bus(&dev->qdev, "i2c");

    iomemtype =
        cpu_register_io_memory(s5pc1xx_i2c_readfn, s5pc1xx_i2c_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_I2C_REG_MEM_SIZE, iomemtype);

    return 0;
}

static void s5pc1xx_i2c_register(void)
{
    sysbus_register_dev("s5pc1xx,i2c", sizeof(S5pc1xxI2CState),
                        s5pc1xx_i2c_init);
}

device_init(s5pc1xx_i2c_register)
