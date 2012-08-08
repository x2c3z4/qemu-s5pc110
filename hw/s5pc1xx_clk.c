/*
 * Clock controller for Samsung S5PC1XX-based board emulation
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *
 * Based on OMAP CMU (hw/omap_clk.c)
 */

#include "sysbus.h"
#include "s5pc1xx.h"


/* PLL lock */
#define APLL_LOCK   0x000  /* R/W Control PLL locking period for APLL 0x0000_0FFF */
#define MPLL_LOCK   0x008  /* R/W Control PLL locking period for MPLL 0x0000_0FFF */
#define EPLL_LOCK   0x010  /* R/W Control PLL locking period for EPLL 0x0000_0FFF */
#define VPLL_LOCK   0x020  /* R/W Control PLL locking period for VPLL 0x0000_0FFF */

/* PLL control */
#define APLL_CON    0x100   /* R/W Control PLL output frequency for APLL 0x0C80_0301 */
#define MPLL_CON    0x108   /* R/W Control PLL output frequency for MPLL 0x014D_0301 */
#define EPLL_CON    0x110   /* R/W Control PLL output frequency for EPLL 0x0085_0302 */
#define VPLL_CON    0x120   /* R/W Control PLL output frequency for VPLL 0x006C_0303 */

/* Clock source */
#define CLK_SRC0    0x200   /* R/W Select clock source 0 (Main) 0x0000_0000 */

    #define ONENAND_SHIFT   28
    #define MUX133_SHIFT    24
    #define MUX166_SHIFT    20
    #define MUX200_SHIFT    16
    #define VPLL_SHIFT      12
    #define EPLL_SHIFT      8
    #define MPLL_SHIFT      4
    #define APLL_SHIFT      0

#define CLK_SRC1    0x204   /* R/W Select clock source 1 (Multimedia) 0x0000_0000 */
#define CLK_SRC2    0x208   /* R/W Select clock source 2 (Multimedia) 0x0000_0000 */

    #define MFC_SHIFT       4
    #define G3D_SHIFT       0

#define CLK_SRC3    0x20C   /* R/W Select clock source 3 (Multimedia) 0x0000_0000 */
#define CLK_SRC4    0x210   /* R/W Select clock source 4 (Connectivity) 0x0000_0000 */
#define CLK_SRC5    0x214   /* R/W Select clock source 5 (Connectivity) 0x0000_0000 */

    #define MUX_PWM_SHIFT       12

#define CLK_SRC6    0x218   /* R/W Select clock source 6 (Audio) 0x0000_0000 */

    #define MUX_ONEDRAM_SHIFT   24
    #define MUX_HPM_SHIFT       16
    #define MUX_SPDIF_SHIFT     12
    #define MUX_AUDIO_2_SHIFT   8
    #define MUX_AUDIO_1_SHIFT   4
    #define MUX_AUDIO_0_SHIFT   0

#define CLK_SRC_MASK0   0x280  /* R/W Clock source mask0 0xFFFF_FFFF */
#define CLK_SRC_MASK1   0x284  /* R/W Clock source mask1 0xFFFF_FFFF */

/* Clock divider */
#define CLK_DIV0    0x300   /* R/W Set clock divider ratio 0 (System clocks) 0x0000_0000 */

#define PCLK66_SHIFT    28
#define HCLK133_SHIFT   24
#define PCLK83_SHIFT    20
#define HCLK166_SHIFT   16
#define PCLK100_SHIFT   12
#define HCLK200_SHIFT   8
#define A2M_SHIFT       4
#define APLL_SHIFT      0

#define CLK_DIV1    0x304   /* R/W Set clock divider ratio 1 (Multimedia) 0x0000_0000 */
#define CLK_DIV2    0x308   /* R/W Set clock divider ratio 2 (Multimedia) 0x0000_0000 */
#define CLK_DIV3    0x30C   /* R/W Set clock divider ratio 3 (Multimedia) 0x0000_0000 */
#define CLK_DIV4    0x310   /* R/W Set clock divider ratio 4 (Connectivity) 0x0000_0000 */
#define CLK_DIV5    0x314   /* R/W Set clock divider ratio 5 (Connectivity) 0x0000_0000 */

    #define DIV_PWM_SHIFT       12

