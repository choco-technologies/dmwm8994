#include "dmod.h"
#include "dmwm8994.h"
#include "dmsai_ioctl.h"

static int16_t pcm[256 * 2];

/** @brief Send a short low-level test tone through the SAI DMA path. */
static int stream_pcm(void *sai)
{
    for (unsigned i = 0; i < 256; ++i) {
        int16_t sample = (i & 8U) ? -1000 : 1000;
        pcm[2 * i] = sample;
        pcm[2 * i + 1] = sample;
    }
    if (Dmod_FileWrite(pcm, 1, sizeof(pcm), sai) != sizeof(pcm)) return -1;
    if (Dmod_Ioctl(sai, DMSAI_IOCTL_DRAIN, NULL) != 0) return -2;
    dmsai_status_t status;
    if (Dmod_Ioctl(sai, DMSAI_IOCTL_GET_STATUS, &status) != 0 ||
        status.transfer_errors) return -3;
    return 0;
}

/** @brief Check the mute bit while SAI is supplying the codec clock. */
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        Dmod_Printf("Usage: wm8994playtest CODEC_DEVICE SAI_DEVICE\n");
        return 1;
    }
    void *codec = Dmod_FileOpen(argv[1], "r+");
    void *sai = Dmod_FileOpen(argv[2], "r+");
    if (!codec || !sai) {
        Dmod_Printf("WM8994 PLAY TEST: cannot open codec or SAI\n");
        if (sai) Dmod_FileClose(sai);
        if (codec) Dmod_FileClose(codec);
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
    int ret = Dmod_Ioctl(sai, DMSAI_IOCTL_START, NULL);
    bool started = ret == 0;
    if (!ret) ret = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_CONFIGURE, &config);
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
    if (!ret) ret = stream_pcm(sai);
    muted = true;
    int cleanup = Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    if (!ret && cleanup) ret = cleanup;
    if (started)
    {
        cleanup = Dmod_Ioctl(sai, DMSAI_IOCTL_STOP, NULL);
        if (!ret && cleanup) ret = cleanup;
    }
    Dmod_FileClose(sai);
    Dmod_FileClose(codec);
    Dmod_Printf("WM8994 PLAY TEST: %s (%d)\n", ret ? "FAIL" : "PASS", ret);
    return ret ? 1 : 0;
}
