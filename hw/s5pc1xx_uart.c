/*
 * S5PC1XX UART Emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

#include "sysbus.h"
#include "qemu-char.h"
#include "s5pc1xx.h"
#include "s5pc1xx_gpio_regs.h"


#define QUEUE_SIZE   257

#define INT_RXD     (1 << 0)
#define INT_ERROR   (1 << 1)
#define INT_TXD     (1 << 2)
#define INT_MODEM   (1 << 3)

#define TRSTATUS_TRANSMITTER_READY  (1 << 2)
#define TRSTATUS_BUFFER_EMPTY       (1 << 1)
#define TRSTATUS_DATA_READY         (1 << 0)

#define UFSTAT_RX_FIFO_FULL         (1 << 8)

#define UFCON_FIFO_ENABLED          (1 << 0)
#define UFCON_TX_LEVEL_SHIFT        8
#define UFCON_TX_LEVEL              (7 << UFCON_TX_LEVEL_SHIFT)

#define UFSTAT_TX_COUNT_SHIT        16
#define UFSTAT_TX_COUNT             (0xFF << UFSTAT_TX_COUNT_SHIT)

#define QI(x) ((x + 1) % QUEUE_SIZE)
#define QD(x) ((x - 1 + QUEUE_SIZE) % QUEUE_SIZE)

#define S5PC1XX_UART_REG_MEM_SIZE 0x3C

typedef struct UartQueue {
    uint8_t queue[QUEUE_SIZE];
    uint32_t s, t;
    uint32_t size;
} UartQueue;

typedef struct S5pc1xxUartState {
    SysBusDevice busdev;

    UartQueue rx;

    uint32_t ulcon;
    uint32_t ucon;
    uint32_t ufcon;
    uint32_t umcon;
    uint32_t utrstat;
    uint32_t uerstat;
    uint32_t ufstat;
    uint32_t umstat;
    uint32_t utxh;
    uint32_t urxh;
    uint32_t ubrdiv;
    uint32_t udivslot;
    uint32_t uintp;
    uint32_t uintsp;
    uint32_t uintm;

    CharDriverState *chr;
    qemu_irq irq;
    uint32_t instance;
} S5pc1xxUartState;


static inline int queue_elem_count(const UartQueue *s)
{
    if (s->t >= s->s) {
        return s->t - s->s;
    } else {
        return QUEUE_SIZE - s->s + s->t;
    }
}

static inline int queue_empty_count(const UartQueue *s)
{
    return s->size - queue_elem_count(s) - 1;
}

static inline int queue_empty(const UartQueue *s)
{
    return (queue_elem_count(s) == 0);
}

static inline void queue_push(UartQueue *s, uint8_t x)
{
    s->queue[s->t] = x;
    s->t = QI(s->t);
}

static inline uint8_t queue_get(UartQueue *s)
{
    uint8_t ret;

    ret = s->queue[s->s];
    s->s = QI(s->s);
    return ret;
}

static inline void queue_reset(UartQueue *s)
{
    s->s = 0;
    s->t = 0;
}

static void s5pc1xx_uart_update(S5pc1xxUartState *s)
{
    if (s->ufcon && UFCON_FIFO_ENABLED) {
        if (((s->ufstat && UFSTAT_TX_COUNT) >> UFSTAT_TX_COUNT_SHIT) <=
            ((s->ufcon && UFCON_TX_LEVEL) >> UFCON_TX_LEVEL_SHIFT) * 2 ) {
            s->uintsp |= INT_TXD;
        }
    }

    s->uintp = s->uintsp & ~s->uintm;
    if (s->uintp) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

/* Read UART by GPIO */
static uint32_t s5pc1xx_uart_gpio_read(void *opaque,
                                       int io_index)
{
    S5pc1xxUartState *s = (S5pc1xxUartState *)opaque;

    /* TODO: check if s->uintp should be used instead of s->uintsp */
    if (io_index == GPIO_UART_RXD(s->instance))
        return (s->uintsp & INT_RXD);
    if (io_index == GPIO_UART_TXD(s->instance))
        return (s->uintsp & INT_TXD);

    /* TODO: check if this is correct */
    if (io_index == GPIO_UART_CTS(s->instance))
        return ~(s->umstat & 0x1);
    if (io_index == GPIO_UART_RTS(s->instance))
        return ~(s->umcon  & 0x1);

    /* TODO: return correct values */
    if (io_index == GPIO_UART_AUDIO_RXD)
        return 0;
    if (io_index == GPIO_UART_AUDIO_TXD)
        return 0;

    return 0;
}

