/*
 * S5PC1XX SROM controller.
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 */

#include "sysbus.h"
#include "s5pc1xx.h"


typedef struct S5pc1xxSROMState {
    SysBusDevice busdev;

    /* SROM_BW - SROM Bus width & wait control */
    uint32_t control;
    /* SROM_BCn - SROM Bank n control register */
    uint32_t *bank_control;
    uint32_t num_banks;
} S5pc1xxSROMState;


static uint32_t s5pc1xx_srom_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxSROMState *s = (S5pc1xxSROMState *)opaque;

    if ((offset > (s->num_banks + 1) * 4) || (offset & 3))
        hw_error("s5pc1xx_srom: bad read offset " TARGET_FMT_plx "\n",
                 offset);

    switch (offset) {
    case 0x00:
        return s->control;
    default:
        return s->bank_control[offset / 4 - 1];
    }
}

static void s5pc1xx_srom_write(void *opaque, target_phys_addr_t offset,
                               uint32_t val)
{
    S5pc1xxSROMState *s = (S5pc1xxSROMState *)opaque;

    if ((offset > (s->num_banks + 1) * 4) || (offset & 3))
        hw_error("s5pc1xx_srom: bad write offset " TARGET_FMT_plx "\n",
                 offset);

    switch (offset) {
    case 0x00:
        s->control = val;
        break;
    default:
        s->bank_control[offset / 4 - 1] = val;
        break;
    }
}

static CPUReadMemoryFunc * const s5pc1xx_srom_mm_read[] = {
    s5pc1xx_srom_read,
    s5pc1xx_srom_read,
    s5pc1xx_srom_read
};

static CPUWriteMemoryFunc * const s5pc1xx_srom_mm_write[] = {
    s5pc1xx_srom_write,
    s5pc1xx_srom_write,
    s5pc1xx_srom_write
};

static void s5pc1xx_srom_reset(S5pc1xxSROMState *s)
{
    int i = 0;

    s->control = 0x00000008;
    for (i = 0; i < s->num_banks; i++)
        s->bank_control[i] = 0x000F0000;
}

DeviceState *s5pc1xx_srom_init(target_phys_addr_t base, int num_banks)
{
    DeviceState *dev = qdev_create(NULL, "s5pc1xx,srom");

    qdev_prop_set_uint32(dev, "num-banks", num_banks);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    return dev;
}

static int s5pc1xx_srom_init1(SysBusDevice *dev)
{
    S5pc1xxSROMState *s = FROM_SYSBUS(S5pc1xxSROMState, dev);
    int iomemtype;

    iomemtype =
        cpu_register_io_memory(s5pc1xx_srom_mm_read, s5pc1xx_srom_mm_write, s);
    sysbus_init_mmio(dev, (s->num_banks + 1) * 4, iomemtype);
    s->bank_control = qemu_mallocz(s->num_banks * sizeof(uint32_t));

    s5pc1xx_srom_reset(s);

    return 0;
}

static SysBusDeviceInfo s5pc1xx_srom_info = {
    .init = s5pc1xx_srom_init1,
    .qdev.name  = "s5pc1xx,srom",
    .qdev.size  = sizeof(S5pc1xxSROMState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("num-banks", S5pc1xxSROMState, num_banks, 6),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5pc1xx_srom_register(void)
{
    sysbus_register_withprop(&s5pc1xx_srom_info);
}

device_init(s5pc1xx_srom_register)
