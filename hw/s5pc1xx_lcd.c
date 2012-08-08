/*
 * S5PC1XX LCD Controller
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

/*
 * Known issues:
 *    multiple windows blending - implemented but not tested
 *    shadow registers - not implemented
 *    i80 indirect interface - not implemented
 *    dithering - not implemented
 *    RTQoS - not implemented
 */

#include "console.h"
#include "pixel_ops.h"
#include "s5pc1xx.h"
#include "sysbus.h"


#define BISWP   0x8
#define BYSWP   0x4
#define HWSWP   0x2
#define WSWP    0x1


typedef struct {
    uint8_t r, g, b;
    uint32_t a;
} rgba;

struct DrawConfig;

typedef void pixel_to_rgb_func(uint32_t pixel, rgba *p);
typedef void draw_line_func(struct DrawConfig *cfg, uint8_t *src,
                            uint8_t *dst, uint8_t *ifb);
typedef uint32_t coef_func(const struct DrawConfig *cfg, rgba pa, rgba pb);

typedef struct DrawConfig {
    pixel_to_rgb_func *pixel_to_rgb;
    draw_line_func *draw_line;
    int (*put_pixel)(rgba p, uint8_t *pixel);
    int (*get_pixel)(uint8_t *src, rgba *p);
    void (*blend)(struct DrawConfig *cfg, rgba p_old, rgba p_new, rgba *p);
    coef_func *coef_p, *coef_q, *coef_a, *coef_b;
    uint8_t is_palletized;
    uint32_t bg_alpha[2], fg_alpha[2];
    uint32_t color_key, color_mask, color_ctl;
    uint8_t fg_alpha_pix, bg_alpha_pix;
    int width;
    int bpp;
    uint32_t *palette;
    uint8_t swap;
    uint8_t fg_pixel_blending, bg_pixel_blending;
    uint8_t fg_alpha_sel, bg_alpha_sel;
} DrawConfig;

typedef struct S5pc1xxLcdWindow {
    uint32_t wincon;
    uint32_t vidosd[4];
    uint32_t buf_start[2];
    uint32_t buf_end[2];
    uint32_t buf_size;
    uint32_t keycon[2];
    uint32_t winmap;
    uint32_t vidw_alpha[2];
    uint32_t blendeq;
    uint32_t palette[256];
} S5pc1xxLcdWindow;

typedef struct {
    SysBusDevice busdev;

    uint32_t shadowcon;
    uint32_t vidcon[3];
    uint32_t prtcon;
    uint32_t vidtcon[3];
    uint32_t vp1tcon[2];
    uint32_t vidintcon[2];
    uint32_t dithcon;
    uint32_t wpalcon[2];
    uint32_t trigcon;
    uint32_t ituifcon;
    uint32_t i80ifcon[4];
    uint32_t ldi_cmdcon[2];
    uint32_t sifccon[3];
    uint32_t blendcon;
    uint32_t ldi_cmd[12];

    S5pc1xxLcdWindow window[5];
    uint8_t *ifb;
    uint8_t *valid_line;
    uint8_t *valid_line_prev;
    DisplayState *console;
    uint8_t invalidate;
    qemu_irq irq[3];
} S5pc1xxLcdState;


/* Palette/pixel to RGB conversion */

#define DEF_PIXEL_TO_RGB(N,R,G,B,A) \
static void N(uint32_t pixel, rgba *p) \
{ \
    p->b = (pixel & ((1 << (B)) - 1)) << (8 - (B)); \
    pixel >>= (B); \
    p->g = (pixel & ((1 << (G)) - 1)) << (8 - (G)); \
    pixel >>= (G); \
    p->r = (pixel & ((1 << (R)) - 1)) << (8 - (R)); \
    pixel >>= (R); \
    if (1 == (A)) { \
        p->a = pixel & 1; \
    } else if (8 == (A)) { \
        p->a = pixel & 0xFF; \
        p->a = (p->a << 16) | (p->a << 8) | p->a; \
    } else { \
        p->a = (pixel & ((1 << (A)) - 1)) << (8 - (A)); \
    } \
}

DEF_PIXEL_TO_RGB(pixel_a232_to_rgb, 2, 3, 2, 1)
DEF_PIXEL_TO_RGB(pixel_a444_to_rgb, 4, 4, 4, 1)
DEF_PIXEL_TO_RGB(pixel_4444_to_rgb, 4, 4, 4, 4)
DEF_PIXEL_TO_RGB(pixel_565_to_rgb,  5, 6, 5, 0)
DEF_PIXEL_TO_RGB(pixel_a555_to_rgb, 5, 5, 5, 1)
DEF_PIXEL_TO_RGB(pixel_555_to_rgb,  5, 5, 5, 0)
DEF_PIXEL_TO_RGB(pixel_666_to_rgb,  6, 6, 6, 0)
DEF_PIXEL_TO_RGB(pixel_a666_to_rgb, 6, 6, 6, 1)
DEF_PIXEL_TO_RGB(pixel_a665_to_rgb, 6, 6, 5, 1)
DEF_PIXEL_TO_RGB(pixel_888_to_rgb,  8, 8, 8, 0)
DEF_PIXEL_TO_RGB(pixel_a888_to_rgb, 8, 8, 8, 1)
DEF_PIXEL_TO_RGB(pixel_a887_to_rgb, 8, 8, 7, 1)
DEF_PIXEL_TO_RGB(pixel_8888_to_rgb, 8, 8, 8, 8)

/* Special case for (5+1,5+1,5+1) mode */
static void pixel_1555_to_rgb(uint32_t pixel, rgba *p)
{
    uint8_t u = (pixel >> 15) & 1;
    p->b = (((pixel & 0x1F) << 1) | u) << 2;
    pixel >>= 5;
    p->g = (((pixel & 0x3F) << 1) | u) << 2;
    pixel >>= 6;
    p->r = (((pixel & 0x1F) << 1) | u) << 2;
}


/* Write RGB to QEMU's GraphicConsole framebuffer */

static int put_pixel8(rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel8(p.r, p.g, p.b);
    *(uint8_t *)d = pixel;
    return 1;
}

static int put_pixel15(rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel15(p.r, p.g, p.b);
    *(uint16_t *)d = pixel;
    return 2;
}

