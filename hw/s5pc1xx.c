/*
 * Samsung S5PC1XX-based board emulation.
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 *                Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *                Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 */


/* turn off some memory-hungry but useless (yet) devices */
#define LOWMEMORY


#include "s5pc1xx.h"
#include "boards.h"
#include "arm-misc.h"
#include "primecell.h"
#include "qemu-timer.h"
#include "net.h"
#include "i2c.h"
#include "sysbus.h"
#include "s5pc1xx_gpio_regs.h"


/* Memory map */
#define S5PC1XX_BOOT_BASE        0x00000000
#define S5PC1XX_DRAM0_BASE       0x20000000
#define S5PC1XX_ONEDRAM_BASE     0x30000000
#define S5PC1XX_DRAM1_BASE       0x40000000
#define S5PC1XX_SROMC_B0_BASE    0x80000000
#define S5PC1XX_SROMC_B1_BASE    0x88000000
#define S5PC1XX_SROMC_B2_BASE    0x90000000
#define S5PC1XX_SROMC_B3_BASE    0x98000000
#define S5PC1XX_SROMC_B4_BASE    0xA0000000
#define S5PC1XX_SROMC_B5_BASE    0xA8000000
#define S5PC1XX_ONENAND_BASE     0xB0000000
#define S5PC1XX_NAND_BASE        0xB0E00000
#define S5PC1XX_MP3_SRAM_BASE    0xC0000000
#define S5PC1XX_IROM_BASE        0xD0000000 /* Internal SROM */
#define S5PC1XX_IRAM_BASE        0xD0020000 /* Internal SRAM */
#define S5PC1XX_DMZ_ROM          0xD8000000
#define S5PC1XX_SFRS             0xE0000000

#define S5PC1XX_JPEG_BASE        0xFB600000
#define S5PC1XX_USB_OTG_BASE     0xEC000000

/* Memory size */
#define S5PC1XX_BOOT_SIZE        (1 << 29)  /* 512 MB */
#define S5PC1XX_ISROM_SIZE       (1 << 16)  /* 64 KB  */
#define S5PC1XX_ISRAM_SIZE       (1 << 17)  /* 128 KB */
#define S5PC1XX_DRAM_MAX_SIZE    (1 << 29)  /* 512 MB */

#ifndef LOWMEMORY
#define S5PC1XX_SFRS_SIZE        (1 << 29)  /* 512 MB */
#else
#define S5PC1XX_SFRS_SIZE        (1 << 27)  /* 128 MB */
#endif

/* Interrputs Handling */

/* Number of interrupts */
#define S5PC1XX_IRQ_COUNT        128
/* Number of Vector Interrupt Controllers */
#define S5PC1XX_VIC_N            4
/* Number of vectors in each Vector Interrupt Controller */
#define S5PC1XX_VIC_SIZE         32
#define S5PC1XX_VIC_BASE         0xF2000000
#define S5PC1XX_VIC_SHIFT        0x00100000

#define S5PC1XX_LCD_BASE         0xF8000000
#define S5PC1XX_AC97_BASE        0xE2200000

#define S5PC1XX_SPI_BASE         0xE1300000
#define S5PC1XX_SPI_SHIFT        0x00100000

#define S5PC1XX_IRQ_FAKE_ETH     9
#define S5PC1XX_IRQ_EXTEND       16
#define S5PC1XX_IRQ_DMAMEM       18
#define S5PC1XX_IRQ_DMA0         19
#define S5PC1XX_IRQ_DMA1         20
#define S5PC1XX_IRQ_TIMER0       21
#define S5PC1XX_IRQ_TIMER1       22
#define S5PC1XX_IRQ_TIMER2       23
#define S5PC1XX_IRQ_TIMER3       24
#define S5PC1XX_IRQ_TIMER4       25
#define S5PC1XX_IRQ_SYS_TIMER    26
#define S5PC1XX_IRQ_WDT          27
#define S5PC1XX_IRQ_RTC_ALARM    28
#define S5PC1XX_IRQ_RTC_TICK     29
#define S5PC1XX_IRQ_GPIOINT      30

#define S5PC1XX_IRQ_UART0        42
#define S5PC1XX_IRQ_UART1        43
#define S5PC1XX_IRQ_UART2        44
#define S5PC1XX_IRQ_UART3        45
#define S5PC1XX_IRQ_SPI0         47
#define S5PC1XX_IRQ_SPI1         48
#define S5PC1XX_IRQ_SPI2         49

