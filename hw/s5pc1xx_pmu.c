/*
 * S5PC1XX Power Management Unit controller,
 * Maxim MAX17040 Fuel Gauge,
 * Maxim MAX8998 Battery Charger and Real Time Clock.
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Dmitry Zhurikhin <zhur@ispras.ru>
 *                Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 */

#include "sysbus.h"
#include "smbus.h"
#include "s5pc1xx.h"
#include "qemu-timer.h"


#define S5PC110_PMU_SFR_SIZE 0x8000


typedef struct S5pc1xxPMUState {
    SysBusDevice busdev;

    uint32_t osc_con;
    uint32_t rst_stat;
    uint32_t pwr_cfg;
    uint32_t eint_wakeup_mask;
    uint32_t wakeup_mask;
    uint32_t pwr_mode;
    uint32_t normal_cfg;
    uint32_t idle_cfg;
    uint32_t stop_cfg;
    uint32_t stop_mem_cfg;
    uint32_t sleep_cfg;
    uint32_t osc_freq;
    uint32_t osc_stable;
    uint32_t pwr_stable;
    uint32_t mtc_stable;
    uint32_t clamp_stable;
    uint32_t wakeup_stat;
    uint32_t blk_pwr_stat;
    uint32_t body_bias_con;
    uint32_t ion_skew_con;
    uint32_t ion_skew_mon;
    uint32_t ioff_skew_con;
    uint32_t ioff_skew_mon;
    uint32_t others;
    uint32_t om_stat;
    uint32_t mie_control;
    uint32_t hdmi_control;
    uint32_t usb_phy_control;
    uint32_t dac_control;
    uint32_t mipi_dphy_control;
    uint32_t adc_control;
    uint32_t ps_hold_control;
    uint32_t inform0;
    uint32_t inform1;
    uint32_t inform2;
    uint32_t inform3;
    uint32_t inform4;
    uint32_t inform5;
    uint32_t inform6;
    uint32_t inform7;
} S5pc1xxPMUState;


static uint32_t s5pc1xx_pmu_read(void *opaque, target_phys_addr_t offset)
{
    S5pc1xxPMUState *s = (S5pc1xxPMUState *)opaque;
    target_phys_addr_t test_offset = offset + 0x8000;

    if (offset & 3)
        hw_error("s5pc1xx_pmu: bad read offset " TARGET_FMT_plx "\n", offset);

    switch (test_offset) {
    case 0x8000:
        return s->osc_con;
    case 0xA000:
        return s->rst_stat;
    case 0xC000:
        return s->pwr_cfg;
    case 0xC004:
        return s->eint_wakeup_mask;
    case 0xC008:
        return s->wakeup_mask;
    case 0xC00C:
        return s->pwr_mode;
    case 0xC010:
        return s->normal_cfg;
    case 0xC020:
        return s->idle_cfg;
    case 0xC030:
        return s->stop_cfg;
    case 0xC034:
        return s->stop_mem_cfg;
    case 0xC040:
        return s->sleep_cfg;
    case 0xC100:
        return s->osc_freq;
    case 0xC104:
        return s->osc_stable;
    case 0xC108:
        return s->pwr_stable;
    case 0xC110:
        return s->mtc_stable;
    case 0xC114:
        return s->clamp_stable;
    case 0xC200:
        return s->wakeup_stat;
    case 0xC204:
        return s->blk_pwr_stat;
    case 0xC300:
        return s->body_bias_con;
    case 0xC310:
        return s->ion_skew_con;
    case 0xC314:
        return s->ion_skew_mon;
    case 0xC320:
        return s->ioff_skew_con;
    case 0xC324:
        return s->ioff_skew_mon;
    case 0xE000:
        return s->others;
    case 0xE100:
        return s->om_stat;
    case 0xE800:
        return s->mie_control;
    case 0xE804:
        return s->hdmi_control;
    case 0xE80C:
        return s->usb_phy_control;
    case 0xE810:
        return s->dac_control;
    case 0xE814:
        return s->mipi_dphy_control;
    case 0xE818:
        return s->adc_control;
    case 0xE81C:
        return s->ps_hold_control;
    case 0xF000:
        return s->inform0;
    case 0xF004:
        return s->inform1;
    case 0xF008:
        return s->inform2;
    case 0xF00C:
        return s->inform3;
    case 0xF010:
        return s->inform4;
    case 0xF014:
        return s->inform5;
    case 0xF018:
        return s->inform6;
    case 0xF01C:
        return s->inform7;
    default:
        hw_error("s5pc1xx_pmu: bad read offset " TARGET_FMT_plx "\n", offset);
    }
}

