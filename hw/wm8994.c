/*
 * WM8994 Audio Codec
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Alexey Merkulov <steelart@ispras.ru>
 *                Dmitry Zhurikhin <zhur@ispras.ru>
 *                Vladimir Monakhov <vmonakhov@ispras.ru>
 */

#include "i2c-addressable.h"
#include "audio/audio.h"
#include "wm8994_reg.h"


#define CODEC       "wm8994"


typedef struct WM8994State {

    I2CAddressableState i2c_addressable;

    uint16_t registers[WM8994_REGISTER_MEM_SIZE];

    void (*data_req)(void *, int);
    void *opaque;

    SWVoiceOut *dac_voice;
    QEMUSoundCard card;

    uint8_t data_out[4 * 4096]; /* magic */

    int idx_out, req_out;
} WM8994State;


static inline uint8_t wm8994_volume(WM8994State *s, uint16_t reg)
{
    return s->registers[reg] & 0x3F;
}

static inline uint8_t wm8994_mute(WM8994State *s, uint16_t reg)
{
    return (s->registers[reg] >> 6) & 1;
}

static inline uint8_t wm8994_outvol_transform(WM8994State *s, uint16_t reg)
{
    return 0xFF * wm8994_volume(s, reg) / 0x3F;
}

static inline int wm8994_rate(WM8994State *s, uint16_t reg)
{
    switch ((s->registers[reg] >> 4) & 0xF) {
    case  0: return  8000;
    case  1: return 11025;
    case  2: return 12000;
    case  3: return 16000;
    case  4: return 22050;
    case  5: return 24000;
    case  6: return 32000;
    case  7: return 44100;
    case  8: return 48000;
    case  9: return 88200;
    case 10: return 96000;

    default:
        return 0;
    }
}

static inline audfmt_e wm8994_format(WM8994State *s, uint16_t reg)
{
    switch ((s->registers[reg] >> 5) & 0x3) {
    case 0: return AUD_FMT_S16;
    case 3: return AUD_FMT_S32;
    case 1:
        /*TODO: implement conversion */
        hw_error("wm8994: unsupported format (20 bits for channel)\n");
        return AUD_FMT_S16;
    case 2:
        /*TODO: implement conversion */
        hw_error("wm8994: unsupported format (24 bits for channel)\n");
        return AUD_FMT_S16;
    default:
        hw_error("wm8994: unknown format\n");
        return AUD_FMT_S16;
    }
}

static inline void wm8994_out_flush(WM8994State *s)
{
    int sent = 0;

    if (!s->dac_voice)
        return;
    while (sent < s->idx_out) {
        sent +=
            AUD_write(s->dac_voice, s->data_out + sent, s->idx_out - sent) ?:
            s->idx_out;
    }
    s->idx_out = 0;
}

static void wm8994_audio_out_cb(void *opaque, int free_b)
{
    WM8994State *s = (WM8994State *) opaque;

    if (s->idx_out >= free_b) {
        s->idx_out = free_b;
        s->req_out = 0;
        wm8994_out_flush(s);
    } else {
        s->req_out = free_b - s->idx_out;
    }

    if (s->data_req) {
        s->data_req(s->opaque, s->req_out >> 2);
    }
}

static void wm8994_vol_update(WM8994State *s)
{
    int volume_left  = wm8994_volume(s, WM8994_SPEAKER_VOLUME_LEFT);
    int volume_right = wm8994_volume(s, WM8994_SPEAKER_VOLUME_RIGHT);

    /* Speaker */
    AUD_set_volume_out(s->dac_voice, 1, volume_left, volume_right);
}

static void wm8994_set_format(WM8994State *s)
{
    struct audsettings out_fmt;

    wm8994_out_flush(s);

    if (s->dac_voice) {
        AUD_set_active_out(s->dac_voice, 0);
    }

    if (s->dac_voice) {
        AUD_close_out(&s->card, s->dac_voice);
        s->dac_voice = NULL;
    }

    /* Setup output */
    out_fmt.endianness = 0;
    out_fmt.nchannels = 2;
    out_fmt.freq = wm8994_rate(s, WM8994_AIF1_RATE);
    out_fmt.fmt = wm8994_format(s, WM8994_AIF1_CONTROL_1);

    s->dac_voice =
        AUD_open_out(&s->card, s->dac_voice, CODEC ".speaker",
                     s, wm8994_audio_out_cb, &out_fmt);
    wm8994_vol_update(s);

    if (s->dac_voice) {
        AUD_set_active_out(s->dac_voice, 1);
    }
}

