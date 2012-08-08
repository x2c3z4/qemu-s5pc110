/*
 * S5PC1XX Keypad Interface
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 */

#include "console.h"
#include "s5pc1xx.h"
#include "s5pc1xx_gpio_regs.h"
#include "sysbus.h"


#define KEY_RESERVED    0
#define KEY_ESC         1
#define KEY_1           2
#define KEY_2           3
#define KEY_3           4
#define KEY_4           5
#define KEY_5           6
#define KEY_6           7
#define KEY_7           8
#define KEY_8           9
#define KEY_9           10
#define KEY_0           11
#define KEY_MINUS       12
#define KEY_EQUAL       13
#define KEY_BACKSPACE   14
#define KEY_TAB         15
#define KEY_Q           16
#define KEY_W           17
#define KEY_E           18
#define KEY_R           19
#define KEY_T           20
#define KEY_Y           21
#define KEY_U           22
#define KEY_I           23
#define KEY_O           24
#define KEY_P           25
#define KEY_LEFTBRACE   26
#define KEY_RIGHTBRACE  27
#define KEY_ENTER       28
#define KEY_LEFTCTRL    29
#define KEY_A           30
#define KEY_S           31
#define KEY_D           32
#define KEY_F           33
#define KEY_G           34
#define KEY_H           35
#define KEY_J           36
#define KEY_K           37
#define KEY_L           38
#define KEY_SEMICOLON   39
#define KEY_APOSTROPHE  40
#define KEY_GRAVE       41
#define KEY_LEFTSHIFT   42
#define KEY_BACKSLASH   43
#define KEY_Z           44
#define KEY_X           45
#define KEY_C           46
#define KEY_V           47
#define KEY_B           48
#define KEY_N           49
#define KEY_M           50
#define KEY_COMMA       51
#define KEY_DOT         52
#define KEY_SLASH       53
#define KEY_RIGHTSHIFT  54
#define KEY_KPASTERISK  55
#define KEY_LEFTALT     56
#define KEY_SPACE       57
#define KEY_CAPSLOCK    58
#define KEY_F1          59
#define KEY_F2          60
#define KEY_F3          61
#define KEY_F4          62
#define KEY_F5          63
#define KEY_F6          64
#define KEY_F7          65
#define KEY_F8          66
#define KEY_F9          67
#define KEY_F10         68
#define KEY_NUMLOCK     69
#define KEY_SCROLLLOCK  70
#define KEY_KP7         71
#define KEY_KP8         72
#define KEY_KP9         73
#define KEY_KPMINUS     74
#define KEY_KP4         75
#define KEY_KP5         76
#define KEY_KP6         77
#define KEY_KPPLUS      78
#define KEY_KP1         79
#define KEY_KP2         80
#define KEY_KP3         81
#define KEY_KP0         82
#define KEY_KPDOT       83

#define KEY_ZENKAKUHANKAKU 85
#define KEY_102ND       86
#define KEY_F11         87
#define KEY_F12         88
#define KEY_RO          89
#define KEY_KATAKANA    90
#define KEY_HIRAGANA    91
#define KEY_HENKAN      92
#define KEY_KATAKANAHIRAGANA 93
#define KEY_MUHENKAN    94
#define KEY_KPJPCOMMA   95
#define KEY_KPENTER     96
#define KEY_RIGHTCTRL   97
#define KEY_KPSLASH     98
#define KEY_SYSRQ       99
#define KEY_RIGHTALT    100
#define KEY_LINEFEED    101
#define KEY_HOME        102
#define KEY_UP          103
#define KEY_PAGEUP      104
#define KEY_LEFT        105
#define KEY_RIGHT       106
#define KEY_END         107
#define KEY_DOWN        108
#define KEY_PAGEDOWN    109
#define KEY_INSERT      110
#define KEY_DELETE      111
#define KEY_MACRO       112
#define KEY_MUTE        113
#define KEY_VOLUMEDOWN  114
#define KEY_VOLUMEUP    115
#define KEY_POWER       116 /* SC System Power Down */
#define KEY_KPEQUAL     117
#define KEY_KPPLUSMINUS 118
#define KEY_PAUSE       119
#define KEY_SCALE       120 /* AL Compiz Scale (Expose) */

#define KEY_KPCOMMA     121
#define KEY_HANGEUL     122
#define KEY_HANGUEL     KEY_HANGEUL
#define KEY_HANJA       123
#define KEY_YEN         124
#define KEY_LEFTMETA    125
#define KEY_RIGHTMETA   126
#define KEY_COMPOSE     127

#define COLUMN_NUM      8
#define ROW_INIT        0x3FFF
#define KEY_MAX_NUMBER  128
#define S5PC1XX_KEYIF_REG_MEM_SIZE 0x14