static void s5pc1xx_pmu_write(void *opaque, target_phys_addr_t offset,
                              uint32_t val)
{
    S5pc1xxPMUState *s = (S5pc1xxPMUState *)opaque;
    target_phys_addr_t test_offset = offset + 0x8000;

    /* TODO: Check if writing any values should change emulation flow */

    if (offset & 3)
        hw_error("s5pc1xx_pmu: bad write offset " TARGET_FMT_plx "\n", offset);

    switch (test_offset) {
    case 0x8000:
        s->osc_con = val;
        break;
    case 0xC000:
        s->pwr_cfg = val;
        break;
    case 0xC004:
        s->eint_wakeup_mask = val;
        break;
    case 0xC008:
        s->wakeup_mask = val;
        break;
    case 0xC00C:
        s->pwr_mode = val;
        break;
    case 0xC010:
        s->normal_cfg = val;
        break;
    case 0xC020:
        s->idle_cfg = val;
        break;
    case 0xC030:
        s->stop_cfg = val;
        break;
    case 0xC034:
        s->stop_mem_cfg = val;
        break;
    case 0xC040:
        s->sleep_cfg = val;
        break;
    case 0xC100:
        s->osc_freq = val;
        break;
    case 0xC104:
        s->osc_stable = val;
        break;
    case 0xC108:
        s->pwr_stable = val;
        break;
    case 0xC110:
        s->mtc_stable = val;
        break;
    case 0xC114:
        s->clamp_stable = val;
        break;
    case 0xC200:
        s->wakeup_stat = val;
        break;
    case 0xC300:
        s->body_bias_con = val;
        break;
    case 0xC310:
        s->ion_skew_con = val;
        break;
    case 0xC320:
        s->ioff_skew_con = val;
        break;
    case 0xE000:
        s->others = val;
        break;
    case 0xE800:
        s->mie_control = val;
        break;
    case 0xE804:
        s->hdmi_control = val;
        break;
    case 0xE80C:
        s->usb_phy_control = val;
        break;
    case 0xE810:
        s->dac_control = val;
        break;
    case 0xE814:
        s->mipi_dphy_control = val;
        break;
    case 0xE818:
        s->adc_control = val;
        break;
    case 0xE81C:
        s->ps_hold_control = val;
        break;
    case 0xF000:
        s->inform0 = val;
        break;
    case 0xF004:
        s->inform1 = val;
        break;
    case 0xF008:
        s->inform2 = val;
        break;
    case 0xF00C:
        s->inform3 = val;
        break;
    case 0xF010:
        s->inform4 = val;
        break;
    case 0xF014:
        s->inform5 = val;
        break;
    case 0xF018:
        s->inform6 = val;
        break;
    case 0xF01C:
        s->inform7 = val;
        break;
    case 0xA000: /* rst_stat */
    case 0xC204: /* blk_pwr_stat */
    case 0xC314: /* ion_skew_mon */
    case 0xC324: /* ioff_skew_mon */
    case 0xE100: /* om_stat */
        hw_error("s5pc1xx_pmu: bad write offset " TARGET_FMT_plx "\n", offset);
        break;
    default:
        hw_error("s5pc1xx_pmu: bad write offset " TARGET_FMT_plx "\n", offset);
    }
}
static CPUReadMemoryFunc * const s5pc1xx_pmu_readfn[] = {
    s5pc1xx_pmu_read,
    s5pc1xx_pmu_read,
    s5pc1xx_pmu_read
};

static CPUWriteMemoryFunc * const s5pc1xx_pmu_writefn[] = {
    s5pc1xx_pmu_write,
    s5pc1xx_pmu_write,
    s5pc1xx_pmu_write
};

static void s5pc1xx_pmu_reset(S5pc1xxPMUState *s)
{
    s->osc_con         = 0x00000003;
    s->rst_stat        = 0x00000001;
    s->normal_cfg      = 0xFFFFFFBF;
    s->idle_cfg        = 0x60000000;
    s->stop_cfg        = 0x96000000;
    s->stop_mem_cfg    = 0x000000FF;
    s->osc_freq        = 0x0000000F;
    s->osc_stable      = 0x000FFFFF;
    s->mtc_stable      = 0xFFFFFFFF;
    s->clamp_stable    = 0x03FF03FF;
    s->blk_pwr_stat    = 0x000000BF;
    s->body_bias_con   = 0x00000606;
    s->mie_control     = 0x00000001;
    s->hdmi_control    = 0x00960000;
    s->dac_control     = 0x00000001;
    s->ps_hold_control = 0x00005200;
}

