/*
 * SPDIF transmitter for Samsung S5PC110-based board emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "s5pc1xx.h"
#include "qemu-timer.h"
#include "s5pc1xx_gpio_regs.h"
#include "sysbus.h"

#define ALL_BITS(b,a)               (((1 << (b - a + 1)) - 1) << a)

/* R/W Specifies the Clock control register 0x0000_0002 */
#define SPDCLKCON       0x00

    #define MAIN_CLK_SEL                (1 << 2)
    #define CLK_DWN_READY               (1 << 1)
    #define POWER_ON                    (1 << 0)

/* R/W Specifies the Control register 0x0000_0000 */
#define SPDCON          0x04

    #define FIFO_LEVEL_SHIFT            22      /* 5 bits */
    #define FIFO_LEVEL_THR_SHIFT        19      /* 3 bits */
    #define FIFO_TRANSFER_MODE_SHIFT    17      /* 2 bits */
    #define FIFO_LEVEL_INT_ST           (1 << 16)
    #define FIFO_LEVEL_INT_EN           (1 << 15)
    #define ENDIAN_FORMAT_SHIFT         13      /* 2 bits */
    #define USER_DATA_ATTACH            (1 << 12)
    #define USER_DATA_INT_ST            (1 << 11)
    #define USER_DATA_INT_EN            (1 << 10)
    #define BUF_EMPTY_INT_ST            (1 << 9)
    #define BUF_EMPTY_INT_EN            (1 << 8)
    #define STREAM_END_INT_ST           (1 << 7)
    #define STREAM_END_INT_EN           (1 << 6)
    #define SOFTWARE_RESET              (1 << 5)
    #define MAIN_CLK_FREQ_SHIFT         3       /* 2 bits */
    #define PCM_DATA_SIZE_SHIFT         1       /* 2 bits */
    /* TODO: Stream data coding is not described in the documentation.
     * So, this is not implemented so far. */
    #define PCM_OR_STREAM               (1 << 0)

/* R/W Specifies the Burst status register 0x0000_0000 */
#define SPDBSTAS        0x08

    #define BURST_DATA_L_BIT_SHIFT      16      /* 16 bits */
    #define BITSTREAM_NUMBER_SHIFT      13      /* 3 bits */
    #define DATA_TYPE_DEP_INFO_SHIFT    8       /* 5 bits */
    #define ERROR_FLAG                  (1 << 7)
    #define COMPR_DATA_TYPE_SHIFT       0       /* 5 bits */

/* R/W Specifies the Channel status register 0x0000_0000 */
#define SPDCSTAS        0x0C

    #define CLK_ACCURACY_SHIFT          28      /* 2 bits */
    #define SAMP_FREQ_SHIFT             24      /* 4 bits */
    #define CH_NUM_SHIFT                20      /* 4 bits */
    #define SRC_NUM_SHIFT               16      /* 4 bits */
    #define CAT_CODE_SHIFT              8       /* 8 bits */
    #define CH_STAT_MODE_SHIFT          6       /* 2 bits */
    #define EMPHASIS_SHIFT              3       /* 3 bits */
    #define COPYRIGHT_ASSERTION         (1 << 2)
    #define AUDIO_WORD                  (1 << 1)
    #define CH_STAT_BLOCK               (1 << 0)

/* W Specifies the SPDIFOUT data buffer 0x0000_0000 */
#define SPDDAT          0x10

/* R/W Specifies the Repetition count register 0x0000_0000 */
#define SPDCNT          0x14

/* R Specifies the Shadowed Burst Status Register 0x0000_0000 */
#define SPDBSTAS_SHD    0x18

/* R Specifies the Shadowed Repetition Count Register 0x0000_0000 */
#define SPDCNT_SHD      0x1C

/* R/W Specifies the Subcode Q1 ~ Q32 0x0000_0000 */
#define USERBIT1        0x20

/* R/W Specifies the Subcode Q33 ~ Q64 0x0000_0000 */
#define USERBIT2        0x24

/* R/W Specifies the Subcode Q65 ~ Q96 0x0000_0000 */
#define USERBIT3        0x28

