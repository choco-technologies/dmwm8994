# dmwm8994

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmwm8994/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmwm8994/actions/workflows/ci.yml)

WM8994 audio codec driver for DMOD. `dmdevfs` mounts the codec node, `dmi2c` carries register transactions, and the codec uses its `dmsai` friend for DMA audio. Applications open only the codec node for controls and stereo PCM input/output. The control interface uses the standard `DMDRVI_IOCTL_AUDIO_*` commands available in [dmdrvi v2.6](https://github.com/choco-technologies/dmdrvi/releases/tag/v2.6). No CPU family specific code is required in this module.

## Board configuration

[codec.ini](configs/board/stm32f746g-disco/codec.ini) contains the I2C3 pins and bus, SAI2 pins and stream, and the WM8994 node for STM32F746G-DISCO. All sections use `friends_group=wm8994`; the bus has `friend_role=i2c_bus` and SAI has `friend_role=audio_stream`. The codec's unshifted 7-bit address is `0x1A` (decimal 26): ST defines `AUDIO_I2C_ADDRESS` as the shifted value `0x34` in its [board header](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery.h). `driver_order=10` configures pins, `11` creates controllers, and `12` creates the codec. The application-facing node is `/dev/wm8994`.

This configuration owns I2C3 and SAI2. Do not also load `dmft5336`'s `touch.ini`, `dmi2c`'s `i2c3.ini`, or `dmsai`'s `sai2.ini`: each of those would configure one of the same controllers again. To use touch and audio together, put their devices on one shared I2C3 node and assign the same friends group in the board configuration.

On another board, provide an INI section with `driver_name=dmwm8994`, `address=<unshifted 7-bit address>`, and a `friends_group` shared with a `dmi2c` node whose `friend_role=i2c_bus` and a `dmsai` node whose `friend_role=audio_stream`. Set `input=dmic2` for digital microphone 2 capture, or `input=none` for playback only. The section name becomes the codec node name. The driver supports headphone output with two 16-bit channels at 8, 11.025, 16, 22.05, 32, 44.1, 48 or 96 kHz; 48 kHz has been checked on STM32F746G-DISCO.

## Control API

Include `dmdrvi_ioctl.h` for the generic types and commands. `DMDRVI_IOCTL_AUDIO_GET_INFO` reads the WM8994 ID and current settings. `DMDRVI_IOCTL_AUDIO_CONFIGURE` accepts `dmdrvi_audio_config_t`. `DMDRVI_IOCTL_AUDIO_SET_VOLUME` takes a `uint8_t` percentage from 0 to 100, mapped to the WM8994 gain code. `DMDRVI_IOCTL_AUDIO_SET_MUTE` takes a `bool`. See [API reference](docs/api-reference.md) for arguments, errors and sequencing.

```c
#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include "dmsai_ioctl.h"
#include <stdbool.h>
#include <stdint.h>

const char *codec_path = "/dev/wm8994";
static int16_t pcm[256 * 2]; /* 256 interleaved stereo frames, outside the stack. */
void *codec = Dmod_FileOpen(codec_path, "r+");
if (codec) {
    dmdrvi_audio_config_t cfg = {
        .sample_rate_hz = 48000,
        .channels = 2,
        .sample_bits = 16,
        .output = DMDRVI_AUDIO_OUTPUT_HEADPHONE,
        .volume_percent = 50,
        .muted = true,
    };
    if (Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_CONFIGURE, &cfg) == 0) {
        bool muted = false;
        if (Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted) == 0) {
            Dmod_FileWrite(pcm, 1, sizeof(pcm), codec);
            Dmod_Ioctl(codec, DMSAI_IOCTL_DRAIN, NULL);
            /* With input=dmic2, this reads stereo microphone PCM. */
            Dmod_FileRead(pcm, 1, sizeof(pcm), codec);
        }
        muted = true;
        Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    }
}
if (codec) Dmod_FileClose(codec);
```

`AUDIO_CONFIGURE` starts the SAI DMA stream before resetting the codec, so MCLK is present during setup. Drain pending playback with `DMSAI_IOCTL_DRAIN` before closing the codec node, which stops the stream. With `input=dmic2`, `Dmod_FileRead` returns interleaved stereo 16-bit samples from microphone slots 1 and 3; `Dmod_FileWrite` maps stereo samples into headphone slots 0 and 2. `DMSAI_IOCTL_DRAIN`, status and I/O timeout commands can be issued on the codec handle. `GET_INFO` detects a lost codec configuration by reading back the AIF1 rate.

## Tools and tests

`wm8994ctl DEVICE info|setup|mute|unmute` works on a caller-supplied codec node. For the board configuration above, run `wm8994ctl /dev/wm8994 info`.

The optional board test `wm8994playtest CODEC_DEVICE` in `tests/board-test` exercises the codec node: it configures WM8994, sends 256 stereo frames through SAI/DMA, captures 256 microphone frames, checks stream status, drains and mutes. Build it with `-DDMWM8994_BUILD_BOARD_TEST=ON`. The host test mounts `dmdevfs` and exercises a caller-supplied codec node with valid and invalid INI fixtures; it does not enable the codec driver manually.

With the same board-test option, `wm8994melody /dev/wm8994` plays the public-domain melody "Twinkle Twinkle Little Star" repeatedly through the headphone output. It generates 48 kHz stereo PCM in a static 256-frame buffer, checks DMA status after each repeat, and runs until the board is reset or an audio error occurs.

```sh
cmake -S . -B build -DDMOD_DIR=/path/to/dmod
cmake --build build
ctest --test-dir build --output-on-failure
```

The build fetches dmdrvi v2.6 or newer because that release first contains the generic audio controls. Until `dmsai` has a release package, CMake fetches its public headers from the [organization repository](https://github.com/choco-technologies/dmsai); the firmware must also load a matching `dmsai` module. For STM32F746G-DISCO also use `-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7 -DDMOD_CPU_FAMILY=stm32f7`.

## Sources

The AIF1 routing, clock and headphone startup register values follow [ST's WM8994 component driver](https://github.com/STMicroelectronics/stm32-wm8994/blob/main/wm8994.c); register bit definitions follow its [register header](https://github.com/STMicroelectronics/stm32-wm8994/blob/main/wm8994_reg.h). Board connections follow [ST's STM32F746G Discovery audio BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_audio.c).

## License

MIT. See [LICENSE](LICENSE).
