/*
 * S5C GPIO Controller Constants
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */


/* Registers */

#define STEP                0x20

#define CON(group)          (0x000 + STEP * group)  /* R/W Configuration Register */
#define DAT(group)          (0x004 + STEP * group)  /* R/W Data Register */
#define PUD(group)          (0x008 + STEP * group)  /* R/W Pull-up/down Register */
#define DRV(group)          (0x00C + STEP * group)  /* R/W Drive Strength Control Register */
#define CONPDN(group)       (0x010 + STEP * group)  /* R/W Power Down Mode Configuration Register */
#define PUDPDN(group)       (0x014 + STEP * group)  /* R/W Power Down Mode Pullup/down Register */

/* General PIN configurations */
#define GIPIO_CONF_INPUT    0x0
#define GIPIO_CONF_OUTPUT   0x1
#define GIPIO_CONF_INT      0xF

/* Warning: the group GPI is absent in the registers below */

/* inter - group without GPI */
#define GROUP_TO_INT_NUM(group) ((group) - ((group) < 17 ? 0:1))
#define INT_TO_GROUP_NUM(inter) ((inter) + ((inter) < 17 ? 0:1))

#define INT_REGS_SIZE     0x58

#define INT_CON_BASE     0x700
/* FLTCON has double set of regesters. So its' size is 2*INT_REGS_SIZE */
#define INT_FLTCON_BASE  0x800

#define INT_MASK_BASE    0x900

#define INT_PEND_BASE    0xA00
#define INT_FIXPRI_BASE  0xB14

/* R/W GPIO Interrupt Configuration Register */
#define INT_CON(group)  (INT_CON_BASE + 4 * GROUP_TO_INT_NUM(group))
#define GET_GROUP_INT_CON(addr) INT_TO_GROUP_NUM(((addr) - INT_CON_BASE) / 4)

/* R/W GPIO Interrupt Filter Configuration Register 0 and 1 */
#define INT_FLTCON(group) (INT_FLTCON_BASE + 4 * GROUP_TO_INT_NUM(group))
#define GET_GROUP_INT_FLTCON(addr) INT_TO_GROUP_NUM(((addr) - INT_FLTCON_BASE) / 8)


/* R/W GPIO Interrupt Mask Register */
#define INT_MASK(group) (INT_MASK_BASE + 4 * GROUP_TO_INT_NUM(group))
#define GET_GROUP_INT_MASK(addr) INT_TO_GROUP_NUM(((addr) - INT_MASK_BASE) / 4)

/* R/W GPIO Interrupt Pending Register */
#define INT_PEND(group) (INT_PEND_BASE + 4 * GROUP_TO_INT_NUM(group))
#define GET_GROUP_INT_PEND(addr) INT_TO_GROUP_NUM(((addr) - INT_PEND_BASE) / 4)


#define GPIO_INT_GRPPRI     0xB00   /* R/W GPIO Interrupt Group Priority Control Register 0x0 */
#define GPIO_INT_PRIORITY   0xB04   /* R/W GPIO Interrupt Priority Control Register 0x00 */
#define GPIO_INT_SER        0xB08   /* R Current Service Register 0x00 */
#define GPIO_INT_SER_PEND   0xB0C   /* R Current Service Pending Register 0x00 */
#define GPIO_INT_GRPFIXPRI  0xB10   /* R/W GPIO Interrupt Group Fixed Priority Control Register 0x00 */

/* R/W GPIO Interrupt Fixed Priority Control Register */
#define INT_FIXPRI(group) (INT_PEND_FIXPRI + 4 * GROUP_TO_INT_NUM(group))
#define GET_GROUP_INT_FIXPRI(addr) INT_TO_GROUP_NUM(((addr)-INT_FIXPRI_BASE) / 4)

/* R/W External Interrupt Configuration Register */
#define EXT_INT_CON(n)      (0xE00 + 0x04 * n)

/* R/W External Interrupt Filter Configuration Register 0 */
#define EXT_INT_FLTCON0(n)  (0xE80 + 0x08 * n)

/* R/W External Interrupt Filter Configuration Register 1 */
#define EXT_INT_FLTCON1(n)  (0xE84 + 0x08 * n)

/* R/W External Interrupt Mask Register */
#define EXT_INT_MASK(n)     (0xF00 + 0x04 * n)

/* R/W External Interrupt Pending Register */
#define EXT_INT_PEND(n)     (0xF40 + 0x04 * n)

/* R/W Power down mode Pad Configure Register */
#define PDNEN               0xF80


/* Groups */

#define GPA0    0
#define GPA1    1
#define GPB     2
#define GPC0    3
#define GPC1    4
#define GPD0    5
#define GPD1    6
#define GPE0    7
#define GPE1    8
#define GPF0    9
#define GPF1    10
#define GPF2    11
#define GPF3    12
#define GPG0    13
#define GPG1    14
#define GPG2    15
#define GPG3    16
#define GPI     17
#define GPJ0    18
#define GPJ1    19
#define GPJ2    20
#define GPJ3    21
#define GPJ4    22

#define MP0_1   23
#define MP0_2   24
#define MP0_3   25
#define MP0_4   26
#define MP0_5   27
#define MP0_6   28
#define MP0_7   29

#define MP1_0   30
#define MP1_1   31
#define MP1_2   32
#define MP1_3   33
#define MP1_4   34
#define MP1_5   35
#define MP1_6   36
#define MP1_7   37
#define MP1_8   38