/* R Specifies the Shadowed Register Userbit1 0x0000_0000 */
#define USERBIT1_SHD    0x2C

/* R Specifies the Shadowed Register Userbit2 0x0000_0000 */
#define USERBIT2_SHD    0x30

/* R Specifies the Shadowed Register Userbit3 0x0000_0000 */
#define USERBIT3_SHD    0x34

/* R Specifies the RTL Version Information 0x0000_000C */
#define VERSION_INFO    0x38

#define S5PC1XX_SPDIF_REG_MEM_SIZE  0x3C

#define M_PREAMBLE      1
#define B_PREAMBLE      2
#define W_PREAMBLE      3

#define FIFO_DEPTH      (s->write_idx - s->read_idx)
#define INT_EN(stat)    (stat >> 1)
#define ALL_STAT        (STREAM_END_INT_ST | BUF_EMPTY_INT_ST | \
                         USER_DATA_INT_ST  | FIFO_LEVEL_INT_ST)


typedef struct S5pc1xxSpdifState {
    SysBusDevice busdev;
    qemu_irq     irq;

    uint32_t  spdclkcon;
    uint32_t  spdcon;
    uint32_t  spdbstas;
    uint32_t  spdcstas;
    uint32_t  spdcnt;
    uint32_t  userbit[3];
    uint32_t  version_info;

    struct shadowed_regs {
        uint32_t  spdbstas;
        uint32_t  spdcnt;
        uint32_t  userbit[3];
    } shd;

    QEMUTimer *spdif_timer;
    uint32_t  sclk_freq;
    int64_t   count;                /* has signed type */
    uint8_t   updated;

    uint32_t  fifo[2][16];
    uint64_t  read_idx, write_idx;
    uint8_t   read_ch, write_ch;

    uint32_t  sub_frame;
    int8_t    fifo_thr, endian_f;   /* have signed type */
    int16_t   Pa, Pb, Pc, Pd;       /* have signed type */
    uint8_t   lsb_pos, non_linear_pcm, stat_bit[16];
    uint8_t   first_state, second_state;
    uint16_t  rep_period, data_sframe_num;
} S5pc1xxSpdifState;


/* Reset SPDIF */
static void s5pc1xx_spdif_reset(S5pc1xxSpdifState *s)
{
    uint8_t i;

    s->spdclkcon    = 0x2;
    s->spdcon       = 0x1;
    s->spdbstas     = 0;
    s->spdcstas     = 0;
    s->spdcnt       = 0;
    s->shd.spdbstas = 0;
    s->shd.spdcnt   = 0;
    s->version_info = 0xC;

    for (i = 0; i < 3; i++) {
        s->userbit[i]     = 0;
        s->shd.userbit[i] = 0;
    }

    s->sclk_freq    = 0;
    s->count        = -1;

    s->read_idx     = 0;
    s->write_idx    = 0;
    s->read_ch      = 0;
    s->write_ch     = 0;

    s->fifo_thr     = -1;
    s->endian_f     = -1;
    s->lsb_pos      = 0;
    s->first_state  = 0;
    s->second_state = 0;

    s->Pa           = 0xF872;
    s->Pb           = 0x4E1F;
    s->Pc           = -1;
    s->Pd           = -1;
    s->data_sframe_num = 0;         /* used for non-linear PCM */
}

/* Interrupts handler */
static void s5pc1xx_spdif_irq(S5pc1xxSpdifState *s,
                              uint32_t stat, uint8_t to_clear)
{
    if (to_clear) {
        s->spdcon &= ~(stat);
        if (!(s->spdcon & ALL_STAT))    /* if all clear */
            qemu_irq_lower(s->irq);
    } else {
        s->spdcon |= stat;
        if (s->spdcon & INT_EN(stat))   /* if enabled */
            qemu_irq_raise(s->irq);
    }
}

/* -=FIFO HANDLING=- */

/* Determine threshold FIFO depth */
static void s5pc1xx_spdif_fifo_thr(S5pc1xxSpdifState *s)
{
    uint8_t thr[8]  = {0, 1, 4, 6, 10, 12, 14, 15};
    uint8_t thr_idx = (s->spdcon >> FIFO_LEVEL_THR_SHIFT) & 0x7;

    s->fifo_thr = thr[thr_idx];
}