typedef struct S5pc1xxKeyIFState {
    SysBusDevice busdev;

    /* Specifies the KEYPAD interface control register */
    union {
        /* raw register data */
        uint32_t v;

        /* register bits */
        struct keyifcon_bits {
            unsigned int_f_en : 1;
            unsigned int_r_en : 1;
            unsigned df_en    : 1;
            unsigned fc_en    : 1;
            unsigned wakeupen : 1;
        } b;
    } keyifcon;

    /* Specifies the KEYPAD interface status and clear register */
    union {
        /* raw register data */
        uint32_t v;

        /* register bits */
        struct keyifstsclr_bits {
            unsigned p_int         : 14;
            unsigned reserved14_15 : 2;
            unsigned r_int         : 14;
        } b;
    } keyifstsclr;

    /* Specifies the KEYPAD interface column data output register */
    union {
        /* raw register data */
        uint32_t v;

        /* register bits */
        struct keyifcol_bits {
            unsigned keyifcol   : 8;
            unsigned keyifcolen : 8;
        } b;
    } keyifcol;

    /* Specifies the KEYPAD interface row data input register */
    uint32_t keyifrow;

    /* Specifies the KEYPAD interface debouncing filter clock
     * division register */
    uint32_t keyiffc;

    qemu_irq irq_keypad;

    /* The keypad supports 14 rows and 8 columns */
    uint16_t keypad[COLUMN_NUM];
} S5pc1xxKeyIFState;


/* Mapping from QEMU keycodes to keypad row-column */
static struct keymap {
    int column;
    int row;
} map[KEY_MAX_NUMBER] = {{0, 0}};

/* And reverse one */
static int keypad_keycode[8][8] = {
    {1, 2, KEY_1, KEY_Q, KEY_A, 6, 7, KEY_KP4/*KEY_LEFT*/},
    {9, 10, KEY_2, KEY_W, KEY_S, KEY_Z, KEY_KP6/*KEY_RIGHT*/, 16},
    {17, 18, KEY_3, KEY_E, KEY_D, KEY_X, 23, KEY_KP8/*KEY_UP*/},
    {25, 26, KEY_4, KEY_R, KEY_F, KEY_C, 31, 32},
    {33, KEY_O, KEY_5, KEY_T, KEY_G, KEY_V, KEY_KP2/*KEY_DOWN*/, KEY_BACKSPACE},
    {KEY_P, KEY_0, KEY_6, KEY_Y, KEY_H, KEY_SPACE, 47, 48},
    {KEY_M, KEY_L, KEY_7, KEY_U, KEY_J, KEY_N, 55, KEY_ENTER},
    {KEY_LEFTSHIFT, KEY_9, KEY_8, KEY_I, KEY_K, KEY_B, 63, KEY_COMMA}
};


static void s5pc1xx_keyif_reset(S5pc1xxKeyIFState *s)
{
    int i = 0;

    s->keyifcon.v    = 0x00000000;
    s->keyifstsclr.v = 0x00000000;
    s->keyifcol.v    = 0x00000000;
    s->keyifrow      = 0x00000000;
    s->keyiffc       = 0x00000000;

    for (i = 0; i < COLUMN_NUM; i++)
        s->keypad[i] = ROW_INIT;
}

static void s5pc1xx_keypad_event(void *opaque, int keycode)
{
    S5pc1xxKeyIFState *s = (S5pc1xxKeyIFState *)opaque;
    struct keymap k = {0, 0};
    int rel = 0;

    rel = (keycode & 0x80) ? 1 : 0; /* key release from qemu */
    keycode &= ~(0x80); /* strip qemu key release bit */

    k = map[keycode];

    /* don't report unknown keypress */
    if (k.column < 0 || k.row < 0)
        return;

    if (rel)
        s->keypad[k.column] |=   1 << k.row;
    else
        s->keypad[k.column] &= ~(1 << k.row);

    if (rel && s->keyifcon.b.int_r_en) {
        qemu_irq_raise(s->irq_keypad);
        s->keyifstsclr.b.r_int = 1;
    }

    if (!rel && s->keyifcon.b.int_f_en) {
        qemu_irq_raise(s->irq_keypad);
        s->keyifstsclr.b.p_int = 1;
    }
}

/* Read KEYIF by GPIO */
static uint32_t s5pc1xx_keyif_gpio_read(void *opaque,
                                        int io_index)
{
    S5pc1xxKeyIFState *s = (S5pc1xxKeyIFState *)opaque;
    int i;

    for (i = 0; i < 8; i++)
        if (io_index == GPIO_KP_COL(i))
            return s->keyifcol.v >> i;

    for (i = 0; i < 14; i++)
        if (io_index == GPIO_KP_ROW(i))
            return s->keyifrow >> i;

    return 0;
}

