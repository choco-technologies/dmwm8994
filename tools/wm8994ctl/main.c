#include "dmod.h"
#include "dmwm8994.h"
#include <string.h>

/** @brief Print codec identity and applied settings. */
static int show_info(void *fp)
{
    dmwm8994_info_t info;
    int ret = Dmod_Ioctl(fp, DMWM8994_IOCTL_GET_INFO, &info);
    if (ret) { Dmod_Printf("wm8994ctl: GET_INFO failed (%d)\n", ret); return ret; }
    Dmod_Printf("WM8994 ID: 0x%04X, configured: %u, rate: %lu Hz, volume: %u, muted: %u\n",
        info.chip_id, info.configured, (unsigned long)info.config.sample_rate_hz,
        info.config.volume, info.config.muted);
    Dmod_Printf("WM8994 registers: AIF1_RATE=0x%04X, DAC1_FILTER=0x%04X\n",
        info.aif1_rate_register, info.dac1_filter_register);
    return info.chip_id == DMWM8994_CHIP_ID ? 0 : -1;
}

/** @brief Probe or configure the board's default codec node. */
int main(int argc, char **argv)
{
    const char *command = argc > 1 ? argv[1] : "info";
    const char *path = argc > 2 ? argv[2] : "/dev/codec";
    void *fp = Dmod_FileOpen(path, "r+");
    if (!fp) { Dmod_Printf("wm8994ctl: cannot open %s\n", path); return 1; }
    int ret;
    if (strcmp(command, "info") == 0) {
        ret = show_info(fp);
    } else if (strcmp(command, "setup") == 0) {
        dmwm8994_config_t config = { 48000, dmwm8994_output_headphone, 32, true };
        ret = Dmod_Ioctl(fp, DMWM8994_IOCTL_CONFIGURE, &config);
        Dmod_Printf("wm8994ctl: CONFIGURE returned %d\n", ret);
        if (!ret) ret = show_info(fp);
    } else if (strcmp(command, "mute") == 0 || strcmp(command, "unmute") == 0) {
        bool muted = strcmp(command, "mute") == 0;
        ret = Dmod_Ioctl(fp, DMWM8994_IOCTL_SET_MUTE, &muted);
        Dmod_Printf("wm8994ctl: SET_MUTE returned %d\n", ret);
        if (!ret) ret = show_info(fp);
    } else {
        Dmod_Printf("Usage: wm8994ctl info|setup|mute|unmute [device]\n");
        ret = 1;
    }
    Dmod_FileClose(fp);
    return ret ? 1 : 0;
}