static GPIOReadMemoryFunc *s5pc1xx_uart_gpio_readfn   = s5pc1xx_uart_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_uart_gpio_writefn = s5pc1xx_empty_gpio_write;   /* a gag */

static uint32_t s5pc1xx_uart_mm_read(void *opaque, target_phys_addr_t offset)
{
    uint32_t res;
    S5pc1xxUartState *s = (S5pc1xxUartState *)opaque;

    switch (offset) {
    case 0x00:
        return s->ulcon;
    case 0x04:
        return s->ucon;
    case 0x08:
        return s->ufcon;
    case 0x0C:
        return s->umcon;
    case 0x10:
        return s->utrstat;
    case 0x14:
        res = s->uerstat;
        s->uerstat = 0;
        return res;
    case 0x18:
        s->ufstat = queue_elem_count(&s->rx) & 0xff;
        if (queue_empty_count(&s->rx) == 0)
            s->ufstat |= UFSTAT_RX_FIFO_FULL;
        return s->ufstat;
    case 0x1C:
        return s->umstat;
    case 0x24:
        if (s->ufcon & 1) {
            if (! queue_empty(&s->rx)) {
                res = queue_get(&s->rx);
                if (queue_empty(&s->rx)) {
                    s->utrstat &= ~TRSTATUS_DATA_READY;
                } else {
                    s->utrstat |= TRSTATUS_DATA_READY;
                }
            } else {
                s->uintsp |= INT_ERROR;
                s5pc1xx_uart_update(s);
                res = 0;
            }
        } else {
            s->utrstat &= ~TRSTATUS_DATA_READY;
            res = s->urxh;
        }
        return res;
    case 0x28:
        return s->ubrdiv;
    case 0x2C:
        return s->udivslot;
    case 0x30:
        return s->uintp;
    case 0x34:
        return s->uintsp;
    case 0x38:
        return s->uintm;
    default:
        hw_error("s5pc1xx_uart: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static void s5pc1xx_uart_mm_write(void *opaque, target_phys_addr_t offset,
                                  uint32_t val)
{
    uint8_t ch;
    S5pc1xxUartState *s = (S5pc1xxUartState *)opaque;

    switch (offset) {
    case 0x00:
        s->ulcon = val;
        break;
    case 0x04:
        s->ucon = val;
        break;
    case 0x08:
        s->ufcon = val;
        if (val & 2) {
            queue_reset(&s->rx);
        }
        s->ufcon &= ~6;
        break;
    case 0x0C:
        s->umcon = val;
        break;
    case 0x20:
        if (s->chr) {
            s->utrstat &= ~(TRSTATUS_TRANSMITTER_READY | TRSTATUS_BUFFER_EMPTY);
            ch = (uint8_t)val;
            qemu_chr_write(s->chr, &ch, 1);
            s->utrstat |= TRSTATUS_TRANSMITTER_READY | TRSTATUS_BUFFER_EMPTY;
            s->uintsp |= INT_TXD;
        }
        break;
    case 0x28:
        s->ubrdiv = val;
        break;
    case 0x2C:
        s->udivslot = val;
        break;
    case 0x30:
        s->uintp &= ~val;
        s->uintsp &= ~val; /* TODO: does this really work in this way??? */
        break;
    case 0x34:
        s->uintsp = val;
        break;
    case 0x38:
        s->uintm = val;
        break;
    default:
        hw_error("s5pc1xx_uart: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
    s5pc1xx_uart_update(s);
}

CPUReadMemoryFunc * const s5pc1xx_uart_readfn[] = {
    s5pc1xx_uart_mm_read,
    s5pc1xx_uart_mm_read,
    s5pc1xx_uart_mm_read
};

CPUWriteMemoryFunc * const s5pc1xx_uart_writefn[] = {
    s5pc1xx_uart_mm_write,
    s5pc1xx_uart_mm_write,
    s5pc1xx_uart_mm_write
};

static int s5pc1xx_uart_can_receive(void *opaque)
{
    S5pc1xxUartState *s = (S5pc1xxUartState *)opaque;

    return queue_empty_count(&s->rx);
}

static void s5pc1xx_uart_trigger_level(S5pc1xxUartState *s)
{
    /* TODO: fix this */
    if (! queue_empty(&s->rx)) {
        s->uintsp |= INT_RXD;
    }
}

static void s5pc1xx_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    int i;
    S5pc1xxUartState *s = (S5pc1xxUartState *)opaque;
    if (s->ufcon & 1) {
        if (queue_empty_count(&s->rx) < size) {
            for (i = 0; i < queue_empty_count(&s->rx); i++) {
                queue_push(&s->rx, buf[i]);
            }
            s->uintp |= INT_ERROR;
            s->utrstat |= TRSTATUS_DATA_READY;
        } else {
            for (i = 0; i < size; i++) {
                queue_push(&s->rx, buf[i]);
            }
            s->utrstat |= TRSTATUS_DATA_READY;
        }
        s5pc1xx_uart_trigger_level(s);
    } else {
        s->urxh = buf[0];
        s->uintsp |= INT_RXD;
        s->utrstat |= TRSTATUS_DATA_READY;
    }
    s5pc1xx_uart_update(s);
}

static void s5pc1xx_uart_event(void *opaque, int event)
{
    /* TODO: implement this */
}

static void s5pc1xx_uart_reset(void *opaque)
{
    S5pc1xxUartState *s = (S5pc1xxUartState *)opaque;

    s->ulcon    = 0;
    s->ucon     = 0;
    s->ufcon    = 0;
    s->umcon    = 0;
    s->utrstat  = 0x6;
    s->uerstat  = 0;
    s->ufstat   = 0;
    s->umstat   = 0;
    s->ubrdiv   = 0;
    s->udivslot = 0;
    s->uintp    = 0;
    s->uintsp   = 0;
    s->uintm    = 0;
    queue_reset(&s->rx);
}

DeviceState *s5pc1xx_uart_init(target_phys_addr_t base, int instance,
                               int queue_size, qemu_irq irq,
                               CharDriverState *chr)
{
    DeviceState *dev = qdev_create(NULL, "s5pc1xx,uart");
    char str[] = "s5pc1xx,uart,00";

    if (!chr) {
        snprintf(str, strlen(str) + 1, "s5pc1xx,uart,%02d", instance % 100);
        chr = qemu_chr_open(str, "null", NULL);
    }
    qdev_prop_set_chr(dev, "chr", chr);
    qdev_prop_set_uint32(dev, "queue-size", queue_size);
    qdev_prop_set_uint32(dev, "instance", instance);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

static int s5pc1xx_uart_init1(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxUartState *s = FROM_SYSBUS(S5pc1xxUartState, dev);

    s5pc1xx_uart_reset(s);

    sysbus_init_irq(dev, &s->irq);

    qemu_chr_add_handlers(s->chr, s5pc1xx_uart_can_receive,
                          s5pc1xx_uart_receive, s5pc1xx_uart_event, s);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_uart_readfn, s5pc1xx_uart_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_UART_REG_MEM_SIZE, iomemtype);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_UART, s->instance,
                                    s5pc1xx_uart_gpio_readfn,
                                    s5pc1xx_uart_gpio_writefn, NULL, s);
    return 0;
}

static SysBusDeviceInfo s5pc1xx_uart_info = {
    .init = s5pc1xx_uart_init1,
    .qdev.name  = "s5pc1xx,uart",
    .qdev.size  = sizeof(S5pc1xxUartState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("instance",   S5pc1xxUartState, instance, 0),
        DEFINE_PROP_UINT32("queue-size", S5pc1xxUartState, rx.size, 16),
        DEFINE_PROP_CHR("chr", S5pc1xxUartState, chr),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5pc1xx_uart_register(void)
{
    sysbus_register_withprop(&s5pc1xx_uart_info);
}

device_init(s5pc1xx_uart_register)