static int s5pc1xx_pmu_init(SysBusDevice *dev)
{
    S5pc1xxPMUState *s = FROM_SYSBUS(S5pc1xxPMUState, dev);
    int iomemtype;

    iomemtype =
        cpu_register_io_memory(s5pc1xx_pmu_readfn, s5pc1xx_pmu_writefn, s);
    sysbus_init_mmio(dev, S5PC110_PMU_SFR_SIZE, iomemtype);

    s5pc1xx_pmu_reset(s);

    return 0;
}


/* Maxim MAX17040 Fuel Gauge */

#define MAX17040_VCELL_MSB      0x02
#define MAX17040_VCELL_LSB      0x03
#define MAX17040_SOC_MSB        0x04
#define MAX17040_SOC_LSB        0x05
#define MAX17040_MODE_MSB       0x06
#define MAX17040_MODE_LSB       0x07
#define MAX17040_VER_MSB        0x08
#define MAX17040_VER_LSB        0x09
#define MAX17040_RCOMP_MSB      0x0C
#define MAX17040_RCOMP_LSB      0x0D
#define MAX17040_CMD_MSB        0xFE
#define MAX17040_CMD_LSB        0xFF

#define MAX17040_DELAY          1000
#define MAX17040_BATTERY_FULL   95
#define MAX17040_BATTERY_CHARGE 40


typedef struct MAX17040State {
    SMBusDevice smbusdev;

    uint16_t charge;
    uint16_t vcell;
    uint16_t mode;
    uint16_t ver;
    uint16_t rcomp;
    uint16_t cmd;
} MAX17040State;

static void max17040_reset(MAX17040State *s)
{
    s->charge = MAX17040_BATTERY_CHARGE << 8;
    s->vcell = 4200;
    s->mode = 0;
    s->ver = 0xAA28;
    s->rcomp = 0;
    s->cmd = 0;
}

static void max17040_write_data(SMBusDevice *dev, uint8_t cmd,
                                uint8_t *buf, int len)
{
    MAX17040State *s = (MAX17040State *)dev;
    int shift = 0, i;
    uint16_t *reg = NULL;

    switch (cmd) {
    case MAX17040_VCELL_LSB:
        shift = 1;
    case MAX17040_VCELL_MSB:
        reg = &s->vcell;
        break;
    case MAX17040_SOC_LSB:
        shift = 1;
    case MAX17040_SOC_MSB:
        reg = &s->charge;
        break;
    case MAX17040_MODE_LSB:
        shift = 1;
    case MAX17040_MODE_MSB:
        reg = &s->mode;
        break;
    case MAX17040_VER_LSB:
        shift = 1;
    case MAX17040_VER_MSB:
        reg = &s->ver;
        break;
    case MAX17040_RCOMP_LSB:
        shift = 1;
    case MAX17040_RCOMP_MSB:
        reg = &s->rcomp;
        break;
    case MAX17040_CMD_LSB:
        shift = 1;
    case MAX17040_CMD_MSB:
        reg = &s->cmd;
        break;
    default:
        hw_error("max17040: bad write offset 0x%x\n", cmd);
    }

    if (len > 2 - shift)
        hw_error("max17040: bad write length %d\n", len);

    for (i = 0; i < len; i++)
        *((uint8_t *)reg + i + shift) = buf[i];
}

static uint8_t max17040_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    MAX17040State *s = (MAX17040State *)dev;
    int shift = 0;
    uint16_t val;

    if (n > 0)
        hw_error("max17040: bad read length %d\n", n);

    switch (cmd) {
    case MAX17040_VCELL_MSB:
        shift = 1;
    case MAX17040_VCELL_LSB:
        val = s->vcell;
        break;
    case MAX17040_SOC_MSB:
        shift = 1;
    case MAX17040_SOC_LSB:
        val = s->charge;
        break;
    case MAX17040_MODE_MSB:
        shift = 1;
    case MAX17040_MODE_LSB:
        val = s->mode;
        break;
    case MAX17040_VER_MSB:
        shift = 1;
    case MAX17040_VER_LSB:
        val = s->ver;
        break;
    case MAX17040_RCOMP_MSB:
        shift = 1;
    case MAX17040_RCOMP_LSB:
        val = s->rcomp;
        break;
    case MAX17040_CMD_MSB:
        shift = 1;
    case MAX17040_CMD_LSB:
        val = s->cmd;
        break;
    default:
        hw_error("max17040: bad read offset 0x%x\n", cmd);
    }

    return ((val >> (shift * 8)) & 0xFF);
}