#define S5PC1XX_IRQ_UHOST        55
#define S5PC1XX_IRQ_OTG          56

#define S5PC1XX_IRQ_MMC0         58
#define S5PC1XX_IRQ_MMC1         59
#define S5PC1XX_IRQ_MMC2         60
#define S5PC1XX_IRQ_MMC3         98

#define S5PC1XX_IRQ_I2C_0        46
#define S5PC1XX_IRQ_I2C_2        51
#define S5PC1XX_IRQ_I2C_PHY      52
#define S5PC1XX_IRQ_I2C_DDC      77

#define S5PC1XX_IRQ_JPEG         72

#define S5PC1XX_IRQ_I2S0         80
#define S5PC1XX_IRQ_I2S1         81
#define S5PC1XX_IRQ_I2S_V5       S5PC1XX_IRQ_I2S0

#define S5PC1XX_IRQ_AC97         83
#define S5PC1XX_IRQ_PCM0         84
#define S5PC1XX_IRQ_PCM1         85
#define S5PC1XX_IRQ_SPDIF        86
#define S5PC1XX_IRQ_ADC0         87
#define S5PC1XX_IRQ_PENDN0       88
#define S5PC1XX_IRQ_KEYPAD       89
#define S5PC1XX_IRQ_PCM2         93

#define S5PC1XX_IRQ_LCD0         64
#define S5PC1XX_IRQ_LCD1         65
#define S5PC1XX_IRQ_LCD2         66

#define S5PC1XX_IRQ_ADC1         105
#define S5PC1XX_IRQ_PENDN1       106

#define S5PC1XX_DMAMEM_BASE      0xFA200000
#define S5PC1XX_DMA0_BASE        0xE0900000
#define S5PC1XX_DMA1_BASE        0xE0A00000

#define S5PC1XX_GPIO_BASE        0xE0200000
#define S5PC1XX_PWM_BASE         0xE2500000
#define S5PC1XX_ST_BASE          0xE2600000
#define S5PC1XX_WDT_BASE         0xE2700000
#define S5PC1XX_RTC_BASE         0xE2800000

#define S5PC1XX_UART_BASE        0xE2900000
#define S5PC1XX_UART_SHIFT       0x00000400

#define S5PC1XX_PCM0_BASE        0xE2300000
#define S5PC1XX_PCM1_BASE        0xE1200000
#define S5PC1XX_PCM2_BASE        0xE2B00000

#define S5PC1XX_SPDIF_BASE       0xE1100000

#define S5PC1XX_SROMC_BASE       0xE8000000

#define S5PC1XX_I2S1_BASE        0xE2100000
#define S5PC1XX_I2S2_BASE        0xE2A00000

#define S5PC1XX_I2C_0_BASE       0xE1800000
#define S5PC1XX_I2C_2_BASE       0xE1A00000
#define S5PC1XX_I2C_PHY_BASE     0xFA900000
#define S5PC1XX_I2C_DDC_BASE     0xFAB00000

#define S5PC1XX_USB_EHCI_BASE    0xEC200000
#define S5PC1XX_USB_OHCI_BASE    0xEC300000

#define S5PC1XX_SROMC_MAX_BANK_SIZE  0x08000000

#define S5PC1XX_PMU_BASE         0xE0108000
#define S5PC1XX_CMU_BASE         0xE0100000

#define S5PC1XX_KEYPAD_BASE      0xE1600000
#define S5PC1XX_TSADC0_BASE      0xE1700000
#define S5PC1XX_TSADC1_BASE      0xE1701000

#define S5PC1XX_I2S2_V5_BASE     0xEEE30000

#define S5PC1XX_HSMMC0_BASE      0xEB000000
#define S5PC1XX_HSMMC1_BASE      0xEB100000
#define S5PC1XX_HSMMC2_BASE      0xEB200000
#define S5PC1XX_HSMMC3_BASE      0xEB300000

#define S5PC1XX_IRQ_ONEDRAM_INT_AP      11

#define S5PC1XX_USB_NUM_PORTS    1
#define S5PC1XX_SROMC_NUM_BANKS  6