/* Control FIFO level */
static void s5pc1xx_spdif_fifo_control(S5pc1xxSpdifState *s)
{
    if (s->fifo_thr < 0)
        s5pc1xx_spdif_fifo_thr(s);

    if (FIFO_DEPTH > s->fifo_thr)
        s5pc1xx_spdif_irq(s, FIFO_LEVEL_INT_ST, 0);

    if (FIFO_DEPTH == 0)
        s5pc1xx_spdif_irq(s, BUF_EMPTY_INT_ST, 0);
}

/* Determine endian format index */
static void s5pc1xx_spdif_endian_idx(S5pc1xxSpdifState *s)
{
    s->endian_f = (s->spdcon >> ENDIAN_FORMAT_SHIFT) & 0x3;
}

/* Convert value according to current endian format */
static uint32_t s5pc1xx_spdif_endian_format(S5pc1xxSpdifState *s,
                                            uint32_t value)
{
    uint32_t ret_val = 0;

    if (s->endian_f < 0)
        s5pc1xx_spdif_endian_idx(s);

    switch(s->endian_f) {
    case 0:
        ret_val = value & ALL_BITS(23, 0);
        break;
    case 1:
        ret_val = (((value & ALL_BITS(15,  8)) <<  8) |
                   ((value & ALL_BITS(23, 16)) >>  8) |
                   ((value & ALL_BITS(31, 24)) >> 24));
        break;
    case 2:
        ret_val = (((value & ALL_BITS( 7,  0)) << 16) |
                   ((value & ALL_BITS(15,  8)) >>  0) |
                   ((value & ALL_BITS(23, 16)) >> 16));
        break;
    case 3:
        ret_val = (((value & ALL_BITS( 7,  0)) <<  8) |
                   ((value & ALL_BITS(15,  8)) >>  8));
        break;
    }
    return ret_val;
}

/* Get value from FIFO */
static uint32_t s5pc1xx_spdif_fifo_read(S5pc1xxSpdifState *s)
{
    uint32_t ret_val = 0;

    if (FIFO_DEPTH > 0) {
        ret_val = s->fifo[s->read_ch][s->read_idx % 16];

        if (s->read_ch == 1)
            s->read_idx++;
    }
    s->read_ch = (s->read_ch + 1) % 2;
    s5pc1xx_spdif_fifo_control(s);

    return ret_val;
}

/* Put value into FIFO */
static void s5pc1xx_spdif_fifo_write(S5pc1xxSpdifState *s, uint32_t value)
{
    value = s5pc1xx_spdif_endian_format(s, value);

    if (FIFO_DEPTH < 16) {
        s->fifo[s->write_ch][s->write_idx % 16] = value;

        if (s->write_ch == 1)
            s->write_idx++;
    }
    s->write_ch = (s->write_ch + 1) % 2;
    s5pc1xx_spdif_fifo_control(s);
}

/* -=SPDIF MAIN LOGIC=- */

/* Update burst params (for non-linear PCM) */
static void s5pc1xx_spdif_stream_end(S5pc1xxSpdifState *s)
{
    s->shd.spdbstas = s->spdbstas;
    s->shd.spdcnt   = s->spdcnt;

    s->Pc = (s->shd.spdbstas >>  0) & ALL_BITS(15, 0);
    s->Pd = (s->shd.spdbstas >> 16) & ALL_BITS(15, 0);
    s->rep_period = (s->shd.spdcnt) & ALL_BITS(12, 0);

    s5pc1xx_spdif_irq(s, STREAM_END_INT_ST, 0);
}