#define CLK_DIV6    0x318   /* R/W Set clock divider ratio 6 (Audio & Others) 0x0000_0000 */

    #define DIV_AUDIO_2_SHIFT   8
    #define DIV_AUDIO_1_SHIFT   4
    #define DIV_AUDIO_0_SHIFT   0

#define CLK_DIV7    0x31C   /* R/W Set clock divider ratio 7 (IEM_IEC) 0x0000_0000 */

/* Clock gating */
#define CLK_GATE_MAIN0  0x400   /* R/W Control AXI/AHB clock gating 0 0xFFFF_FFFF */
#define CLK_GATE_MAIN1  0x404   /* R/W Control AXI/AHB clock gating 1 0xFFFF_FFFF */
#define CLK_GATE_MAIN2  0x408   /* R/W Control AXI/AHB clock gating 2 0xFFFF_FFFF */
#define CLK_GATE_PERI0  0x420   /* R/W Control APB clock gating 0 0xFFFF_FFFF */
#define CLK_GATE_PERI1  0x424   /* R/W Control APB clock gating 1 0xFFFF_FFFF */
#define CLK_GATE_SCLK0  0x440   /* R/W Control SCLK clock gating0 0xFFFF_FFFF */
#define CLK_GATE_SCLK1  0x444   /* R/W Control SCLK clock gating1 0xFFFF_FFFF */
#define CLK_GATE_IP0    0x460   /* R/W Control IP clock gating0 0xFFFF_FFFF */
#define CLK_GATE_IP1    0x464   /* R/W Control IP clock gating1 0xFFFF_FFFF */
#define CLK_GATE_IP2    0x468   /* R/W Control IP clock gating2 0xFFFF_FFFF */
#define CLK_GATE_IP3    0x46C   /* R/W Control IP clock gating3 0xFFFF_FFFF */
#define CLK_GATE_IP4    0x470   /* R/W Control IP clock gating4 0xFFFF_FFFF */
#define CLK_GATE_BLOCK  0x480   /* R/W Control block clock gating 0xFFFF_FFFF */
#define CLK_GATE_BUS0   0x484   /* R/W Control AXI/AHB bus clock gating 0 0xFFFF_FFFF */
#define CLK_GATE_BUS1   0x488   /* R/W Control AXI/AHB bus clock gating 1 0xFFFF_FFFF */

/* Clock output */
#define CLK_OUT         0x500   /* R/W Select clock output 0x0000_0000 */

/* Clock divider status */
#define CLK_DIV_STAT0   0x1000  /* R Clock divider status 0 (CLK_DIV0~3) 0x1111_1111 */
#define CLK_DIV_STAT1   0x1004  /* R Clock divider status 1 (CLK_DIV4~5) 0x0001_0000 */

/* Clock MUX status */
#define CLK_MUX_STAT0   0x1100  /* R Clock MUX status 0 0x0000_0000 */
#define CLK_MUX_STAT1   0x1104  /* R Clock MUX status 1 0x0000_0000 */

/* Control bits */
#define XOM_0           0
#define VPLLSRC_SEL     1
#define ALL_MUX_BITS_0  (1 << ONENAND_SHIFT)    | (1 << MUX133_SHIFT) | \
                            (1 << MUX166_SHIFT) | (1 << MUX200_SHIFT) | \
                            (1 << VPLL_SHIFT)   | (1 << EPLL_SHIFT)   | \
                            (1 << MPLL_SHIFT)   | (1 << APLL_SHIFT)
#define ALL_DIV_BITS_0  (7 << PCLK66_SHIFT)     | (0xf << HCLK133_SHIFT) | \
                            (7 << PCLK83_SHIFT) | (0xf << HCLK166_SHIFT) | \
                            (7 << PCLK100_SHIFT)| (7 << HCLK200_SHIFT)   | \
                            (7 << A2M_SHIFT)    | (7 << APLL_SHIFT)

#define ALL_MUX_BITS_5  0xffff  /* all the [15:0] bits are used */
#define ALL_DIV_BITS_5  0xffff  /* all the [15:0] bits are used */

#define NONE            -1

#define S5PC1XX_CLK_REG_MEM_SIZE 0x1110