DeviceState *max17040_init(i2c_bus *bus, int addr)
{
    DeviceState *dev = qdev_create((BusState *)bus, "max17040");
    qdev_init_nofail(dev);
    i2c_set_slave_address((i2c_slave *)dev, addr);
    return dev;
}

static int max17040_init1(SMBusDevice *dev)
{
    MAX17040State *s = (MAX17040State *) dev;
    max17040_reset(s);
    return 0;
}

static SMBusDeviceInfo max17040_info = {
    .i2c.qdev.name = "max17040",
    .i2c.qdev.size = sizeof(MAX17040State),
    .init = max17040_init1,
    .write_data = max17040_write_data,
    .read_data = max17040_read_data
};


/* Maxim MAX8998 Battery Charger */

#define MAX8998_REG_IRQ1                   0
#define MAX8998_REG_IRQ2                   1
#define MAX8998_REG_IRQ3                   2
#define MAX8998_REG_IRQ4                   3
#define MAX8998_REG_IRQM1                  4
#define MAX8998_REG_IRQM2                  5
#define MAX8998_REG_IRQM3                  6
#define MAX8998_REG_IRQM4                  7
#define MAX8998_REG_STATUS1                8
#define MAX8998_REG_STATUS2                9
#define MAX8998_REG_STATUSM1               10
#define MAX8998_REG_STATUSM2               11
#define MAX8998_REG_CHGR1                  12
#define MAX8998_REG_CHGR2                  13
#define MAX8998_REG_LDO_ACTIVE_DISCHARGE1  14
#define MAX8998_REG_LDO_ACTIVE_DISCHARGE2  15
#define MAX8998_REG_BUCK_ACTIVE_DISCHARGE3 16
#define MAX8998_REG_ONOFF1                 17
#define MAX8998_REG_ONOFF2                 18
#define MAX8998_REG_ONOFF3                 19
#define MAX8998_REG_ONOFF4                 20
#define MAX8998_REG_BUCK1_DVSARM1          21
#define MAX8998_REG_BUCK1_DVSARM2          22
#define MAX8998_REG_BUCK1_DVSARM3          23
#define MAX8998_REG_BUCK1_DVSARM4          24
#define MAX8998_REG_BUCK2_DVSINT1          25
#define MAX8998_REG_BUCK2_DVSINT2          26
#define MAX8998_REG_BUCK3                  27
#define MAX8998_REG_BUCK4                  28
#define MAX8998_REG_LDO2_LDO3              29
#define MAX8998_REG_LDO4                   30
#define MAX8998_REG_LDO5                   31
#define MAX8998_REG_LDO6                   32
#define MAX8998_REG_LDO7                   33
#define MAX8998_REG_LDO8_LDO9              34
#define MAX8998_REG_LDO10_LDO11            35
#define MAX8998_REG_LDO12                  36
#define MAX8998_REG_LDO13                  37
#define MAX8998_REG_LDO14                  38
#define MAX8998_REG_LDO15                  39
#define MAX8998_REG_LDO16                  40
#define MAX8998_REG_LDO17                  41
#define MAX8998_REG_BKCHR                  42
#define MAX8998_REG_LBCNFG1                43
#define MAX8998_REG_LBCNFG2                44


typedef struct MAX8998State {
    SMBusDevice smbusdev;

    uint32_t irq;
    uint32_t irqm;
    uint16_t status;
    uint16_t statusm;
    uint16_t chgr;
    uint16_t ldo_active_discharge;
    uint8_t  buck_active_discharge;
    uint32_t onoff;
    uint32_t buck1_dvsarm;
    uint16_t buck2_dvsint;
    uint8_t  buck3;
    uint8_t  buck4;
    uint32_t ldo23_4_5_6;
    uint32_t ldo7_89_1011_12;
    uint32_t ldo13_14_15_16;
    uint8_t  ldo17;
    uint8_t  bkchr;
    uint16_t lbcnfg;
} MAX8998State;