#define S5PC1XX_QT602240_ADDR    0x4A
#define S5PC1XX_QT602240_IRQ     GPIOINT_IRQ(GPJ0, 5)

#define S5PC1XX_WM8994_ADDR      0x1A

#define S5PC1XX_MCS5000_ADDR     0x20
#define S5PC1XX_MCS5000_IRQ      GPIOINT_IRQ(GPJ2, 7)

#define S5PC1XX_MAX17040_ADDR    0x36
#define S5PC1XX_MAX8998_ADDR     0x66
#define S5PC1XX_MAX8998_RTC_ADDR 0x06

#define S5PC1XX_AK8973_ADDR      0x1C
#define S5PC1XX_AK8973_IRQ       GPIOEXT_IRQ(29)

/* pl330 peripheral numbers */
#define PL330_PERIPH_NUM_I2S1    10
#define PL330_PERIPH_NUM_I2S2    11


static const uint32_t dmamem_cfg[] =
{ 0x003E1071, 0x00000075, 0x0, 0xFFFFFFFF, 0x00000003, 0x01F73733 };

static const uint32_t dma0_cfg[] =
{ 0x003FF075, 0x00000074, 0x0, 0xFFFFFFFF, 0xFFFFFFFF, 0x00773732 };

static const uint32_t dma1_cfg[] =
{ 0x003FF075, 0x00000074, 0x0, 0xFFFFFFFF, 0xFFFFFFFF, 0x00773732 };

static const uint8_t chipid_and_omr[] =
{ 0x00, 0x02, 0x11, 0x43, 0x09, 0x00, 0x00, 0x00 }; /* Little-endian */


/* Find IRQ by it's number */
static inline qemu_irq s5pc1xx_get_irq(struct s5pc1xx_state_s *s, int n)
{
    return s->irq[n / S5PC1XX_VIC_SIZE][n % S5PC1XX_VIC_SIZE];
}

//#include "s5pc1xx_debug.c"

struct i2c
{
    i2c_bus *intrf0;
    i2c_bus *intrf2;
    i2c_bus *intrfDDC;
    i2c_bus *intrfPHY;

    i2c_bus *gpio3;
    i2c_bus *gpio4;
    i2c_bus *gpio5;
    i2c_bus *gpio10;
};

/* Helper structure and two functions. Allow you to connect several irq
   sources to one irq input using logical OR. */
struct irq_multiplexer_s {
    qemu_irq parent;
    int count;
    uint8_t mask[];
};

static void irq_mult_handler(void *opaque, int irq, int level)
{
    uint8_t old_level;
    struct irq_multiplexer_s *s = (struct irq_multiplexer_s *)opaque;

    old_level = (s->mask[irq >> 3] >> (irq & 7)) & 1;
    if (!old_level && level) {
        s->count++;
        s->mask[irq >> 3] |= (1 << (irq & 7));
        if (s->count == 1) {
            qemu_irq_raise(s->parent);
        }
    }
    if (old_level && !level) {
        s->count--;
        s->mask[irq >> 3] &= ~(1 << (irq & 7));
        if (s->count == 0) {
            qemu_irq_lower(s->parent);
        }
    }
}

static qemu_irq *irq_multiplexer(qemu_irq irq, int n)
{
    struct irq_multiplexer_s *s =
        qemu_mallocz(sizeof(struct irq_multiplexer_s) +
                ((n + 7) << 3) * sizeof(uint8_t));
    s->parent = irq;
    return qemu_allocate_irqs(irq_mult_handler, s, n);
}

static struct arm_boot_info s5pc110x_binfo = {
    .loader_start = 0x0,
    //.board_id = 0xA74, /* Aquila */
    .board_id = 3084, /* Aquila */
    .revision = 0x803, /* Aquila LiMo Universal */
};

static void s5pc110_reset(void *opaque)
{
    struct s5pc1xx_state_s *s = (struct s5pc1xx_state_s *)opaque;

    s->env->regs[15] = 0x30000000;
}

