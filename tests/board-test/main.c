#include "dmod.h"
#include "dmwm8994.h"
#include "dmsai_ioctl.h"

static int16_t pcm[256 * 2];
static int16_t captured[256 * 2];

/** @brief Send a short low-level test tone through the SAI DMA path. */
static int stream_pcm(void *codec)
{
    for (unsigned i = 0; i < 256; ++i) {
        int16_t sample = (i & 8U) ? -1000 : 1000;
        pcm[2 * i] = sample;
        pcm[2 * i + 1] = sample;
    }
    if (Dmod_FileWrite(pcm, 1, sizeof(pcm), codec) != sizeof(pcm)) return -1;
    if (Dmod_Ioctl(codec, DMSAI_IOCTL_DRAIN, NULL) != 0) return -2;
    if (Dmod_FileRead(captured, 1, sizeof(captured), codec) != sizeof(captured))
        return -3;
    int32_t peak = 0;
    for (unsigned i = 0; i < sizeof(captured) / sizeof(captured[0]); ++i)
    {
        int32_t sample = captured[i];
        int32_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > peak) peak = magnitude;
    }
    Dmod_Printf("WM8994 PLAY TEST: microphone peak=%ld\n", (long)peak);
    dmsai_status_t status;
    if (Dmod_Ioctl(codec, DMSAI_IOCTL_GET_STATUS, &status) != 0 ||
        status.transfer_errors) return -4;
    return 0;
}

/** @brief Check the mute bit while SAI is supplying the codec clock. */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        Dmod_Printf("Usage: wm8994playtest CODEC_DEVICE\n");
        return 1;
    }
    void *codec = Dmod_FileOpen(argv[1], "r+");
    if (!codec) {
        Dmod_Printf("WM8994 PLAY TEST: cannot open codec\n");
        return 1;
    }
    dmdrvi_audio_config_t config = {
        .sample_rate_hz = 48000,
        .channels = 2,
        .sample_bits = 16,
        .output = DMDRVI_AUDIO_OUTPUT_HEADPHONE,
        .volume_percent = 50,
        .muted = true,
    };
    int ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_CONFIGURE, &config);
    uint32_t timeout_ms = 1000;
    if (!ret) ret = Dmod_Ioctl(codec, DMSAI_IOCTL_SET_IO_TIMEOUT, &timeout_ms);
    uint8_t volume_percent = 60;
    if (!ret) ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_VOLUME, &volume_percent);
    bool muted = false;
    if (!ret) ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    dmdrvi_audio_info_t info;
    if (!ret) ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_GET_INFO, &info);
    if (!ret) {
        Dmod_Printf("WM8994 PLAY TEST: ID=0x%04X, rate=%lu, muted=%u\n",
            (unsigned)info.hardware_id,
            (unsigned long)info.config.sample_rate_hz, info.config.muted);
        if (!info.configured || info.hardware_id != DMWM8994_CHIP_ID ||
            info.config.sample_rate_hz != 48000 || info.config.muted ||
            info.config.volume_percent != volume_percent) ret = -1;
    }
    if (!ret) ret = stream_pcm(codec);
    muted = true;
    int cleanup = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    if (!ret && cleanup) ret = cleanup;
    Dmod_FileClose(codec);
    Dmod_Printf("WM8994 PLAY TEST: %s (%d)\n", ret ? "FAIL" : "PASS", ret);
    return ret ? 1 : 0;
}
