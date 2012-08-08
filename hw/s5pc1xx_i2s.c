/*
 * IIS Multi Audio Interface for Samsung S5PC1XX-based board emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *
 * TODO: make support of second channel!
 */

#include "sysbus.h"
#include "i2c.h"
#include "s5pc1xx.h"


#define AUDIO_WORDS_SEND  0x800

/*Magic size*/
#define AUDIO_BUFFER_SIZE 0x100000

#define S5PC1XX_I2S_IISCON    0x00
#define S5PC1XX_I2S_IISMOD    0x04
#define S5PC1XX_I2S_IISFIC    0x08
#define S5PC1XX_I2S_IISPSR    0x0C
#define S5PC1XX_I2S_IISTXD    0x10
#define S5PC1XX_I2S_IISRXD    0x14
#define S5PC1XX_I2S_IISFICS   0x18
#define S5PC1XX_I2S_IISTXDS   0x1C
#define S5PC1XX_I2S_IISAHB    0x20
#define S5PC1XX_I2S_IISSTR    0x24
#define S5PC1XX_I2S_IISSIZE   0x28
#define S5PC1XX_I2S_IISTRNCNT 0x2C
#define S5PC1XX_I2S_IISADDR0  0x30
#define S5PC1XX_I2S_IISADDR1  0x34
#define S5PC1XX_I2S_IISADDR2  0x38
#define S5PC1XX_I2S_IISADDR3  0x3C
#define S5PC1XX_I2S_IISSTR1   0x40

#define S5PC1XX_I2S_REG_MEM_SIZE 0x44

#define S5PC1XX_IISCON_I2SACTIVE    (0x1<< 0)
#define S5PC1XX_IISCON_RXDMACTIVE   (0x1<< 1)
#define S5PC1XX_IISCON_TXDMACTIVE   (0x1<< 2)
#define S5PC1XX_IISCON_RXCHPAUSE    (0x1<< 3)
#define S5PC1XX_IISCON_TXCHPAUSE    (0x1<< 4)
#define S5PC1XX_IISCON_RXDMAPAUSE   (0x1<< 5)
#define S5PC1XX_IISCON_TXDMAPAUSE   (0x1<< 6)
#define S5PC1XX_IISCON_FRXFULL      (0x1<< 7) /*Read only*/
#define S5PC1XX_IISCON_FTX0FULL     (0x1<< 8) /*Read only*/
#define S5PC1XX_IISCON_FRXEMPT      (0x1<< 9) /*Read only*/
#define S5PC1XX_IISCON_FTX0EMPT     (0x1<<10) /*Read only*/
#define S5PC1XX_IISCON_LRI          (0x1<<11) /*Read only*/
#define S5PC1XX_IISCON_FTX1FULL     (0x1<<12) /*Read only*/
#define S5PC1XX_IISCON_FTX2FULL     (0x1<<13) /*Read only*/
#define S5PC1XX_IISCON_FTX1EMPT     (0x1<<14) /*Read only*/
#define S5PC1XX_IISCON_FTX2EMPT     (0x1<<15) /*Read only*/
#define S5PC1XX_IISCON_FTXURINTEN   (0x1<<16)
#define S5PC1XX_IISCON_FTXURSTATUS  (0x1<<17) /*Write to clear*/
#define S5PC1XX_IISCON_TXSDMACTIVE  (0x1<<18)
#define S5PC1XX_IISCON_TXSDMAPAUSE  (0x1<<20)
#define S5PC1XX_IISCON_FTXSFULL     (0x1<<21) /*Read only*/
#define S5PC1XX_IISCON_FTXSEMPT     (0x1<<22) /*Read only*/
#define S5PC1XX_IISCON_FTXSURINTEN  (0x1<<23)
#define S5PC1XX_IISCON_FTXSURSTAT   (0x1<<24) /*Write to clear*/
#define S5PC1XX_IISCON_FRXOFINTEN   (0x1<<25)
#define S5PC1XX_IISCON_FRXOFSTAT    (0x1<<26) /*Write to clear*/
#define S5PC1XX_IISCON_SWRESET      (0x1<<31)