typedef struct {
    SysBusDevice busdev;

    uint32_t lock_data[5];      /* PLL lock */
    uint32_t conf_data[5];      /* PLL control */
    uint32_t clk_src[7];        /* Clock source */
    uint32_t src_mask[2];       /* Clock source mask */
    uint32_t clk_div[8];        /* Clock divider */
    uint32_t clk_gate[23];      /* Clock gating */
    uint32_t clk_out;           /* Clock output */
    uint32_t div_stat[2];       /* Clock divider status */
    uint32_t mux_stat[2];       /* Clock MUX status */
} CmuStat;

typedef struct Clk {
    const char *name;           /* Clock name */
    const char *alias;          /* Clock notes */
    struct Clk *parent;         /* Parent clock */

    struct Clk *parents[9];     /* Parent cases */

    unsigned short enabled;     /* Is enabled, regardless of its input clk */
    unsigned long rate;         /* Current rate */

    unsigned int div_val;       /* Rate relative to input (if .enabled) */
    unsigned int mult_val;      /* Rate relative to input (if .enabled) */

    short mux_shift;            /* MANDATORY FIELD - Shift for mux value in CLK_SRC<src_reg_num> */
    short div_shift;            /* MANDATORY FIELD - Shift for divisor value in CLK_DIV<div_reg_num> */

    unsigned short src_reg_num; /* See above (default value = 0) */
    unsigned short div_reg_num; /* See above (default value = 0) */
} Clk;


/* Clocks */

