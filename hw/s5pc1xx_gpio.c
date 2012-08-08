/*
 * GPIO controller for Samsung S5PC110-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "s5pc1xx.h"
#include "sysbus.h"
#include "s5pc1xx_gpio_regs.h"


#define GPIO_CONF_CASE 6
#define GPIO_PIN_CONF(s, group, pin) (((s)->con[(group)] >> ((pin) * 4)) & 0xF)
#define S5PC1XX_GPIO_REG_MEM_SIZE 0xF84


typedef struct S5pc1xxGPIOState {
    SysBusDevice busdev;

    /* Port Registers */
    uint32_t con[GPH3 + 1];

    /* Interrupt GPIO Registers
     * GPI has no interrupts so corresponding field is not used */
    uint32_t int_con[GPJ4 + 1];
    uint32_t int_fltcon[GPJ4 + 1][2];
    uint32_t int_mask[GPJ4 + 1];
    uint32_t int_pend[GPJ4 + 1];
    uint32_t int_fixpri[GPJ4 + 1];

    uint32_t int_grppri;
    uint32_t int_priority;
    uint32_t int_ser;
    uint32_t int_ser_pend;
    uint32_t int_grpfixpri;

    /* Extended Interrupt Registers */
    uint32_t ext_int_mask[4];
    uint32_t ext_int_pend[4];

    /* Parent Interrupt for GPIO interrupts */
    qemu_irq gpioint;
    /* Parent Extended Interrupt */
    qemu_irq extend;
} S5pc1xxGPIOState;