#define S5PC1XX_IISCON_WRITE_MASK           (0x8295007F)
#define S5PC1XX_IISCON_READ_MASK            (0x0060FF80)
#define S5PC1XX_IISCON_WRITE_TO_CLEAR_MASK  (0x05020000)


#define S5PC1XX_IISMOD_BFSMASK  (3<<1)
#define S5PC1XX_IISMOD_32FS     (0<<1)
#define S5PC1XX_IISMOD_48FS     (1<<1)
#define S5PC1XX_IISMOD_16FS     (2<<1)
#define S5PC1XX_IISMOD_24FS     (3<<1)

#define S5PC1XX_IISMOD_RFSMASK  (3<<3)
#define S5PC1XX_IISMOD_256FS    (0<<3)
#define S5PC1XX_IISMOD_512FS    (1<<3)
#define S5PC1XX_IISMOD_384FS    (2<<3)
#define S5PC1XX_IISMOD_768FS    (3<<3)

#define S5PC1XX_IISMOD_SDFMASK  (3<<5)
#define S5PC1XX_IISMOD_IIS      (0<<5)
#define S5PC1XX_IISMOD_MSB      (1<<5)
#define S5PC1XX_IISMOD_LSB      (2<<5)

#define S5PC1XX_IISMOD_LRP      (1<<7)

#define S5PC1XX_IISMOD_TXRMASK  (3<<8)
#define S5PC1XX_IISMOD_TX       (0<<8)
#define S5PC1XX_IISMOD_RX       (1<<8)
#define S5PC1XX_IISMOD_TXRX     (2<<8)

#define S5PC1XX_IISMOD_TX_SET(r) (!((1<<8)&(r)))
#define S5PC1XX_IISMOD_RX_SET(r) (((3<<8)&(r)) != 0)


#define S5PC1XX_IISMOD_IMSMASK      (3<<10)
#define S5PC1XX_IISMOD_MSTPCLK      (0<<10)
#define S5PC1XX_IISMOD_MSTCLKAUDIO  (1<<10)
#define S5PC1XX_IISMOD_SLVPCLK      (2<<10)
#define S5PC1XX_IISMOD_SLVI2SCLK    (3<<10)

#define S5PC1XX_IISMOD_CDCLKCON     (1<<12)

#define S5PC1XX_IISMOD_BLCMASK      (3<<13)
#define S5PC1XX_IISMOD_16BIT        (0<<13)
#define S5PC1XX_IISMOD_8BIT         (1<<13)
#define S5PC1XX_IISMOD_24BIT        (2<<13)

#define S5PC1XX_IISMOD_SD1EN        (1<<16)
#define S5PC1XX_IISMOD_SD2EN        (1<<17)

#define S5PC1XX_IISMOD_CCD1MASK     (3<<18)
#define S5PC1XX_IISMOD_CCD1ND       (0<<18)
#define S5PC1XX_IISMOD_CCD11STD     (1<<18)
#define S5PC1XX_IISMOD_CCD12NDD     (2<<18)

#define S5PC1XX_IISMOD_CCD2MASK     (3<<20)
#define S5PC1XX_IISMOD_CCD2ND       (0<<20)
#define S5PC1XX_IISMOD_CCD21STD     (1<<20)
#define S5PC1XX_IISMOD_CCD22NDD     (2<<20)

#define S5PC1XX_IISMOD_BLCPMASK     (3<<24)
#define S5PC1XX_IISMOD_P16BIT       (0<<24)
#define S5PC1XX_IISMOD_P8BIT        (1<<24)
#define S5PC1XX_IISMOD_P24BIT       (2<<24)
#define S5PC1XX_IISMOD_BLCSMASK     (3<<26)
#define S5PC1XX_IISMOD_S16BIT       (0<<26)
#define S5PC1XX_IISMOD_S8BIT        (1<<26)
#define S5PC1XX_IISMOD_S24BIT       (2<<26)
#define S5PC1XX_IISMOD_TXSLP        (1<<28)
#define S5PC1XX_IISMOD_OPMSK        (3<<30)
#define S5PC1XX_IISMOD_OPCCO        (0<<30)
#define S5PC1XX_IISMOD_OPCCI        (1<<30)
#define S5PC1XX_IISMOD_OPBCO        (2<<30)
#define S5PC1XX_IISMOD_OPPCLK       (3<<30)