/* Spdif_tx block */
static uint32_t s5pc1xx_spdif_tx_block(S5pc1xxSpdifState *s)
{
    uint32_t value;
    uint16_t next_payload_size;

    /* check for beginning of new payload frame */
    if ((s->data_sframe_num > 3) && !(s->data_sframe_num % 2)) {
        /* check if bit stream size will be past s->rep_period
         * value after next write routine and avoid this */
        if ((s->data_sframe_num + 2) * 16 > s->rep_period) {
            s5pc1xx_spdif_stream_end(s);
            s->data_sframe_num = 0;
        }
    }

    next_payload_size =
        (s->data_sframe_num > 3) ? (s->data_sframe_num - 4 + 2) * 16 : 0;

    switch(s->data_sframe_num) {
    case 0:
        value = s->Pa;
        break;
    case 1:
        value = s->Pb;
        break;
    case 2:
        value = s->Pc;
        break;
    case 3:
        value = s->Pd;
        break;
    default:
        if (next_payload_size > s->Pd) {
            value = 0;
        } else {
            value = s5pc1xx_spdif_fifo_read(s);
        }
    }
    s->data_sframe_num++;

    return value;
}

/* Determine LSB position within sub-frame */
static void s5pc1xx_spdif_lsb(S5pc1xxSpdifState *s)
{
    uint8_t data_size;

    if (s->non_linear_pcm) {
        s->lsb_pos = 12;
        return;
    }
    data_size = (((s->spdcon >> PCM_DATA_SIZE_SHIFT) & 0x3) + 4) << 2;
    s->lsb_pos = 28 - data_size;
}

/* Convert audio data to SPDIF format
 * (compose 0~31 time slots into s->sub_frame) */
static void s5pc1xx_spdif_sub_frame(S5pc1xxSpdifState *s)
{
    uint32_t value;
    uint8_t  ballast, preamble, v_flag, user_bit, parity_bit;
    uint8_t  i, ones;
    uint16_t carrier_sframe_num = (s->count / 64) % 384;

    ballast = (s->second_state) ? 0 : 0x3;  /* 2b'00 or 2b'11 */

    /* Determine preamble */
    if (carrier_sframe_num % 2) {
        /* channel 2 has odd sub-frames numbers and always has W-preamble */
        preamble = W_PREAMBLE;
    } else {
        /* channel 1 has even sub-frames numbers and has M-preamble except
         * the first sub-frame */
        if (carrier_sframe_num) {
            preamble = M_PREAMBLE;
        } else {
            preamble = B_PREAMBLE;
        }
    }

    /* Determine audio sample word */
    if (s->non_linear_pcm) {
        value = s5pc1xx_spdif_tx_block(s);
    } else {
        value = s5pc1xx_spdif_fifo_read(s);
    }

    /* Validity flag, user bit and channel status */
    v_flag = (value) ? 0 : 1;
    /* TODO: User bit is always 0 for linear-PCM, but PCM user data is set
     * for non-linear PCM in the registers USERBIT1~3(_SHD). The allocation
     * of user data between two channels is unclear in the documentation.
     * So, user data transmission is not inplemented so far. */
    user_bit = 0;

    /* Compose sub_frame without parity_bit */
    if (!(s->lsb_pos))
        s5pc1xx_spdif_lsb(s);

    s->sub_frame = ballast | (preamble << 2) |
                   (value << s->lsb_pos & ALL_BITS(27, s->lsb_pos)) |
                   (v_flag << 28) | (user_bit << 29) |
                   (s->stat_bit[carrier_sframe_num % 2] << 30);

    /* Determine parity_bit */
    ones = parity_bit = 0;
    for (i = 4; i < 31; i++) {
        if (s->sub_frame >> i & 0x1)
            ones++;
    }
    if (ones % 2)
        parity_bit = 1;

    s->sub_frame |= (parity_bit << 31);
}

/* Channel coding of source signal */
static void s5pc1xx_spdif_channel_coding(S5pc1xxSpdifState *s)
{
    uint8_t cur_pos = (s->count % 64) / 2;
    uint8_t source_coding = s->sub_frame >> cur_pos & 0x1;

    /* Ballast handling (ballast ensures one deference
     * from bi-phase scheme in channel coded preamble) */
    if (cur_pos < 2) {
        s->first_state = s->second_state = source_coding;
        return;
    }

    /* Preamble and data handling */
    if (cur_pos < 3) {
        /* the line below ensures one more deference
         * from bi-phase scheme in channel coded preamble */
        s->first_state = s->second_state;
    } else {
        s->first_state = !(s->second_state);
    }

    if (source_coding) {
        s->second_state = !(s->first_state);
    } else {
        s->second_state = s->first_state;
    }
}