int s5pc1xx_gpio_case[GPH3 + 1][8][7] = {

/* GPA0 */      {{GPIO_UART_RXD(0)},   {GPIO_UART_TXD(0)},     {GPIO_UART_CTS(0)},     {GPIO_UART_RTS(0)},
                 {GPIO_UART_RXD(1)},   {GPIO_UART_TXD(1)},     {GPIO_UART_CTS(1)},     {GPIO_UART_RTS(1)}},

/* GPA1 */      {{GPIO_UART_RXD(2), 0, GPIO_UART_AUDIO_RXD},   {GPIO_UART_TXD(2), 0, GPIO_UART_AUDIO_TXD},
                 {GPIO_UART_RXD(3), 0, GPIO_UART_CTS(2)},      {GPIO_UART_TXD(3), 0, GPIO_UART_RTS(2)}},
/* not
 * covered */   {},

/* GPC0 */      {{0, PCM_SCLK(1), GPIO_AC97BITCLK},  {0, PCM_EXTCLK(1), GPIO_AC97RESETn},    {0, PCM_FSYNC(1), GPIO_AC97SYNC},
                 {0, PCM_SIN(1), GPIO_AC97SDI},      {0, PCM_SOUT(1), GPIO_AC97SDO}},

/* GPC1 */      {{PCM_SCLK(0), SPDIF_0_OUT},    {PCM_EXTCLK(0), SPDIF_EXTCLK},
                 {PCM_FSYNC(0), LCD_FRM},       {PCM_SIN(0)},                       {PCM_SOUT(0)}},

/* GPD0 */      {{GPIO_PWM_TOUT(0)},   {GPIO_PWM_TOUT(1)},     {GPIO_PWM_TOUT(2)},     {GPIO_PWM_TOUT(3), PWM_MIE_MDNI}},

/* not
 * covered */   {}, {}, {},

/* GPF0 */      {{LCD_HSYNC},   {LCD_VSYNC},    {LCD_VDEN},     {LCD_VCLK},
                 {LCD_VD(0)},   {LCD_VD(1)},    {LCD_VD(2)},    {LCD_VD(3)}},

/* GPF1 */      {{LCD_VD(4)},   {LCD_VD(5)},    {LCD_VD(6)},    {LCD_VD(7)},
                 {LCD_VD(8)},   {LCD_VD(9)},    {LCD_VD(10)},   {LCD_VD(11)}},

/* GPF2 */      {{LCD_VD(12)},  {LCD_VD(13)},   {LCD_VD(14)},   {LCD_VD(15)},
                 {LCD_VD(16)},  {LCD_VD(17)},   {LCD_VD(18)},   {LCD_VD(19)}},

/* GPF3 */      {{LCD_VD(20)},  {LCD_VD(21)},   {LCD_VD(22)},   {LCD_VD(23)}},

/* not
 * covered */   {}, {}, {}, {},

/* GPI */       {{0, PCM_SCLK(2)}, {0, PCM_EXTCLK(2)}, {0, PCM_FSYNC(2)}, {0, PCM_SIN(2)}, {0, PCM_SOUT(2)}},

/* not
 * covered */   {},

/* GPJ1 */      {{}, {}, {}, {}, {0, GPIO_KP_COL(0)}},

/* GPJ2 */      {{0, GPIO_KP_COL(1)},   {0, GPIO_KP_COL(2)},    {0, GPIO_KP_COL(3)},    {0, GPIO_KP_COL(4)},
                 {0, GPIO_KP_COL(5)},   {0, GPIO_KP_COL(6)},    {0, GPIO_KP_COL(7)},    {0, GPIO_KP_ROW(0)}},

/* GPJ3 */      {{0, GPIO_KP_ROW(1), 0, 0, GPIO_I2C_SDA(10), 0, GPIO_I2C_SDA(10)},
                 {0, GPIO_KP_ROW(2), 0, 0, GPIO_I2C_SCL(10), 0, GPIO_I2C_SCL(10)},
                 {0, GPIO_KP_ROW(3)},
                 {0, GPIO_KP_ROW(4)},   {0, GPIO_KP_ROW(5)},    {0, GPIO_KP_ROW(6)},
                 {0, GPIO_KP_ROW(7), 0, 0, GPIO_I2C_SDA(3), 0, GPIO_I2C_SDA(3)},
                 {0, GPIO_KP_ROW(8), 0, 0, GPIO_I2C_SCL(3), 0, GPIO_I2C_SCL(3)}},

/* GPJ4 */      {{0, GPIO_KP_ROW(9), 0, 0, GPIO_I2C_SDA(4), 0, GPIO_I2C_SDA(4)},
                 {0, GPIO_KP_ROW(10)},   {0, GPIO_KP_ROW(11)},
                 {0, GPIO_KP_ROW(12), 0, 0, GPIO_I2C_SCL(4), 0, GPIO_I2C_SCL(4)},
                 {0, GPIO_KP_ROW(13)}},
/* not
 * covered */   {}, {}, {}, {},

/* MP0_5 */     {{}, {}, {0, 0, 0, 0, GPIO_I2C_SCL(5), 0, GPIO_I2C_SCL(5)}, {0, 0, 0, 0, GPIO_I2C_SDA(5), 0, GPIO_I2C_SDA(5)},
                 {}, {}, {}, {}},
/* not
* covered */    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
                {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
                {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},

/* GPH2 */      {{0, GPIO_KP_COL(0)},   {0, GPIO_KP_COL(1)},    {0, GPIO_KP_COL(2)},    {0, GPIO_KP_COL(3)},
                 {0, GPIO_KP_COL(4)},   {0, GPIO_KP_COL(5)},    {0, GPIO_KP_COL(6)},    {0, GPIO_KP_COL(7)}},

/* GPH3 */      {{0, GPIO_KP_ROW(0)},   {0, GPIO_KP_ROW(1)},    {0, GPIO_KP_ROW(2)},    {0, GPIO_KP_ROW(3)},
                 {0, GPIO_KP_ROW(4)},   {0, GPIO_KP_ROW(5)},    {0, GPIO_KP_ROW(6)},    {0, GPIO_KP_ROW(7)}},
};


/* IO Memory Support */

GPIOWriteMemoryFunc *gpio_io_mem_write[GPIO_IDX_MAX];
GPIOReadMemoryFunc *gpio_io_mem_read[GPIO_IDX_MAX];
GPIOWriteMemoryFunc *gpio_io_mem_conf[GPIO_IDX_MAX] = {NULL};
void *gpio_io_mem_opaque[GPIO_IDX_MAX][8];


void s5pc1xx_gpio_register_io_memory(int io_index, int instance,
                                     GPIOReadMemoryFunc *mem_read,
                                     GPIOWriteMemoryFunc *mem_write,
                                     GPIOWriteMemoryFunc *mem_conf,
                                     void *opaque)
{
    gpio_io_mem_read[io_index]  = mem_read;
    gpio_io_mem_write[io_index] = mem_write;
    gpio_io_mem_conf[io_index]  = mem_conf;
    gpio_io_mem_opaque[io_index][instance] = opaque;
}

uint32_t s5pc1xx_empty_gpio_read(void *opaque, int io_index)
{
    return 0;
}

void s5pc1xx_empty_gpio_write(void *opaque, int io_index, uint32_t value)
{
}

/* Empty read and write functions references are used as gags
 * for the elements gpio_io_mem_read[0] and gpio_io_mem_write[0] */
static GPIOReadMemoryFunc *s5pc1xx_empty_gpio_readfn   = s5pc1xx_empty_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_empty_gpio_writefn = s5pc1xx_empty_gpio_write;


/* GPIO General Logic */

/* GPIO Reset Function */
static void s5pc1xx_gpio_reset(S5pc1xxGPIOState *s)
{
    unsigned int group;

    /* Note: no groups from ETC2 (50) till GPH0 (96) */
    for (group = GPA0;
         group <= GPH3;
         group = (group == ETC2) ? GPH0 : group + 1) {
        s->con[group] = 0x0;
    }

    for (group = 0; group < 4; group++) {
        s->ext_int_mask[group] = 0xFF;
        s->ext_int_pend[group] = 0x0;
    }

    for (group = GPA0; group <= GPJ4; group++) {
        s->int_con[group] = 0x0;
        s->int_fltcon[group][0] = 0x0;
        s->int_fltcon[group][1] = 0x0;
        s->int_mask[group] = 0xFF; /* other values in documentation */
        s->int_pend[group] = 0x0;
        s->int_fixpri[group] = 0x0;
    }

    s->int_grppri    = 0x0;
    s->int_priority  = 0x0;
    s->int_ser       = 0x0;
    s->int_ser_pend  = 0x0;
    s->int_grpfixpri = 0x0;
}

static void s5pc1xx_gpio_irq_lower(S5pc1xxGPIOState *s,
                                   unsigned int group, uint32_t pinmask)
{
    unsigned int i = 0;
    s->int_pend[group] &= ~pinmask;
    for (i = GPA0; i <= GPJ4; i++)
        if (s->int_pend[i])
            return;

    qemu_irq_lower(s->gpioint);
}

static void s5pc1xx_gpio_extended_irq_lower(S5pc1xxGPIOState *s,
                                            unsigned int group, uint32_t pinmask)
{
    unsigned int i = 0;
    s->ext_int_pend[group] &= ~pinmask;
    for (i = 0; i < 4; i++)
        if (s->ext_int_pend[i])
            return;

    qemu_irq_lower(s->extend);
}

static void s5pc1xx_gpio_irq_handler(void *opaque, int irq, int level)
{
    S5pc1xxGPIOState *s = (S5pc1xxGPIOState *)opaque;
    unsigned int group = GPIOINT_GROUP(irq);
    unsigned int pin   = GPIOINT_PIN  (irq);

    /* Special case of extended IRQs */
    if (irq < IRQ_EXTEND_NUM) {
        group = irq >> 3;
        pin   = irq & 0x7;

        /* FIXME: IRQs 0~15 are not supported */
        if (irq < 16)
            hw_error("s5pc1xx_gpio: "
                     "extended IRQs 0-15 through GPIO are not supported");

        if (s->ext_int_mask[group] & (1 << pin))
            return;

        if (s->ext_int_pend[group] & (1 << pin))
            return;

        if (level) {
            s->ext_int_pend[group] |= (1 << pin);
            qemu_irq_raise(s->extend);
        } else {
            s5pc1xx_gpio_extended_irq_lower(s, group, (1 << pin));
        }

        return;
    }

    if (GPIO_PIN_CONF(s, group, pin) != GIPIO_CONF_INT)
        return;

    if (s->int_mask[group] & (1 << pin))
        return;

    /* if the interrupt has already happened */
    if (s->int_pend[group] & (1 << pin))
        return;

    if (level) {
        s->int_pend[group] |= (1 << pin);
        qemu_irq_raise(s->gpioint);
    } else {
        s5pc1xx_gpio_irq_lower(s, group, (1 << pin));
    }
}

/* GPIO Read Function */
static uint32_t s5pc1xx_gpio_read(void *opaque,
                                  target_phys_addr_t offset)
{
    S5pc1xxGPIOState *s = (S5pc1xxGPIOState *)opaque;
    uint32_t value = 0, ret_val;
    int index, device, instance, io_index;
    unsigned int group, pin, n;
    int gpio_case;

    group = offset / STEP;

    if ((group <= ETC2) || ((group >= GPH0) && (group <= GPH3))) {

        if (offset % STEP == 0x00)
            return s->con[group];

        if (offset % STEP == 0x04) {
            ret_val = 0;

            for (pin = 0; pin < 8; pin++) {
                index = (s->con[group] >> pin * 4) & 0xF;
                switch (index) {
                    case 0x1:
                        /* Input port can't be read */
                        break;
                    case 0x0:
                    case 0x2:
                    case 0x3:
                    case 0x4:
                    case 0x5:
                        gpio_case = s5pc1xx_gpio_case[group][pin][CASE(index)];
                        if (gpio_case) {
                            device   = gpio_case >> 11 & 0x1F;
                            instance = gpio_case >>  6 & 0x1F;
                            io_index = gpio_case >>  0 & 0x3F;

                            if (!(gpio_io_mem_read[device]))
                                device = 0;

                            value = (gpio_io_mem_read[device])
                                      (gpio_io_mem_opaque[device][instance],
                                       io_index);
                        }
                        break;
                    case 0xF:
                        /* TODO: implement this case */
                        break;
                    default:
                        hw_error("s5pc1xx_gpio: "
                                 "bad pin configuration index 0x%X\n", index);
                }
                ret_val |= (value & 0x1) << pin;
            }
            return ret_val;
        }
    }

    if (offset >= INT_CON_BASE && offset < INT_CON_BASE + INT_REGS_SIZE)
        return s->int_con[GET_GROUP_INT_CON(offset)];

    if (offset >= INT_FLTCON_BASE &&
        offset < INT_FLTCON_BASE + INT_REGS_SIZE * 2)
        return s->int_fltcon[GET_GROUP_INT_FLTCON(offset)]
                            [(offset & 0x4) >> 2];

    if (offset >= INT_MASK_BASE && offset < INT_MASK_BASE + INT_REGS_SIZE)
        return s->int_mask[GET_GROUP_INT_MASK(offset)];

    if (offset >= INT_PEND_BASE && offset < INT_PEND_BASE + INT_REGS_SIZE)
        return s->int_pend[GET_GROUP_INT_PEND(offset)];

    if (offset >= INT_FIXPRI_BASE && offset < INT_FIXPRI_BASE + INT_REGS_SIZE)
        return s->int_fixpri[GET_GROUP_INT_FIXPRI(offset)];

    switch (offset) {
    case GPIO_INT_GRPPRI:
        return s->int_grppri;
    case GPIO_INT_PRIORITY:
        return s->int_priority;
    case GPIO_INT_SER:
        return s->int_ser;
    case GPIO_INT_SER_PEND:
        return s->int_ser_pend;
    case GPIO_INT_GRPFIXPRI:
        return s->int_grpfixpri;
    }

    for (n = 0; n < 4; n++) {
        /* EINT Mask Register */
        if (offset == EXT_INT_MASK(n))
            return s->ext_int_mask[n];
        /* EINT Pend Register */
        if (offset == EXT_INT_PEND(n))
            return s->ext_int_pend[n];
    }

    return 0;
}

/* GPIO Write Function */
static void s5pc1xx_gpio_write(void *opaque,
                               target_phys_addr_t offset,
                               uint32_t value)
{
    S5pc1xxGPIOState *s = (S5pc1xxGPIOState *)opaque;
    int index, device, instance, io_index;
    unsigned int group, pin, n;
    int gpio_case;

    group = offset / STEP;

    if ((group <= ETC2) || ((group >= GPH0) && (group <= GPH3))) {

        if (offset % STEP == 0x00) {
            for (pin = 0; pin < 8; pin++) {
                int new_con = (value >> pin * 4) & 0xF;

                gpio_case = s5pc1xx_gpio_case[group][pin][GPIO_CONF_CASE];
                if (gpio_case) {
                    device   = gpio_case >> 11 & 0x1F;
                    instance = gpio_case >>  6 & 0x1F;
                    io_index = gpio_case >>  0 & 0x3F;

                    if (!(gpio_io_mem_conf[device]))
                        device = 0;

                    gpio_io_mem_conf[device](gpio_io_mem_opaque[device][instance],
                                             io_index, new_con);
                }
            }
            s->con[group] = value;
            return;
        }

        if (offset % STEP == 0x04) {
            for (pin = 0; pin < 8; pin++) {
                index = (s->con[group] >> pin * 4) & 0xF;
                switch (index) {
                    case 0x0:
                        /* Output port can't be written */
                        break;
                    case 0x1:
                    case 0x2:
                    case 0x3:
                    case 0x4:
                    case 0x5:
                        gpio_case = s5pc1xx_gpio_case[group][pin][CASE(index)];
                        if (gpio_case) {
                            device   = gpio_case >> 11 & 0x1F;
                            instance = gpio_case >>  6 & 0x1F;
                            io_index = gpio_case >>  0 & 0x3F;

                            if (!(gpio_io_mem_write[device]))
                                device = 0;

                            gpio_io_mem_write[device]
                                (gpio_io_mem_opaque[device][instance],
                                 io_index, (value >> pin) & 0x1);
                        }
                        break;
                    case 0xF:
                        /* TODO: implement this case */
                        break;
                    default:
                        hw_error("s5pc1xx_gpio: "
                                 "bad pin configuration index 0x%X\n", index);
                }
            }
            return;
        }
    }

    if (offset >= INT_CON_BASE && offset < INT_CON_BASE + INT_REGS_SIZE) {
        s->int_con[GET_GROUP_INT_CON(offset)] = value;
        return;
    }

    if (offset >= INT_FLTCON_BASE &&
        offset < INT_FLTCON_BASE + INT_REGS_SIZE * 2) {
        s->int_fltcon[GET_GROUP_INT_FLTCON(offset)][(offset & 0x4) >> 2] =
            value;
        return;
    }

    if (offset >= INT_MASK_BASE && offset < INT_MASK_BASE + INT_REGS_SIZE) {
        s->int_mask[GET_GROUP_INT_MASK(offset)] = value;
        return;
    }

    if (offset >= INT_PEND_BASE && offset < INT_PEND_BASE + INT_REGS_SIZE) {
        group = GET_GROUP_INT_PEND(offset);
        s5pc1xx_gpio_irq_lower(s, group, value);
        return;
    }

    if (offset >= INT_FIXPRI_BASE && offset < INT_FIXPRI_BASE + INT_REGS_SIZE) {
        s->int_fixpri[GET_GROUP_INT_FIXPRI(offset)] = value;
        return;
    }

    switch (offset) {
    case GPIO_INT_GRPPRI:
        s->int_grppri = value;
        return;
    case GPIO_INT_PRIORITY:
        s->int_priority = value;
        return;
    case GPIO_INT_SER:
        s->int_ser = value;
        return;
    case GPIO_INT_SER_PEND:
        s->int_ser_pend = value;
        return;
    case GPIO_INT_GRPFIXPRI:
        s->int_grpfixpri = value;
        return;
    }

    for (n = 0; n < 4; n++) {
        /* EINT Mask Register */
        if (offset == EXT_INT_MASK(n)) {
            s->ext_int_mask[n] = value;
            return;
        }
        /* EINT Pend Register */
        if (offset == EXT_INT_PEND(n)) {
            s5pc1xx_gpio_extended_irq_lower(s, group, value);
            return;
        }
    }
}

static CPUReadMemoryFunc * const s5pc1xx_gpio_readfn[] = {
   s5pc1xx_gpio_read,
   s5pc1xx_gpio_read,
   s5pc1xx_gpio_read
};

static CPUWriteMemoryFunc * const s5pc1xx_gpio_writefn[] = {
   s5pc1xx_gpio_write,
   s5pc1xx_gpio_write,
   s5pc1xx_gpio_write
};

qemu_irq s5pc1xx_gpoint_irq(DeviceState *d, int irq_num)
{
    return qdev_get_gpio_in(d, irq_num);
}

/* GPIO Init Function */
static int s5pc1xx_gpio_init(SysBusDevice *dev)
{
    S5pc1xxGPIOState *s = FROM_SYSBUS(S5pc1xxGPIOState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->gpioint);
    sysbus_init_irq(dev, &s->extend);
    iomemtype =
        cpu_register_io_memory(s5pc1xx_gpio_readfn, s5pc1xx_gpio_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_GPIO_REG_MEM_SIZE, iomemtype);

    /* First IRQ_EXTEND_NUM interrupts are for extended interrupts */
    qdev_init_gpio_in(&dev->qdev, s5pc1xx_gpio_irq_handler,
                      IRQ_EXTEND_NUM + MAX_PIN_IN_GROUP * (GPJ4 + 1));

    s5pc1xx_gpio_register_io_memory(0, 0, s5pc1xx_empty_gpio_readfn,
                                    s5pc1xx_empty_gpio_writefn,
                                    s5pc1xx_empty_gpio_writefn, NULL);
    s5pc1xx_gpio_reset(s);

    return 0;
}

static void s5pc1xx_gpio_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,gpio", sizeof(S5pc1xxGPIOState),
                        s5pc1xx_gpio_init);
}

device_init(s5pc1xx_gpio_register_devices)