static int put_pixel16(rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel16(p.r, p.g, p.b);
    *(uint16_t *)d = pixel;
    return 2;
}

static int put_pixel24(rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel24(p.r, p.g, p.b);
    *(uint8_t *)d++ = (pixel >>  0) & 0xFF;
    *(uint8_t *)d++ = (pixel >>  8) & 0xFF;
    *(uint8_t *)d++ = (pixel >> 16) & 0xFF;
    return 3;
}

static int put_pixel32(rgba p, uint8_t *d)
{
    uint32_t pixel = rgb_to_pixel24(p.r, p.g, p.b);
    *(uint32_t *)d = pixel;
    return 4;
}


/* Put/get pixel to/from internal LCD Controller framebuffer */

static int put_rgba(rgba p, uint8_t *d)
{
    *(uint8_t *)d++ = p.r;
    *(uint8_t *)d++ = p.g;
    *(uint8_t *)d++ = p.b;
    *(uint32_t *)d = p.a;
    return 7;
}

static int get_rgba(uint8_t *s, rgba *p)
{
    p->r = *(uint8_t *)s++;
    p->g = *(uint8_t *)s++;
    p->b = *(uint8_t *)s++;
    p->a = *(uint32_t *)s;
    return 7;
}


/* Perform byte/halfword/word swap of data accrding to config */

static inline uint64_t swap_data(const DrawConfig *cfg, uint64_t x)
{
    int i;
    uint64_t res;

    return x;

    if (cfg->swap & BISWP) {
        res = 0;
        for (i = 0; i < 64; i++) {
            if (x & (1ULL << (64 - i))) {
                res |= (1ULL << i);
            }
        }
        x = res;
    }
    if (cfg->swap & BYSWP) {
        x = ((x & 0x00000000000000FFULL) << 56) |
            ((x & 0x000000000000FF00ULL) << 40) |
            ((x & 0x0000000000FF0000ULL) << 24) |
            ((x & 0x00000000FF000000ULL) <<  8) |
            ((x & 0x000000FF00000000ULL) >>  8) |
            ((x & 0x0000FF0000000000ULL) >> 24) |
            ((x & 0x00FF000000000000ULL) >> 40) |
            ((x & 0xFF00000000000000ULL) >> 56);
    }
    if (cfg->swap & HWSWP) {
        x = ((x & 0x000000000000FFFFULL) << 48) |
            ((x & 0x00000000FFFF0000ULL) << 16) |
            ((x & 0x0000FFFF00000000ULL) >> 16) |
            ((x & 0xFFFF000000000000ULL) >> 48);
    }
    if (cfg->swap & WSWP) {
        x = ((x & 0x00000000FFFFFFFFULL) << 32) |
            ((x & 0xFFFFFFFF00000000ULL) >> 32);
    }
    return x;
}


/* Coefficient extraction functions */

static uint32_t coef_zero(const DrawConfig *cfg,
                          rgba pa, rgba pb)
{
    return 0;
}

static uint32_t coef_one(const DrawConfig *cfg,
                         rgba pa, rgba pb)
{
    return 0xFFFFFF;
}

static uint32_t coef_alphaa(const DrawConfig *cfg,
                            rgba pa, rgba pb)
{
    if (!cfg->fg_pixel_blending) {
        pa.a = cfg->fg_alpha_sel;
    }
    if (cfg->fg_alpha_pix) {
        return pa.a;
    } else {
        return cfg->fg_alpha[pa.a];
    }
}

static uint32_t coef_one_minus_alphaa(const DrawConfig *cfg,
                                      rgba pa, rgba pb)
{
    if (!cfg->fg_pixel_blending) {
        pa.a = cfg->fg_alpha_sel;
    }
    if (cfg->fg_alpha_pix) {
        return 0xFFFFFF - pa.a;
    } else {
        return 0xFFFFFF - cfg->fg_alpha[pa.a];
    }
}

static uint32_t coef_alphab(const DrawConfig *cfg,
                            rgba pa, rgba pb)
{
    if (!cfg->bg_pixel_blending) {
        pb.a = cfg->bg_alpha_sel;
    }
    if (cfg->bg_alpha_pix) {
        return pb.a;
    } else {
        return cfg->bg_alpha[pb.a];
    }
}

static uint32_t coef_one_minus_alphab(const DrawConfig *cfg,
                                      rgba pa, rgba pb)
{
    if (!cfg->bg_pixel_blending) {
        pb.a = cfg->bg_alpha_sel;
    }
    if (cfg->bg_alpha_pix) {
        return 0xFFFFFF - pb.a;
    } else {
        return 0xFFFFFF - cfg->bg_alpha[pb.a];
    }
}

static uint32_t coef_a(const DrawConfig *cfg,
                       rgba pa, rgba pb)
{
    return (pa.r << 16) | (pa.g << 8) | pa.b;
}

static uint32_t coef_one_minus_a(const DrawConfig *cfg,
                                 rgba pa, rgba pb)
{
    return 0xFFFFFF - ((pa.r << 16) | (pa.g << 8) | pa.b);
}

static uint32_t coef_b(const DrawConfig *cfg,
                       rgba pa, rgba pb)
{
    return (pb.r << 16) | (pb.g << 8) | pb.b;
}

static uint32_t coef_one_minus_b(const DrawConfig *cfg,
                                 rgba pa, rgba pb)
{
    return 0xFFFFFF - ((pb.r << 16) | (pb.g << 8) | pb.b);
}


/* Blending functions */

