#include "private.h"
#include "registers.h"
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
    uint16_t gain_code = (uint16_t)(((uint16_t)volume * WM_HEADPHONE_GAIN_MAX + 50U) / 100U);
    uint16_t value = (uint16_t)(WM_HEADPHONE_UPDATE | WM_HEADPHONE_UNMUTE | gain_code);
    int ret = write_reg(c, WM_REG_HEADPHONE_LEFT, value);
    if (!ret) ret = write_reg(c, WM_REG_HEADPHONE_RIGHT, value);
    if (!ret) c->info.config.volume_percent = volume;
    return ret;
}

/** @brief Toggle digital mute of AIF1 DAC1. */
int codec_set_mute(dmdrvi_context_t c, bool mute)
{
    int ret = write_reg(c, WM_REG_DAC1_FILTER,
                        (mute ? WM_DAC1_MUTE : 0U) | WM_DAC1_FILTER_ENABLE);
    uint16_t actual = 0;
    if (!ret) ret = codec_read_reg(c, WM_REG_DAC1_FILTER, &actual);
    if (!ret && ((actual & WM_DAC1_MUTE) != 0U) != mute) ret = -EIO;
    if (!ret) {
        c->info.config.muted = mute;
    }
    return ret;
}

/** @brief Apply ST's WM8994 errata workaround and raise VMID. */
static int start_bias(dmdrvi_context_t c)
{
    static const uint16_t sequence[][2] = {
        { WM_REG_STARTUP_WORKAROUND_1, WM_STARTUP_WORKAROUND_ON },
        { WM_REG_STARTUP_WORKAROUND_2, 0x0000 },
        { WM_REG_STARTUP_WORKAROUND_1, 0x0000 },
        { WM_REG_ANTIPOP_2, WM_ANTIPOP_VMID_RAMP },
        { WM_REG_POWER_1, WM_POWER_BIAS_VMID },
    };
    int ret = write_sequence(c, sequence, sizeof(sequence) / sizeof(sequence[0]));
    if (!ret) dmosi_thread_sleep(50);
    return ret;
}

/** @brief Route AIF1 timeslot zero to DAC1 and the headphone mixers. */
static int start_digital(dmdrvi_context_t c)
{
    static const uint16_t sequence[][2] = {
        { WM_REG_POWER_5, WM_POWER_DAC1_AIF1 },
        { WM_REG_DAC1_LEFT_ROUTE, WM_ROUTE_AIF1_TO_DAC1 },
        { WM_REG_DAC1_RIGHT_ROUTE, WM_ROUTE_AIF1_TO_DAC1 },
        { WM_REG_DAC2_LEFT_ROUTE, 0x0000 },
        { WM_REG_DAC2_RIGHT_ROUTE, 0x0000 },
        { WM_REG_AIF1_CONTROL, 0x0000 },
        { WM_REG_AIF1_MASTER, 0x0000 },
        { WM_REG_CORE_CLOCKING, WM_CORE_AIF1_DSP_CLOCKS },
        { WM_REG_AIF1_CLOCKING, WM_AIF1_MCLK1_ENABLE },
        { WM_REG_DAC1_LEFT_VOLUME, WM_DAC_UNITY_GAIN },
        { WM_REG_DAC1_RIGHT_VOLUME, WM_DAC_UNITY_GAIN },
        { WM_REG_DAC1_FILTER, WM_DAC1_MUTE | WM_DAC1_FILTER_ENABLE },
        { WM_REG_DAC1_LEFT_GAIN, WM_DAC_UNITY_GAIN },
        { WM_REG_DAC1_RIGHT_GAIN, WM_DAC_UNITY_GAIN },
        { WM_REG_OUTPUT_MIXER_LEFT, WM_OUTPUT_DAC1_TO_HP },
        { WM_REG_OUTPUT_MIXER_RIGHT, WM_OUTPUT_DAC1_TO_HP },
    };
    return write_sequence(c, sequence, sizeof(sequence) / sizeof(sequence[0]));
}

/** @brief Route the board's digital microphone 2 to AIF1 ADC timeslot one. */
static int start_digital_mic2(dmdrvi_context_t c)
{
    static const uint16_t sequence[][2] = {
        { WM_REG_POWER_4, WM_DMIC2_POWER },
        { WM_REG_DRC2, WM_DMIC2_DRC },
        { WM_REG_POWER_2, WM_DMIC2_THERMAL },
        { WM_REG_ADC2_LEFT_ROUTE, WM_ROUTE_DMIC2_TO_ADC2 },
        { WM_REG_ADC2_RIGHT_ROUTE, WM_ROUTE_DMIC2_TO_ADC2 },
        { WM_REG_GPIO1, WM_DMIC2_GPIO1 },
        { WM_REG_POWER_1, WM_POWER_BIAS_VMID_MICBIAS },
        { WM_REG_OVERSAMPLING, WM_ADC_OVERSAMPLING },
        { WM_REG_ADC2_FILTER, WM_ADC2_VOICE_FILTER },
        { WM_REG_ADC2_LEFT_VOLUME, WM_ADC_VOLUME_UPDATE | WM_ADC_UNITY_GAIN },
        { WM_REG_ADC2_RIGHT_VOLUME, WM_ADC_VOLUME_UPDATE | WM_ADC_UNITY_GAIN },
    };
    return write_sequence(c, sequence, sizeof(sequence) / sizeof(sequence[0]));
}

/** @brief Map a PCM rate to its documented AIF1 256fs register code. */
static int rate_code(uint32_t hz)
{
    switch (hz) {
        case 8000: return WM_AIF1_RATE_8000;
        case 11025: return WM_AIF1_RATE_11025;
        case 16000: return WM_AIF1_RATE_16000;
        case 22050: return WM_AIF1_RATE_22050;
        case 32000: return WM_AIF1_RATE_32000;
        case 44100: return WM_AIF1_RATE_44100;
        case 48000: return WM_AIF1_RATE_48000;
        case 96000: return WM_AIF1_RATE_96000;
        default: return -ENOTSUP;
    }
}

/** @brief Reject unsupported PCM format, output and volume before bus access. */
int codec_validate_config(const dmdrvi_audio_config_t *config)
{
    if (!config) return -EINVAL;
    if (config->output != DMDRVI_AUDIO_OUTPUT_HEADPHONE ||
        config->channels != 2 || config->sample_bits != 16)
        return -ENOTSUP;
    if (config->volume_percent > 100) return -EINVAL;
    return rate_code(config->sample_rate_hz) < 0 ? -ENOTSUP : 0;
}

/** @brief Probe, reset and start the headphone anti-pop sequence. */
int codec_configure(dmdrvi_context_t c, const dmdrvi_audio_config_t *config)
{
    int validation = codec_validate_config(config);
    if (validation) return validation;
    int code = rate_code(config->sample_rate_hz);
    uint16_t id = 0;
    int ret = codec_read_reg(c, WM_REG_RESET, &id);
    if (ret) return ret;
    if (id != DMWM8994_CHIP_ID)
    {
        DMOD_LOG_ERROR("dmwm8994: unexpected chip ID 0x%04X\n", id);
        return -ENODEV;
    }
    c->info.configured = false;
    ret = write_reg(c, WM_REG_RESET, 0x0000);
    if (!ret) ret = start_bias(c);
    if (!ret) ret = start_digital(c);
    if (!ret && c->digital_mic2) ret = start_digital_mic2(c);
    if (!ret) ret = write_reg(c, WM_REG_AIF1_RATE, (uint16_t)code);
    if (!ret) ret = write_reg(c, WM_REG_WRITE_SEQUENCER, WM_WRITE_SEQ_START);
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