/* Write KEYIF by GPIO */
static void s5pc1xx_keyif_gpio_write(void *opaque,
                                     int io_index,
                                     uint32_t value)
{
    S5pc1xxKeyIFState *s = (S5pc1xxKeyIFState *)opaque;
    int i;

    for (i = 0; i < 8; i++)
        if (io_index == GPIO_KP_COL(i))
            s->keyifcol.v |= value << i;
}

static GPIOReadMemoryFunc *s5pc1xx_keyif_gpio_readfn   = s5pc1xx_keyif_gpio_read;
static GPIOWriteMemoryFunc *s5pc1xx_keyif_gpio_writefn = s5pc1xx_keyif_gpio_write;

static uint32_t s5pc1xx_keyif_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxKeyIFState *s = (S5pc1xxKeyIFState *)opaque;

    switch (offset) {
    case 0x00: /* KEYIFCON */
        return s->keyifcon.v;
    case 0x04: /* KEYIFSTSCLR */
        return s->keyifstsclr.v;
    case 0x08: /* KEYIFCOL */
        return s->keyifcol.v;
    case 0x0C: /* KEYIFROW */
        return s->keyifrow;
    case 0x10: /* KEYIFFC */
        return s->keyiffc;
    default:
        hw_error("s5pc1xx_keyif: bad read offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static void s5pc1xx_keyif_write(void *opaque, target_phys_addr_t offset,
                                uint32_t val)
{
    S5pc1xxKeyIFState *s = (S5pc1xxKeyIFState *)opaque;
    struct keyifstsclr_bits* v = NULL;
    int i = 0;

    switch (offset) {
    case 0x00: /* KEYIFCON */
        s->keyifcon.v =  val;
        break;
    case 0x04: /* KEYIFSTSCLR */
        v = (struct keyifstsclr_bits*)&val;
        if (v->p_int) {
            s->keyifstsclr.b.p_int = 0;
            qemu_irq_lower(s->irq_keypad);
        }
        if (v->r_int) {
            s->keyifstsclr.b.r_int = 0;
            qemu_irq_lower(s->irq_keypad);
        }
        break;
    case 0x08: /* KEYIFCOL */
        s->keyifcol.v = val & ~0xFF00;
        s->keyifrow = ROW_INIT; /* 14 bit */
        for (i = 0; i < COLUMN_NUM; i++)
            if (!(s->keyifcol.b.keyifcol & (1 << i)))
                s->keyifrow &= s->keypad[i];
        break;
    case 0x0C: /* KEYIFROW */
        /* Read-only */
        break;
    case 0x10: /* KEYIFFC */
        s->keyiffc = val;
        break;
    default:
        hw_error("s5pc1xx_keyif: bad write offset " TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5pc1xx_keyif_mm_read[] = {
    s5pc1xx_keyif_read,
    s5pc1xx_keyif_read,
    s5pc1xx_keyif_read
};

static CPUWriteMemoryFunc * const s5pc1xx_keyif_mm_write[] = {
    s5pc1xx_keyif_write,
    s5pc1xx_keyif_write,
    s5pc1xx_keyif_write
};

static void s5pc1xx_init_keymap(void)
{
    int i, j;
    struct keymap init = {-1, -1};

    for (i = 0; i < KEY_MAX_NUMBER; i++)
        map[i] = init;

    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++) {
            struct keymap k = {i, j};
            map[keypad_keycode[i][j]] = k;
        }
}

static int s5pc1xx_keyif_init(SysBusDevice *dev)
{
    S5pc1xxKeyIFState *s = FROM_SYSBUS(S5pc1xxKeyIFState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq_keypad);
    iomemtype = cpu_register_io_memory(s5pc1xx_keyif_mm_read,
                                       s5pc1xx_keyif_mm_write, s);
    sysbus_init_mmio(dev, S5PC1XX_KEYIF_REG_MEM_SIZE, iomemtype);

    s5pc1xx_gpio_register_io_memory(GPIO_IDX_KEYIF, 0,
                                    s5pc1xx_keyif_gpio_readfn,
                                    s5pc1xx_keyif_gpio_writefn, NULL, s);
    s5pc1xx_init_keymap();
    qemu_add_kbd_event_handler(s5pc1xx_keypad_event, s);

    s5pc1xx_keyif_reset(s);

    return 0;
}

static void s5pc1xx_keyif_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,keyif", sizeof(S5pc1xxKeyIFState),
                        s5pc1xx_keyif_init);
}

device_init(s5pc1xx_keyif_register_devices)