static void blend_alpha(const DrawConfig *cfg,
                        rgba p_bg, rgba p_fg, rgba *res)
{
    uint32_t pl, ql, al, bl;
    uint32_t p, q, a, b, fg, bg, fga, bga;

    pl = cfg->coef_p(cfg, p_fg, p_bg);
    ql = cfg->coef_q(cfg, p_fg, p_bg);
    al = cfg->coef_a(cfg, p_fg, p_bg);
    bl = cfg->coef_b(cfg, p_fg, p_bg);
    res->a = 0;
    /* B */
    p = pl & 0xFF;
    pl >>= 8;
    q = ql & 0xFF;
    ql >>= 8;
    a = al & 0xFF;
    al >>= 8;
    b = bl & 0xFF;
    bl >>= 8;
    fg = p_fg.b;
    bg = p_bg.b;
    if (cfg->fg_pixel_blending) {
        if (cfg->fg_alpha_pix) {
            fga = p_fg.a & 0xFF;
        } else {
            fga = cfg->fg_alpha[p_fg.a] & 0xFF;
        }
    } else {
        fga = cfg->fg_alpha[cfg->fg_alpha_sel] & 0xFF;
    }
    if (cfg->bg_pixel_blending) {
        if (cfg->bg_alpha_pix) {
            bga = p_bg.a & 0xFF;
        } else {
            bga = cfg->bg_alpha[p_bg.a] & 0xFF;
        }
    } else {
        bga = cfg->bg_alpha[cfg->bg_alpha_sel] & 0xFF;
    }
    bg = (bg * b + fg * a) / 0xFF;
    if (bg > 0xFF) {
        res->b = 0xFF;
    } else {
        res->b = bg;
    }
    bga = (bga * p + fga * q) / 0xFF;
    if (bga > 0xFF) {
        res->a |= 0xFF;
    } else {
        res->a |= bga;
    }
    /* G */
    p = pl & 0xFF;
    pl >>= 8;
    q = ql & 0xFF;
    ql >>= 8;
    a = al & 0xFF;
    al >>= 8;
    b = bl & 0xFF;
    bl >>= 8;
    fg = p_fg.g;
    bg = p_bg.g;
    if (cfg->fg_pixel_blending) {
        if (cfg->fg_alpha_pix) {
            fga = (p_fg.a >> 8) & 0xFF;
        } else {
            fga = (cfg->fg_alpha[p_fg.a] >> 8) & 0xFF;
        }
    } else {
        fga = (cfg->fg_alpha[cfg->fg_alpha_sel] >> 8) & 0xFF;
    }
    if (cfg->bg_pixel_blending) {
        if (cfg->bg_alpha_pix) {
            bga = (p_bg.a >> 8) & 0xFF;
        } else {
            bga = (cfg->bg_alpha[p_bg.a] >> 8) & 0xFF;
        }
    } else {
        bga = (cfg->bg_alpha[cfg->bg_alpha_sel] >> 8) & 0xFF;
    }
    bg = (bg * b + fg * a) / 0xFF;
    if (bg > 0xFF) {
        res->g = 0xFF;
    } else {
        res->g = bg;
    }
    bga = (bga * p + fga * q) / 0xFF;
    if (bga > 0xFF) {
        res->a |= 0xFF << 8;
    } else {
        res->a |= bga << 8;
    }
    /* R */
    p = pl & 0xFF;
    pl >>= 8;
    q = ql & 0xFF;
    ql >>= 8;
    a = al & 0xFF;
    al >>= 8;
    b = bl & 0xFF;
    bl >>= 8;
    fg = p_fg.r;
    bg = p_bg.r;
    if (cfg->fg_pixel_blending) {
        if (cfg->fg_alpha_pix) {
            fga = (p_fg.a >> 16) & 0xFF;
        } else {
            fga = (cfg->fg_alpha[p_fg.a] >> 16) & 0xFF;
        }
    } else {
        fga = (cfg->fg_alpha[cfg->fg_alpha_sel] >> 16) & 0xFF;
    }
    if (cfg->bg_pixel_blending) {
        if (cfg->bg_alpha_pix) {
            bga = (p_bg.a >> 16) & 0xFF;
        } else {
            bga = (cfg->bg_alpha[p_bg.a] >> 16) & 0xFF;
        }
    } else {
        bga = (cfg->bg_alpha[cfg->bg_alpha_sel] >> 16) & 0xFF;
    }
    bg = (bg * b + fg * a) / 0xFF;
    if (bg > 0xFF) {
        res->r = 0xFF;
    } else {
        res->r = bg;
    }
    bga = (bga * p + fga * q) / 0xFF;
    if (bga > 0xFF) {
        res->a |= 0xFF << 16;
    } else {
        res->a |= bga << 16;
    }
}

static void blend_colorkey(DrawConfig *cfg,
                           rgba p_bg, rgba p_fg, rgba *p)
{
    uint8_t r, g, b;

    if (cfg->color_ctl & 2) {
        blend_alpha(cfg, p_bg, p_fg, p);
        return ;
    }
    r = ((cfg->color_key & ~cfg->color_mask) >> 16) & 0xFF;
    g = ((cfg->color_key & ~cfg->color_mask) >>  8) & 0xFF;
    b = ((cfg->color_key & ~cfg->color_mask) >>  0) & 0xFF;
    if (cfg->color_ctl & 1) {
        if ((p_fg.r & ~((cfg->color_mask >> 16) & 0xFF)) == r &&
                (p_fg.g & ~((cfg->color_mask >>  8) & 0xFF)) == g &&
                (p_fg.b & ~((cfg->color_mask >>  0) & 0xFF)) == b) {
            if (cfg->color_ctl & 4) {
                p_fg.a = 1;
                cfg->fg_pixel_blending = 0;
                blend_alpha(cfg, p_bg, p_fg, p);
            } else {
                *p = p_bg;
            }
        } else {
            if (cfg->color_ctl & 4) {
                p_fg.a = 0;
                cfg->fg_pixel_blending = 0;
                blend_alpha(cfg, p_bg, p_fg, p);
            } else {
                *p = p_fg;
            }
        }
    } else {
        if ((p_bg.r & ~((cfg->color_mask >> 16) & 0xFF)) == r &&
                (p_bg.g & ~((cfg->color_mask >>  8) & 0xFF)) == g &&
                (p_bg.b & ~((cfg->color_mask >>  0) & 0xFF)) == b) {
            if (cfg->color_ctl & 4) {
                p_fg.a = 1;
                cfg->fg_pixel_blending = 0;
                blend_alpha(cfg, p_bg, p_fg, p);
            } else {
                *p = p_fg;
            }
        } else {
            if (cfg->color_ctl & 4) {
                p_fg.a = 0;
                cfg->fg_pixel_blending = 0;
                blend_alpha(cfg, p_bg, p_fg, p);
            } else {
                *p = p_bg;
            }
        }
    }
}


