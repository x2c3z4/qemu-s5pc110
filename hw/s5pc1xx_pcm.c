/*
 * PCM audio interface for Samsung S5PC110-based board emulation
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "s5pc1xx.h"
#include "qemu-timer.h"
#include "s5pc1xx_gpio_regs.h"
#include "sysbus.h"


#define ALL_BITS(b,a)               (((1 << (b - a + 1)) - 1) << a)

/* R/W Specifies the PCM Main Control 0x00000000 */
#define PCM_CTL             0x00
    #define TXFIFO_DIPSTICK_SHIFT   13      /* 6 bits */
    #define RXFIFO_DIPSTICK_SHIFT   7       /* 6 bits */
    #define PCM_TX_DMA_EN           (1 << 6)
    #define PCM_RX_DMA_EN           (1 << 5)
    #define TX_MSB_POS              (1 << 4)
    #define RX_MSB_POS              (1 << 3)
    #define PCM_TXFIFO_EN           (1 << 2)
    #define PCM_RXFIFO_EN           (1 << 1)
    #define PCM_PCM_ENABLE          (1 << 0)

/* R/W Specifies the PCM Clock and Shift control 0x00000000 */
#define PCM_CLKCTL          0x04
    #define CTL_SERCLK_EN           (1 << 19)
    #define CTL_SERCLK_SEL          (1 << 18)   /* TODO: now is set high as default */
    #define SCLK_DIV_SHIFT          9
    #define SYNC_DIV_SHIFT          0

/* R/W Specifies the PCM TxFIFO write port 0x00010000 */
#define PCM_TXFIFO          0x08
    #define TXFIFO_DVALID           (1 << 16)

/* R/W Specifies the PCM RxFIFO read port 0x00010000 */
#define PCM_RXFIFO          0x0C
    #define RXFIFO_DVALID           (1 << 16)

/* R/W Specifies the PCM Interrupt Control 0x00000000 */
#define PCM_IRQ_CTL         0x10
    #define INT_EN(stat)            (stat)
    #define EN_IRQ_TO_ARM           (1 << 14)

/* R Specifies the PCM Interrupt Status 0x00000000 */
#define PCM_IRQ_STAT        0x14
    #define IRQ_PENDING             (1 << 13)
    #define TRANSFER_DONE           (1 << 12)
    #define TXFIFO_EMPTY            (1 << 11)
    #define TXFIFO_ALMOST_EMPTY     (1 << 10)
    #define TXFIFO_FULL             (1 << 9)
    #define TXFIFO_ALMOST_FULL      (1 << 8)
    #define TXFIFO_ERROR_STARVE     (1 << 7)
    #define TXFIFO_ERROR_OVERFLOW   (1 << 6)
    #define RXFIFO_EMPTY            (1 << 5)
    #define RXFIFO_ALMOST_EMPTY     (1 << 4)
    #define RXFIFO_FULL             (1 << 3)
    #define RXFIFO_ALMOST_FULL      (1 << 2)
    #define RXFIFO_ERROR_STARVE     (1 << 1)
    #define RXFIFO_ERROR_OVERFLOW   (1 << 0)
    #define ALL_STAT                ALL_BITS(13,0)

/* R Specifies the PCM FIFO Status 0x00000000 */
#define PCM_FIFO_STAT       0x18
    #define TXFIFO_COUNT_SHIFT       14     /* 6 bits */
    #define TXFIFO_EMPTY_FLAG        (1 << 13)
    #define TXFIFO_ALMOST_EMPTY_FLAG (1 << 12)
    #define TXFIFO_FULL_FLAG         (1 << 11)
    #define TXFIFO_ALMOST_FULL_FLAG  (1 << 10)
    #define RXFIFO_COUNT_SHIFT       4      /* 6 bits */
    #define RXFIFO_EMPTY_FLAG        (1 << 3)
    #define RXFIFO_ALMOST_EMPTY_FLAG (1 << 2)
    #define RXFIFO_FULL_FLAG         (1 << 1)
    #define RXFIFO_ALMOST_FULL_FLAG  (1 << 0)

/* W Specifies the PCM Interrupt Clear - */
#define PCM_CLRINT          0x20

#define S5PC1XX_PCM_REG_MEM_SIZE    0x24