/* -=TIMER HANDLING=- */

/* Update timer frequency */
static void s5pc1xx_spdif_sclk_update(S5pc1xxSpdifState *s)
{
    uint8_t freq_id = s->spdcstas >> SAMP_FREQ_SHIFT & 0xF;

    switch (freq_id) {
    case 0:
        /* samplling_freq x 32 time slots x 2 channels x bi-phase mark */
        s->sclk_freq = 44100 * 32 * 2 * 2;
        break;
    case 2:
        s->sclk_freq = 48000 * 32 * 2 * 2;
        break;
    case 3:
        s->sclk_freq = 32000 * 32 * 2 * 2;
        break;
    case 10:
        s->sclk_freq = 96000 * 32 * 2 * 2;
        break;
    default:
        hw_error("s5pc1xx_spdif: frequency id %u is not supported\n", freq_id);
    }
}

/* Sync timer engine */
static void s5pc1xx_spdif_sync(void *opaque)
{
    S5pc1xxSpdifState *s = (S5pc1xxSpdifState *)opaque;
    uint64_t next_spdif_time;

    if (s->spdclkcon & POWER_ON) {
        if (!(s->sclk_freq))
            s5pc1xx_spdif_sclk_update(s);

        /* s->updated is used to avoid conflict between two functions
         * in case of synchronous runs */
        s->updated = 0;

        s->count++;

        if (!(s->count % 64))
            s5pc1xx_spdif_sub_frame(s);

        if (!(s->count % 2))
            s5pc1xx_spdif_channel_coding(s);

        s->updated = 1;

        next_spdif_time = qemu_get_clock(vm_clock) +
                          muldiv64(1, get_ticks_per_sec(), s->sclk_freq);
        qemu_mod_timer(s->spdif_timer, next_spdif_time);
    } else {
        s->spdclkcon |= CLK_DWN_READY;
        qemu_del_timer(s->spdif_timer);
    }
}

/* -=RELATION WITH GPIO AND OS=- */

/* Read SPDIF by GPIO */
static uint32_t s5pc1xx_spdif_gpio_read(void *opaque, int io_index)
{
    S5pc1xxSpdifState *s = (S5pc1xxSpdifState *)opaque;

    if (io_index == SPDIF_0_OUT) {
        if (s->count % 2) {
            return s->second_state;
        } else {
            if (s->updated) {
                return s->first_state;
            } else {
                return s->second_state;
            }
        }
    }
    return 0;
}

static GPIOReadMemoryFunc *s5pc1xx_spdif_gpio_readfn   = s5pc1xx_spdif_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_spdif_gpio_writefn = s5pc1xx_empty_gpio_write;