/* Draw line functions */

#define DEF_DRAW_LINE(N) \
static void glue(draw_line, N)(DrawConfig *cfg, uint8_t *src, \
                               uint8_t *dst, uint8_t *ifb) \
{ \
    rgba p, p_old; \
    uint64_t data; \
    int width = cfg->width; \
    int i; \
    do { \
        data = ldq_raw((void *)src); \
        src += 8; \
        data = swap_data(cfg, data); \
        for (i = 0; i < (64 / (N)); i++) { \
            cfg->pixel_to_rgb(cfg->is_palletized ? \
                                  cfg->palette[data & ((1ULL << (N)) - 1)] : \
                                  data & ((1ULL << (N)) - 1), &p); \
            if (cfg->blend) { \
                ifb += cfg->get_pixel(ifb, &p_old); \
                cfg->blend(cfg, p_old, p, &p); \
            } \
            dst += cfg->put_pixel(p, dst); \
            data >>= (N); \
        } \
        width -= (64 / (N)); \
    } while (width > 0); \
}

DEF_DRAW_LINE(1)
DEF_DRAW_LINE(2)
DEF_DRAW_LINE(4)
DEF_DRAW_LINE(8)
DEF_DRAW_LINE(16)
DEF_DRAW_LINE(32)

static void draw_line_copy(DrawConfig *cfg, uint8_t *src, uint8_t *dst,
                           uint8_t *ifb)
{
    rgba p;
    int width = cfg->width;

    do {
        src += cfg->get_pixel(src, &p);
        dst += cfg->put_pixel(p, dst);
        width--;
    } while (width > 0);
}


/* LCD Functions */

static void s5pc1xx_lcd_update_irq(S5pc1xxLcdState *s)
{
    if (!(s->vidintcon[0] & 1)) {
        qemu_irq_lower(s->irq[0]);
        qemu_irq_lower(s->irq[1]);
        qemu_irq_lower(s->irq[2]);
        return;
    }
    if ((s->vidintcon[0] & 2) && (s->vidintcon[1] & 1)) {
        qemu_irq_raise(s->irq[0]);
    } else {
        qemu_irq_lower(s->irq[0]);
    }
    if ((s->vidintcon[0] & (1 << 12)) && (s->vidintcon[1] & 2)) {
        qemu_irq_raise(s->irq[1]);
    } else {
        qemu_irq_lower(s->irq[1]);
    }
    if ((s->vidintcon[0] & (1 << 17)) && (s->vidintcon[1] & 4)) {
        qemu_irq_raise(s->irq[2]);
    } else {
        qemu_irq_lower(s->irq[2]);
    }
}

static void s5pc1xx_lcd_write(void *opaque, target_phys_addr_t offset,
                              uint32_t val)
{
    S5pc1xxLcdState *s = (S5pc1xxLcdState *)opaque;
    int w, i;

    if (offset & 3) {
        hw_error("s5pc1xx_lcd: bad write offset " TARGET_FMT_plx "\n", offset);
    }

    switch (offset) {
        case 0x000 ... 0x008:
            s->vidcon[(offset - 0x000) >> 2] = val;
            break;
        case 0x00C:
            s->prtcon = val;
            break;
        case 0x010 ... 0x018:
            s->vidtcon[(offset - 0x010) >> 2] = val;
            break;
        case 0x020 ... 0x030:
            s->window[(offset - 0x020) >> 2].wincon = val;
            break;
        case 0x034:
            s->shadowcon = val;
            break;
        case 0x040 ... 0x088:
            w = (offset - 0x040) >> 4;
            i = ((offset - 0x040) & 0xF) >> 2;
            if (i < 2) {
                s->window[w].vidosd[i] = val;
            } else if (i == 3) {
                if (w != 1 && w != 2) {
                    hw_error("s5pc1xx_lcd: bad write offset " TARGET_FMT_plx "\n",
                             offset);
                } else {
                    s->window[w].vidosd[i] = val;
                }
            } else {
                if (w == 0) {
                    i++;
                }
                s->window[w].vidosd[i] = val;
            }
            break;
        case 0x0A0 ... 0x0C0:
            w = (offset - 0x0A0) >> 3;
            i = ((offset - 0x0A0) >> 2) & 1;
            if (i == 1 && w >= 2) {
                hw_error("s5pc1xx_lcd: bad write offset " TARGET_FMT_plx "\n",
                         offset);
            }
            s->window[w].buf_start[i] = val;
            break;
        case 0x0D0 ... 0x0F0:
            w = (offset - 0x0D0) >> 3;
            i = ((offset - 0x0D0) >> 2) & 1;
            if (i == 1 && w >= 2) {
                hw_error("s5pc1xx_lcd: bad write offset " TARGET_FMT_plx "\n",
                         offset);
            }
            s->window[w].buf_end[i] = val;
            break;
        case 0x100 ... 0x110:
            s->window[(offset - 0x100) >> 2].buf_size = val;
            break;
        case 0x118 ... 0x11C:
            s->vp1tcon[(offset - 0x118)] = val;
            break;
        case 0x130:
            s->vidintcon[0] = val;
        case 0x134:
            s->vidintcon[1] &= ~(val & 7);
            s5pc1xx_lcd_update_irq(s);
            break;
        case 0x140 ... 0x15C:
            w = ((offset - 0x140) >> 3) + 1;
            i = ((offset - 0x140) >> 2) & 1;
            s->window[w].keycon[i] = val;
            break;
        case 0x170:
            s->dithcon = val;
            break;
        case 0x180 ... 0x190:
            s->window[(offset - 0x180) >> 2].winmap = val;
            break;
        case 0x19C ... 0x1A0:
            s->wpalcon[(offset - 0x19C) >> 2] = val;
            break;
        case 0x1A4:
            s->trigcon = val;
            break;
        case 0x1A8:
            s->ituifcon = val;
            break;
        case 0x1B0 ... 0x1BC:
            s->i80ifcon[(offset - 0x1B0) >> 2] = val;
            break;
        case 0x1D0 ... 0x1D4:
            s->ldi_cmdcon[(offset - 0x1D0) >> 2] = val;
            break;
        case 0x1E0 ... 0x1E8:
            i = (offset - 0x1E0) >> 2;
            if (i == 2) {
                hw_error("s5pc1xx_lcd: bad write offset " TARGET_FMT_plx "\n",
                         offset);
            }
            s->sifccon[i] = val;
            break;
        case 0x200 ... 0x224:
            w = ((offset - 0x200) >> 3);
            i = ((offset - 0x200) >> 2) & 1;
            s->window[w].vidw_alpha[i] = val;
            break;
        case 0x244 ... 0x250:
            s->window[(offset - 0x244) >> 2].blendeq = val;
            break;
        case 0x260:
            s->blendcon = val;
            break;
        case 0x280 ... 0x2AC:
            s->ldi_cmd[(offset - 0x280) >> 2] = val;
            break;
        case 0x2400 ... 0x37FC: /* TODO: verify offset!!! */
            w = (offset - 0x2400) >> 10;
            i = ((offset - 0x2400) >> 2) & 0xFF;
            s->window[w].palette[i] = val;
            break;
        default:
            hw_error("s5pc1xx_lcd: bad write offset " TARGET_FMT_plx "\n",
                     offset);
    }
}