typedef struct S5pc1xxPCMState {

    SysBusDevice busdev;
    qemu_irq     irq;
    uint32_t     instance;

    uint32_t ctl;
    uint32_t clk_ctl;
    uint32_t irq_ctl;
    uint32_t irq_stat;
    uint32_t fifo_stat;

    struct FrameIn {
        uint16_t fifo[32];

        uint64_t write_idx;
        uint64_t read_idx;

        uint8_t fifo_dipstick;
        uint8_t delay;
   } rx;

    struct FrameOut {
        uint16_t fifo[32];

        uint64_t write_idx;
        uint64_t read_idx;

        uint8_t fifo_dipstick;
        uint8_t delay;
    } tx;

    QEMUTimer *pcm_timer;
    uint16_t  sync_div;
    uint32_t  sync_freq;
    uint32_t  sclk_freq;
    uint64_t  last_pcm_time;

    uint8_t   sclk_en;
    uint8_t   pcm_io_en;

} S5pc1xxPCMState;


/* Function for initialization and reset */
static void s5pc1xx_pcm_reset(S5pc1xxPCMState *s)
{
    s->ctl           = 0;
    s->clk_ctl       = 0x40000;
    s->irq_ctl       = 0;
    s->irq_stat      = 0;
    s->fifo_stat     = 0;

    s->rx.read_idx   = 0;
    s->rx.write_idx  = 0;
    s->rx.delay      = 0;

    s->tx.read_idx   = 0;
    s->tx.write_idx  = 0;
    s->tx.delay      = 0;

    s->sync_div      = 0;
    s->sync_freq     = 0;
    s->sclk_freq     = 0;
    s->last_pcm_time = 0;

    s->sclk_en       = 0;
    s->pcm_io_en     = 0;
}

/* Interrupts handler */
static void s5pc1xx_pcm_irq(S5pc1xxPCMState *s,
                            uint32_t stat, uint8_t clear)
{
    if (stat) {
        s->irq_stat |= (IRQ_PENDING | stat);
        if ((s->irq_ctl & EN_IRQ_TO_ARM) &&
            (s->irq_ctl & INT_EN(stat)))    /* if enabled */
                qemu_irq_raise(s->irq);
    }
    if (clear) {
        s->irq_stat &= ~(ALL_STAT); /* clear all at once */
        qemu_irq_lower(s->irq);
    }
}

/* Controls RXFIFO stage */
static void s5pc1xx_pcm_rx_control(S5pc1xxPCMState *s)
{
    uint8_t rx_depth;

    rx_depth = (s->rx.write_idx - s->rx.read_idx) % 33;

    if (rx_depth == 0) {
        s5pc1xx_pcm_irq(s, RXFIFO_EMPTY, 0);
        s->fifo_stat |= RXFIFO_EMPTY_FLAG;
    } else {
        s->fifo_stat &= ~(RXFIFO_EMPTY_FLAG);
    }

    if (rx_depth == 32) {
        s5pc1xx_pcm_irq(s, RXFIFO_FULL, 0);
        s->fifo_stat |= RXFIFO_FULL_FLAG;
    } else {
        s->fifo_stat &= ~(RXFIFO_FULL_FLAG);
    }

    if (rx_depth < s->rx.fifo_dipstick) {
        s5pc1xx_pcm_irq(s, RXFIFO_ALMOST_EMPTY, 0);
        s->fifo_stat |= RXFIFO_ALMOST_EMPTY_FLAG;
    } else {
        s->fifo_stat &= ~(RXFIFO_ALMOST_EMPTY_FLAG);
    }

    if (rx_depth > (32 - s->rx.fifo_dipstick)) {
        s5pc1xx_pcm_irq(s, RXFIFO_ALMOST_FULL, 0);
        s->fifo_stat |= RXFIFO_ALMOST_FULL_FLAG;
    } else {
        s->fifo_stat &= ~(RXFIFO_ALMOST_FULL_FLAG);
    }
}

/* Controls TXFIFO stage */
static void s5pc1xx_pcm_tx_control(S5pc1xxPCMState *s)
{
    uint8_t tx_depth;

    tx_depth = (s->tx.write_idx - s->tx.read_idx) % 33;

    if (tx_depth ==  0) {
        s5pc1xx_pcm_irq(s, TXFIFO_EMPTY, 0);
        s->fifo_stat |= TXFIFO_EMPTY_FLAG;
    } else {
        s->fifo_stat &= ~(TXFIFO_EMPTY_FLAG);
    }

    if (tx_depth == 32) {
        s5pc1xx_pcm_irq(s, TXFIFO_FULL, 0);
        s->fifo_stat |= TXFIFO_FULL_FLAG;
    } else {
        s->fifo_stat &= ~(TXFIFO_FULL_FLAG);
    }

    if (tx_depth < s->tx.fifo_dipstick) {
        s5pc1xx_pcm_irq(s, TXFIFO_ALMOST_EMPTY, 0);
        s->fifo_stat |= TXFIFO_ALMOST_EMPTY_FLAG;
    } else {
        s->fifo_stat &= ~(TXFIFO_ALMOST_EMPTY_FLAG);
    }

    if (tx_depth > (32 - s->tx.fifo_dipstick)) {
        s5pc1xx_pcm_irq(s, TXFIFO_ALMOST_FULL, 0);
        s->fifo_stat |= TXFIFO_ALMOST_FULL_FLAG;
    } else {
        s->fifo_stat &= ~(TXFIFO_ALMOST_FULL_FLAG);
    }
}