static void max8998_reset(MAX8998State *s)
{
    s->irq             = 0;
    s->irqm            = 0;
    s->status          = 0;
    s->statusm         = 0xEFFF;
    s->chgr            = 0xA80;
    s->ldo_active_discharge = 0;
    s->buck_active_discharge = 0;
    s->onoff           = 0;
    s->buck1_dvsarm    = 0;
    s->buck2_dvsint    = 0;
    s->buck3           = 0;
    s->buck4           = 0;
    s->ldo23_4_5_6     = 0;
    s->ldo7_89_1011_12 = 0;
    s->ldo13_14_15_16  = 0;
    s->ldo17           = 0;
    s->bkchr           = 0;
    s->lbcnfg          = 0;
}

static uint8_t *max8998_get_reg_addr(MAX8998State *s, uint8_t cmd)
{
    int shift = 0;
    uint8_t *reg = NULL;

    switch (cmd) {
    case MAX8998_REG_IRQ4:
    case MAX8998_REG_IRQM4:
    case MAX8998_REG_STATUS2:
    case MAX8998_REG_STATUSM2:
    case MAX8998_REG_CHGR2:
    case MAX8998_REG_LDO_ACTIVE_DISCHARGE2:
    case MAX8998_REG_BUCK_ACTIVE_DISCHARGE3:
    case MAX8998_REG_ONOFF4:
    case MAX8998_REG_BUCK1_DVSARM4:
    case MAX8998_REG_BUCK2_DVSINT2:
    case MAX8998_REG_BUCK3:
    case MAX8998_REG_BUCK4:
    case MAX8998_REG_LDO6:
    case MAX8998_REG_LDO12:
    case MAX8998_REG_LDO16:
    case MAX8998_REG_LDO17:
    case MAX8998_REG_BKCHR:
    case MAX8998_REG_LBCNFG2:
        break;
    case MAX8998_REG_IRQ3:
    case MAX8998_REG_IRQM3:
    case MAX8998_REG_STATUS1:
    case MAX8998_REG_STATUSM1:
    case MAX8998_REG_CHGR1:
    case MAX8998_REG_LDO_ACTIVE_DISCHARGE1:
    case MAX8998_REG_ONOFF3:
    case MAX8998_REG_BUCK1_DVSARM3:
    case MAX8998_REG_BUCK2_DVSINT1:
    case MAX8998_REG_LDO5:
    case MAX8998_REG_LDO10_LDO11:
    case MAX8998_REG_LDO15:
    case MAX8998_REG_LBCNFG1:
        shift = 1;
        break;
    case MAX8998_REG_IRQ2:
    case MAX8998_REG_IRQM2:
    case MAX8998_REG_ONOFF2:
    case MAX8998_REG_BUCK1_DVSARM2:
    case MAX8998_REG_LDO4:
    case MAX8998_REG_LDO8_LDO9:
    case MAX8998_REG_LDO14:
        shift = 2;
        break;
    case MAX8998_REG_IRQ1:
    case MAX8998_REG_IRQM1:
    case MAX8998_REG_ONOFF1:
    case MAX8998_REG_BUCK1_DVSARM1:
    case MAX8998_REG_LDO2_LDO3:
    case MAX8998_REG_LDO7:
    case MAX8998_REG_LDO13:
        shift = 3;
        break;
    default:
        hw_error("max8998: bad write offset 0x%x\n", cmd);
    }

    switch (cmd) {
    case MAX8998_REG_IRQ1:
    case MAX8998_REG_IRQ2:
    case MAX8998_REG_IRQ3:
    case MAX8998_REG_IRQ4:
        reg = (uint8_t *)&s->irq;
        break;
    case MAX8998_REG_IRQM1:
    case MAX8998_REG_IRQM2:
    case MAX8998_REG_IRQM3:
    case MAX8998_REG_IRQM4:
        reg = (uint8_t *)&s->irqm;
        break;
    case MAX8998_REG_STATUS1:
    case MAX8998_REG_STATUS2:
        reg = (uint8_t *)&s->status;
        break;
    case MAX8998_REG_STATUSM1:
    case MAX8998_REG_STATUSM2:
        reg = (uint8_t *)&s->statusm;
        break;
    case MAX8998_REG_CHGR1:
    case MAX8998_REG_CHGR2:
        reg = (uint8_t *)&s->chgr;
        break;
    case MAX8998_REG_LDO_ACTIVE_DISCHARGE1:
    case MAX8998_REG_LDO_ACTIVE_DISCHARGE2:
        reg = (uint8_t *)&s->ldo_active_discharge;
        break;
    case MAX8998_REG_BUCK_ACTIVE_DISCHARGE3:
        reg = (uint8_t *)&s->buck_active_discharge;
        break;
    case MAX8998_REG_ONOFF1:
    case MAX8998_REG_ONOFF2:
    case MAX8998_REG_ONOFF3:
    case MAX8998_REG_ONOFF4:
        reg = (uint8_t *)&s->onoff;
        break;
    case MAX8998_REG_BUCK1_DVSARM1:
    case MAX8998_REG_BUCK1_DVSARM2:
    case MAX8998_REG_BUCK1_DVSARM3:
    case MAX8998_REG_BUCK1_DVSARM4:
        reg = (uint8_t *)&s->buck1_dvsarm;
        break;
    case MAX8998_REG_BUCK2_DVSINT1:
    case MAX8998_REG_BUCK2_DVSINT2:
        reg = (uint8_t *)&s->buck2_dvsint;
        break;
    case MAX8998_REG_BUCK3:
        reg = (uint8_t *)&s->buck3;
        break;
    case MAX8998_REG_BUCK4:
        reg = (uint8_t *)&s->buck4;
        break;
    case MAX8998_REG_LDO2_LDO3:
    case MAX8998_REG_LDO4:
    case MAX8998_REG_LDO5:
    case MAX8998_REG_LDO6:
        reg = (uint8_t *)&s->ldo23_4_5_6;
        break;
    case MAX8998_REG_LDO7:
    case MAX8998_REG_LDO8_LDO9:
    case MAX8998_REG_LDO10_LDO11:
    case MAX8998_REG_LDO12:
        reg = (uint8_t *)&s->ldo7_89_1011_12;
        break;
    case MAX8998_REG_LDO13:
    case MAX8998_REG_LDO14:
    case MAX8998_REG_LDO15:
    case MAX8998_REG_LDO16:
        reg = (uint8_t *)&s->ldo13_14_15_16;
        break;
    case MAX8998_REG_LDO17:
        reg = (uint8_t *)&s->ldo17;
        break;
    case MAX8998_REG_BKCHR:
        reg = (uint8_t *)&s->bkchr;
        break;
    case MAX8998_REG_LBCNFG1:
    case MAX8998_REG_LBCNFG2:
        reg = (uint8_t *)&s->lbcnfg;
        break;
    default:
        hw_error("max8998: bad write offset 0x%x\n", cmd);
    }

    return (reg + shift);
}