static uint32_t s5pc1xx_lcd_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxLcdState *s = (S5pc1xxLcdState *)opaque;
    int w, i;

    if (offset & 3) {
        hw_error("s5pc1xx_lcd: bad read offset " TARGET_FMT_plx "\n", offset);
    }

    switch (offset) {
        case 0x000 ... 0x008:
            return s->vidcon[(offset - 0x000) >> 2];
        case 0x00C:
            return s->prtcon;
        case 0x010 ... 0x018:
            return s->vidtcon[(offset - 0x010) >> 2];
        case 0x020 ... 0x030:
            return s->window[(offset - 0x020) >> 2].wincon;
        case 0x034:
            return s->shadowcon;
        case 0x040 ... 0x088:
            w = (offset - 0x040) >> 4;
            i = ((offset - 0x040) & 0xF) >> 2;
            if (i < 2) {
                return s->window[w].vidosd[i];
            } else if (i == 3) {
                if (w != 1 && w != 2) {
                    hw_error("s5pc1xx_lcd: bad read offset " TARGET_FMT_plx "\n",
                             offset);
                } else {
                    return s->window[w].vidosd[i];
                }
            } else {
                if (w == 0) {
                    i++;
                }
                return s->window[w].vidosd[i];
            }
        case 0x0A0 ... 0x0C0:
            w = (offset - 0x0A0) >> 3;
            i = ((offset - 0x0A0) >> 2) & 1;
            if (i == 1 && w >= 2) {
                hw_error("s5pc1xx_lcd: bad read offset " TARGET_FMT_plx "\n",
                         offset);
            }
            return s->window[w].buf_start[i];
        case 0x0D0 ... 0x0F0:
            w = (offset - 0x0D0) >> 3;
            i = ((offset - 0x0D0) >> 2) & 1;
            if (i == 1 && w >= 2) {
                hw_error("s5pc1xx_lcd: bad read offset " TARGET_FMT_plx "\n",
                         offset);
            }
            return s->window[w].buf_end[i];
        case 0x100 ... 0x110:
            return s->window[(offset - 0x100) >> 2].buf_size;
        case 0x118 ... 0x11C:
            return s->vp1tcon[(offset - 0x118)];
        case 0x130 ... 0x134:
            return s->vidintcon[(offset - 0x130) >> 2];
        case 0x140 ... 0x15C:
            w = ((offset - 0x140) >> 3) + 1;
            i = ((offset - 0x140) >> 2) & 1;
            return s->window[w].keycon[i];
        case 0x170:
            return s->dithcon;
        case 0x180 ... 0x190:
            return s->window[(offset - 0x180) >> 2].winmap;
        case 0x19C ... 0x1A0:
            return s->wpalcon[(offset - 0x19C) >> 2];
        case 0x1A4:
            return s->trigcon;
        case 0x1A8:
            return s->ituifcon;
        case 0x1B0 ... 0x1BC:
            return s->i80ifcon[(offset - 0x1B0) >> 2];
        case 0x1D0 ... 0x1D4:
            return s->ldi_cmdcon[(offset - 0x1D0) >> 2];
        case 0x1E0 ... 0x1E8:
            i = (offset - 0x1E0) >> 2;
            return s->sifccon[i];
        case 0x200 ... 0x224:
            w = ((offset - 0x200) >> 3);
            i = ((offset - 0x200) >> 2) & 1;
            return s->window[w].vidw_alpha[i];
        case 0x244 ... 0x250:
            return s->window[(offset - 0x244) >> 2].blendeq;
        case 0x260:
            return s->blendcon;
        case 0x280 ... 0x2AC:
            return s->ldi_cmd[(offset - 0x280) >> 2];
        case 0x2400 ... 0x37FC: /* TODO: verify offset!!! */
            w = (offset - 0x2400) >> 10;
            i = ((offset - 0x2400) >> 2) & 0xFF;
            return s->window[w].palette[i];
        default:
            hw_error("s5pc1xx_lcd: bad read offset " TARGET_FMT_plx "\n",
                     offset);
    }
}

static CPUReadMemoryFunc *s5pc1xx_lcd_readfn[] = {
    s5pc1xx_lcd_read,
    s5pc1xx_lcd_read,
    s5pc1xx_lcd_read
};

static CPUWriteMemoryFunc *s5pc1xx_lcd_writefn[] = {
    s5pc1xx_lcd_write,
    s5pc1xx_lcd_write,
    s5pc1xx_lcd_write
};

static void s5pc1xx_update_resolution(S5pc1xxLcdState *s)
{
    uint32_t width, height;
    /* LCD resolution is stored in VIDEO TIME CONTROL REGISTER 2 */
    width = (s->vidtcon[2] & 0x7FF) + 1;
    height = ((s->vidtcon[2] >> 11) & 0x7FF) + 1;
    if (s->ifb == NULL || ds_get_width(s->console) != width ||
        ds_get_height(s->console) != height) {

        qemu_console_resize(s->console, width, height);
        s->ifb = qemu_realloc(s->ifb, width * height * 7);
        s->valid_line =
            qemu_realloc(s->valid_line, (height >> 3) * sizeof(uint8_t));
        s->valid_line_prev =
            qemu_realloc(s->valid_line_prev, (height >> 3) * sizeof(uint8_t));
        memset(s->ifb, 0, width * height * 7);
        s->invalidate = 1;
    }
}