/* Increase fifo indexes */
static void s5pc1xx_pcm_next_frame(S5pc1xxPCMState *s)
{
    if (s->ctl & PCM_RXFIFO_EN) {
        if (s->rx.write_idx < s->rx.read_idx + 32)
            s->rx.write_idx++;
        s5pc1xx_pcm_rx_control(s);
    }

    if (s->ctl & PCM_TXFIFO_EN) {
        if (s->tx.read_idx < s->tx.write_idx)
            s->tx.read_idx++;
        s5pc1xx_pcm_tx_control(s);
    }
}

/* Determine sclk/sync_freq and sync_div */
static void s5pc1xx_pcm_sclk_update(S5pc1xxPCMState *s)
{
    S5pc1xxClk clk;
    uint16_t sclk_div;

    clk = s5pc1xx_findclk("pclk_66");

    if (!(s->pcm_io_en))
        s->clk_ctl = 0x40000;

    sclk_div = 2 * ((s->clk_ctl >> SCLK_DIV_SHIFT & ALL_BITS(8, 0)) + 1);
    s->sclk_freq = s5pc1xx_clk_getrate(clk) / sclk_div;

    if (!(s->sclk_freq))
        hw_error("s5pc1xx_pcm: timer frequency is zero\n");

    s->sync_div = (s->clk_ctl >> SYNC_DIV_SHIFT & ALL_BITS(8, 0)) + 1;
    s->sync_freq = s->sclk_freq / s->sync_div;
}

/* Sync timer */
static void s5pc1xx_pcm_sync(void *opaque)
{
    S5pc1xxPCMState *s = (S5pc1xxPCMState *)opaque;
    uint64_t next_pcm_time;

    if (s->sclk_en) {
        if (!(s->sync_freq))
            s5pc1xx_pcm_sclk_update(s);

        s5pc1xx_pcm_next_frame(s);

        s->last_pcm_time = qemu_get_clock(vm_clock);
        next_pcm_time =
            s->last_pcm_time + muldiv64(1, get_ticks_per_sec(), s->sync_freq);
        qemu_mod_timer(s->pcm_timer, next_pcm_time);
    } else {
        qemu_del_timer(s->pcm_timer);
    }
}

/* SCLK x2 cycles counter */
static uint16_t s5pc1xx_pcm_2sclk(S5pc1xxPCMState *s)
{
    uint32_t spent_1G;
    uint16_t spent_2sclk;

    if (s->last_pcm_time) {
        spent_1G = qemu_get_clock(vm_clock) - s->last_pcm_time;
        spent_2sclk =
            muldiv64(spent_1G, (s->sclk_freq * 2), get_ticks_per_sec());
        return spent_2sclk;
    } else {
        return 0;
    }
}

/* SCLK state */
static uint8_t s5pc1xx_pcm_sclk_s(S5pc1xxPCMState *s)
{
    if (s->sclk_en)
        return ((s5pc1xx_pcm_2sclk(s) % 2) ? 0 : 1);
    else
        return 0;
}

/* SYNC signal state */
static uint8_t s5pc1xx_pcm_sync_s(S5pc1xxPCMState *s)
{
    if (s->sclk_en)
        return (s5pc1xx_pcm_2sclk(s) < 2);
    else
        return 0;
}

/* Put PCM_SIN bit */
static void  s5pc1xx_write_rxfifo(S5pc1xxPCMState *s, uint32_t sdata)
{
    uint16_t cur_pos;

    if (!(s->sclk_en) || !(s->ctl & PCM_RXFIFO_EN))
        return;

    if (s->rx.write_idx == s->rx.read_idx + 32) {
        s5pc1xx_pcm_irq(s, RXFIFO_ERROR_OVERFLOW, 0);
        return;
    }

    cur_pos = (s5pc1xx_pcm_2sclk(s) / 2) % s->sync_div - s->rx.delay;

    if (cur_pos < 16)
        s->rx.fifo[s->rx.write_idx % 32] |= (sdata & 0x1) << (15 - cur_pos);
}