#define I2S_TOGGLE_BIT(old, new, bit) (((old) & (bit)) != ((new) & (bit)))


/* I2C Interface */
typedef struct S5pc1xxI2SState {
    SysBusDevice busdev;

    uint32_t iiscon;
    uint32_t iismod;
    uint32_t iisfic;
    uint32_t iispsr;
    uint32_t iisfics;
    uint32_t iisahb;
    uint32_t iisstr0;
    uint32_t iissize;
    uint32_t iistrncnt;
    uint32_t iislvl0addr;
    uint32_t iislvl1addr;
    uint32_t iislvl2addr;
    uint32_t iislvl3addr;
    uint32_t iisstr1;

    uint8_t *buffer;
    uint buf_size;
    uint play_pos;
    uint last_free;

    qemu_irq irq;
    qemu_irq dma_irq_stop1;
    qemu_irq dma_irq_stop2;
    DeviceState *wm8994;
} S5pc1xxI2SState;


static void s5pc1xx_i2s_stop(S5pc1xxI2SState *s)
{
    qemu_irq_raise(s->dma_irq_stop1);
    qemu_irq_raise(s->dma_irq_stop2);
}

static void s5pc1xx_i2s_resume(S5pc1xxI2SState *s)
{
    qemu_irq_lower(s->dma_irq_stop1);
    qemu_irq_lower(s->dma_irq_stop2);
}


static void s5pc1xx_i2s_reset(S5pc1xxI2SState *s)
{
    s->iiscon      = 0;
    s->iismod      = 0;
    s->iisfic      = 0;
    s->iispsr      = 0;
    s->iisfics     = 0;
    s->iisahb      = 0;
    s->iisstr0     = 0;
    s->iissize     = 0x7FFF0000;
    s->iistrncnt   = 0;
    s->iislvl0addr = 0;
    s->iislvl1addr = 0;
    s->iislvl2addr = 0;
    s->iislvl3addr = 0;
    s->iisstr1     = 0;

    s->play_pos    = 0;
    s->last_free   = 0;
}

static int s5pc1xx_i2s_pause(S5pc1xxI2SState *s) {
    return s->iiscon & S5PC1XX_IISCON_TXCHPAUSE;
}

static int s5pc1xx_i2s_transmit(S5pc1xxI2SState *s) {
    return S5PC1XX_IISMOD_TX_SET(s->iismod);
}

static void s5pc1xx_i2s_audio_callback(void *opaque, int free_out)
{
    S5pc1xxI2SState *s = (S5pc1xxI2SState *)opaque;
    int8_t *codec_buffer = NULL;
    int block_size = 0;


    if (free_out <= 0) {
        return;
    }
    if (free_out > AUDIO_WORDS_SEND) {
        free_out = AUDIO_WORDS_SEND;
    }

    block_size = 4 * free_out;

    if (s5pc1xx_i2s_pause(s)) {
        return;
    }

    if (s->play_pos > s->last_free &&
        s->play_pos + block_size > s->buf_size &&
        s->play_pos + block_size - s->buf_size > s->last_free) {
        s5pc1xx_i2s_resume(s);
        return;
    }

    if (s->play_pos <= s->last_free &&
        s->play_pos + block_size > s->last_free) {
        s5pc1xx_i2s_resume(s);
        return;
    }

    codec_buffer = wm8994_dac_buffer(s->wm8994, block_size);
    memcpy(codec_buffer, s->buffer + s->play_pos, block_size);
    s->play_pos = (s->play_pos + block_size) % s->buf_size;

    s5pc1xx_i2s_resume(s);

    wm8994_dac_commit(s->wm8994);
}