static void max8998_write_data(SMBusDevice *dev, uint8_t cmd,
                               uint8_t *buf, int len)
{
    MAX8998State *s = (MAX8998State *)dev;

    if (len > 1)
        hw_error("max8998: bad write length %d\n", len);

    *(max8998_get_reg_addr(s, cmd)) = buf[0];
}

static uint8_t max8998_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    MAX8998State *s = (MAX8998State *)dev;

    if (n > 0 && cmd != MAX8998_REG_IRQ1)
        hw_error("max8998: bad read length %d\n", n);

    return *(max8998_get_reg_addr(s, cmd) + n);
}

DeviceState *max8998_init(i2c_bus *bus, int addr)
{
    DeviceState *dev = qdev_create((BusState *)bus, "max8998");
    qdev_init_nofail(dev);
    i2c_set_slave_address((i2c_slave *)dev, addr);
    return dev;
}

static int max8998_init1(SMBusDevice *dev)
{
    MAX8998State *s = (MAX8998State *) dev;
    max8998_reset(s);
    return 0;
}

static SMBusDeviceInfo max8998_info = {
    .i2c.qdev.name = "max8998",
    .i2c.qdev.size = sizeof(MAX8998State),
    .init = max8998_init1,
    .write_data = max8998_write_data,
    .read_data = max8998_read_data
};


/* Maxim MAX8998 Real Time Clock */

/* MX8998 RTC I2C MAP */
#define RTC_SEC         0x0         /* second 00-59 */
#define RTC_MIN         0x1         /* minute 00-59 */
#define RTC_HR          0x2         /* hour AM/PM 1-12 or 00-23 */
#define RTC_DAY         0x3         /* weekday 1-7 */
#define RTC_DATE        0x4         /* date 01-31 */
#define RTC_MT          0x5         /* month 01-12 */
#define RTC_YEAR        0x6         /* year 00-99 */
#define RTC_CEN         0x7         /* century 00-99 */