/* Return PCM_SOUT bit */
static uint8_t s5pc1xx_read_txfifo(S5pc1xxPCMState *s)
{
    uint16_t word, cur_pos;

    if (!(s->sclk_en) || !(s->ctl & PCM_TXFIFO_EN))
        return 0;

    if (s->tx.read_idx == s->tx.write_idx) {
        s5pc1xx_pcm_irq(s, TXFIFO_ERROR_STARVE, 0);
        return 0;
    }

    cur_pos = (s5pc1xx_pcm_2sclk(s) / 2) % s->sync_div - s->tx.delay;

    if (cur_pos > 15) {
        s5pc1xx_pcm_irq(s, TRANSFER_DONE, 0);
        return 0;
    }

    word = s->tx.fifo[s->tx.read_idx % 32];
    return (word >> (15 - cur_pos) & 0x1);
}

/* Read PCM by GPIO */
static uint32_t s5pc1xx_pcm_gpio_read(void *opaque, int io_index)
{
    S5pc1xxPCMState *s = (S5pc1xxPCMState *)opaque;

    if (!(s->pcm_io_en))
        return 0;

    if (io_index == PCM_SCLK(s->instance))
        return s5pc1xx_pcm_sclk_s(s);

    if (io_index == PCM_FSYNC(s->instance))
        return s5pc1xx_pcm_sync_s(s);

    if (io_index == PCM_SOUT(s->instance))
        return s5pc1xx_read_txfifo(s);

    return 0;
}

/* Write PCM by GPIO */
static void s5pc1xx_pcm_gpio_write(void *opaque, int io_index, uint32_t value)
{
    S5pc1xxPCMState *s = (S5pc1xxPCMState *)opaque;

    /* TODO: not implemented for now */
    if (io_index == PCM_EXTCLK(s->instance))
        return;

    if (io_index == PCM_SIN(s->instance)) {
        s5pc1xx_write_rxfifo(s, value);
    }
}

static GPIOReadMemoryFunc *s5pc1xx_pcm_gpio_readfn   = s5pc1xx_pcm_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_pcm_gpio_writefn = s5pc1xx_pcm_gpio_write;

