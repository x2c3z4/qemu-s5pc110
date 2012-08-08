/*
 * S5PC1XX SPI Emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Dmitry Zhurikhin <zhur@ispras.ru>
 */

#include "sysbus.h"


#define S5PC1XX_WDT_REG_MEM_SIZE 0x30


typedef struct S5pc1xxSPIState {
    SysBusDevice busdev;

    uint32_t ch_cfg;
    uint32_t clk_cfg;
    uint32_t mode_cfg;
    uint32_t cs_reg;
    uint32_t spi_int_en;
    uint32_t spi_status;
    uint32_t spi_tx_dat;
    uint32_t spi_rx_dat;
    uint32_t packet_cnt_reg;
    uint32_t pending_clr_reg;
    uint32_t swap_cfg;
    uint32_t fb_clk_sel;

    qemu_irq irq;
} S5pc1xxSPIState;


static uint32_t s5pc1xx_spi_mm_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxSPIState *s = (S5pc1xxSPIState *)opaque;

    switch (offset) {
    case 0x00:
        return s->ch_cfg;
    case 0x04:
        return s->clk_cfg;
    case 0x08:
        return s->mode_cfg;
    case 0x0C:
        return s->cs_reg;
    case 0x10:
        return s->spi_int_en;
    case 0x14:
        return s->spi_status;
    case 0x18:
        return s->spi_tx_dat;
    case 0x1C:
        return s->spi_rx_dat;
    case 0x20:
        return s->packet_cnt_reg;
    case 0x24:
        return s->pending_clr_reg;
    case 0x28:
        return s->swap_cfg;
    case 0x2C:
        return s->fb_clk_sel;
    default:
        hw_error("s5pc1x_spi: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static void s5pc1xx_spi_mm_write(void *opaque, target_phys_addr_t offset,
                                 uint32_t val)
{
    S5pc1xxSPIState *s = (S5pc1xxSPIState *)opaque;

    switch (offset) {
    case 0x00:
        s->ch_cfg = val;
        break;
    case 0x04:
        s->clk_cfg = val;
        break;
    case 0x08:
        s->mode_cfg = val;
        break;
    case 0x0C:
        s->cs_reg = val;
        break;
    case 0x10:
        s->spi_int_en = val;
        break;
    case 0x14:
        s->spi_status = val;
        break;
    case 0x18:
        s->spi_tx_dat = val;
        break;
    case 0x1C:
        s->spi_rx_dat = val;
        break;
    case 0x20:
        s->packet_cnt_reg = val;
        break;
    case 0x24:
        s->pending_clr_reg = val;
        break;
    case 0x28:
        s->swap_cfg = val;
        break;
    case 0x2C:
        s->fb_clk_sel = val;
        break;
    default:
        hw_error("s5pc1x_spi: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

CPUReadMemoryFunc * const s5pc1xx_spi_readfn[] = {
    s5pc1xx_spi_mm_read,
    s5pc1xx_spi_mm_read,
    s5pc1xx_spi_mm_read
};

CPUWriteMemoryFunc * const s5pc1xx_spi_writefn[] = {
    s5pc1xx_spi_mm_write,
    s5pc1xx_spi_mm_write,
    s5pc1xx_spi_mm_write
};

static void s5pc1xx_spi_reset(S5pc1xxSPIState *s)
{
    s->ch_cfg = 0;
    s->clk_cfg = 0;
    s->mode_cfg = 0;
    s->cs_reg = 1;
    s->spi_int_en = 0;
    s->spi_status = 0;
    s->spi_tx_dat = 0;
    s->spi_rx_dat = 0;
    s->packet_cnt_reg = 0;
    s->pending_clr_reg = 0;
    s->swap_cfg = 0;
    s->fb_clk_sel = 0;
}

static int s5pc1xx_spi_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxSPIState *s = FROM_SYSBUS(S5pc1xxSPIState, dev);

    sysbus_init_irq(dev, &s->irq);
    iomemtype =
        cpu_register_io_memory(s5pc1xx_spi_readfn, s5pc1xx_spi_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_WDT_REG_MEM_SIZE, iomemtype);

    s5pc1xx_spi_reset(s);

    return 0;
}

static void s5pc1xx_spi_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,spi", sizeof(S5pc1xxSPIState),
                        s5pc1xx_spi_init);
}

device_init(s5pc1xx_spi_register_devices)