#define RTC_CON         0x1A
    #define RTC_EN          0x01    /* RTC control enable */

#define MAX8998_REG(x, y)   (0x8 * y + x)

/* Time Keeper */
#define MAX8998_RTC         0
/* Alarm0 */
#define MAX8998_ALRM0       1
/* Alarm1 */
#define MAX8998_ALRM1       2
/* Conf */
#define MAX8998_ALRM0_CONF  0x18
#define MAX8998_ALRM1_CONF  0x19

#define MAX8998_ALRM_ON     0x77
#define MAX8998_ALRM_OFF    0x0


typedef struct MAX8998RTCState {
    SMBusDevice smbusdev;

    uint16_t  regs[RTC_CON + 1];

    /* seconds update */
    QEMUTimer *seconds_timer;
    struct tm current_tm;
} MAX8998RTCState;

static void max8998_rtc_seconds_update(void *opaque);


/* Set default values for all fields */
static void max8998_rtc_reset(MAX8998RTCState *s)
{
    short i;

    /* stop seconds timer */
    s->regs[RTC_CON] = 0x0;
    max8998_rtc_seconds_update(s);

    for (i = RTC_SEC; i <= RTC_CEN; i++)
        s->regs[MAX8998_REG(i, MAX8998_RTC)] = 0;
}

