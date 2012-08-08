/*
 * MMC controller for Samsung S5PC1XX-based board emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *                Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *
 * Based on SMDK6400 MMC (hw/smdk6400/smdk_mmc.c)
 */

#include "hw.h"
#include "sd.h"
#include "s5pc1xx.h"
#include "s5pc1xx_hsmmc_regs.h"
#include "block_int.h"
#include "sysbus.h"

#include "qemu-timer.h"

/*#define DEBUG_MMC*/

#ifdef DEBUG_MMC
#define DPRINTF(fmt, args...) \
do { fprintf(stderr, "QEMU SD/MMC: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define CMD_RESPONSE  (3 << 0)

#define INSERTION_DELAY (get_ticks_per_sec())

typedef struct S5pc1xxMMCState {
    SysBusDevice busdev;

    SDState *card;
    uint8_t dma_transcpt;
    uint32_t cmdarg;
    uint32_t respcmd;
    uint32_t response[4];
    qemu_irq irq;

    uint32_t sysad;
    uint16_t blksize;
    uint16_t blkcnt;
    uint32_t argument;
    uint16_t trnmod;
    uint16_t cmdreg;
    uint32_t prnsts;
    uint8_t hostctl;
    uint8_t pwrcon;
    uint16_t clkcon;
    uint8_t timeoutcon;
    uint8_t swrst;
    uint16_t norintsts;
    uint16_t errintsts;
    uint16_t norintstsen;
    uint16_t errintstsen;
    uint16_t norintsigen;
    uint16_t errintsigen;
    uint32_t control2;
    uint32_t control3;

    QEMUTimer *response_timer; /* command response. */
    QEMUTimer *insert_timer; /* timer for 'changing' sd card. */

    qemu_irq eject;
} S5pc1xxMMCState;


static void mmc_dmaInt(S5pc1xxMMCState *s)
{
    if (s->dma_transcpt == 1)
        s->norintsts |= S5C_HSMMC_NIS_TRSCMP;
    else
        s->norintsts |= S5C_HSMMC_NIS_DMA;
    qemu_set_irq(s->irq, 1);
}

static void mmc_fifo_push(S5pc1xxMMCState *s, uint32_t pos, uint32_t value)
{
    cpu_physical_memory_write(s->sysad + pos, (uint8_t *)(&value), 4);
}

static uint32_t mmc_fifo_pop(S5pc1xxMMCState *s, uint32_t pos)
{
    uint32_t value = 0;

    cpu_physical_memory_read(s->sysad + pos, (uint8_t *)(&value), 4);
    return value;
}

static void s5pc1xx_mmc_raise_end_command_irq(void *opaque)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;
    DPRINTF("raise IRQ response\n");
    qemu_irq_raise(s->irq);
    s->norintsts |= S5C_HSMMC_NIS_CMDCMP;
}

static void mmc_send_command(S5pc1xxMMCState *s)
{
    SDRequest request;
    uint8_t response[16];
    int rlen;

    s->errintsts = 0;
    qemu_mod_timer(s->response_timer, qemu_get_clock(vm_clock));
    if (!s->card)
        return;

    request.cmd = s->cmdreg >> 8;
    request.arg = s->cmdarg;
    DPRINTF("Command %d %08x\n", request.cmd, request.arg);
    rlen = sd_do_command(s->card, &request, response);
    if (rlen < 0)
        goto error;
    if ((s->cmdreg & CMD_RESPONSE) != 0) {
#define RWORD(n) ((n >= 0 ? (response[n] << 24) : 0) \
                  | (response[n + 1] << 16) \
                  | (response[n + 2] << 8) \
                  |  response[n + 3])

        if (rlen == 0)
            goto error;
        if (rlen != 4 && rlen != 16)
            goto error;
        s->response[0] = RWORD(0);
        if (rlen == 4) {
            s->response[1] = s->response[2] = s->response[3] = 0;
        } else {
            s->response[0] = RWORD(11);
            s->response[1] = RWORD(7);
            s->response[2] = RWORD(3);
            s->response[3] = RWORD(-1);
        }
        DPRINTF("Response received\n");
#undef RWORD
    } else {
        DPRINTF("Command sent\n");
    }
    return;

error:
    DPRINTF("Timeout\n");
    s->errintsts |= S5C_HSMMC_EIS_CMDTIMEOUT;
}

