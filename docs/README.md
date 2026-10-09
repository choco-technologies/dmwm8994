# WM8994 audio codec driver

`dmwm8994` is the WM8994 codec module. `dmdevfs` creates its device node from an INI section; the driver finds a `dmi2c` friend with `friend_role=i2c_bus` for register transfers and a `dmsai` friend with `friend_role=audio_stream` for DMA audio. Applications read and write stereo PCM through the codec node. No architecture-specific WM8994 port is needed.

## Bring-up on STM32F746G-DISCO

Load `configs/board/stm32f746g-disco/codec.ini` with the GPIO, `dmi2c`, `dmsai` and `dmwm8994` modules. It configures the I2C3 and SAI2 pins, their controllers and the codec in one `friends_group=wm8994`. The codec address is the board's unshifted 7-bit address `0x1A`, derived from ST's shifted [AUDIO_I2C_ADDRESS](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery.h) of `0x34`. The codec section is named `wm8994`, giving `/dev/wm8994`. Its `input=dmic2` route enables capture from the board microphone.

Do not load another configuration for I2C3 or SAI2 at the same time. A board using the touch controller must share its single I2C3 node with the codec and assign matching friends groups.

## Controls

Use the generic `DMDRVI_IOCTL_AUDIO_*` commands from `dmdrvi_ioctl.h`:

| Command | Argument | Effect |
| --- | --- | --- |
| `AUDIO_GET_INFO` | `dmdrvi_audio_info_t *` | Read chip ID and current audio state |
| `AUDIO_CONFIGURE` | `const dmdrvi_audio_config_t *` | Configure 16-bit stereo AIF1 and headphone output |
| `AUDIO_SET_VOLUME` | `const uint8_t *` | Set volume from 0 to 100 percent |
| `AUDIO_SET_MUTE` | `const bool *` | Set playback mute |

The full command names start with `DMDRVI_IOCTL_`. `AUDIO_CONFIGURE` starts SAI before resetting WM8994 so MCLK is present during setup. Write interleaved signed 16-bit stereo PCM to the codec node, drain, then mute and close it. With `input=dmic2`, read the same PCM format from the codec node. SAI timeout, status and drain ioctls are forwarded through the codec handle. A rate-register mismatch makes `GET_INFO` report `configured=false` and prevents volume or mute changes until reconfiguration.

Supported rates are 8, 11.025, 16, 22.05, 32, 44.1, 48 and 96 kHz at AIF1 256fs. The implemented analog route is stereo headphones. Only 48 kHz has been exercised on STM32F746G-DISCO so far.

## PCM example

```c
#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include "dmsai_ioctl.h"
#include <stdint.h>

int16_t samples[256 * 2] = {0};
void *codec = Dmod_FileOpen("/dev/wm8994", "r+");
dmdrvi_audio_config_t config = {
    .sample_rate_hz = 48000, .channels = 2, .sample_bits = 16,
    .output = DMDRVI_AUDIO_OUTPUT_HEADPHONE,
    .volume_percent = 50, .muted = false,
};
if (codec && Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_CONFIGURE, &config) == 0) {
    Dmod_FileWrite(samples, 1, sizeof(samples), codec);
    Dmod_Ioctl(codec, DMSAI_IOCTL_DRAIN, NULL);
    /* With input=dmic2, capture interleaved stereo microphone samples. */
    Dmod_FileRead(samples, 1, sizeof(samples), codec);
}
if (codec) Dmod_FileClose(codec);
```

The `dmsai` drain, status and I/O timeout commands can be sent through the codec node. The codec opens and starts its SAI friend when configured, then closes that stream with the codec handle.

## Commands

```sh
wm8994ctl /dev/wm8994 info
wm8994ctl /dev/wm8994 setup
wm8994playtest /dev/wm8994
```

`wm8994ctl setup` configures playback muted, then closes its handle and stops the stream. `wm8994playtest` configures the codec, sends a short PCM tone through DMA, captures microphone PCM, checks stream status, then mutes and closes.

See [API reference](api-reference.md) for exact types, errors and the portable board configuration contract. See the repository [README](../README.md) for a C example and build instructions.
