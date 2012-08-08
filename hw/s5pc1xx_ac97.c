/*
 * AC97 controller for Samsung S5PC110-based board emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "s5pc1xx.h"
#include "qemu-timer.h"
#include "s5pc1xx_gpio_regs.h"
#include "sysbus.h"

#define ALL_BITS(b,a)               (((1 << (b - a + 1)) - 1) << a)

/* R/W Specifies the AC97 Global Control Register 0x00000000 */
#define AC_GLBCTRL          0x00
    #define ALL_CLEAR               ALL_BITS(30, 24)
    #define INT_EN(stat)            (stat)
    #define TRANSFER_EN             (1 << 3)
    #define AC_LINK_ON              (1 << 2)
    #define WARM_RESET              (1 << 1)
    #define COLD_RESET              (1 << 0)

/* R Specifies the AC97 Global Status Register 0x00000001 */
#define AC_GLBSTAT          0x04
    #define CODEC_READY_INT         (1 << 22)
    #define PCM_OUT_UNDER_INT       (1 << 21)
    #define PCM_IN_OVER_INT         (1 << 20)
    #define MIC_IN_OVER_INT         (1 << 19)
    #define PCM_OUT_TH_INT          (1 << 18)
    #define PCM_IN_TH_INT           (1 << 17)
    #define MIC_IN_TH_INT           (1 << 16)
    #define ALL_STAT                ALL_BITS(22, 16)

/* R/W Specifies the AC97 Codec Command Register 0x00000000 */
#define AC_CODEC_CMD        0x08
/* R Specifies the AC97 Codec Status Register 0x00000000 */
#define AC_CODEC_STAT       0x0C
/* R Specifies the AC97 PCM Out/In Channel FIFO Address Register 0x00000000 */
#define AC_PCMADDR          0x10
/* R Specifies the AC97 MIC In Channel FIFO Address Register 0x00000000 */
#define AC_MICADDR          0x14
/* R/W Specifies the AC97 PCM Out/In Channel FIFO Data Register 0x00000000 */
#define AC_PCMDATA          0x18
/* R/W Specifies the AC97 MIC In Channel FIFO Data Register 0x00000000 */
#define AC_MICDATA          0x1C

#define S5PC1XX_AC97_REG_MEM_SIZE 0x20


typedef struct S5pc1xxAC97State {
    SysBusDevice busdev;

    uint32_t glbctrl;
    uint32_t glbstat;
    uint32_t codec_cmd;
    uint32_t codec_stat;
    uint32_t pcmaddr;
    uint32_t micaddr;
    uint32_t pcmdata;
    uint32_t micdata;

    struct FrameIn {
        uint16_t pcm_left_fifo[16];
        uint16_t pcm_right_fifo[16];

        uint64_t pcm_write_idx;
        uint64_t pcm_read_idx;

        uint16_t mic_fifo[16];

        uint64_t mic_write_idx;
        uint64_t mic_read_idx;

        uint16_t tag_phase;
        uint32_t data_phase[12];
    } in;

    struct FrameOut {
        uint16_t pcm_left_fifo[16];
        uint16_t pcm_right_fifo[16];

        uint64_t pcm_write_idx;
        uint64_t pcm_read_idx;

        uint16_t tag_phase;
        uint32_t data_phase[12];
    } out;

    int delay;

    uint8_t cur_pos;

    unsigned stream  : 1;
    unsigned sync_en : 1;
    unsigned reset   : 1;

    qemu_irq  irq;
    QEMUTimer *ac97_timer;
    uint64_t  last_ac97_time;
} S5pc1xxAC97State;


/* Function for initialization and cold reset */
static void s5pc1xx_ac97_reset(S5pc1xxAC97State *s)
{
    int i;

    s->delay      = 2;
    s->stream     = 0;
    s->sync_en    = 0;

    s->glbctrl    = 0;
    s->glbstat    = 0;
    s->codec_cmd  = 0;
    s->codec_stat = 0;
    s->pcmaddr    = 0;
    s->micaddr    = 0;
    s->pcmdata    = 0;
    s->micdata    = 0;

    s->in.tag_phase  = 0;
    s->out.tag_phase = 0;

    for (i = 0; i < 12; i++) {
        s->in.data_phase[i]  = 0;
        s->out.data_phase[i] = 0;
    }
}