/* Read PCM by OS */
static uint32_t s5pc1xx_pcm_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxPCMState *s = (S5pc1xxPCMState *)opaque;
    uint8_t rx_depth, tx_depth;
    uint32_t ret_val;

    switch(offset) {
    case PCM_CTL:
        return s->ctl;
    case PCM_CLKCTL:
        return s->clk_ctl;

    case PCM_TXFIFO:
        if (!(s->ctl & PCM_TX_DMA_EN) || !(s->ctl & PCM_TXFIFO_EN))
            return 0;

        return (s->tx.fifo[s->tx.read_idx % 32] | TXFIFO_DVALID);

    case PCM_RXFIFO:
        if (!(s->ctl & PCM_RX_DMA_EN) || !(s->ctl & PCM_RXFIFO_EN))
            return 0;

        if (s->rx.read_idx ==  s->rx.write_idx) {
            s5pc1xx_pcm_irq(s, RXFIFO_ERROR_STARVE, 0);
            return 0;
        }

        ret_val = s->rx.fifo[s->rx.read_idx % 32] | RXFIFO_DVALID;

        if (s->rx.read_idx < s->rx.write_idx)
            s->rx.read_idx++;

        s5pc1xx_pcm_rx_control(s);

        return ret_val;

    case PCM_IRQ_CTL:
        return s->irq_ctl;
    case PCM_IRQ_STAT:
        return s->irq_stat;

    case PCM_FIFO_STAT:
        rx_depth = (s->rx.write_idx - s->rx.read_idx) % 33;
        tx_depth = (s->tx.write_idx - s->tx.read_idx) % 33;
        ret_val  = (s->fifo_stat & ALL_BITS(3, 0)) | (rx_depth << 4) |
                   (s->fifo_stat & ALL_BITS(13, 10)) | (tx_depth << 14);
        return ret_val;

    default:
        hw_error("s5pc1xx_pcm: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

/* Write PCM by OS */
static void s5pc1xx_pcm_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5pc1xxPCMState *s = (S5pc1xxPCMState *)opaque;

    switch(offset) {
    case PCM_CTL:
        if ((value & PCM_PCM_ENABLE) < (s->ctl & PCM_PCM_ENABLE)) {
            s->pcm_io_en = 0;
            s5pc1xx_pcm_sclk_update(s);
        }
        /* the value is set low above */
        s->pcm_io_en = (!(value & PCM_PCM_ENABLE)) ? : 1;

        if ((value & PCM_RXFIFO_EN) < (s->ctl & PCM_RXFIFO_EN)) {
            s->rx.read_idx  = 0;
            s->rx.write_idx = 0;
        }
        if ((value & PCM_TXFIFO_EN) < (s->ctl & PCM_TXFIFO_EN)) {
            s->tx.read_idx  = 0;
            s->tx.write_idx = 0;
        }

        s->rx.delay         = (value & RX_MSB_POS) ? 1 : 0;
        s->rx.fifo_dipstick = value >> RXFIFO_DIPSTICK_SHIFT & ALL_BITS(6, 0);

        s->tx.delay         = (value & TX_MSB_POS) ? 1 : 0;
        s->tx.fifo_dipstick = value >> TXFIFO_DIPSTICK_SHIFT & ALL_BITS(6, 0);

        s->ctl = value;
        break;

    case PCM_CLKCTL:
        if ((value & CTL_SERCLK_EN) > (s->clk_ctl & CTL_SERCLK_EN)) {
            s->sclk_en = 1;
            s5pc1xx_pcm_sync(s);
        }
        /* the value is set high above */
        s->sclk_en = (value & CTL_SERCLK_EN) ? : 0;

        if (value != s->clk_ctl)
            s5pc1xx_pcm_sclk_update(s);

        s->clk_ctl = value;
        break;

    case PCM_TXFIFO:
        if (!(s->ctl & PCM_TX_DMA_EN) || !(s->ctl & PCM_TXFIFO_EN))
            break;

        if (s->tx.write_idx == s->tx.read_idx + 32) {
            s5pc1xx_pcm_irq(s, TXFIFO_ERROR_OVERFLOW, 0);
            break;
        }

        s->tx.fifo[s->tx.write_idx % 32] = value;

        if (s->tx.write_idx <  s->tx.read_idx + 32)
            s->tx.write_idx++;

        s5pc1xx_pcm_tx_control(s);

        break;

    case PCM_RXFIFO:
        if (!(s->ctl & PCM_RX_DMA_EN) || !(s->ctl & PCM_RXFIFO_EN))
            break;

        s->rx.fifo[s->rx.write_idx % 32] = value;
        break;

    case PCM_IRQ_CTL:
        s->irq_ctl = value;
        break;

    case PCM_CLRINT:
        /* clear all irq stats and lower the interrupt */
        s5pc1xx_pcm_irq(s, 0, 1);
        break;

    default:
        hw_error("s5pc1xx_pcm: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_pcm_readfn[] = {
   s5pc1xx_pcm_read,
   s5pc1xx_pcm_read,
   s5pc1xx_pcm_read
};

static CPUWriteMemoryFunc * const s5pc1xx_pcm_writefn[] = {
   s5pc1xx_pcm_write,
   s5pc1xx_pcm_write,
   s5pc1xx_pcm_write
};

DeviceState *s5pc1xx_pcm_init(target_phys_addr_t base, int instance,
                              qemu_irq irq)
{
    DeviceState *dev = qdev_create(NULL, "s5pc1xx,pcm");
    qdev_prop_set_uint32(dev, "instance", instance);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

/* PCM initialization */
static int s5pc1xx_pcm_init1(SysBusDevice *dev)
{
    S5pc1xxPCMState *s = FROM_SYSBUS(S5pc1xxPCMState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_pcm_readfn, s5pc1xx_pcm_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_PCM_REG_MEM_SIZE, iomemtype);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_PCM, s->instance,
                                    s5pc1xx_pcm_gpio_readfn,
                                    s5pc1xx_pcm_gpio_writefn, NULL, s);
    s->pcm_timer = qemu_new_timer(vm_clock, s5pc1xx_pcm_sync, s);

    s5pc1xx_pcm_reset(s);

    return 0;
}

static SysBusDeviceInfo s5pc1xx_pcm_info = {
    .init       = s5pc1xx_pcm_init1,
    .qdev.name  = "s5pc1xx,pcm",
    .qdev.size  = sizeof(S5pc1xxPCMState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("instance", S5pc1xxPCMState, instance, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5pc1xx_pcm_register(void)
{
    sysbus_register_withprop(&s5pc1xx_pcm_info);
}

device_init(s5pc1xx_pcm_register)