/* Initialize and start system */
static void s5pc110_init(ram_addr_t ram_size, const char *boot_device,
                         const char *kernel_fname, const char *kernel_cmdline,
                         const char *initrd_name, const char *cpu_model)
{
    ram_addr_t sdram_off, isrom_off, isram_off, chipid_off;
    qemu_irq *cpu_irq;
    DeviceState *dev, *dev_prev, *gpio, *dmamem, *dma0, *dma1;
    CharDriverState *chr2, *chr3;
    qemu_irq *dma_irqs, dma_stop_irq1, dma_stop_irq2;
    struct i2c *i2c = (struct i2c *)qemu_mallocz(sizeof(*i2c));
    struct s5pc1xx_state_s *s =
        (struct s5pc1xx_state_s *)qemu_mallocz(sizeof(*s));
    int i, j;

    /* Core */
    s->mpu_model = s5pc110;
    s->env = cpu_init("cortex-a8");
    if (!s->env)
        hw_error("Unable to find CPU definition (%s)\n", "cortex-a8");

    /* Chip-ID and OMR */
    chipid_off = qemu_ram_alloc(sizeof(chipid_and_omr));
    cpu_register_physical_memory(S5PC1XX_SFRS, sizeof(chipid_and_omr),
                                 chipid_off | IO_MEM_ROM);
    cpu_physical_memory_write_rom(S5PC1XX_SFRS, chipid_and_omr,
                                  sizeof(chipid_and_omr));

    /* Memory */
    /* Main RAM */
    if (ram_size > S5PC1XX_DRAM_MAX_SIZE) {
        hw_error("Too much memory was requested for this board "
                 "(requested: %lu MB, max: %u MB)\n",
                 ram_size / (1024 * 1024),
                 S5PC1XX_DRAM_MAX_SIZE / (1024 * 1024));
    }
    s->sdram_size = ram_size;
    sdram_off = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(S5PC1XX_DRAM1_BASE, ram_size,
                                 sdram_off | IO_MEM_RAM);
    /* Also map the same memory to boot area.  */
    cpu_register_physical_memory(S5PC1XX_BOOT_BASE, ram_size,
                                 sdram_off | IO_MEM_RAM);

    /* Internal SROM */
    s->isrom_size = S5PC1XX_ISROM_SIZE;
    isrom_off = qemu_ram_alloc(s->isrom_size);
    cpu_register_physical_memory(S5PC1XX_IROM_BASE, s->isrom_size,
                                 isrom_off | IO_MEM_ROM);

    /* Internal SRAM */
    s->isram_size = S5PC1XX_ISRAM_SIZE;
    isram_off = qemu_ram_alloc(s->isram_size);
    cpu_register_physical_memory(S5PC1XX_IRAM_BASE, s->isram_size,
                                 isram_off | IO_MEM_RAM);

#ifndef LOWMEMORY
    /* SROMC banks */
    for (i = 0; i < S5PC1XX_SROMC_NUM_BANKS; i++) {
        ram_addr_t srom_bank_off = qemu_ram_alloc(S5PC1XX_SROMC_MAX_BANK_SIZE);
        cpu_register_physical_memory(S5PC1XX_SROMC_B0_BASE +
                                         i * S5PC1XX_SROMC_MAX_BANK_SIZE,
                                     S5PC1XX_SROMC_MAX_BANK_SIZE,
                                     srom_bank_off | IO_MEM_ROM);
    }
#endif

    /* System devices */
    /* Interrupts */
    cpu_irq = arm_pic_init_cpu(s->env);
    s->irq =
        qemu_mallocz(S5PC1XX_VIC_N * sizeof(qemu_irq *));
    dev = sysbus_create_varargs("pl192", S5PC1XX_VIC_BASE,
                                cpu_irq[ARM_PIC_CPU_IRQ],
                                cpu_irq[ARM_PIC_CPU_FIQ], NULL);
    s->irq[0] = qemu_mallocz(S5PC1XX_VIC_SIZE * sizeof(qemu_irq));
    for (i = 0; i < S5PC1XX_VIC_SIZE; i++)
        s->irq[0][i] = qdev_get_gpio_in(dev, i);
    for (j = 1; j < S5PC1XX_VIC_N; j++) {
        dev_prev = dev;
        dev =
            sysbus_create_varargs("pl192",
                                  S5PC1XX_VIC_BASE + S5PC1XX_VIC_SHIFT * j,
                                  NULL);
        s->irq[j] = qemu_mallocz(S5PC1XX_VIC_SIZE * sizeof(qemu_irq));
        for (i = 0; i < S5PC1XX_VIC_SIZE; i++)
            s->irq[j][i] = qdev_get_gpio_in(dev, i);
        pl192_chain(sysbus_from_qdev(dev_prev), sysbus_from_qdev(dev));
    }

    /* GPIO */
    gpio = sysbus_create_varargs("s5pc1xx,gpio", S5PC1XX_GPIO_BASE,
                                 s5pc1xx_get_irq(s, S5PC1XX_IRQ_GPIOINT),
                                 s5pc1xx_get_irq(s, S5PC1XX_IRQ_EXTEND),
                                 NULL);

    /* DMA Controller */
    dma_irqs = irq_multiplexer(s5pc1xx_get_irq(s, S5PC1XX_IRQ_DMAMEM),
                               ((dmamem_cfg[0] >> 17) & 0x1f) + 2);
    dmamem =
        pl330_init(S5PC1XX_DMAMEM_BASE, dmamem_cfg, dma_irqs + 1, *dma_irqs);
    dma_irqs = irq_multiplexer(s5pc1xx_get_irq(s, S5PC1XX_IRQ_DMA0),
                               ((dma0_cfg[0] >> 17) & 0x1f) + 2);
    dma0 = pl330_init(S5PC1XX_DMA0_BASE, dma0_cfg, dma_irqs + 1, *dma_irqs);
    dma_irqs = irq_multiplexer(s5pc1xx_get_irq(s, S5PC1XX_IRQ_DMA1),
                               ((dma1_cfg[0] >> 17) & 0x1f) + 2);
    dma1 = pl330_init(S5PC1XX_DMA1_BASE, dma1_cfg, dma_irqs + 1, *dma_irqs);

    /* I2C */
    dev = sysbus_create_simple("s5pc1xx,i2c", S5PC1XX_I2C_0_BASE,
                               s5pc1xx_get_irq(s, S5PC1XX_IRQ_I2C_0));
    i2c->intrf0 = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    dev = sysbus_create_simple("s5pc1xx,i2c", S5PC1XX_I2C_2_BASE,
                               s5pc1xx_get_irq(s, S5PC1XX_IRQ_I2C_2));
    i2c->intrf2 = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    dev = sysbus_create_simple("s5pc1xx,i2c", S5PC1XX_I2C_PHY_BASE,
                               s5pc1xx_get_irq(s, S5PC1XX_IRQ_I2C_PHY));
    i2c->intrfPHY = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    dev = sysbus_create_simple("s5pc1xx,i2c", S5PC1XX_I2C_DDC_BASE,
                               s5pc1xx_get_irq(s, S5PC1XX_IRQ_I2C_DDC));
    i2c->intrfDDC = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    /* GPIO I2C */
    dev = s5pc1xx_i2c_gpio_init(3);
    i2c->gpio3 = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    dev = s5pc1xx_i2c_gpio_init(4);
    i2c->gpio4 = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    dev = s5pc1xx_i2c_gpio_init(5);
    i2c->gpio5 = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
    dev = s5pc1xx_i2c_gpio_init(10);
    i2c->gpio10 = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    /* SPI */
    sysbus_create_simple("s5pc1xx,spi",
                         S5PC1XX_SPI_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_SPI0));
    sysbus_create_simple("s5pc1xx,spi",
                         S5PC1XX_SPI_BASE + S5PC1XX_SPI_SHIFT,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_SPI1));
    sysbus_create_simple("s5pc1xx,spi",
                         S5PC1XX_SPI_BASE + S5PC1XX_SPI_SHIFT * 2,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_SPI2));

    /* PMU */
    sysbus_create_simple("s5pc1xx,pmu", S5PC1XX_PMU_BASE, NULL);

    /* MAX17040 */
    max17040_init(i2c->gpio3, S5PC1XX_MAX17040_ADDR);

    /* MAX8998 */
    max8998_init(i2c->gpio4, S5PC1XX_MAX8998_ADDR);

    /* MAX8998 RTC */
    max8998_rtc_init(i2c->gpio4, S5PC1XX_MAX8998_RTC_ADDR);

    /* USB */
    if (usb_enabled) {
        sysbus_create_simple("sysbus-ohci", S5PC1XX_USB_OHCI_BASE,
                             s5pc1xx_get_irq(s, S5PC1XX_IRQ_UHOST));
        sysbus_create_simple("usb-ehci", S5PC1XX_USB_EHCI_BASE,
                             s5pc1xx_get_irq(s, S5PC1XX_IRQ_UHOST));
    }

    /* Clock devices */
    /* CMU */
    sysbus_create_simple("s5pc1xx,clk", S5PC1XX_CMU_BASE, NULL);

    /* RTC */
    sysbus_create_varargs("s5pc1xx,rtc", S5PC1XX_RTC_BASE,
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_RTC_ALARM),
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_RTC_TICK), NULL);

    /* PWM */
    sysbus_create_varargs("s5pc1xx,pwm", S5PC1XX_PWM_BASE,
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_TIMER0),
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_TIMER1),
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_TIMER2),
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_TIMER3),
                          s5pc1xx_get_irq(s, S5PC1XX_IRQ_TIMER4), NULL);

    /* WDT */
    sysbus_create_simple("s5pc1xx,wdt", S5PC1XX_WDT_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_WDT));

    /* ST */
    sysbus_create_simple("s5pc1xx,st", S5PC1XX_ST_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_SYS_TIMER));

    /* Storage devices */
    /* NAND */
    sysbus_create_simple("s5pc1xx,nand", S5PC1XX_NAND_BASE, NULL);

    /* OneNAND */
    s5pc1xx_onenand_init(S5PC1XX_ONENAND_BASE);

    /* SROMC */
    s5pc1xx_srom_init(S5PC1XX_SROMC_BASE, S5PC1XX_SROMC_NUM_BANKS);

    /* SD/MMC */
    sysbus_create_simple("s5pc1xx,mmc", S5PC1XX_HSMMC0_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_MMC0));

    sysbus_create_simple("s5pc1xx,mmc", S5PC1XX_HSMMC1_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_MMC1));

    sysbus_create_simple("s5pc1xx,mmc", S5PC1XX_HSMMC2_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_MMC2));

    sysbus_create_simple("s5pc1xx,mmc", S5PC1XX_HSMMC3_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_MMC3));

    /* Input/Output devices */
    /* UART */
    if (serial_hds[0]) {
        chr2 = serial_hds[0];
    } else {
        chr2 = qemu_chr_open("s5pc1xx,uart", "vc:800x600", NULL);
    }
    chr3 =
        qemu_chr_open("AT_socket", "tcp:localhost:7776,server,nowait", NULL);
    s5pc1xx_uart_init(S5PC1XX_UART_BASE, 0, 256,
                      s5pc1xx_get_irq(s, S5PC1XX_IRQ_UART0), chr2);//lfeng,change Null->chr2
    s5pc1xx_uart_init(S5PC1XX_UART_BASE + S5PC1XX_UART_SHIFT, 1, 64,
                      s5pc1xx_get_irq(s, S5PC1XX_IRQ_UART1), NULL);
    s5pc1xx_uart_init(S5PC1XX_UART_BASE + S5PC1XX_UART_SHIFT * 2, 2, 16,
                      s5pc1xx_get_irq(s, S5PC1XX_IRQ_UART2), NULL);//lfeng,change chr2->NUlL
    s5pc1xx_uart_init(S5PC1XX_UART_BASE + S5PC1XX_UART_SHIFT * 3, 3, 16,
                      s5pc1xx_get_irq(s, S5PC1XX_IRQ_UART3), chr3);

    /* S3C Touchscreen */
    s5pc1xx_tsadc_init(S5PC1XX_TSADC0_BASE,
                       s5pc1xx_get_irq(s, S5PC1XX_IRQ_ADC0),
                       s5pc1xx_get_irq(s, S5PC1XX_IRQ_PENDN0),
                       1, 12, 0, 120, 0, 200);
    s5pc1xx_tsadc_init(S5PC1XX_TSADC1_BASE,
                       s5pc1xx_get_irq(s, S5PC1XX_IRQ_ADC1),
                       s5pc1xx_get_irq(s, S5PC1XX_IRQ_PENDN1),
                       1, 12, 0, 120, 0, 200);

    /* QT602240 Touchscreen */
    dev = i2c_create_slave(i2c->intrf2, "qt602240", S5PC1XX_QT602240_ADDR);
    qdev_connect_gpio_out(dev, 0,
                          s5pc1xx_gpoint_irq(gpio, S5PC1XX_QT602240_IRQ));

    /* FIXME: QEMU can support several mice and allows user to select one as
     * acrive by 'mouse_set' monitor command; keyboard for some reason works
     * in another way and there only may be one keyboard initialized */