/* Transfer data between the card and the FIFO.  This is complicated by
   the FIFO holding 32-bit words and the card taking data in single byte
   chunks.  FIFO bytes are transferred in little-endian order.  */
static void mmc_fifo_run(S5pc1xxMMCState *s)
{
    uint32_t value;
    int n;
    uint32_t pos;
    int is_read;
    uint32_t datacnt, boundary_chk, boundary_count;
    uint8_t dma_buf_boundary, dma_mask_flag;

    is_read = (s->trnmod & S5C_HSMMC_TRNS_READ) != 0;

    if (s->blkcnt != 0 && (!is_read || sd_data_ready(s->card))) {
        n = 0;
        value = 0;

        if (s->blkcnt > 1) {
            /* multi block */
            if (s->norintstsen & 0x8)
                dma_mask_flag = 1;  /* DMA enable */
            else
                dma_mask_flag = 0;  /* DMA disable */

            dma_buf_boundary = (s->blksize & 0xf000) >> 12;
            boundary_chk = 1 << (dma_buf_boundary+12);
            boundary_count = boundary_chk - (s->sysad % boundary_chk);
            while (s->blkcnt) {
                datacnt = s->blksize & 0x0fff;
                pos = 0;
                while (datacnt) {
                    if (is_read) {
                        value |= (uint32_t)sd_read_data(s->card) << (n * 8);
                        n++;
                        if (n == 4) {
                            mmc_fifo_push(s, pos, value);
                            value = 0;
                            n = 0;
                            pos += 4;
                        }
                    } else {
                        if (n == 0) {
                            value = mmc_fifo_pop(s, pos);
                            n = 4;
                            pos += 4;
                        }
                        sd_write_data(s->card, value & 0xff);
                        value >>= 8;
                        n--;
                    }
                    datacnt--;
                }
                s->sysad += s->blksize & 0x0fff;
                boundary_count -= s->blksize & 0x0fff;
                s->blkcnt--;

                if ((boundary_count == 0) && dma_mask_flag)
                    break;
            }
            if (s->blkcnt == 0)
                s->norintsts |= S5C_HSMMC_NIS_TRSCMP;
            else
                s->norintsts |= S5C_HSMMC_NIS_DMA;
        } else {
            /* single block */
            datacnt = s->blksize & 0x0fff;
            pos = 0;
            while (datacnt) {
                if (is_read) {
                    value |=
                        (uint32_t)sd_read_data(s->card) << (n * 8);
                    n++;
                    if (n == 4) {
                        mmc_fifo_push(s, pos, value);
                        value = 0;
                        n = 0;
                        pos += 4;
                    }
                } else {
                    if (n == 0) {
                        value = mmc_fifo_pop(s, pos);
                        n = 4;
                        pos += 4;
                    }
                sd_write_data(s->card, value & 0xff);
                    value >>= 8;
                    n--;
                }
                datacnt--;
            }
            s->blkcnt--;
            s->norintsts |= S5C_HSMMC_NIS_TRSCMP;
        }
    }
}