static void wm8994_reset(WM8994State *s)
{
    memset(s->registers, 0, sizeof(s->registers));
    s->registers[WM8994_SOFTWARE_RESET] = 0x8994;

    s->registers[WM8994_POWER_MANAGEMENT_2] = 0x6000;

    s->registers[WM8994_SPEAKER_VOLUME_LEFT]  = 0x79;
    s->registers[WM8994_SPEAKER_VOLUME_RIGHT] = 0x79;

    s->registers[WM8994_AIF1_RATE] = 0x73;

    s->idx_out = 0;
    s->req_out = 0;
    s->opaque = NULL;
    s->data_req = NULL;
    s->dac_voice = NULL;

    wm8994_set_format(s);
}

static uint8_t wm8994_read(void *opaque, uint32_t address, uint8_t offset)
{
    WM8994State *s = (WM8994State *)opaque;

    if (offset >= 2) {
        /* FIXME: there should be an error but kernel wants to read more
         * than allowed; so just pretend there's nothing here */
        /* hw_error("wm8994: too much data requested"); */
        return 0;
    }
    if (address < WM8994_REGISTER_MEM_SIZE) {
        return (s->registers[address] >> ((1 - offset) * 8)) & 0xFF;
    } else {
        hw_error("wm8994: illegal read offset 0x%x\n", address + offset);
    }
}

static void wm8994_write(void *opaque, uint32_t address, uint8_t offset,
                         uint8_t val)
{
    WM8994State *s = (WM8994State *)opaque;

    address += offset / 2;
    offset %= 2;

    if (address < WM8994_REGISTER_MEM_SIZE) {
        s->registers[address] &= ~(0xFF << ((1 - offset) * 8));
        s->registers[address] |= val << ((1 - offset) * 8);

        if (offset == 1) {
            switch (address) {
            case WM8994_SOFTWARE_RESET:
                wm8994_reset(s);
                break;

            case WM8994_SPEAKER_VOLUME_LEFT:
            case WM8994_SPEAKER_VOLUME_RIGHT:
                wm8994_vol_update(s);
                break;

            case WM8994_AIF1_RATE:
            case WM8994_AIF1_CONTROL_1:
                wm8994_set_format(s);
                break;

            default:
                break;
            }
        }
    } else {
        hw_error("wm8994: illegal write offset 0x%x\n", address + offset);
    }
}

void wm8994_data_req_set(DeviceState *dev, void (*data_req)(void *, int),
                         void *opaque)
{
    WM8994State *s =
        FROM_I2CADDR_SLAVE(WM8994State, I2CADDR_SLAVE_FROM_QDEV(dev));

    s->data_req = data_req;
    s->opaque = opaque;
}

void *wm8994_dac_buffer(DeviceState *dev, int samples)
{
    WM8994State *s =
        FROM_I2CADDR_SLAVE(WM8994State, I2CADDR_SLAVE_FROM_QDEV(dev));

    /* XXX: Should check if there are <i>samples</i> free samples available */
    void *ret = s->data_out + s->idx_out;

    s->idx_out += samples << 2;
    s->req_out -= samples << 2;
    return ret;
}

void wm8994_dac_dat(DeviceState *dev, uint32_t sample)
{
    WM8994State *s =
        FROM_I2CADDR_SLAVE(WM8994State, I2CADDR_SLAVE_FROM_QDEV(dev));

    *(uint32_t *) &s->data_out[s->idx_out] = sample;
    s->req_out -= 4;
    s->idx_out += 4;
    if (s->idx_out >= sizeof(s->data_out) || s->req_out <= 0) {
        wm8994_out_flush(s);
    }
}

void wm8994_dac_commit(DeviceState *dev)
{
    WM8994State *s =
        FROM_I2CADDR_SLAVE(WM8994State, I2CADDR_SLAVE_FROM_QDEV(dev));

    return wm8994_out_flush(s);
}

static int wm8994_init(I2CAddressableState *i2c)
{
    WM8994State *s = FROM_I2CADDR_SLAVE(WM8994State, i2c);

    AUD_register_card(CODEC, &s->card);

    wm8994_reset(s);

    return 0;
}

static I2CAddressableDeviceInfo wm8994_info = {
    .i2c.qdev.name  = "wm8994",
    .i2c.qdev.size  = sizeof(WM8994State),
    .init = wm8994_init,
    .read = wm8994_read,
    .write = wm8994_write,
    .size = 2,
    .rev = 1
};

static void wm8994_register_devices(void)
{
    i2c_addressable_register_device(&wm8994_info);
}

device_init(wm8994_register_devices)