#if 0
    /* Keypad */
    sysbus_create_simple("s5pc1xx,keyif", S5PC1XX_KEYPAD_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_KEYPAD));
#else
    /* MSC5000 Touchkeys */
    dev = i2c_create_slave(i2c->gpio10, "mcs5000,universal",
                           S5PC1XX_MCS5000_ADDR);
    qdev_connect_gpio_out(dev, 0,
                          s5pc1xx_gpoint_irq(gpio, S5PC1XX_MCS5000_IRQ));
#endif

    /* USB-OTG */
    s5pc1xx_usb_otg_init(&nd_table[0],
                         S5PC1XX_USB_OTG_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_OTG));

    /* OneDRAM */
    s5pc1xx_onedram_init("s5pc1xx,onedram,aquila,xmm", S5PC1XX_ONEDRAM_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_ONEDRAM_INT_AP));

    /* AK8973 Compass Emulation */
    dev = i2c_create_slave(i2c->intrfDDC, "ak8973", S5PC1XX_AK8973_ADDR);
    qdev_connect_gpio_out(dev, 0, s5pc1xx_gpoint_irq(gpio, S5PC1XX_AK8973_IRQ));

    /* Audio devices and interfaces */
    /* SPDIF */
    sysbus_create_simple("s5pc1xx,spdif", S5PC1XX_SPDIF_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_SPDIF));

    /* AC97 */
    sysbus_create_simple("s5pc1xx,ac97", S5PC1XX_AC97_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_AC97));

    /* PCM */
    s5pc1xx_pcm_init(S5PC1XX_PCM0_BASE, 0,
                     s5pc1xx_get_irq(s, S5PC1XX_IRQ_PCM0));
    s5pc1xx_pcm_init(S5PC1XX_PCM1_BASE, 1,
                     s5pc1xx_get_irq(s, S5PC1XX_IRQ_PCM1));
    s5pc1xx_pcm_init(S5PC1XX_PCM2_BASE, 2,
                     s5pc1xx_get_irq(s, S5PC1XX_IRQ_PCM2));

    /* WM8994 */
    dev = i2c_create_slave(i2c->gpio5, "wm8994", S5PC1XX_WM8994_ADDR);

    /* I2S */
    dma_stop_irq1 = qdev_get_gpio_in(dma0, PL330_PERIPH_NUM_I2S1);
    dma_stop_irq2 = qdev_get_gpio_in(dma1, PL330_PERIPH_NUM_I2S2);
    s5pc1xx_i2s_init(S5PC1XX_I2S2_V5_BASE,
                     s5pc1xx_get_irq(s, S5PC1XX_IRQ_I2S_V5), dev /* WM8994 */,
                     dma_stop_irq1, dma_stop_irq2);

    /* Video devices */
    /* LCD */
    sysbus_create_simple("s5pc1xx,lcd", S5PC1XX_LCD_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_LCD0));

    /* JPEG */
    sysbus_create_simple("s5pc1xx,jpeg", S5PC1XX_JPEG_BASE,
                         s5pc1xx_get_irq(s, S5PC1XX_IRQ_JPEG));

    /* Load the kernel */
    s5pc110x_binfo.ram_size = ram_size;
    s5pc110x_binfo.kernel_filename = kernel_fname;
    s5pc110x_binfo.initrd_filename = initrd_name;
    s5pc110x_binfo.loader_start = 0x30000000;
    if (kernel_cmdline)
        s5pc110x_binfo.kernel_cmdline = kernel_cmdline;
    else
        s5pc110x_binfo.kernel_cmdline =
            "root=/dev/mtdblock2 rootfstype=cramfs init=/linuxrc "
            "console=ttySAC2,115200 mem=128M debug";

    arm_load_kernel(s->env, &s5pc110x_binfo);

    qemu_register_reset(s5pc110_reset, s);
    s5pc110_reset(s);

    /* Don't hide cursor since we are using touchscreen */
    cursor_hide = 0;
}

static QEMUMachine s5pc110_machine = {
    .name = "s5pc110",
    .desc = "Samsung S5PC110-base board",
    .init = s5pc110_init,
};

static void s5pc1xx_machine_init(void)
{
    qemu_register_machine(&s5pc110_machine);
}

machine_init(s5pc1xx_machine_init);