/* Returns WxPAL for given window number WINDOW */
static uint32_t s5pc1xx_wxpal(S5pc1xxLcdState *s, int window)
{
    switch (window) {
    case 0:
        return s->wpalcon[1] & 0x7;
    case 1:
        return (s->wpalcon[1] >> 3) & 0x7;
    case 2:
        return ((s->wpalcon[0] >> 8) & 0x6) | ((s->wpalcon[1] >> 6) & 0x1);
    case 3:
        return ((s->wpalcon[0] >> 12) & 0x6) | ((s->wpalcon[1] >> 7) & 0x1);
    case 4:
        return ((s->wpalcon[0] >> 16) & 0x6) | ((s->wpalcon[1] >> 8) & 0x1);
    default:
        hw_error("s5pc1xx_lcd: incorrect window number %d\n", window);
    }
}

/* Parse BPPMODE_F bits and setup known DRAW_CONFIG fields accordingly.
   BPPMODE_F = WINCON1[5:2] */
static void s5pc1xx_parse_win_bppmode(S5pc1xxLcdState *s,
                                      DrawConfig *cfg, int window)
{
    switch ((s->window[window].wincon >> 2) & 0xF) {
    case 0:
        cfg->draw_line = draw_line1;
        cfg->is_palletized = 1;
        cfg->bpp = 1;
        break;
    case 1:
        cfg->draw_line = draw_line2;
        cfg->is_palletized = 1;
        cfg->bpp = 2;
        break;
    case 2:
        cfg->draw_line = draw_line4;
        cfg->is_palletized = 1;
        cfg->bpp = 4;
        break;
    case 3:
        cfg->draw_line = draw_line8;
        cfg->is_palletized = 1;
        cfg->bpp = 8;
        break;
    case 4:
        cfg->draw_line = draw_line8;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_a232_to_rgb;
        cfg->bpp = 8;
        break;
    case 5:
        cfg->draw_line = draw_line16;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_565_to_rgb;
        cfg->bpp = 16;
        break;
    case 6:
        cfg->draw_line = draw_line16;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_a555_to_rgb;
        cfg->bpp = 16;
        break;
    case 7:
        cfg->draw_line = draw_line16;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_1555_to_rgb;
        cfg->bpp = 16;
        break;
    case 8:
        cfg->draw_line = draw_line32;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_666_to_rgb;
        cfg->bpp = 32;
        break;
    case 9:
        cfg->draw_line = draw_line32;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_a665_to_rgb;
        cfg->bpp = 32;
        break;
    case 10:
        cfg->draw_line = draw_line32;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_a666_to_rgb;
        cfg->bpp = 32;
        break;
    case 11:
        cfg->draw_line = draw_line32;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_888_to_rgb;
        cfg->bpp = 32;
        break;
    case 12:
        cfg->draw_line = draw_line32;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_a887_to_rgb;
        cfg->bpp = 32;
        break;
    case 13:
        cfg->draw_line = draw_line32;
        cfg->is_palletized = 0;
        if ((s->window[window].wincon & (1 << 6)) &&
            (s->window[window].wincon & 2)) {
            cfg->pixel_to_rgb = pixel_8888_to_rgb;
            cfg->fg_alpha_pix = 1;
        } else {
            cfg->pixel_to_rgb = pixel_a888_to_rgb;
        }
        cfg->bpp = 32;
        break;
    case 14:
        cfg->draw_line = draw_line16;
        cfg->is_palletized = 0;
        if ((s->window[window].wincon & (1 << 6)) &&
            (s->window[window].wincon & 2)) {
            cfg->pixel_to_rgb = pixel_4444_to_rgb;
            cfg->fg_alpha_pix = 1;
        } else {
            cfg->pixel_to_rgb = pixel_a444_to_rgb;
        }
        cfg->bpp = 16;
        break;
    case 15:
        cfg->draw_line = draw_line16;
        cfg->is_palletized = 0;
        cfg->pixel_to_rgb = pixel_555_to_rgb;
        cfg->bpp = 16;
        break;
    }
}

pixel_to_rgb_func *wxpal_to_rgb[8] = {
    [0] = pixel_565_to_rgb,
    [1] = pixel_a555_to_rgb,
    [2] = pixel_666_to_rgb,
    [3] = pixel_a665_to_rgb,
    [4] = pixel_a666_to_rgb,
    [5] = pixel_888_to_rgb,
    [6] = pixel_a888_to_rgb,
    [7] = pixel_8888_to_rgb
};

static inline uint32_t unpack_by_4(uint32_t x)
{
    return ((x & 0xF00) << 12) | ((x & 0xF0) << 8) | ((x & 0xF) << 4);
}

static coef_func *coef_decode(uint32_t x)
{
    switch (x) {
    case 0:
        return coef_zero;
    case 1:
        return coef_one;
    case 2:
        return coef_alphaa;
    case 3:
        return coef_one_minus_alphaa;
    case 4:
        return coef_alphab;
    case 5:
        return coef_one_minus_alphab;
    case 10:
        return coef_a;
    case 11:
        return coef_one_minus_a;
    case 12:
        return coef_b;
    case 13:
        return coef_one_minus_b;
    default:
        hw_error("s5pc1xx_lcd: illegal value\n");
    }
}

static inline void putpixel_by_bpp(DrawConfig *cfg, int bpp)
{
    switch (bpp) {
    case 8:
        cfg->put_pixel = put_pixel8;
        break;
    case 15:
        cfg->put_pixel = put_pixel15;
        break;
    case 16:
        cfg->put_pixel = put_pixel16;
        break;
    case 24:
        cfg->put_pixel = put_pixel24;
        break;
    case 32:
        cfg->put_pixel = put_pixel32;
        break;
    default:
        hw_error("s5pc1xx_lcd: unsupported BPP (%d)", bpp);
    }
}