/* Get days in month. Month is between 0 and 11 */
static int get_days_in_month(int month, int year)
{
    short d;
    static const int days_tab[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if ((unsigned)month >= 12)
        return 31;
    d = days_tab[month];
    if (month == 1) {
        if ((year % 4) == 0)
            d++;
    }
    return d;
}

/* Update 'struct tm' to the next second */
static void max8998_rtc_next_second(struct tm *tm)
{
    int days_in_month;

    tm->tm_sec++;
    if ((unsigned)tm->tm_sec >= 60) {
        tm->tm_sec = 0;
        tm->tm_min++;
        if ((unsigned)tm->tm_min >= 60) {
            tm->tm_min = 0;
            tm->tm_hour++;
            if ((unsigned)tm->tm_hour >= 24) {
                tm->tm_hour = 0;
                /* next day */
                tm->tm_wday++;
                if ((unsigned)tm->tm_wday >= 8)
                    tm->tm_wday = 1;
                days_in_month = get_days_in_month(tm->tm_mon, tm->tm_year);
                tm->tm_mday++;
                if (tm->tm_mday < 1) {
                    tm->tm_mday = 1;
                } else if (tm->tm_mday > days_in_month) {
                    tm->tm_mday = 1;
                    tm->tm_mon++;
                    if (tm->tm_mon >= 12) {
                        tm->tm_mon = 0;
                        tm->tm_year++;
                    }
                }
            }
        }
    }
}

/* Using of qemu_timer to increase seconds */
static void max8998_rtc_seconds_update(void *opaque)
{
    MAX8998RTCState *s = (MAX8998RTCState *)opaque;
    uint64_t next_seconds_time;

    if (s->regs[RTC_CON] & RTC_EN) {
        max8998_rtc_next_second(&(s->current_tm));
        next_seconds_time = qemu_get_clock(vm_clock) + get_ticks_per_sec();
        qemu_mod_timer(s->seconds_timer, next_seconds_time);
    } else {
        qemu_del_timer(s->seconds_timer);
    }
}

/* Convert time from bcd */
static void max8998_rtc_set_time(MAX8998RTCState *s)
{
    struct tm *tm = &(s->current_tm);

    tm->tm_sec  = from_bcd(s->regs[RTC_SEC]);
    tm->tm_min  = from_bcd(s->regs[RTC_MIN]);
    tm->tm_hour = from_bcd(s->regs[RTC_HR] & 0x3f);
    tm->tm_mday = from_bcd(s->regs[RTC_DATE]);
    tm->tm_mon  = from_bcd(s->regs[RTC_MT]);
    tm->tm_year = from_bcd(s->regs[RTC_YEAR]) +
                  from_bcd(s->regs[RTC_CEN]) * 100 - 1900;
    tm->tm_wday = s->regs[RTC_DAY]; /* one decimal digit */
}

/* Convert time to bcd */
static void max8998_rtc_read_time(MAX8998RTCState *s)
{
    struct tm *tm = &(s->current_tm);

    s->regs[RTC_SEC]  = to_bcd(tm->tm_sec);
    s->regs[RTC_MIN]  = to_bcd(tm->tm_min);
    s->regs[RTC_HR]   = to_bcd(tm->tm_hour);
    s->regs[RTC_DATE] = to_bcd(tm->tm_mday);
    s->regs[RTC_MT]   = to_bcd(tm->tm_mon);
    s->regs[RTC_YEAR] = to_bcd(tm->tm_year % 100);
    s->regs[RTC_CEN]  = to_bcd((tm->tm_year + 1900) / 100);
    s->regs[RTC_DAY]  = tm->tm_wday; /* one decimal digit */
}

/* Write RTC MAX8998 by I2C through SMBus */
static void max8998_rtc_write(SMBusDevice *dev, uint8_t cmd,
                              uint8_t *buf, int len)
{
    MAX8998RTCState *s = (MAX8998RTCState *)dev;
    uint16_t *reg = NULL;
    int i;

    switch (cmd) {
    case MAX8998_REG(RTC_SEC, MAX8998_RTC) ...
         MAX8998_REG(RTC_CEN, MAX8998_RTC):
    case MAX8998_ALRM0_CONF:
    case MAX8998_ALRM1_CONF:
        reg = &s->regs[cmd];
        break;

    /* TODO: Implement alarm support */
    case MAX8998_REG(RTC_SEC, MAX8998_ALRM0) ...
         MAX8998_REG(RTC_CEN, MAX8998_ALRM1):
        return;

    default:
        hw_error("max8998-rtc: bad write offset 0x%x\n", cmd);
    }

    for (i = 0; i < len; i++)
        *((uint8_t *)reg + i) = buf[i];

    if (cmd <= MAX8998_REG(RTC_CEN, MAX8998_RTC))
        max8998_rtc_set_time(s);
}

/* Read RTC MAX8998 by I2C through SMBus */
static uint8_t max8998_rtc_read(SMBusDevice *dev, uint8_t cmd, int n)
{
    MAX8998RTCState *s = (MAX8998RTCState *)dev;
    uint16_t val;

    if (n > 0)
        hw_error("max8998-rtc: bad read length %d\n", n);

    switch (cmd) {
    case MAX8998_REG(RTC_SEC, MAX8998_RTC) ...
         MAX8998_REG(RTC_CEN, MAX8998_RTC):
        max8998_rtc_read_time(s);
    case MAX8998_ALRM0_CONF:
    case MAX8998_ALRM1_CONF:
        val = s->regs[cmd];
        break;

    /* TODO: Implement alarm support */
    case MAX8998_REG(RTC_SEC, MAX8998_ALRM0) ...
         MAX8998_REG(RTC_CEN, MAX8998_ALRM1):
        return 0;

    default:
        hw_error("max8998-rtc: bad read offset 0x%x\n", cmd);
    }
    return (val & 0xFF);
}

DeviceState *max8998_rtc_init(i2c_bus *bus, int addr)
{
    DeviceState *dev = qdev_create((BusState *)bus, "max8998-rtc");
    qdev_init_nofail(dev);
    i2c_set_slave_address((i2c_slave *)dev, addr);
    return dev;
}

static int max8998_rtc_init1(SMBusDevice *dev)
{
    MAX8998RTCState *s = (MAX8998RTCState *) dev;

    s->seconds_timer =
            qemu_new_timer(vm_clock, max8998_rtc_seconds_update, s);

    /* initialize values */
    max8998_rtc_reset(s);

    /* get time from host */
    qemu_get_timedate(&s->current_tm, 0);

    /* start seconds timer */
    s->regs[RTC_CON] |= RTC_EN;
    max8998_rtc_seconds_update(s);

    return 0;
}

static SMBusDeviceInfo max8998_rtc_info = {
    .i2c.qdev.name = "max8998-rtc",
    .i2c.qdev.size = sizeof(MAX8998RTCState),
    .init          = max8998_rtc_init1,
    .write_data    = max8998_rtc_write,
    .read_data     = max8998_rtc_read
};

static void s5pc1xx_pmu_register_devices(void)
{
    sysbus_register_dev("s5pc1xx,pmu", sizeof(S5pc1xxPMUState), s5pc1xx_pmu_init);
    smbus_register_device(&max17040_info);
    smbus_register_device(&max8998_info);
    smbus_register_device(&max8998_rtc_info);
}

device_init(s5pc1xx_pmu_register_devices)