/* Interrupts handler */
static void s5pc1xx_ac97_irq(S5pc1xxAC97State *s,
                             uint32_t stat, uint32_t clear)
{
    if (stat) {
        s->glbstat |= stat;
        if (s->glbctrl & INT_EN(stat))  /* if enabled */
            qemu_irq_raise(s->irq);
    }
    if (clear) {
        s->glbstat &= ~(clear >> 8);
        if (!(s->glbstat & ALL_STAT))   /* if all clear */
            qemu_irq_lower(s->irq);
    }
}

/* Controls input fifo stage */
static void s5pc1xx_ac97_infifo_control(S5pc1xxAC97State *s)
{
    uint8_t pcm_depth, mic_depth;

    pcm_depth = (s->in.pcm_write_idx - s->in.pcm_read_idx) % 17;
    mic_depth = (s->in.mic_write_idx - s->in.mic_read_idx) % 17;

    if (pcm_depth == 16)
        s5pc1xx_ac97_irq(s, PCM_IN_OVER_INT, 0);

    if (pcm_depth > 7)
        s5pc1xx_ac97_irq(s, PCM_IN_TH_INT, 0);

    if (mic_depth == 16)
        s5pc1xx_ac97_irq(s, MIC_IN_OVER_INT, 0);

    if (mic_depth > 7)
        s5pc1xx_ac97_irq(s, MIC_IN_TH_INT, 0);
}

/* Controls output fifo stage */
static void s5pc1xx_ac97_outfifo_control(S5pc1xxAC97State *s)
{
    uint8_t pcm_depth;

    pcm_depth = (s->out.pcm_write_idx - s->out.pcm_read_idx) % 17;

    if (pcm_depth == 0)
        s5pc1xx_ac97_irq(s, PCM_OUT_UNDER_INT, 0);

    if (pcm_depth < 9)
        s5pc1xx_ac97_irq(s, PCM_OUT_TH_INT, 0);
}

/* Sync timer */
static void s5pc1xx_ac97_sync(void *opaque)
{
    S5pc1xxAC97State *s = (S5pc1xxAC97State *)opaque;
    uint64_t next_ac97_time;

    if (s->sync_en) {
        s->delay = 0;   /* used for 1/12MHz delay */
        s->last_ac97_time = qemu_get_clock(vm_clock);
        next_ac97_time = s->last_ac97_time +
            muldiv64(1, get_ticks_per_sec(), 48000);  /* 48 KHz cycle */
        qemu_mod_timer(s->ac97_timer, next_ac97_time);
    } else {
        qemu_del_timer(s->ac97_timer);
    }
}

/* Bitclk cycles counter */
static uint8_t s5pc1xx_ac97_spent_cycles(S5pc1xxAC97State *s)
{
    uint64_t spent_1G, spent_12M;

    spent_1G = qemu_get_clock(vm_clock) - s->last_ac97_time;
    spent_12M = muldiv64(spent_1G, 12288000, get_ticks_per_sec());

    return (spent_12M % 256);
}

