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
int main(void)
{
    void *codec = Dmod_FileOpen("/dev/codec", "r+");
    void *sai = Dmod_FileOpen("/dev/dmsai1", "r+");
    if (!codec || !sai) {
        Dmod_Printf("WM8994 PLAY TEST: cannot open codec or SAI\n");
        if (sai) Dmod_FileClose(sai);
        if (codec) Dmod_FileClose(codec);
        return 1;
    }
    dmwm8994_config_t config = { 48000, dmwm8994_output_headphone, 32, true };
    int ret = Dmod_Ioctl(sai, DMSAI_IOCTL_START, NULL);
    bool started = ret == 0;
    if (!ret) ret = Dmod_Ioctl(codec, DMWM8994_IOCTL_CONFIGURE, &config);
    bool muted = false;
    if (!ret) ret = Dmod_Ioctl(codec, DMWM8994_IOCTL_SET_MUTE, &muted);
    dmwm8994_info_t info;
    if (!ret) ret = Dmod_Ioctl(codec, DMWM8994_IOCTL_GET_INFO, &info);
    if (!ret) {
        Dmod_Printf("WM8994 PLAY TEST: AIF1_RATE=0x%04X, DAC1_FILTER=0x%04X\n",
            info.aif1_rate_register, info.dac1_filter_register);
        if (info.aif1_rate_register != 0x0083 ||
            (info.dac1_filter_register & 0x0200) != 0) ret = -1;
    }
    if (!ret) ret = stream_pcm(sai);
    muted = true;
    (void)Dmod_Ioctl(codec, DMWM8994_IOCTL_SET_MUTE, &muted);
    if (started) (void)Dmod_Ioctl(sai, DMSAI_IOCTL_STOP, NULL);
    Dmod_FileClose(sai);
    Dmod_FileClose(codec);
    Dmod_Printf("WM8994 PLAY TEST: %s (%d)\n", ret ? "FAIL" : "PASS", ret);
    return ret ? 1 : 0;
}
