/*
 * S5PC1XX OneNAND controller.
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 */

#include "sysbus.h"
#include "hw.h"
#include "flash.h"
#include "s5pc1xx.h"


/* OneNAND Device Description:
   EC - Samsung, 50 - device properties, 2E - version */
#define ONENAND_DEVICE_ID  0xEC502E

/* Device is mux */
#define IS_MUX_TYPE 1

#define ONENAND_CONTR_REG_BASE  0x00600000
#define ONENAND_CONTR_REGS_SIZE 0x0000106C

#define ONENAND_BUFFRES_SIZE    0x00012000

#define S5PC110_DMA_TRANS_STATUS_TD (0x1 << 18)
#define S5PC110_DMA_TRANS_STATUS_TB (0x1 << 17)
#define S5PC110_DMA_TRANS_STATUS_TE (0x1 << 16)


typedef struct S5pc1xxOneNANDState {
    SysBusDevice busdev;
    target_phys_addr_t base;

    /* OneNAND Interface Control register */
    uint32_t onenand_if_clrt;
    /* OneNAND Interface Async. Timing Control register */
    uint32_t onenand_async_timing;
    /* DMA Source Address Register */
    uint32_t dma_src_addr;
    /* DMA Source Configuration Register */
    uint32_t dma_src_cfg;
    /* DMA Destination Address Register */
    uint32_t dma_dst_addr;
    /* DMA Destination Configuration Register */
    uint32_t dma_dst_cfg;
    /* DMA Transfer Size Register */
    uint32_t dma_trans_size;
    /* DMA Transfer Direction Register */
    uint32_t dma_trans_dir;
    /* Sequencer Start Address Offset Register */
    uint32_t sqc_sao;
    /* Sequencer Register Control Register */
    uint32_t sqc_reg_ctrl;
    /* Sequencer Breakpoint Address Offset#0 Register */
    uint32_t sqc_brpao0;
    /* Sequencer Breakpoint Address Offset#1 Register */
    uint32_t sqc_brpao1;
    /* Interrupt Controller Sequencer Mask Register */
    uint32_t intc_sqc_mask;
    /* Interrupt Controller DMA Mask Register */
    uint32_t intc_dma_mask;
    /* Interrupt Controller OneNAND Mask Register */
    uint32_t intc_onenand_mask;
} S5pc1xxOneNANDState;