/* Get current input frame and prepare next output frame */
static void s5pc1xx_ac97_next_frame(S5pc1xxAC97State *s)
{
    short i;

    /* Get input frame */

    /* check if codec_ready */
    if  (s->in.tag_phase & (1 << 15)) {
        /* check tag bits and check if the received address
         * is equal to the most recent sent address */
        if ((s->in.tag_phase & (1 << 14)) &&
            (s->in.tag_phase & (1 << 13)) &&
            (s->in.data_phase[0] & ALL_BITS(18, 12)) ==
            (s->out.data_phase[0] & ALL_BITS(18, 12)))
            s->codec_stat = (s->in.data_phase[0] << 4 & ALL_BITS(22, 16)) |
                            (s->in.data_phase[1] >> 4);

        if (s->in.pcm_write_idx < s->in.pcm_read_idx + 16) {

            if (s->in.tag_phase & (1 << 12))
                s->in.pcm_left_fifo[s->in.pcm_write_idx % 16]  =
                    (s->in.data_phase[2] >> 4);

            if (s->in.tag_phase & (1 << 11))
                s->in.pcm_right_fifo[s->in.pcm_write_idx % 16] =
                    (s->in.data_phase[3] >> 4);

            s->in.pcm_write_idx++;
        }

        if (s->in.mic_write_idx < s->in.mic_read_idx + 16) {

            if (s->in.tag_phase & (1 << 9))
                s->in.mic_fifo[s->in.mic_write_idx % 16] =
                    (s->in.data_phase[5] >> 4);

            s->in.mic_write_idx++;
        }

        s5pc1xx_ac97_infifo_control(s);
    }

    /* Set output frame */

    s->out.tag_phase = 0;
    for (i = 0; i < 4; i++)
        s->out.data_phase[i] = 0;

    if (s->codec_cmd) {
        /* enable slots 1 and 2 */
        s->out.tag_phase |= ALL_BITS(14, 13);
        s->out.data_phase[0] = s->codec_cmd >> 4 & ALL_BITS(19, 12);
        s->out.data_phase[1] = s->codec_cmd << 4 & ALL_BITS(19, 4);
        s->codec_cmd = 0;
    } else {
        s->out.tag_phase &= ~ALL_BITS(14, 13);
    }

    /* check if fifo is not empty */
    if (s->out.pcm_read_idx < s->out.pcm_write_idx) {
        /* enable slots 3 and 4 */
        s->out.tag_phase |= ALL_BITS(12, 11);
        s->out.data_phase[2] =
            s->out.pcm_left_fifo[s->out.pcm_read_idx % 16] << 4;
        s->out.data_phase[3] =
            s->out.pcm_right_fifo[s->out.pcm_read_idx % 16] << 4;
        s->out.pcm_read_idx++;
    } else {
        s->out.tag_phase &= ~ALL_BITS(12, 11);
    }

    /* set bit 15 if any of bits 14~11 is high */
    if (s->out.tag_phase & ALL_BITS(14, 11))
        s->out.tag_phase |= (1 << 15);

    s5pc1xx_ac97_outfifo_control(s);
}

/* Read AC97 by GPIO */
static uint32_t s5pc1xx_ac97_gpio_read(void *opaque, int io_index)
{
    S5pc1xxAC97State *s = (S5pc1xxAC97State *)opaque;
    uint8_t ret_val;

    switch (io_index) {
    case GPIO_AC97RESETn:
        return s->reset;

    case GPIO_AC97SYNC:
        return ((s->sync_en) && (s5pc1xx_ac97_spent_cycles(s) < 16));

    case GPIO_AC97SDO:
        if (!s->stream)
            break;

        /* Note: '-1' is a delay just after start */
        s->cur_pos = s5pc1xx_ac97_spent_cycles(s) - 1;

        if (s->cur_pos < 16)
            ret_val = s->out.tag_phase >> (15 - s->cur_pos) & 0x1;
        else
            ret_val =
                s->out.data_phase[(s->cur_pos - 16) / 20] >>
                    (19 - (s->cur_pos - 16) % 20) & 0x1;
        return ret_val;
    }
    return 0;
}

/* Write AC97 by GPIO */
static void s5pc1xx_ac97_gpio_write(void *opaque, int io_index, uint32_t value)
{
    S5pc1xxAC97State *s = (S5pc1xxAC97State *)opaque;

    switch (io_index) {
    case GPIO_AC97BITCLK:
        if (value) {
            if (s->delay == 1)
                s5pc1xx_ac97_next_frame(s);
            if (s->delay < 2)
                s->delay++;
        }
        break;

    case GPIO_AC97SDI:
        if (!(s->stream))
            break;

        /* Note: '-1' is a delay just after start */
        s->cur_pos = s5pc1xx_ac97_spent_cycles(s) - 1;

        if (s->cur_pos < 16)
            s->in.tag_phase |= value << (15 - s->cur_pos);
        else
            s->in.data_phase[(s->cur_pos - 16) / 20] |=
                value << (19 - (s->cur_pos - 16) % 20);
        break;
    }
}

static GPIOReadMemoryFunc *s5pc1xx_ac97_gpio_readfn   = s5pc1xx_ac97_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_ac97_gpio_writefn = s5pc1xx_ac97_gpio_write;