static void s5pc1xx_lcd_update(void *opaque)
{
    S5pc1xxLcdState *s = (S5pc1xxLcdState *)opaque;
    DrawConfig cfg;
    int i, dirty[2], x;
    int line;
    target_phys_addr_t scanline, newline, map_len, pd, inc_size;
    uint8_t *mapline, *startline, *valid_line_tmp;
    int lefttop_x, lefttop_y, rightbottom_x, rightbottom_y;
    int ext_line_size;
    int width, height;
    uint32_t tmp;
    int buf_id;
    int need_redraw;
    int global_width, global_height;
    int bpp;
    uint8_t *d;
    uint8_t is_first_window;

    if (!s || !s->console || !ds_get_bits_per_pixel(s->console)) {
        return;
    }

    if (! (s->vidcon[0] & 2)) {
        return;
    }

    memset(&cfg, 0, sizeof(cfg));

    s5pc1xx_update_resolution(s);

    /* First we will mark lines of the display which need to be redrawn */
    memset(s->valid_line, 0xFF,
           ((((s->vidtcon[2] >> 11) & 0x7FF) + 1 + 7) >> 3) * sizeof(uint8_t));
    for (i = 0; i < 5; i++) {
        if (s->window[i].wincon & 1) {
            lefttop_x = (s->window[i].vidosd[0] >> 11) & 0x7FF;
            lefttop_y = (s->window[i].vidosd[0] >>  0) & 0x7FF;
            rightbottom_x = (s->window[i].vidosd[1] >> 11) & 0x7FF;
            rightbottom_y = (s->window[i].vidosd[1] >>  0) & 0x7FF;
            height = rightbottom_y - lefttop_y + 1;
            width = rightbottom_x - lefttop_x + 1;
            ext_line_size = s->window[i].buf_size & 0x1FFF;
            buf_id = 0;
            if (i <= 1) {
                buf_id = (s->window[i].wincon >> 20) & 1;
            }
            /* According to documentation framebuffer is always located in
               single bank of DRAM. Bits [31:24] of BUF_START encode bank
               number, and [23:0] - address of the buffer in bank. We will
               assume that DRAM Controller uses linear memory mapping so
               BUF_START will be just address of the framebuffer. In the
               other case framebuffer will be dispersed all over the system
               memory so it is unclear how such system will work.

               Moreover, we will ignore absence of carry bit bitween bits 23
               and 24 while incrementing address in the hope that no
               programmer will use such hack. */
            scanline = s->window[i].buf_start[buf_id];
            inc_size = (s->window[i].buf_size & 0x1FFF) +
                       ((s->window[i].buf_size >> 13) & 0x1FFF);
            cpu_physical_sync_dirty_bitmap(scanline,
                                           scanline + height * inc_size);
            pd = (cpu_get_physical_page_desc(scanline) & TARGET_PAGE_MASK) +
                 (scanline & ~TARGET_PAGE_MASK);
            dirty[0] = dirty[1] =
                cpu_physical_memory_get_dirty(scanline, VGA_DIRTY_FLAG);
            cpu_physical_memory_reset_dirty(scanline, scanline, VGA_DIRTY_FLAG);
            for (line = 0; line < height; line++) {
                newline = scanline + ext_line_size;
                for (x = scanline;
                     x < newline;
                     x += TARGET_PAGE_SIZE) {
                    pd = (cpu_get_physical_page_desc(x) & TARGET_PAGE_MASK) +
                         (x & ~TARGET_PAGE_MASK);
                    dirty[1] = cpu_physical_memory_get_dirty(pd, VGA_DIRTY_FLAG);
                    dirty[0] |= dirty[1];
                }
                if (dirty[0]) {
                    tmp = line + lefttop_y;
                    s->valid_line[tmp >> 3] &= ~(1 << (tmp & 0x7));
                }
                dirty[0] = dirty[1] = 0;
                scanline += (s->window[i].buf_size & 0x1FFF) +
                    ((s->window[i].buf_size >> 13) & 0x1FFF);
            }
            scanline = s->window[i].buf_start[buf_id];
            pd = (cpu_get_physical_page_desc(scanline) & TARGET_PAGE_MASK) +
                 (scanline & ~TARGET_PAGE_MASK);
            cpu_physical_memory_reset_dirty(pd, pd + inc_size * height,
                                            VGA_DIRTY_FLAG);
        }
    }

    need_redraw = 0;
    is_first_window = 1;
    for (i = 0; i < 5; i++) {
        if (s->window[i].wincon & 1) {
            cfg.fg_alpha_pix = 0;
            s5pc1xx_parse_win_bppmode(s, &cfg, i);
            /* If we have mode with palletized color then we need to parse
               palette color mode and set pixel-to-rgb conversion function
               accordingly. */
            if (cfg.is_palletized) {
                tmp = s5pc1xx_wxpal(s, i);
                /* Different windows have different mapping WxPAL to palette
                   pixel format. This transform happens to unify them all. */
                if (i < 2 && tmp < 7) {
                    tmp = 6 - tmp;
                }
                cfg.pixel_to_rgb = wxpal_to_rgb[tmp];
                if (tmp == 7) {
                    cfg.fg_alpha_pix = 1;
                }
            }
            cfg.put_pixel = put_rgba;
            cfg.get_pixel = get_rgba;
            cfg.bg_alpha_pix = 1;
            cfg.color_mask = s->window[i].keycon[0] & 0xFFFFFF;
            cfg.color_key = s->window[i].keycon[1];
            cfg.color_ctl = (s->window[i].keycon[0] >> 24) & 7;
            if (i == 0) {
                cfg.fg_alpha[0] = s->window[i].vidw_alpha[0];
                cfg.fg_alpha[1] = s->window[i].vidw_alpha[1];
            } else {
                cfg.fg_alpha[0] =
                    unpack_by_4((s->window[i].vidosd[3] & 0xFFF000) >> 12) |
                    (s->window[i].vidw_alpha[0] & 0xF0F0F);
                cfg.fg_alpha[1] =
                    unpack_by_4(s->window[i].vidosd[3] & 0xFFF) |
                    (s->window[i].vidw_alpha[0] & 0xF0F0F);
            }
            cfg.bg_pixel_blending = 1;
            cfg.fg_pixel_blending = s->window[i].wincon & (1 << 6);
            cfg.fg_alpha_sel = (s->window[i].wincon >> 1) & 1;
            cfg.palette = s->window[i].palette;
            cfg.swap = (s->window[i].wincon >> 15) & 0xF;
            cfg.coef_q = coef_decode((s->window[i].blendeq >> 18) & 0xF);
            cfg.coef_p = coef_decode((s->window[i].blendeq >> 12) & 0xF);
            cfg.coef_b = coef_decode((s->window[i].blendeq >>  6) & 0xF);
            cfg.coef_a = coef_decode((s->window[i].blendeq >>  0) & 0xF);
            if (is_first_window) {
                cfg.blend = NULL;
            } else {
                cfg.blend = blend_colorkey;
            }
            is_first_window = 0;
            /* At this point CFG is fully set up except WIDTH. We can proceed
               with drawing. */
            lefttop_x = (s->window[i].vidosd[0] >> 11) & 0x7FF;
            lefttop_y = (s->window[i].vidosd[0] >>  0) & 0x7FF;
            rightbottom_x = (s->window[i].vidosd[1] >> 11) & 0x7FF;
            rightbottom_y = (s->window[i].vidosd[1] >>  0) & 0x7FF;
            height = rightbottom_y - lefttop_y + 1;
            width = rightbottom_x - lefttop_x + 1;
            cfg.width = width;
            ext_line_size = (width * cfg.bpp) >> 3;
            buf_id = 0;
            if (i <= 1) {
                buf_id = (s->window[i].wincon >> 20) & 1;
            }
            scanline = s->window[i].buf_start[buf_id];
            global_width = (s->vidtcon[2] & 0x7FF) + 1;
            global_height = ((s->vidtcon[2] >> 11) & 0x7FF) + 1;
            /* See comment above about DRAM Controller memory mapping. */
            map_len = ((s->window[i].buf_size & 0x1FFF) +
                    ((s->window[i].buf_size >> 13) & 0x1FFF)) * height;
            mapline = cpu_physical_memory_map(scanline, &map_len, 0);
            if (!mapline) {
                return;
            }
            startline = mapline;
            for (line = 0; line < height; line++) {
                tmp = line + lefttop_y;
                if (s->invalidate ||
                    !(s->valid_line[tmp >> 3] & (1 << (tmp & 7))) ||
                    !(s->valid_line_prev[tmp >> 3] & (1 << (tmp & 7)))) {
                    need_redraw = 1;
                    cfg.draw_line(&cfg, mapline,
                                  s->ifb + lefttop_x * 7 +
                                      (lefttop_y + line) * global_width * 7,
                                  s->ifb + lefttop_x * 7 +
                                      (lefttop_y + line) * global_width * 7);
                }
                mapline += (s->window[i].buf_size & 0x1FFF) +
                    ((s->window[i].buf_size >> 13) & 0x1FFF);
            }
            cpu_physical_memory_unmap(startline, map_len, 0, 0);
        }
    }
    /* Last pass: copy resulting image to QEMU_CONSOLE. */
    if (need_redraw) {
        width = (s->vidtcon[2] & 0x7FF) + 1;
        height = ((s->vidtcon[2] >> 11) & 0x7FF) + 1;
        cfg.get_pixel = get_rgba;
        bpp = ds_get_bits_per_pixel(s->console);
        putpixel_by_bpp(&cfg, bpp);
        bpp = (bpp + 1) >> 3;
        d = ds_get_data(s->console);
        for (line = 0; line < height; line++) {
            draw_line_copy(&cfg, s->ifb + width * line * 7,
                           d + width * line * bpp, NULL);
        }
        dpy_update(s->console, 0, 0, width, height);
    }
    valid_line_tmp = s->valid_line;
    s->valid_line = s->valid_line_prev;
    s->valid_line_prev = valid_line_tmp;
    s->invalidate = 0;
    s->vidintcon[1] |= 2;
    s5pc1xx_lcd_update_irq(s);
}