static uint32_t s5pc1xx_onenand_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxOneNANDState *s = (S5pc1xxOneNANDState *)opaque;
    /* for compatibility with documentation */
    int test_offset = offset + ONENAND_CONTR_REG_BASE;

    switch (test_offset) {
    case 0x00600100: /*RW OneNAND Interface Control register*/
        return s->onenand_if_clrt;

    case 0x00600400: /*RW DMA Source Address Register*/
        return s->dma_src_addr;
    case 0x00600404: /*RW DMA Source Configuration Register*/
        return s->dma_src_cfg;
    case 0x00600408: /*RW DMA Destination Address Register*/
        return s->dma_dst_addr;
    case 0x0060040C: /*RW DMA Destination Configuration Register*/
        return s->dma_dst_cfg;
    case 0x00600414: /*RW DMA Transfer Size Register*/
        return s->dma_trans_size;
    case 0x00600418: /*WO DMA Transfer Command Register*/
        return 0;
    case 0x0060041C: /*RO DMA Transfer Status Register*/
        return S5PC110_DMA_TRANS_STATUS_TD;
    case 0x00600420: /*RW DMA Transfer Direction Register*/
        return s->dma_trans_dir;
#if 0 /*TODO: implement support for all of these registers*/
    case 0x00600104: /*WO OneNAND Interface Command register*/
        return 0;
    case 0x00600108: /*RW OneNAND Interface Async. Timing*/
        return s->onenand_async_timing;
    case 0x0060010C: /*RO OneNAND Interface Status Register*/
        return 0x00FC0000;
    case 0x00600600: /*RW Sequencer Start Address Offset Register*/
        return s->sqc_sao;
    case 0x00600608: /*WO Sequencer Command Register*/
        return 0;
    case 0x0060060C: /*RO Sequencer Status Register*/
    case 0x00600610: /*RO Sequencer Current Address Offset*/
    case 0x00600614: /*RW Sequencer Register Control Register*/
        return s->sqc_reg_ctrl;
    case 0x00600618: /*RO Sequencer Register Value Register*/
    case 0x00600620: /*RW Sequencer Breakpoint Address Offset#0*/
        return s->sqc_brpao0;
    case 0x00600624: /*RW Sequencer Breakpoint Address Offset#1*/
        return s->sqc_brpao1;
    case 0x00601000: /*WO Interrupt Controller Sequencer Clear*/
        return 0;
    case 0x00601004: /*WO Interrupt Controller DMA Clear Register*/
        return 0;
    case 0x00601008: /*WO Interrupt Controller OneNAND Clear*/
        return 0;
    case 0x00601020: /*RW Interrupt Controller Sequencer Mask*/
        return s->intc_sqc_mask;
    case 0x00601024: /*RW Interrupt Controller DMA Mask Register*/
        return s->intc_dma_mask;
    case 0x00601028: /*RW Interrupt Controller OneNAND Mask*/
        return s->intc_onenand_mask;
    case 0x00601040: /*RO Interrupt Controller Sequencer Pending*/
    case 0x00601044: /*RO Interrupt Controller DMA Pending*/
    case 0x00601048: /*RO Interrupt Controller OneNAND Pending*/
    case 0x00601060: /*RO Interrupt Controller Sequencer Status*/
    case 0x00601064: /*RO Interrupt Controller DMA Status Register*/
    case 0x00601068: /*RO Interrupt Controller OneNAND Status*/
#endif
    default:
        hw_error("s5pc1xx_onenand: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static int is_inside_onenand(uint32_t offset, target_phys_addr_t base, uint32_t size)
{
    return offset >= base &&
           offset < base + ONENAND_BUFFRES_SIZE &&
           offset + size < base + ONENAND_BUFFRES_SIZE;
}

static void s5pc1xx_onenand_write(void *opaque, target_phys_addr_t offset,
                                  uint32_t val)
{
    S5pc1xxOneNANDState *s = (S5pc1xxOneNANDState *)opaque;
    uint8_t *buf = NULL;
    /* for compatibility with documentation */
    int test_offset = offset + ONENAND_CONTR_REG_BASE;

    switch (test_offset) {
    case 0x00600100: /*RW OneNAND Interface Control register*/
        s->onenand_if_clrt = val;
        break;
    case 0x00600400: /*RW DMA Source Address Register*/
        s->dma_src_addr = val;
        break;
    case 0x00600404: /*RW DMA Source Configuration Register*/
        s->dma_src_cfg = val;
        break;
    case 0x00600408: /*RW DMA Destination Address Register*/
        s->dma_dst_addr = val;
        break;
    case 0x0060040C: /*RW DMA Destination Configuration Register*/
        s->dma_dst_cfg = val;
        break;
    case 0x00600414: /*RW DMA Transfer Size Register*/
        s->dma_trans_size = val;
        break;
    case 0x00600418: /*WO DMA Transfer Command Register*/
        if (!is_inside_onenand(s->dma_src_addr, s->base, s->dma_trans_size) &&
            !is_inside_onenand(s->dma_dst_addr, s->base, s->dma_trans_size)) {
            hw_error("s5pc1xx_onenand: invalide memory transfer: src = "
                     TARGET_FMT_plx ", dst = " TARGET_FMT_plx ", size = 0x%X\n",
                     (target_phys_addr_t)s->dma_src_addr,
                     (target_phys_addr_t)s->dma_dst_addr,
                     s->dma_trans_size);
        }

        buf = qemu_malloc(s->dma_trans_size);
        cpu_physical_memory_read (s->dma_src_addr, buf, s->dma_trans_size);
        cpu_physical_memory_write(s->dma_dst_addr, buf, s->dma_trans_size);
        qemu_free(buf);
        break;
    case 0x0060041C: /*RO DMA Transfer Status Register*/
        break;
    case 0x00600420: /*RW DMA Transfer Direction Register*/
        s->dma_trans_dir = val;
        break;
#if 0 /*TODO: implement support for all of these registers*/
    case 0x00600104: /*WO OneNAND Interface Command register*/
        /* TODO see WR bits */
        break;
    case 0x00600108: /*RW OneNAND Interface Async. Timing*/
        s->onenand_async_timing = val;
        break;
    case 0x0060010C: /*RO OneNAND Interface Status Register*/
        break;
    case 0x00600600: /*RW Sequencer Start Address Offset Register*/
        s->sqc_sao = val;
        break;
    case 0x00600608: /*WO Sequencer Command Register*/
        break;
    case 0x0060060C: /*RO Sequencer Status Register*/
        break;
    case 0x00600610: /*RO Sequencer Current Address Offset*/
        break;
    case 0x00600614: /*RW Sequencer Register Control Register*/
        s->sqc_reg_ctrl = val;
        break;
    case 0x00600618: /*RO Sequencer Register Value Register*/
        break;
    case 0x00600620: /*RW Sequencer Breakpoint Address Offset#0*/
        s->sqc_brpao0 = val;
        break;
    case 0x00600624: /*RW Sequencer Breakpoint Address Offset#1*/
        s->sqc_brpao1 = val;
        break;
    case 0x00601000: /*WO Interrupt Controller Sequencer Clear*/
        break;
    case 0x00601004: /*WO Interrupt Controller DMA Clear Register*/
        break;
    case 0x00601008: /*WO Interrupt Controller OneNAND Clear*/
        break;
    case 0x00601020: /*RW Interrupt Controller Sequencer Mask*/
        s->intc_sqc_mask = val;
        break;
    case 0x00601024: /*RW Interrupt Controller DMA Mask Register*/
        s->intc_dma_mask = val;
        break;
    case 0x00601028: /*RW Interrupt Controller OneNAND Mask*/
        s->intc_onenand_mask = val;
        break;
    case 0x00601040: /*RO Interrupt Controller Sequencer Pending*/
        break;
    case 0x00601044: /*RO Interrupt Controller DMA Pending*/
        break;
    case 0x00601048: /*RO Interrupt Controller OneNAND Pending*/
        break;
    case 0x00601060: /*RO Interrupt Controller Sequencer Status*/
        break;
    case 0x00601064: /*RO Interrupt Controller DMA Status Register*/
        break;
    case 0x00601068: /*RO Interrupt Controller OneNAND Status*/
        break;
#endif
    default:
        hw_error("s5pc1xx_onenand: bad write offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const onenand_config_reg_mm_read[] = {
    s5pc1xx_onenand_read,
    s5pc1xx_onenand_read,
    s5pc1xx_onenand_read
};

static CPUWriteMemoryFunc * const onenand_config_reg_mm_write[] = {
    s5pc1xx_onenand_write,
    s5pc1xx_onenand_write,
    s5pc1xx_onenand_write
};

static void onenand_config_reg_reset(S5pc1xxOneNANDState *s)
{
    s->onenand_if_clrt      = 0x00004000 | (IS_MUX_TYPE << 31);
    s->onenand_async_timing = 0x00003415;
    s->dma_src_addr         = 0x00000000;
    s->dma_src_cfg          = 0x00040002;
    s->dma_dst_addr         = 0x00000000;
    s->dma_dst_cfg          = 0x00040002;
    s->dma_trans_size       = 0x00000000;
    s->dma_trans_dir        = 0x00000000;
    s->sqc_sao              = 0x00000000;
    s->sqc_reg_ctrl         = 0x00000000;
    s->sqc_brpao0           = 0x00000000;
    s->sqc_brpao1           = 0x00000000;
    s->intc_sqc_mask        = 0x01010000;
    s->intc_dma_mask        = 0x01010000;
    s->intc_onenand_mask    = 0x000000FF;
}

DeviceState *s5pc1xx_onenand_init(target_phys_addr_t base)
{
    DeviceState *dev;
    SysBusDevice *s;
    void *onenand_dev;

    dev = qdev_create(NULL, "s5pc1xx,onenand");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);

    sysbus_mmio_map(s, 0, base + ONENAND_CONTR_REG_BASE);

    FROM_SYSBUS(S5pc1xxOneNANDState, s)->base = base;

    onenand_dev =
        onenand_init(ONENAND_DEVICE_ID, 1, NULL, ONENAND_4KB_PAGE, 1);
    onenand_base_update(onenand_dev, base);

    return dev;
}

static int s5pc1xx_onenand_init1(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxOneNANDState *s = FROM_SYSBUS(S5pc1xxOneNANDState, dev);

    iomemtype = cpu_register_io_memory(onenand_config_reg_mm_read,
                                       onenand_config_reg_mm_write, s);
    sysbus_init_mmio(dev, ONENAND_CONTR_REGS_SIZE, iomemtype);

    onenand_config_reg_reset(s);

    return 0;
}

static void s5pc1xx_onenand_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,onenand", sizeof(S5pc1xxOneNANDState),
                        s5pc1xx_onenand_init1);
}

device_init(s5pc1xx_onenand_register_devices)