/* MMC read (byte) function */
static uint32_t mmc_readb(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;

    switch (offset) {
    case (S5C_HSMMC_RSPREG0 + 0x3):
        return (uint8_t)(s->response[0] >> 24);
    case (S5C_HSMMC_RSPREG1 + 0x3):
        return (uint8_t)(s->response[1] >> 24);
    case (S5C_HSMMC_RSPREG2 + 0x3):
        return (uint8_t)(s->response[2] >> 24);
    case S5C_HSMMC_HOSTCTL:
        return s->hostctl;
    case S5C_HSMMC_PWRCON:
        return s->pwrcon;
    case S5C_HSMMC_BLKGAP:
        return 0;
    case S5C_HSMMC_WAKCON:
        return 0;
    case S5C_HSMMC_TIMEOUTCON:
        return s->timeoutcon;
    case S5C_HSMMC_SWRST:
        return 0;
    default:
        hw_error("s5pc1xx_mmc: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

/* MMC read (word) function */
static uint32_t mmc_readw(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;

    switch (offset) {
    case S5C_HSMMC_BLKSIZE:
        return s->blksize;
    case S5C_HSMMC_BLKCNT:
        return s->blkcnt;
    case S5C_HSMMC_TRNMOD:
        return s->trnmod;
    case S5C_HSMMC_CLKCON:
        return s->clkcon;
    case S5C_HSMMC_SWRST:
        return s->swrst;
    case S5C_HSMMC_NORINTSTS:
        qemu_set_irq(s->irq, 0);
        return s->norintsts;
    case S5C_HSMMC_NORINTSTSEN:
        return s->norintstsen;
    case S5C_HSMMC_ACMD12ERRSTS:
        return 0;
    case S5C_HSMMC_SLOT_INT_STATUS:
        return 0;
    case S5C_HSMMC_HCVER:
        return 0x2401;
    default:
        hw_error("s5pc1xx_mmc: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

/* MMC read (doubleword) function */
static uint32_t mmc_readl(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;

    switch (offset) {
    case S5C_HSMMC_SYSAD:
        return s->sysad;
    case S5C_HSMMC_ARGUMENT:
        return s->cmdarg;
    case S5C_HSMMC_RSPREG0:
        return s->response[0];
    case S5C_HSMMC_RSPREG1:
        return s->response[1];
    case S5C_HSMMC_RSPREG2:
        return s->response[2];
    case S5C_HSMMC_RSPREG3:
        return s->response[3];
    case S5C_HSMMC_PRNSTS:
        return s->prnsts;
    case S5C_HSMMC_NORINTSTS:
        qemu_set_irq(s->irq, 0);
        return (s->errintsts << 16) | (s->norintsts);
    case S5C_HSMMC_NORINTSTSEN:
        return (s->errintstsen << 16) | s->norintstsen;
    case S5C_HSMMC_NORINTSIGEN:
        return (s->errintsigen << 16) | s->norintsigen;
    case S5C_HSMMC_CAPAREG:
        return 0x05E80080;
    case S5C_HSMMC_MAXCURR:
        return 0;
    case S5C_HSMMC_CONTROL2:
        return s->control2;
    case S5C_HSMMC_CONTROL3:
        return s->control3;
    default:
        hw_error("s5pc1xx_mmc: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

/* MMC write (byte) function */
static void mmc_writeb(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;

    switch (offset) {
    case S5C_HSMMC_HOSTCTL:
        s->hostctl = value;
        break;
    case S5C_HSMMC_PWRCON:
        s->pwrcon = value;
        break;
    case S5C_HSMMC_TIMEOUTCON:
        s->timeoutcon = value;
        break;
    case S5C_HSMMC_SWRST:
        s->swrst = value;
        break;
    default:
        hw_error("s5pc1xx_mmc: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}

/* MMC write (word) function */
static void mmc_writew(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;

    switch (offset) {
    case S5C_HSMMC_BLKSIZE:
        s->blksize = value;
        break;
    case S5C_HSMMC_BLKCNT:
        s->blkcnt = value;
        break;
    case S5C_HSMMC_TRNMOD:
        s->trnmod = value;
        break;
    case S5C_HSMMC_CMDREG: /* Command */
        s->cmdreg = value;
        mmc_send_command(s);
        mmc_fifo_run(s);
        if (s->errintsts)
            s->norintsts |= S5C_HSMMC_NIS_ERR;
        s->norintsts |= S5C_HSMMC_NIS_CMDCMP;
        if (s->norintsts & S5C_HSMMC_NIS_TRSCMP)
            s->norintsts &= ~S5C_HSMMC_NIS_CMDCMP;
        if (s->norintsts & S5C_HSMMC_NIS_DMA)
            s->norintsts &= ~S5C_HSMMC_NIS_CMDCMP;
        break;
    case S5C_HSMMC_CLKCON:
        s->clkcon = value;
        if (S5C_HSMMC_CLOCK_INT_EN & s->clkcon)
            s->clkcon |= S5C_HSMMC_CLOCK_INT_STABLE;
        else
            s->clkcon &= ~S5C_HSMMC_CLOCK_INT_STABLE;
        if (S5C_HSMMC_CLOCK_CARD_EN & s->clkcon)
            s->clkcon |= S5C_HSMMC_CLOCK_EXT_STABLE;
        else
            s->clkcon &= ~S5C_HSMMC_CLOCK_EXT_STABLE;
        break;
    case S5C_HSMMC_NORINTSTS:
        s->norintsts &= ~value;
        s->norintsts &= ~0x8100;
        s->norintsts |= value & 0x8100;
        break;
    case S5C_HSMMC_NORINTSTSEN:
        s->norintstsen = value;
        break;
    case S5C_HSMMC_NORINTSIGEN:
        s->norintsigen = value;
        break;
    default:
        hw_error("s5pc1xx_mmc: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}

/* MMC write (doubleword) function */
static void mmc_writel(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;
    int datacnt;
    int is_read;
    int value1, n;
    uint32_t pos;
    uint32_t boundary_chk, boundary_count;
    uint32_t dma_buf_boundary;
    uint8_t dma_mask_flag;

    switch (offset) {
    case S5C_HSMMC_SYSAD:
        s->sysad = value;
        if (s->blkcnt != 0) {
            n = 0;
            value1 = 0;
            is_read = (s->trnmod & S5C_HSMMC_TRNS_READ) != 0;
            dma_buf_boundary = (s->blksize & 0xf000) >> 12;
            boundary_chk = 1 << (dma_buf_boundary + 12);

            if (s->norintstsen & 0x8)
                dma_mask_flag = 1; /* DMA enable */
            else
                dma_mask_flag = 0; /* DMA disable */

            boundary_count = boundary_chk - (s->sysad % boundary_chk);
            while (s->blkcnt) {
                pos = 0;
                datacnt = s->blksize & 0x0fff;
                while (datacnt) {
                    if (is_read) {
                        value1 |= (uint32_t)sd_read_data(s->card) << (n * 8);
                        n++;
                        if (n == 4) {
                            mmc_fifo_push(s, pos, value1);
                            value1 = 0;
                            n = 0;
                            pos += 4;
                        }
                    } else {
                        if (n == 0) {
                            value1 = mmc_fifo_pop(s, pos);
                            n = 4;
                            pos += 4;
                        }
                        sd_write_data(s->card, value1 & 0xff);
                        value1 >>= 8;
                        n--;
                    }
                    datacnt--;
                }
                s->sysad += s->blksize & 0x0fff;
                boundary_count -= s->blksize & 0x0fff;
                s->blkcnt--;
                if ((boundary_count == 0) && dma_mask_flag)
                    break;
            }
            if (s->blkcnt == 0) {
                s->dma_transcpt = 1;
                mmc_dmaInt(s);
            } else {
                s->dma_transcpt = 0;
                mmc_dmaInt(s);
            }
        }
        break;
    case S5C_HSMMC_ARGUMENT:
        s->argument = value;
        s->cmdarg = value;
        break;
    case S5C_HSMMC_NORINTSTS:
        s->norintsts &= ~value;
        s->norintsts &= ~0x8100;
        s->norintsts |= value & 0x8100;
        s->errintsts &= ~(value >> 16);

        if (s->errintsts == 0) {
            s->norintsts &= ~S5C_HSMMC_NIS_ERR; /* Error Interrupt clear */
        }
        break;
    case S5C_HSMMC_NORINTSTSEN:
        s->norintstsen = (uint16_t)value;
        s->errintstsen = (uint16_t)(value >> 16);
        break;
    case S5C_HSMMC_NORINTSIGEN:
        s->norintsigen = (uint16_t)value;
        s->errintsigen = (uint16_t)(value >> 16);
        break;
    case S5C_HSMMC_CONTROL2:
        s->control2 = value;
        break;
    case S5C_HSMMC_CONTROL3:
        s->control3 = value;
        break;
    case S5C_HSMMC_CONTROL4:
        /* Nothing for QENU emulation */
        break;
    default:
        hw_error("s5pc1xx_mmc: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}

static CPUReadMemoryFunc *mmc_readfn[] = {
   mmc_readb,
   mmc_readw,
   mmc_readl
};

static CPUWriteMemoryFunc *mmc_writefn[] = {
   mmc_writeb,
   mmc_writew,
   mmc_writel
};

static void mmc_reset(void *opaque)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;

    if (s->card) {
        s->prnsts = 0x1ff0000;
        s->norintsts |= 0x0040;
    } else {
        s->prnsts = 0x1fa0000;
        s->norintsts = 0;
    }
    s->cmdarg      = 0;
    s->respcmd     = 0;
    s->response[0] = 0;
    s->response[1] = 0;
    s->response[2] = 0;
    s->response[3] = 0;
}

static void s5pc1xx_mmc_raise_insertion_irq(void *opaque)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;
    DPRINTF("raise IRQ response\n");

    if (s->norintsts & S5C_HSMMC_NIS_REMOVE) {
        DPRINTF("s5pc1xx_mmc_raise_insertion_irq: set timer!\n");
        qemu_mod_timer(s->insert_timer,
                       qemu_get_clock(vm_clock) + INSERTION_DELAY);
    } else {
        DPRINTF("s5pc1xx_mmc_raise_insertion_irq: raise irq!\n");
        s->norintsts |= S5C_HSMMC_NIS_INSERT;
        qemu_irq_raise(s->irq);
    }
}

static void s5pc1xx_mmc_insert_eject(void *opaque, int irq, int level)
{
    S5pc1xxMMCState *s = (S5pc1xxMMCState *)opaque;
    DPRINTF("change card state: %s!\n", level ? "insert" : "eject");

    if (s->norintsts & S5C_HSMMC_NIS_REMOVE) {
        if (level) {
            DPRINTF("change card state: timer set!\n");
            qemu_mod_timer(s->insert_timer,
                           qemu_get_clock(vm_clock) + INSERTION_DELAY);
        }
    } else {
        s->norintsts |= level ? S5C_HSMMC_NIS_INSERT : S5C_HSMMC_NIS_REMOVE;
        qemu_irq_raise(s->irq);
    }
}

static int s5pc1xx_mmc_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxMMCState *s = FROM_SYSBUS(S5pc1xxMMCState, dev);
    BlockDriverState *bd;

    sysbus_init_irq(dev, &s->irq);
    iomemtype = cpu_register_io_memory(mmc_readfn, mmc_writefn, s);
    sysbus_init_mmio(dev, S5C_HSMMC_REG_SIZE, iomemtype);

    bd = qdev_init_bdrv(&dev->qdev, IF_SD);

    if ((bd == NULL)) {
        s->card = NULL;
        DPRINTF("s->card = NULL\n");
    } else {
        s->eject = qemu_allocate_irqs(s5pc1xx_mmc_insert_eject, s, 1)[0];

        DPRINTF("name = %s, sectors = %ld\n",
                bd->device_name, bd->total_sectors);

        s->card = sd_init(bd, 0);
        sd_set_cb(s->card, NULL, s->eject);
    }

    qemu_register_reset(mmc_reset, s);
    mmc_reset(s);

    s->response_timer =
        qemu_new_timer(vm_clock, s5pc1xx_mmc_raise_end_command_irq, s);

    s->insert_timer =
        qemu_new_timer(vm_clock, s5pc1xx_mmc_raise_insertion_irq, s);

    /* ??? Save/restore.  */

    return 0;
}

static void s5pc1xx_mmc_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,mmc", sizeof(S5pc1xxMMCState),
                        s5pc1xx_mmc_init);
}

device_init(s5pc1xx_mmc_register_devices)
