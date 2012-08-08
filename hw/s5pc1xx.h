/*
 * Samsung S5PC1XX-based board emulation.
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 *                Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *                Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 */

#ifndef hw_s5pc1xx_h
#define hw_s5pc1xx_h     "s5pc1xx.h"

#include "qemu-common.h"


typedef struct Clk *S5pc1xxClk;
typedef void GPIOWriteMemoryFunc(void *opaque, int io_index, uint32_t value);
typedef uint32_t GPIOReadMemoryFunc(void *opaque, int io_index);

struct s5pc1xx_state_s {
    /* Model */
    enum s5pc1xx_model {
        s5pc110
    } mpu_model;

    /* CPU Core */
    CPUState *env;

    /* Interrupts */
    qemu_irq **irq;

    /* Amount of different memory types */
    ram_addr_t sdram_size;
    ram_addr_t isrom_size;
    ram_addr_t isram_size;
};


/* s5pc1xx_gpio.c */
void s5pc1xx_gpio_register_io_memory(int io_index, int instance,
                                     GPIOReadMemoryFunc *mem_read,
                                     GPIOWriteMemoryFunc *mem_write,
                                     GPIOWriteMemoryFunc *mem_conf,
                                     void *opaque);

uint32_t s5pc1xx_empty_gpio_read(void *opaque, int io_index);
void s5pc1xx_empty_gpio_write(void *opaque, int io_index, uint32_t value);

/* s5pc1xx_i2c_gpio.c */
DeviceState *s5pc1xx_i2c_gpio_init(int instance);

/* s5pc1xx_clk.c */
S5pc1xxClk s5pc1xx_findclk(const char *name);
int64_t s5pc1xx_clk_getrate(S5pc1xxClk clk);

/* s5pc1xx_pmu.c */
DeviceState *max17040_init(i2c_bus *bus, int addr);
DeviceState *max8998_init(i2c_bus *bus, int addr);
DeviceState *max8998_rtc_init(i2c_bus *bus, int addr);

/* s5pc1xx_srom.c */
DeviceState *s5pc1xx_srom_init(target_phys_addr_t base, int num_banks);

/* s5pc1xx_gpio.c */
qemu_irq s5pc1xx_gpoint_irq(DeviceState *d, int irq_num);

/* s5pc1xx_uart.c */
DeviceState *s5pc1xx_uart_init(target_phys_addr_t base, int instance,
                               int queue_size, qemu_irq irq,
                               CharDriverState *chr);

/* s5pc1xx_onenand.c */
DeviceState *s5pc1xx_onenand_init(target_phys_addr_t base);

/* s5pc1xx_tsadc.c */
DeviceState *s5pc1xx_tsadc_init(target_phys_addr_t base, qemu_irq irq_adc,
                                qemu_irq irq_pennd, int new, int resolution,
                                int minx, int maxx, int miny, int maxy);

/* s5pc1xx_i2s.c */
DeviceState *s5pc1xx_i2s_init(target_phys_addr_t base, qemu_irq irq,
                              DeviceState *wm8994_dev, qemu_irq dma_irq1,
                              qemu_irq dma_irq2);

/* s5pc1xx_pcm.c */
DeviceState *s5pc1xx_pcm_init(target_phys_addr_t base,
                              int instance,
                              qemu_irq irq);

/* s5pc1xx_onedram.c */
DeviceState *s5pc1xx_onedram_init(const char *name, target_phys_addr_t base,
                                  qemu_irq irq_ap);

/* usb-ohci.c */
void usb_ohci_init_pxa(target_phys_addr_t base, int num_ports, int devfn,
                       qemu_irq irq);

void s5pc1xx_usb_otg_init(NICInfo *nd, target_phys_addr_t base, qemu_irq irq);

#endif /* hw_s5pc1xx_h */