/* Read SPDIF by OS */
static uint32_t s5pc1xx_spdif_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxSpdifState *s = (S5pc1xxSpdifState *)opaque;

    switch(offset) {
    case SPDCLKCON:
        return s->spdclkcon;
    case SPDCON:
        s->spdcon = ((FIFO_DEPTH << FIFO_LEVEL_SHIFT) & ALL_BITS(26, 22)) |
                    (s->spdcon & ALL_BITS(21 ,0));
        return s->spdcon;
    case SPDBSTAS:
        return s->spdbstas;
    case SPDCSTAS:
        return s->spdcstas;
    case SPDCNT:
        return s->spdcnt;
    case SPDBSTAS_SHD:
        return s->shd.spdbstas;
    case SPDCNT_SHD:
        return s->shd.spdcnt;
    case USERBIT1 ... USERBIT3:
        return s->userbit[(offset - USERBIT1) / (USERBIT2 - USERBIT1)];
    case USERBIT1_SHD ... USERBIT3_SHD:
        return s->shd.userbit[(offset - USERBIT1_SHD) /
                              (USERBIT2_SHD - USERBIT1_SHD)];
    case VERSION_INFO:
        return s->version_info;
    default:
        hw_error("s5pc1xx_spdif: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

/* Write SPDIF by OS */
static void s5pc1xx_spdif_write(void *opaque, target_phys_addr_t offset,
                                uint32_t value)
{
    uint32_t old_val;
    S5pc1xxSpdifState *s = (S5pc1xxSpdifState *)opaque;

    switch(offset) {
    case SPDCLKCON:
        old_val = s->spdclkcon;

        /* Note: the CLK_DWN_READY field is read-only */
        s->spdclkcon =
            (value & (POWER_ON | MAIN_CLK_SEL)) | (old_val & CLK_DWN_READY);

        if ((value & POWER_ON) > (old_val & POWER_ON)) {
            s5pc1xx_spdif_stream_end(s);
            s5pc1xx_spdif_sync(s);
            s->spdclkcon &= ~CLK_DWN_READY;
        }
        break;
    case SPDCON:
        old_val = s->spdcon;

        /* Note: the 'FIFO level' field is read-only */
        s->spdcon = (old_val & ALL_BITS(26, 22)) | (value & ALL_BITS(21, 0));

        /* Check 'FIFO level threshold' field for update */
        if ((value & ALL_BITS(21, 19)) != (old_val & ALL_BITS(21, 19)))
            s5pc1xx_spdif_fifo_thr(s);

        /* Check 'Endian format' field for update */
        if ((value & ALL_BITS(14, 13)) != (old_val & ALL_BITS(14, 13)))
            s5pc1xx_spdif_endian_idx(s);

        /* Check 'PCM data size' field for update */
        if ((value & ALL_BITS(2, 1)) != (old_val & ALL_BITS(2, 1)))
            s5pc1xx_spdif_lsb(s);

        /* Clear irq states if any */
        if (value & ALL_STAT)
            s5pc1xx_spdif_irq(s, (value & ALL_STAT), 1);

        break;
    case SPDBSTAS:
        s->spdbstas = value;
        break;
    case SPDCSTAS:
        old_val = s->spdcstas;
        s->spdcstas = value;

        /* Check 'Sampling frequency' field for update */
        if ((value & ALL_BITS(27, 24)) != (old_val & ALL_BITS(27, 24)))
            s5pc1xx_spdif_sclk_update(s);

        s->stat_bit[(value >> CH_NUM_SHIFT) & ALL_BITS(3, 0)] = value & 0x1;

        s->non_linear_pcm = (value & AUDIO_WORD) ? 1 : 0;

        break;
    case SPDCNT:
        s->spdcnt = value;
        break;
    case SPDDAT:
        s5pc1xx_spdif_fifo_write(s, value);
        break;
    case USERBIT1 ... USERBIT3:
        s->userbit[(offset - USERBIT1) / (USERBIT2 - USERBIT1)] = value;
        break;
    default:
        hw_error("s5pc1xx_spdif: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_spdif_readfn[] = {
   s5pc1xx_spdif_read,
   s5pc1xx_spdif_read,
   s5pc1xx_spdif_read
};

static CPUWriteMemoryFunc * const s5pc1xx_spdif_writefn[] = {
   s5pc1xx_spdif_write,
   s5pc1xx_spdif_write,
   s5pc1xx_spdif_write
};

/* SPDIF initialization */
static int s5pc1xx_spdif_init(SysBusDevice *dev)
{
    S5pc1xxSpdifState *s = FROM_SYSBUS(S5pc1xxSpdifState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_spdif_readfn, s5pc1xx_spdif_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_SPDIF_REG_MEM_SIZE, iomemtype);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_SPDIF, 0,
                                    s5pc1xx_spdif_gpio_readfn,
                                    s5pc1xx_spdif_gpio_writefn, NULL, s);

    s->spdif_timer = qemu_new_timer(vm_clock, s5pc1xx_spdif_sync, s);

    s5pc1xx_spdif_reset(s);

    return 0;
}

static void s5pc1xx_spdif_register(void)
{
    sysbus_register_dev("s5pc1xx,spdif", sizeof(S5pc1xxSpdifState),
                        s5pc1xx_spdif_init);
}

device_init(s5pc1xx_spdif_register)