/* Read AC97 by OS */
static uint32_t s5pc1xx_ac97_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxAC97State *s = (S5pc1xxAC97State *)opaque;

    switch(offset) {
    case AC_GLBCTRL:
        return s->glbctrl;
    case AC_GLBSTAT:
        return s->glbstat;
    case AC_CODEC_CMD:
        return s->codec_cmd;
    case AC_CODEC_STAT:
        return s->codec_stat;
    case AC_PCMADDR:
        s->pcmaddr = ((s->in.pcm_write_idx % 16) << 0) |
                     ((s->out.pcm_write_idx % 16) << 8) |
                     ((s->in.pcm_read_idx % 16) << 16) |
                     ((s->out.pcm_read_idx % 16) << 24);
        return s->pcmaddr;
    case AC_MICADDR:
        s->micaddr = ((s->in.mic_write_idx % 16) << 0) |
                     ((s->in.mic_read_idx % 16) << 16);
        return s->micaddr;
    case AC_PCMDATA:
        /* check if fifo is not empty */
        if (s->in.pcm_read_idx < s->in.pcm_write_idx) {
            s->pcmdata = s->in.pcm_left_fifo[s->in.pcm_read_idx % 16] |
                (s->in.pcm_right_fifo[s->in.pcm_read_idx % 16] << 16);
            s->in.pcm_read_idx++;
        } else {
            return 0;
        }
        return s->pcmdata;
    case AC_MICDATA:
        /* check if fifo is not empty */
        if (s->in.mic_read_idx < s->in.mic_write_idx) {
            s->micdata = s->in.mic_fifo[s->in.mic_read_idx % 16];
            s->in.mic_read_idx++;
        } else {
            return 0;
        }
        return s->micdata;
    default:
        hw_error("s5pc1xx_ac97: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

/* Write AC97 by OS */
static void s5pc1xx_ac97_write(void *opaque, target_phys_addr_t offset,
                               uint32_t value)
{
    S5pc1xxAC97State *s = (S5pc1xxAC97State *)opaque;

    switch(offset) {
    case AC_GLBCTRL:
        if (value & COLD_RESET) {
            s->reset = 1;
            s5pc1xx_ac97_reset(s);
        }

        if (value & WARM_RESET) {
            s->reset = 0;
            s5pc1xx_ac97_irq(s, CODEC_READY_INT, 0);
        }

        if (s->reset)
            break;

        if ((value & AC_LINK_ON) > (s->glbctrl & AC_LINK_ON)) {
            s->sync_en = 1;
            s5pc1xx_ac97_sync(s);
        }

        /* the value is set high above */
        s->sync_en = (value & AC_LINK_ON) ? : 0;
        s->stream = (value & TRANSFER_EN) ? 1 : 0;

        if (value & ALL_CLEAR)
            s5pc1xx_ac97_irq(s, 0, value & ALL_CLEAR);

        s->glbctrl = value & ~ALL_CLEAR;
        break;
    case AC_CODEC_CMD:
        if (!s->reset)
            s->codec_cmd = value;
        break;
    case AC_PCMDATA:
        if (s->reset)
            break;

        /* check if fifo is full */
        if (s->out.pcm_write_idx == s->out.pcm_read_idx + 16)
            break;

        s->out.pcm_left_fifo[s->out.pcm_write_idx % 16] =
            value & ALL_BITS(15, 0);
        s->out.pcm_right_fifo[s->out.pcm_write_idx % 16] =
            value >> 16 & ALL_BITS(15, 0);

        s->out.pcm_write_idx++;
        break;
    case AC_MICDATA:
        /* mic data can't be written */
        break;
    default:
        hw_error("s5pc1xx_ac97: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_ac97_readfn[] = {
   s5pc1xx_ac97_read,
   s5pc1xx_ac97_read,
   s5pc1xx_ac97_read
};

static CPUWriteMemoryFunc * const s5pc1xx_ac97_writefn[] = {
   s5pc1xx_ac97_write,
   s5pc1xx_ac97_write,
   s5pc1xx_ac97_write
};

/* AC97 initialization */
static int s5pc1xx_ac97_init(SysBusDevice *dev)
{
    S5pc1xxAC97State *s = FROM_SYSBUS(S5pc1xxAC97State, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);
    iomemtype =
        cpu_register_io_memory(s5pc1xx_ac97_readfn, s5pc1xx_ac97_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_AC97_REG_MEM_SIZE, iomemtype);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_AC97, 0, s5pc1xx_ac97_gpio_readfn,
                                    s5pc1xx_ac97_gpio_writefn, NULL, s);
    s->ac97_timer = qemu_new_timer(vm_clock, s5pc1xx_ac97_sync, s);

    s5pc1xx_ac97_reset(s);

    return 0;
}

static void s5pc1xx_ac97_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,ac97", sizeof(S5pc1xxAC97State),
                        s5pc1xx_ac97_init);
}

device_init(s5pc1xx_ac97_register_devices)
