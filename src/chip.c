#include "private.h"
#include "dmi2c_types.h"
#include <errno.h>

/** @brief Open the reported dmi2c friend only on first use. */
int codec_connect(dmdrvi_context_t c)
{
    if (c->bus) return 0;
    if (!c->bus_path)
    {
        DMOD_LOG_ERROR("dmwm8994: I2C bus friend is not ready\n");
        return -ENODEV;
    }
    c->bus = Dmod_FileOpen(c->bus_path, "r+");
    if (!c->bus)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot open I2C bus %s\n", c->bus_path);
        return -ENODEV;
    }
    return 0;
}

/** @brief Close transport after a bus change or node removal. */
void codec_disconnect(dmdrvi_context_t c)
{
    if (c->bus) Dmod_FileClose(c->bus);
    c->bus = NULL;
    c->info.configured = false;
}

/** @brief Read a big-endian 16-bit codec register with repeated START. */
int codec_read_reg(dmdrvi_context_t c, uint16_t reg, uint16_t *value)
{
    int ret = codec_connect(c);
    if (ret) return ret;
    uint8_t address[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    uint8_t data[2] = { 0, 0 };
    dmi2c_message_t messages[2] = {
        { c->address, false, address, sizeof(address) },
        { c->address, true, data, sizeof(data) },
    };
    dmi2c_transfer_t transfer = { messages, 2 };
    ret = Dmod_Ioctl(c->bus, dmi2c_ioctl_cmd_transfer, &transfer);
    if (ret)
    {
        DMOD_LOG_ERROR("dmwm8994: register 0x%04X read failed (%d)\n", reg, ret);
    }
    if (!ret) *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ret;
}

/** @brief Write a big-endian 16-bit codec register. */
static int write_reg(dmdrvi_context_t c, uint16_t reg, uint16_t value)
{
    int ret = codec_connect(c);
    if (ret) return ret;
    uint8_t data[4] = {
        (uint8_t)(reg >> 8), (uint8_t)reg,
        (uint8_t)(value >> 8), (uint8_t)value,
    };
    dmi2c_message_t message = { c->address, false, data, sizeof(data) };
    dmi2c_transfer_t transfer = { &message, 1 };
    ret = Dmod_Ioctl(c->bus, dmi2c_ioctl_cmd_transfer, &transfer);
    if (ret)
    {
        DMOD_LOG_ERROR("dmwm8994: register 0x%04X write failed (%d)\n", reg, ret);
    }
    return ret;
}

/** @brief Apply an ordered register sequence, stopping at the first error. */
static int write_sequence(dmdrvi_context_t c, const uint16_t (*sequence)[2], size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        int ret = write_reg(c, sequence[i][0], sequence[i][1]);
        if (ret) return ret;
    }
    return 0;
}

/** @brief Set both headphone gains with the volume update bit. */
int codec_set_volume(dmdrvi_context_t c, uint8_t volume)
{
    if (volume > 100) return -EINVAL;
    uint16_t gain_code = (uint16_t)(((uint16_t)volume * 63U + 50U) / 100U);
    uint16_t value = (uint16_t)(0x0140U | gain_code);
    int ret = write_reg(c, 0x001C, value);
    if (!ret) ret = write_reg(c, 0x001D, value);
    if (!ret) c->info.config.volume_percent = volume;
    return ret;
}

/** @brief Toggle digital mute of AIF1 DAC1. */
int codec_set_mute(dmdrvi_context_t c, bool mute)
{
    int ret = write_reg(c, 0x0420, mute ? 0x0210U : 0x0010U);
    uint16_t actual = 0;
    if (!ret) ret = codec_read_reg(c, 0x0420, &actual);
    if (!ret && ((actual & 0x0200U) != 0U) != mute) ret = -EIO;
    if (!ret) {
        c->info.config.muted = mute;
    }
    return ret;
}

/** @brief Apply ST's WM8994 errata workaround and raise VMID. */
static int start_bias(dmdrvi_context_t c)
{
    static const uint16_t sequence[][2] = {
        { 0x0102, 0x0003 }, { 0x0817, 0x0000 }, { 0x0102, 0x0000 },
        { 0x0039, 0x006C }, { 0x0001, 0x0003 },
    };
    int ret = write_sequence(c, sequence, sizeof(sequence) / sizeof(sequence[0]));
    if (!ret) dmosi_thread_sleep(50);
    return ret;
}

/** @brief Route AIF1 timeslot zero to DAC1 and the headphone mixers. */
static int start_digital(dmdrvi_context_t c)
{
    static const uint16_t sequence[][2] = {
        { 0x0005, 0x0303 }, { 0x0601, 0x0001 }, { 0x0602, 0x0001 },
        { 0x0604, 0x0000 }, { 0x0605, 0x0000 },
        { 0x0300, 0x0000 }, { 0x0302, 0x0000 },
        { 0x0208, 0x000A }, { 0x0200, 0x0001 },
        { 0x0402, 0x00C0 }, { 0x0403, 0x00C0 }, { 0x0420, 0x0210 },
        { 0x0610, 0x00C0 }, { 0x0611, 0x00C0 },
        { 0x002D, 0x0100 }, { 0x002E, 0x0100 },
    };
    return write_sequence(c, sequence, sizeof(sequence) / sizeof(sequence[0]));
}

/** @brief Map a PCM rate to its documented AIF1 256fs register code. */
static int rate_code(uint32_t hz)
{
    switch (hz) {
        case 8000: return 0x0003;
        case 11025: return 0x0013;
        case 16000: return 0x0033;
        case 22050: return 0x0043;
        case 32000: return 0x0063;
        case 44100: return 0x0073;
        case 48000: return 0x0083;
        case 96000: return 0x00A3;
        default: return -ENOTSUP;
    }
}

/** @brief Probe, reset and start the headphone anti-pop sequence. */
int codec_configure(dmdrvi_context_t c, const dmdrvi_audio_config_t *config)
{
    if (config->output != DMDRVI_AUDIO_OUTPUT_HEADPHONE ||
        config->channels != 2 || config->sample_bits != 16)
        return -ENOTSUP;
    if (config->volume_percent > 100) return -EINVAL;
    int code = rate_code(config->sample_rate_hz);
    if (code < 0) return code;
    uint16_t id = 0;
    int ret = codec_read_reg(c, 0x0000, &id);
    if (ret) return ret;
    if (id != DMWM8994_CHIP_ID)
    {
        DMOD_LOG_ERROR("dmwm8994: unexpected chip ID 0x%04X\n", id);
        return -ENODEV;
    }
    c->info.configured = false;
    ret = write_reg(c, 0x0000, 0x0000);
    if (!ret) ret = start_bias(c);
    if (!ret) ret = start_digital(c);
    if (!ret) ret = write_reg(c, 0x0210, (uint16_t)code);
    if (!ret) ret = write_reg(c, 0x0110, 0x8100);
    if (!ret) dmosi_thread_sleep(325);
    if (!ret) ret = codec_set_volume(c, config->volume_percent);
    if (!ret) ret = codec_set_mute(c, config->muted);
    if (ret) return ret;
    c->info.hardware_id = id;
    c->expected_rate_register = (uint16_t)code;
    c->info.config = *config;
    c->info.configured = true;
    return 0;
}