/* I2S write function */
static void s5pc1xx_i2s_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5pc1xxI2SState *s = (S5pc1xxI2SState *)opaque;
    int use_buf = 0;

    switch (offset) {
    case S5PC1XX_I2S_IISCON:
        if ((value & S5PC1XX_IISCON_SWRESET) &&
            !(s->iiscon & S5PC1XX_IISCON_SWRESET)) {
            s5pc1xx_i2s_reset(s);
        }
        if (I2S_TOGGLE_BIT(s->iiscon, value, S5PC1XX_IISCON_TXCHPAUSE)) {
            if (value & S5PC1XX_IISCON_TXCHPAUSE) {
                s5pc1xx_i2s_stop(s);
            } else {
                s5pc1xx_i2s_resume(s);
            }
        }

        if (I2S_TOGGLE_BIT(s->iiscon, value, S5PC1XX_IISCON_TXSDMACTIVE)) {
            /*TODO: stop dma if (value & S5PC1XX_IISCON_TXSDMACTIVE) is 0*/
        }

        s->iiscon = (s->iiscon & ~S5PC1XX_IISCON_WRITE_MASK) |
                    (value & S5PC1XX_IISCON_WRITE_MASK);


        if (!(s->iiscon & S5PC1XX_IISCON_I2SACTIVE)) {
            s->play_pos = 0;
            s->last_free = 0;
            s5pc1xx_i2s_resume(s);
        }

        /* FIXME: Kernel wants this bit for synchronization. Fix this line */
        s->iiscon |= S5PC1XX_IISCON_LRI;
        break;
    case S5PC1XX_I2S_IISMOD:
        s->iismod = value;
        break;
    case S5PC1XX_I2S_IISFIC:
        s->iisfic = value;
        break;
    case S5PC1XX_I2S_IISPSR:
        s->iispsr = value;
        break;
    case S5PC1XX_I2S_IISTXDS:
    case S5PC1XX_I2S_IISTXD:
        if (!s5pc1xx_i2s_transmit(s))
            break;

        if ( (s->iismod & S5PC1XX_IISMOD_LRP) &&
            ((s->iismod & S5PC1XX_IISMOD_BLCMASK) == S5PC1XX_IISMOD_16BIT ||
             (s->iismod & S5PC1XX_IISMOD_BLCMASK) == S5PC1XX_IISMOD_8BIT)) {
            *(uint32_t *)(s->buffer + s->last_free) =
                (value << 16) | (value >> 16);
        } else {
            *(uint32_t *)(s->buffer + s->last_free) = value;
        }

        s->last_free = (s->last_free + sizeof(value)) % s->buf_size;

        use_buf = s->last_free - s->play_pos;
        if (use_buf < 0) {
            use_buf = s->buf_size + use_buf;
        }

        if (use_buf >= AUDIO_WORDS_SEND*4) {
            s5pc1xx_i2s_stop(s);
        }
        break;
    case S5PC1XX_I2S_IISFICS:
        s->iisfics = value;
        break;
    case S5PC1XX_I2S_IISAHB:
        s->iisahb = value;
        break;
    case S5PC1XX_I2S_IISSTR:
        s->iisstr0 = value;
        break;
    case S5PC1XX_I2S_IISSIZE:
        s->iissize = value;
        break;
    case S5PC1XX_I2S_IISTRNCNT:
        s->iistrncnt = value;
        break;
    case S5PC1XX_I2S_IISADDR0:
        s->iislvl0addr = value;
        break;
    case S5PC1XX_I2S_IISADDR1:
        s->iislvl1addr = value;
        break;
    case S5PC1XX_I2S_IISADDR2:
        s->iislvl2addr = value;
        break;
    case S5PC1XX_I2S_IISADDR3:
        s->iislvl3addr = value;
        break;
    case S5PC1XX_I2S_IISSTR1:
        s->iisstr1 = value;
        break;
    default:
        /* FIXME: all registers are accessible by byte */
        hw_error("s5pc1xx_i2s: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}

/* I2S read function */
static uint32_t s5pc1xx_i2s_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxI2SState *s = (S5pc1xxI2SState *)opaque;


    switch (offset) {
    case S5PC1XX_I2S_IISCON:
        return s->iiscon;
    case S5PC1XX_I2S_IISMOD:
        return s->iismod;
    case S5PC1XX_I2S_IISFIC:
        return s->iisfic;
    case S5PC1XX_I2S_IISPSR:
        return s->iispsr;
    case S5PC1XX_I2S_IISRXD:
        /* TODO: Support receive data register */
        return 0;
    case S5PC1XX_I2S_IISFICS:
        return s->iisfics;
    case S5PC1XX_I2S_IISAHB:
        return s->iisahb;
    case S5PC1XX_I2S_IISSTR:
        return s->iisstr0;
    case S5PC1XX_I2S_IISSIZE:
        return s->iissize;
    case S5PC1XX_I2S_IISTRNCNT:
        return s->iistrncnt;
    case S5PC1XX_I2S_IISADDR0:
        return s->iislvl0addr;
    case S5PC1XX_I2S_IISADDR1:
        return s->iislvl1addr;
    case S5PC1XX_I2S_IISADDR2:
        return s->iislvl2addr;
    case S5PC1XX_I2S_IISADDR3:
        return s->iislvl3addr;
    case S5PC1XX_I2S_IISSTR1:
        return s->iisstr1;
    default:
        /* FIXME: all registers are accessible by byte */
        hw_error("s5pc1xx_i2s: bad write offset " TARGET_FMT_plx "\n", offset);
        return 0;
    }
}

static CPUReadMemoryFunc * const s5pc1xx_i2s_readfn[] = {
    s5pc1xx_i2s_read,
    s5pc1xx_i2s_read,
    s5pc1xx_i2s_read
};

static CPUWriteMemoryFunc * const s5pc1xx_i2s_writefn[] = {
    s5pc1xx_i2s_write,
    s5pc1xx_i2s_write,
    s5pc1xx_i2s_write
};

/* I2S init */
DeviceState *s5pc1xx_i2s_init(target_phys_addr_t base, qemu_irq irq,
                              DeviceState *wm8994_dev, qemu_irq dma_irq1,
                              qemu_irq dma_irq2)
{
    DeviceState *dev = qdev_create(NULL, "s5pc1xx,i2s");

    qdev_prop_set_ptr(dev, "wm8994", wm8994_dev);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    sysbus_connect_irq(sysbus_from_qdev(dev), 1, dma_irq1);
    sysbus_connect_irq(sysbus_from_qdev(dev), 2, dma_irq2);
    return dev;
}

static int s5pc1xx_i2s_init1(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxI2SState *s = FROM_SYSBUS(S5pc1xxI2SState, dev);

    sysbus_init_irq(dev, &s->irq);
    sysbus_init_irq(dev, &s->dma_irq_stop1);
    sysbus_init_irq(dev, &s->dma_irq_stop2);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_i2s_readfn, s5pc1xx_i2s_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_I2S_REG_MEM_SIZE, iomemtype);
    s->buf_size = AUDIO_BUFFER_SIZE;
    s->buffer = qemu_malloc(s->buf_size);

    s5pc1xx_i2s_reset(s);
    wm8994_data_req_set(s->wm8994, s5pc1xx_i2s_audio_callback, s);

    return 0;
}

static SysBusDeviceInfo s5pc1xx_i2s_info = {
    .init = s5pc1xx_i2s_init1,
    .qdev.name  = "s5pc1xx,i2s",
    .qdev.size  = sizeof(S5pc1xxI2SState),
    .qdev.props = (Property[]) {
        {
            .name   = "wm8994",
            .info   = &qdev_prop_ptr,
            .offset = offsetof(S5pc1xxI2SState, wm8994),
        },
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5pc1xx_i2s_register_devices(void)
{
    sysbus_register_withprop(&s5pc1xx_i2s_info);
}

device_init(s5pc1xx_i2s_register_devices)