#define MP2_0   39
#define MP2_1   40
#define MP2_2   41
#define MP2_3   42
#define MP2_4   43
#define MP2_5   44
#define MP2_6   45
#define MP2_7   46
#define MP2_8   47

#define ETC0    48
#define ETC1    49
#define ETC2    50

#define GPH0    96
#define GPH1    97
#define GPH2    98
#define GPH3    99


#define MAX_PIN_IN_GROUP            8
#define IRQ_EXTEND_NUM              32

/* GPIOINT IRQs */
#define GPIOEXT_IRQ(irq)            (irq % 32)
#define GPIOINT_IRQ(group, pin) \
    ((group) * MAX_PIN_IN_GROUP + (pin) + IRQ_EXTEND_NUM)
#define GPIOINT_GROUP(irq)          ((irq - IRQ_EXTEND_NUM) / MAX_PIN_IN_GROUP)
#define GPIOINT_PIN(irq)            ((irq - IRQ_EXTEND_NUM) % MAX_PIN_IN_GROUP)


/* GPIO device indexes and io_cases */

/* CASE_ID has the structure: device_num [15:11];
 *                            instance_num [10:6];
 *                            event_num [5:0]
 */
#define CASE_ID(device, instance, event)    ((device << 11) | \
                                             (instance << 6) | \
                                             (event << 0))

#define GPIO_IDX_PWM            1
    #define GPIO_PWM_TOUT(n)        CASE_ID(GPIO_IDX_PWM, 0, n)     /* n = 0~3 */
    #define PWM_MIE_MDNI            CASE_ID(GPIO_IDX_PWM, 0, 4)

#define GPIO_IDX_UART           2
    #define GPIO_UART_RXD(n)        CASE_ID(GPIO_IDX_UART, n, 1)    /* n = 0~3 */
    #define GPIO_UART_TXD(n)        CASE_ID(GPIO_IDX_UART, n, 2)    /* n = 0~3 */
    #define GPIO_UART_CTS(n)        CASE_ID(GPIO_IDX_UART, n, 3)    /* n = 0~2 */
    #define GPIO_UART_RTS(n)        CASE_ID(GPIO_IDX_UART, n, 4)    /* n = 0~2 */
    #define GPIO_UART_AUDIO_RXD     CASE_ID(GPIO_IDX_UART, 0, 5)
    #define GPIO_UART_AUDIO_TXD     CASE_ID(GPIO_IDX_UART, 0, 6)

#define GPIO_IDX_KEYIF          3
    #define GPIO_KP_COL(n)          CASE_ID(GPIO_IDX_KEYIF, n, 1)   /* n = 0~7 */
    #define GPIO_KP_ROW(n)          CASE_ID(GPIO_IDX_KEYIF, n, 2)   /* n = 0~13 */

#define GPIO_IDX_LCD            4
    #define LCD_FRM                 CASE_ID(GPIO_IDX_LCD, 0, 1)
    #define LCD_HSYNC               CASE_ID(GPIO_IDX_LCD, 0, 2)
    #define LCD_VSYNC               CASE_ID(GPIO_IDX_LCD, 0, 3)
    #define LCD_VDEN                CASE_ID(GPIO_IDX_LCD, 0, 4)
    #define LCD_VCLK                CASE_ID(GPIO_IDX_LCD, 0, 5)
    #define LCD_VD(n)               CASE_ID(GPIO_IDX_LCD, n, 6)     /* n = 0~23 */

#define GPIO_IDX_I2C            5
    #define GPIO_IDX_I2C_SCL        1
    #define GPIO_IDX_I2C_SDA        2
    #define GPIO_I2C_SDA(n)         CASE_ID(GPIO_IDX_I2C, n, GPIO_IDX_I2C_SDA)
    #define GPIO_I2C_SCL(n)         CASE_ID(GPIO_IDX_I2C, n, GPIO_IDX_I2C_SCL)

#define GPIO_IDX_AC97           6
    #define GPIO_AC97BITCLK         CASE_ID(GPIO_IDX_AC97, 0, 1)
    #define GPIO_AC97RESETn         CASE_ID(GPIO_IDX_AC97, 0, 2)
    #define GPIO_AC97SYNC           CASE_ID(GPIO_IDX_AC97, 0, 3)
    #define GPIO_AC97SDI            CASE_ID(GPIO_IDX_AC97, 0, 4)
    #define GPIO_AC97SDO            CASE_ID(GPIO_IDX_AC97, 0, 5)

#define GPIO_IDX_PCM            7
    #define PCM_SCLK(n)             CASE_ID(GPIO_IDX_PCM, n, 1)     /* n = 0~2 */
    #define PCM_EXTCLK(n)           CASE_ID(GPIO_IDX_PCM, n, 2)     /* n = 0~2 */
    #define PCM_FSYNC(n)            CASE_ID(GPIO_IDX_PCM, n, 3)     /* n = 0~2 */
    #define PCM_SIN(n)              CASE_ID(GPIO_IDX_PCM, n, 4)     /* n = 0~2 */
    #define PCM_SOUT(n)             CASE_ID(GPIO_IDX_PCM, n, 5)     /* n = 0~2 */

#define GPIO_IDX_SPDIF          8
    #define SPDIF_0_OUT             CASE_ID(GPIO_IDX_SPDIF, 0, 1)
    #define SPDIF_EXTCLK            CASE_ID(GPIO_IDX_SPDIF, 0, 2)

#define GPIO_IDX_MAX            9


/* GPIO io_cases storage */

/* Note: cases with indexes = 2~5 are stored in the array first */
#define CASE(index)         (index > 1 ? index - 2 : 4 + index)