/* oscillators */
static Clk xtal_osc12m = {
    .name       = "XXTI",
    .rate       = 24000000,
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk xtal_osc27m = {
    .name       = "XXTI27",
    .rate       = 27000000,
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk xtal_usb_osc48m = {
    .name       = "XusbXTI",
    .rate       = 24000000,
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk xtal_rtc_osc32k = {
    .name       = "XrtcXTI",
    .rate       = 32768,
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk sclk_hdmi27m = {
    .name       = "SCLK_HDMI27M",
    .alias      = "clkin",
    .parents    = {&xtal_osc27m},
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk fin_pll = {
    .name       = "fin_pll",
    .alias      = "clkin",
    .parents    = {XOM_0 ? &xtal_usb_osc48m : &xtal_osc12m},
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

/* PLLs */
static Clk apll = {
    .name       = "apll",
    .parents    = {&fin_pll},
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk mpll = {
    .name       = "mpll",
    .parents    = {&fin_pll},
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk epll = {
    .name       = "epll",
    .parents    = {&fin_pll},
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

static Clk vpll = {
    .name       = "vpll",
    .parents    = {VPLLSRC_SEL ? &sclk_hdmi27m : &fin_pll},
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

/* reference clocks */
static Clk sclk_apll = {
    .name       = "sclk_apll",
    .parents    = {&fin_pll, &apll},
    .mux_shift  = APLL_SHIFT,       /* MUXapll */
    .div_shift  = NONE,
};

static Clk sclk_mpll = {
    .name       = "sclk_mpll",
    .parents    = {&fin_pll, &mpll},
    .mux_shift  = MPLL_SHIFT,       /* MUXmpll */
    .div_shift  = NONE,
};

static Clk sclk_epll = {
    .name       = "sclk_epll",
    .parents    = {&fin_pll, &epll},
    .mux_shift  = EPLL_SHIFT,       /* MUXepll */
    .div_shift  = NONE,
};

static Clk sclk_vpll = {
    .name       = "sclk_vpll",
    .parents    = {&fin_pll, &vpll},
    .mux_shift  = VPLL_SHIFT,       /* MUXvpll */
    .div_shift  = NONE,
};

static Clk sclk_a2m = {
    .name       = "sclk_a2m",
    .parents    = {&sclk_apll},
    .mux_shift  = NONE,
    .div_shift  = A2M_SHIFT,        /* DIVa2m (1~8) */
};

/* 133MHz domain clocks */
static Clk hclk133 = {
    .name       = "hclk_133",
    .parents    = {&sclk_mpll, &sclk_a2m},
    .mux_shift  = MUX133_SHIFT,     /* MUX133 */
    .div_shift  = HCLK133_SHIFT,    /* DIVck133 (1~16) */
};

static Clk pclk66 = {
    .name       = "pclk_66",
    .parents    = {&hclk133},
    .mux_shift  = NONE,
    .div_shift  = PCLK66_SHIFT,     /* DIVck66 (1~8) */
};

/* 166MHz domain clocks */
static Clk hclk166 = {
    .name       = "hclk_166",
    .parents    = {&sclk_mpll, &sclk_a2m},
    .mux_shift  = MUX166_SHIFT,     /* MUX166 */
    .div_shift  = HCLK166_SHIFT,    /* DIVck166 (1~16) */
};

static Clk pclk83 = {
    .name       = "pclk_83",
    .parents    = {&hclk166},
    .mux_shift  = NONE,
    .div_shift  = PCLK83_SHIFT,     /* DIVck83 (1~8) */
};

/* 200MHz domain clocks */
static Clk armclk = {
    .name       = "armclk",
    .parents    = {&sclk_apll, &sclk_mpll},
    .mux_shift  = MUX200_SHIFT,     /* MUX200 */
    .div_shift  = APLL_SHIFT,       /* DIVapll (1~8) */
};

static Clk hclk200 = {
    .name       = "hclk_200",
    .parents    = {&armclk},
    .mux_shift  = NONE,
    .div_shift  = HCLK200_SHIFT,    /* DIVck200 (1~8) */
};

static Clk pclk100 = {
    .name       = "pclk_100",
    .parents    = {&hclk200},
    .mux_shift  = NONE,
    .div_shift  = PCLK100_SHIFT,    /* DIVck100 (1~8) */
};

static Clk hclk100 = {
    .name       = "hclk_100",
    .parents    = {&hclk200},
    .div_val    = 2,                /* DIVimem (2) */
    .mux_shift  = NONE,
    .div_shift  = NONE,
};

/* Special clocks */

static Clk sclk_pwm = {
    .name       = "sclk_pwm",
    .parents    = {&xtal_osc12m, &xtal_usb_osc48m, &sclk_hdmi27m,

                   /* TODO: should be SCLK_USBPHY0, SCLK_USBPHY1, SCLK_HDMIPHY below */
                   &sclk_hdmi27m, &sclk_hdmi27m, &sclk_hdmi27m,

                   &sclk_mpll, &sclk_epll, &sclk_vpll},
    .src_reg_num = 5,               /* CLK_SRC5 register */
    .mux_shift   = MUX_PWM_SHIFT,   /* MUXpwm */
    .div_reg_num = 5,               /* CLK_DIV5 register */
    .div_shift   = DIV_PWM_SHIFT,   /* DIVpwm (1~16) */
};

static Clk sclk_audio_0 = {
    .name       = "sclk_audio_0",
    .parents    = {&xtal_osc12m,

                   /* TODO: should be PCMCDCLK0 below */
                   &xtal_usb_osc48m,

                   &sclk_hdmi27m,

                   /* TODO: should be SCLK_USBPHY0, SCLK_USBPHY1, SCLK_HDMIPHY below */
                   &sclk_hdmi27m, &sclk_hdmi27m, &sclk_hdmi27m,

                   &sclk_mpll, &sclk_epll, &sclk_vpll},
    .src_reg_num = 6,                   /* CLK_SRC6 register */
    .mux_shift   = MUX_AUDIO_0_SHIFT,   /* MUXaudio0 */
    .div_reg_num = 6,                   /* CLK_DIV6 register */
    .div_shift   = DIV_AUDIO_0_SHIFT,   /* DIVaudio0 (1~16) */
};

static Clk sclk_audio_1 = {
    .name       = "sclk_audio_1",
    .parents    = {
                   /* TODO should be I2SCDCLK1, PCMCDCLK1 below */
                   &xtal_osc12m, &xtal_usb_osc48m,

                   &sclk_hdmi27m,

                   /* TODO: should be SCLK_USBPHY0, SCLK_USBPHY1, SCLK_HDMIPHY below */
                   &sclk_hdmi27m, &sclk_hdmi27m, &sclk_hdmi27m,

                   &sclk_mpll, &sclk_epll, &sclk_vpll},
    .src_reg_num = 6,                   /* CLK_SRC6 register */
    .mux_shift   = MUX_AUDIO_1_SHIFT,   /* MUXaudio1 */
    .div_reg_num = 6,                   /* CLK_DIV6 register */
    .div_shift   = DIV_AUDIO_1_SHIFT,   /* DIVaudio1 (1~16) */
};

static Clk sclk_audio_2 = {
    .name       = "sclk_audio_2",
    .parents    = {
                   /* TODO: should be I2SCDCLK2, PCMCDCLK2 below */
                   &xtal_osc12m, &xtal_usb_osc48m,

                   &sclk_hdmi27m,

                   /* TODO: should be SCLK_USBPHY0, SCLK_USBPHY1, SCLK_HDMIPHY below */
                   &sclk_hdmi27m, &sclk_hdmi27m, &sclk_hdmi27m,

                   &sclk_mpll, &sclk_epll, &sclk_vpll},
    .src_reg_num = 6,                   /* CLK_SRC6 register */
    .mux_shift   = MUX_AUDIO_2_SHIFT,   /* MUXaudio2 */
    .div_reg_num = 6,                   /* CLK_DIV6 register */
    .div_shift   = DIV_AUDIO_2_SHIFT,   /* DIVaudio2 (1~16) */
};

static Clk sclk_spdif = {
    .name        = "sclk_spdif",
    .parents     = {&sclk_audio_0, &sclk_audio_1, &sclk_audio_2},
    .src_reg_num = 6,                   /* CLK_SRC6 register */
    .mux_shift   = MUX_SPDIF_SHIFT,     /* MUXspdif */
    .div_shift   = NONE,
};

static Clk *onchip_clks[] = {

    /* non-ULPD clocks */
    &xtal_osc12m,
    &xtal_osc27m,
    &xtal_usb_osc48m,
    &xtal_rtc_osc32k,
    &sclk_hdmi27m,
    &fin_pll,

    /* CLOCKS FROM CMU */
    &apll,
    &mpll,
    &epll,
    &vpll,
    &sclk_apll,
    &sclk_mpll,
    &sclk_epll,
    &sclk_vpll,
    &sclk_a2m,
    &hclk133,
    &pclk66,
    &hclk166,
    &pclk83,
    &armclk,
    &hclk200,
    &pclk100,
    &hclk100,
    &sclk_pwm,
    &sclk_audio_0,
    &sclk_audio_1,
    &sclk_audio_2,
    &sclk_spdif,

    0
};

/* Find a clock by its name and return the clk structure */
Clk *s5pc1xx_findclk(const char *name)
{
    Clk **i, *cur;

    for (i = onchip_clks; *i; i++) {
        cur = *i;
        if (!strcmp(cur->name, name) ||
            (cur->alias && !strcmp(cur->alias, name)))
            return cur;
    }
    hw_error("%s: clock %s not found\n", __FUNCTION__, name);
}

/* Get a frequency */
int64_t s5pc1xx_clk_getrate(Clk *clk)
{
    return clk->rate;
}

/* Update parents flow */
static void s5pc1xx_clk_reparent(CmuStat *cmu_stat)
{
    Clk **i, *cur;
    unsigned short parent_num;

    for (i = onchip_clks; *i; i++) {
        cur = *i;
        parent_num = 0;

        if (cur->mux_shift > NONE)
            parent_num =
                cmu_stat->clk_src[cur->src_reg_num] >> cur->mux_shift & 0xf;
        cur->parent = cur->parents[parent_num];
    }
}

/* Update clocks rates */
static void s5pc1xx_clk_rate_update(CmuStat *cmu_stat)
{
    Clk **i, *cur;

    for (i = onchip_clks; *i; i++) {
        cur = *i;

        cur->div_val  = cur->div_val ?: 1;
        cur->mult_val = cur->mult_val ?: 1;

        /* update all divisors using div_shift if any,
         * divisors for (A,E,M,V)PLL are not updated here */
        if (cur->div_shift > NONE)
            cur->div_val =
                (cmu_stat->clk_div[cur->div_reg_num]  >> cur->div_shift & 0xf) + 1;

        /* update frequencies for all the clocks except the oscillators */
        if (cur->parent)
            cur->rate = muldiv64(cur->parent->rate, cur->mult_val, cur->div_val);
    }
}

/* Set a frequency */
static void s5pc1xx_clk_setrate(CmuStat *cmu_stat, Clk *clk,
                                int divide, int multiply)
{
    clk->div_val  = divide;
    clk->mult_val = multiply;
}

/* Set (A,M,E,V)PLL params after write operation */
static void set_pll_conf(void *opaque, target_phys_addr_t offset, uint32_t val)
{
    CmuStat *s = (CmuStat *)opaque;

    /* Calculate control values depending on clock kind */
    switch ((offset - 0x100) >> 3) {

    case 0:
        apll.enabled = (val >> 31) & 0x1;
        /* include/exclude clock depending on .enabled value */
        if (apll.enabled)
            s->clk_src[0] |= (1 << sclk_apll.mux_shift);
        else
            s->clk_src[0] &= ~(1 << sclk_apll.mux_shift);

        s5pc1xx_clk_setrate(s, &apll,
                            ((val >> 8) & 0x3F) << ((val & 0x7) - 1),
                            (val >> 16) & 0x3FF);
        break;

    case 1:
        mpll.enabled = (val >> 31) & 0x1;
        if (mpll.enabled)
            s->clk_src[0] |= (1 << sclk_mpll.mux_shift);
        else
            s->clk_src[0] &= ~(1 << sclk_mpll.mux_shift);

        s5pc1xx_clk_setrate(s, &mpll,
                            ((val >> 8) & 0x3F) << (val & 0x7),
                            (val >> 16) & 0x3FF);
        break;

    case 2:
        epll.enabled = (val >> 31) & 0x1;
        if (epll.enabled)
            s->clk_src[0] |= (1 << sclk_epll.mux_shift);
        else
            s->clk_src[0] &= ~(1 << sclk_epll.mux_shift);

        s5pc1xx_clk_setrate(s, &epll,
                            ((val >> 8) & 0x3F) << (val & 0x7),
                            (val >> 16) & 0x3FF);
        break;

    case 4:
        vpll.enabled = (val >> 31) & 0x1;
        if (vpll.enabled)
            s->clk_src[0] |= (1 << sclk_vpll.mux_shift);
        else
            s->clk_src[0] &= ~(1 << sclk_vpll.mux_shift);

        s5pc1xx_clk_setrate(s, &vpll,
                            ((val >> 8) & 0x3F) << (val & 0x7),
                            (val >> 16) & 0x3FF);
        break;

    default:
        hw_error("s5pc1xx_clk: bad pll offset 0x" TARGET_FMT_plx "\n", offset);
    }
}

static uint32_t register_read(void *opaque, target_phys_addr_t offset)
{
    CmuStat *s = (CmuStat *)opaque;

    switch (offset) {
    case 0x000 ... 0x020:
        return s->lock_data[offset >> 3];
    case 0x100 ... 0x120:
        return s->conf_data[(offset - 0x100) >> 3];
    case 0x200 ... 0x218:
        return s->clk_src[(offset - 0x200) >> 2];
    case 0x280 ... 0x284:
        return s->src_mask[(offset - 0x280) >> 2];
    case 0x300 ... 0x31C:
        return s->clk_div[(offset - 0x300) >> 2];
    case 0x400 ... 0x488:
        return s->clk_gate[(offset - 0x400) >> 2];
    case 0x500:
        return s->clk_out;
    case 0x1000:
    case 0x1004:
        return 0;
    case 0x1100:
        return
            (1 << ((s->clk_src[0] >> ONENAND_SHIFT) & 0x1)) << ONENAND_SHIFT |
            (1 << ((s->clk_src[0] >> MUX133_SHIFT) & 0x1)) << MUX133_SHIFT |
            (1 << ((s->clk_src[0] >> MUX166_SHIFT) & 0x1)) << MUX166_SHIFT |
            (1 << ((s->clk_src[0] >> MUX200_SHIFT) & 0x1)) << MUX200_SHIFT |
            (1 << ((s->clk_src[0] >> VPLL_SHIFT) & 0x1)) << VPLL_SHIFT |
            (1 << ((s->clk_src[0] >> EPLL_SHIFT) & 0x1)) << EPLL_SHIFT |
            (1 << ((s->clk_src[0] >> MPLL_SHIFT) & 0x1)) << MPLL_SHIFT |
            (1 << ((s->clk_src[0] >> APLL_SHIFT) & 0x1)) << APLL_SHIFT;
    case 0x1104:
        return
            (1 << ((s->clk_src[6] >> MUX_HPM_SHIFT) & 0x1)) << MUX_HPM_SHIFT |
            ((((s->clk_src[6] >> MUX_ONEDRAM_SHIFT) << 1) & 0x6) |
             ((s->clk_src[6] >> MUX_ONEDRAM_SHIFT) & 0x1)) << 8 |
            ((((s->clk_src[2] >> MFC_SHIFT) << 1) & 0x6) |
             ((s->clk_src[2] >> MFC_SHIFT) & 0x1)) << MFC_SHIFT |
            ((((s->clk_src[2] >> G3D_SHIFT) << 1) & 0x6) |
             ((s->clk_src[2] >> G3D_SHIFT) & 0x1)) << G3D_SHIFT;
    default:
        hw_error("s5pc1xx_clk: bad read offset 0x" TARGET_FMT_plx "\n", offset);
        return 0;
    }
}

static void register_write(void *opaque, target_phys_addr_t offset,
                           uint32_t val)
{
    CmuStat *s = (CmuStat *)opaque;

    switch (offset) {
    case 0x000 ... 0x020:
        s->lock_data[offset >> 3] = val;
        break;
    case 0x100 ... 0x120:
        /* LOCKED bit is always set */
        s->conf_data[(offset - 0x100) >> 3] = val | (1 << 29);

        set_pll_conf(s, offset, val);
        s5pc1xx_clk_reparent(s);
        s5pc1xx_clk_rate_update(s);

        break;
    case 0x200 ... 0x218:
        s->clk_src[(offset - 0x200) >> 2] = val;

        /* clear reserved bits for security */
        s->clk_src[0] &= ALL_MUX_BITS_0;
        s->clk_src[5] &= ALL_MUX_BITS_5;

        s5pc1xx_clk_reparent(s);

        break;
    case 0x280 ... 0x284:
        s->src_mask[(offset - 0x280) >> 2] = val;
        break;
    case 0x300 ... 0x31C:
        s->clk_div[(offset - 0x300) >> 2] = val;

        /* clear reserved bits for security */
        s->clk_div[0] &= ALL_DIV_BITS_0;
        s->clk_div[5] &= ALL_DIV_BITS_5;

        s5pc1xx_clk_rate_update(s);

        break;
    case 0x400 ... 0x488:
        s->clk_gate[(offset - 0x400) >> 2] = val;
        break;
    case 0x500:
        s->clk_out = val;
        break;
    case 0x1000 ... 0x1004:
    case 0x1100 ... 0x1104:
    default:
        hw_error("s5pc1xx_clk: bad write offset 0x" TARGET_FMT_plx "\n", offset);
    }
}

static CPUReadMemoryFunc * const register_readfn[] = {
   register_read,
   register_read,
   register_read
};

static CPUWriteMemoryFunc * const register_writefn[] = {
   register_write,
   register_write,
   register_write
};

/* Initialize clock */
static int s5pc1xx_clk_init(SysBusDevice *dev)
{
    CmuStat *s = FROM_SYSBUS(CmuStat, dev);
    int iomemtype;

    /* Set default values for registers */
    /* TODO: Add all the rest */
    s->clk_div[0] = 0x14131330;
    s->clk_src[0] = 0x10001111;
    s->clk_src[4] = 0x66666666;
    s->clk_src[5] = 0x777;
    s->clk_src[6] = 0x1000000;

    s5pc1xx_clk_reparent(s);
    s5pc1xx_clk_rate_update(s);

    /* If you want to remove the four lines below,
     * call s5pc1xx_clk_reparent(s) and s5pc1xx_clk_rate_update(s) alone */
    register_write(s, APLL_CON, 0xA0C80601);
    register_write(s, MPLL_CON, 0xA29B0C01);
    register_write(s, EPLL_CON, 0xA0600602);
    register_write(s, VPLL_CON, 0xA06C0603);

    /* memory mapping */
    iomemtype = cpu_register_io_memory(register_readfn, register_writefn, s);
    sysbus_init_mmio(dev, S5PC1XX_CLK_REG_MEM_SIZE, iomemtype);

    return 0;
}

static void s5pc1xx_clk_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,clk", sizeof(CmuStat), s5pc1xx_clk_init);
}

device_init(s5pc1xx_clk_register_devices)
