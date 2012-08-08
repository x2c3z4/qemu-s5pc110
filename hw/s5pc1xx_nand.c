/*
 * S5PC1XX NAND controller.
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

#include "hw.h"
#include "flash.h"
#include "sysbus.h"


#define S5PC1XX_NAND_REG_MEM_SIZE 0x44


typedef struct S5pc1xxNFConState {
    SysBusDevice busdev;

    uint32_t config;
    uint32_t control;
    /* TODO: how does this status and nand chip status corellate? */
    uint32_t status;

    NANDFlashState *flash;
} S5pc1xxNFConState;


static uint32_t nfcon_read32(void *opaque, target_phys_addr_t offset)
{
    uint32_t x, res, i;
    S5pc1xxNFConState *s = (S5pc1xxNFConState *)opaque;

    switch (offset) {
    case 0x00: /* NFCONF */
        return s->config;
    case 0x04: /* NFCONT */
        return s->control;
    case 0x08: /* NFCMMD */
    case 0x0C: /* NFADDR */
        return 0x0;
    case 0x10: /* NFDATA */
        res = 0;
        nand_setpins(s->flash, 0, 0, 0, 1, 0);
        for (i = 0; i < 4; i++) {
            x = nand_getio(s->flash);
            res = res | (x << (8 * i));
        }
        return res;
    case 0x14: /* NFMECCD0 */
    case 0x18: /* NFMECCD1 */
    case 0x1C: /* NFSECCD */
        return 0;
    case 0x20: /* NFSBLK */
    case 0x24: /* NFEBLK */
        /* TODO: implement this */
        return 0;
    case 0x28: /* NFSTAT */
        return s->status;
    case 0x2C: /* NFECCERR0 */
    case 0x30: /* NFECCERR1 */
    case 0x34: /* NFMECC0 */
    case 0x38: /* NFMECC1 */
    case 0x3C: /* NFSECC */
    case 0x40: /* NFMLCBITPT */
        return 0;
    default:
        hw_error("s5pc1xx_nand: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static uint32_t nfcon_read16(void *opaque, target_phys_addr_t offset)
{
    uint32_t x, res, i;
    S5pc1xxNFConState *s = (S5pc1xxNFConState *)opaque;

    if (offset == 0x10) {
        nand_setpins(s->flash, 0, 0, 0, 1, 0);
        res = 0;
        for (i = 0; i < 2; i++) {
            x = nand_getio(s->flash);
            res = res | (x << (8 * i));
        }
        return res;
    } else {
        hw_error("s5pc1xx_nand: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static uint32_t nfcon_read8(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxNFConState *s = (S5pc1xxNFConState *)opaque;

    if (offset == 0x10) {
        nand_setpins(s->flash, 0, 0, 0, 1, 0);
        return nand_getio(s->flash);
    } else {
        hw_error("s5pc1xx_nand: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static void nfcon_write32(void *opaque, target_phys_addr_t offset, uint32_t val)
{
    S5pc1xxNFConState *s = (S5pc1xxNFConState *)opaque;

    switch (offset) {
    case 0x00: /* NFCONF */
        s->config = val;
        break;
    case 0x04: /* NFCONT */
        s->control = val;
        break;
    case 0x08: /* NFCMMD */
    case 0x0C: /* NFADDR */
    case 0x10: /* NFDATA */
        switch (offset) {
        case 0x08:
            nand_setpins(s->flash, 1, 0, 0, 1, 0);
            break;
        case 0x0C:
            nand_setpins(s->flash, 0, 1, 0, 1, 0);
            break;
        case 0x10:
            nand_setpins(s->flash, 0, 0, 0, 1, 0);
            break;
        }
        nand_setio(s->flash, (val >>  0) & 0xff);
        nand_setio(s->flash, (val >>  8) & 0xff);
        nand_setio(s->flash, (val >> 16) & 0xff);
        nand_setio(s->flash, (val >> 24) & 0xff);
        break;
    case 0x14: /* NFMECCD0 */
    case 0x18: /* NFMECCD1 */
    case 0x1C: /* NFSECCD */
        break;
    case 0x20: /* NFSBLK */
    case 0x24: /* NFEBLK */
        /* TODO: implement this */
        break;
    case 0x28: /* NFSTAT */
        /* Ignore written value. Documentation states that this register is
           R/W, but it describes states of the input pins. So what does write
           to it suppose to do? */
        break;
    case 0x2C: /* NFECCERR0 */
    case 0x30: /* NFECCERR1 */
    case 0x34: /* NFMECC0 */
    case 0x38: /* NFMECC1 */
    case 0x3C: /* NFSECC */
    case 0x40: /* NFMLCBITPT */
        break;
    default:
        hw_error("s5pc1xx_nand: bad write offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static void nfcon_write16(void *opaque, target_phys_addr_t offset, uint32_t val)
{
    S5pc1xxNFConState *s = (S5pc1xxNFConState *)opaque;

    switch (offset) {
        case 0x08:
            nand_setpins(s->flash, 1, 0, 0, 1, 0);
            break;
        case 0x0C:
            nand_setpins(s->flash, 0, 1, 0, 1, 0);
            break;
        case 0x10:
            nand_setpins(s->flash, 0, 0, 0, 1, 0);
            break;
        default:
            hw_error("s5pc1xx_nand: bad write offset " TARGET_FMT_plx "\n",
                     offset);
    }
    nand_setio(s->flash, (val >>  0) & 0xff);
    nand_setio(s->flash, (val >>  8) & 0xff);
}

static void nfcon_write8(void *opaque, target_phys_addr_t offset, uint32_t val)
{
    S5pc1xxNFConState *s = (S5pc1xxNFConState *)opaque;

    switch (offset) {
        case 0x08:
            nand_setpins(s->flash, 1, 0, 0, 1, 0);
            break;
        case 0x0C:
            nand_setpins(s->flash, 0, 1, 0, 1, 0);
            break;
        case 0x10:
            nand_setpins(s->flash, 0, 0, 0, 1, 0);
            break;
        default:
            hw_error("s5pc1xx_nand: bad write offset " TARGET_FMT_plx "\n",
                     offset);
    }
    nand_setio(s->flash, (val >>  0) & 0xff);
}

static CPUReadMemoryFunc * const nfcon_readfn[] = {
    nfcon_read8,
    nfcon_read16,
    nfcon_read32
};

static CPUWriteMemoryFunc * const nfcon_writefn[] = {
    nfcon_write8,
    nfcon_write16,
    nfcon_write32
};

static void s5pc1xx_nand_reset(S5pc1xxNFConState *s)
{
    s->config = 0x00001000;
    s->control = 0x000100C6;
    s->status = 0xF0800F0D;
}

static int s5pc1xx_nand_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxNFConState *s = FROM_SYSBUS(S5pc1xxNFConState, dev);

    s->flash = nand_init(NAND_MFR_SAMSUNG, 0xA2);
    s5pc1xx_nand_reset(s);

    iomemtype = cpu_register_io_memory(nfcon_readfn, nfcon_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_NAND_REG_MEM_SIZE, iomemtype);

    return 0;
}

static void s5pc1xx_nand_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,nand", sizeof(S5pc1xxNFConState),
                        s5pc1xx_nand_init);
}

device_init(s5pc1xx_nand_register_devices)
