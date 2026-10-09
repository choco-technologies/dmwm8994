#include "dmod.h"
#include "dmwm8994.h"
#include <string.h>

/** @brief Print codec identity and applied settings. */
static int show_info(void *fp)
{
    dmdrvi_audio_info_t info;
    int ret = Dmod_Ioctl(fp, DMDRVI_IOCTL_AUDIO_GET_INFO, &info);
    if (ret) { Dmod_Printf("wm8994ctl: GET_INFO failed (%d)\n", ret); return ret; }
    Dmod_Printf("WM8994 ID: 0x%04X, configured: %u, rate: %lu Hz, volume: %u, muted: %u\n",
        (unsigned)info.hardware_id, info.configured,
        (unsigned long)info.config.sample_rate_hz,
        info.config.volume_percent, info.config.muted);
    return info.hardware_id == DMWM8994_CHIP_ID ? 0 : -1;
}

/** @brief Probe or configure the codec node supplied by the caller. */
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        Dmod_Printf("Usage: wm8994ctl DEVICE info|setup|mute|unmute\n");
        return 1;
    }
    const char *path = argv[1];
    const char *command = argv[2];
    void *fp = Dmod_FileOpen(path, "r+");
    if (!fp) { Dmod_Printf("wm8994ctl: cannot open %s\n", path); return 1; }
    int ret;
    if (strcmp(command, "info") == 0) {
        ret = show_info(fp);
    } else if (strcmp(command, "setup") == 0) {
        dmdrvi_audio_config_t config = {
            .sample_rate_hz = 48000,
            .channels = 2,
            .sample_bits = 16,
            .output = DMDRVI_AUDIO_OUTPUT_HEADPHONE,
            .volume_percent = 50,
            .muted = true,
        };
        ret = Dmod_Ioctl(fp, DMDRVI_IOCTL_AUDIO_CONFIGURE, &config);
        Dmod_Printf("wm8994ctl: CONFIGURE returned %d\n", ret);
        if (!ret) ret = show_info(fp);
    } else if (strcmp(command, "mute") == 0 || strcmp(command, "unmute") == 0) {
        bool muted = strcmp(command, "mute") == 0;
        ret = Dmod_Ioctl(fp, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
        Dmod_Printf("wm8994ctl: SET_MUTE returned %d\n", ret);
        if (!ret) ret = show_info(fp);
    } else {
        Dmod_Printf("Usage: wm8994ctl DEVICE info|setup|mute|unmute\n");
        ret = 1;
    }
    Dmod_FileClose(fp);
    return ret ? 1 : 0;
}