static void s5pc1xx_lcd_invalidate(void *opaque)
{
    S5pc1xxLcdState *s = (S5pc1xxLcdState *)opaque;
    s->invalidate = 1;
}

static void s5pc1xx_window_reset(S5pc1xxLcdWindow *s)
{
    memset(s, 0, sizeof(*s));
    s->blendeq = 0xC2;
}

static void s5pc1xx_lcd_reset(S5pc1xxLcdState *s)
{
    int i;

    memset((uint8_t *)s + sizeof(SysBusDevice),
            0, offsetof(S5pc1xxLcdState, window));
    for (i = 0; i < 5; i++) {
        s5pc1xx_window_reset(&s->window[i]);
    }
    if (s->ifb != NULL) {
        qemu_free(s->ifb);
    }
    s->ifb = NULL;
    if (s->valid_line != NULL) {
        qemu_free(s->valid_line);
    }
    s->valid_line = NULL;
    if (s->valid_line_prev != NULL) {
        qemu_free(s->valid_line_prev);
    }
    s->valid_line_prev = NULL;
}

static int s5pc1xx_lcd_init(SysBusDevice *dev)
{
    int iomemtype;
    S5pc1xxLcdState *s = FROM_SYSBUS(S5pc1xxLcdState, dev);

    s->ifb = NULL;
    s->valid_line = NULL;
    s->valid_line_prev = NULL;
    s5pc1xx_lcd_reset(s);

    sysbus_init_irq(dev, &s->irq[0]);
    sysbus_init_irq(dev, &s->irq[1]);
    sysbus_init_irq(dev, &s->irq[2]);

    iomemtype =
        cpu_register_io_memory(s5pc1xx_lcd_readfn, s5pc1xx_lcd_writefn, s);
    sysbus_init_mmio(dev, 0x3800, iomemtype);

    s->console = graphic_console_init(s5pc1xx_lcd_update,
                                      s5pc1xx_lcd_invalidate, NULL, NULL, s);
    return 0;
}

static void s5pc1xx_lcd_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,lcd", sizeof(S5pc1xxLcdState),
                        s5pc1xx_lcd_init);
}

device_init(s5pc1xx_lcd_register_devices)
