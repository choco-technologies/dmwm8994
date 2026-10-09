# dmwm8994

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmwm8994/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmwm8994/actions/workflows/ci.yml)

WM8994 audio codec control driver for DMOD. It uses an existing `dmi2c` device, so the same I²C controller can serve the codec and other devices. PCM samples travel through a separate `dmsai` node.

## Supported configuration

The first hardware path is stereo headphone output with 16-bit samples on AIF1 and the codec as a clock slave. The driver accepts 8, 11.025, 16, 22.05, 32, 44.1, 48 and 96 kHz. The STM32F746G-DISCO board uses 48 kHz; its config is [codec.ini](configs/board/stm32f746g-disco/codec.ini). It joins the `lcd` friend group because that board's I²C3 bus is already declared by `dmft5336`'s `touch.ini` there. Other boards choose any group containing a `dmi2c` node with `friend_role=i2c_bus`.

The codec's 7-bit address is configured as decimal `26` (0x1A). The `codec` section becomes `/dev/codec`. `dmdevfs` creates the node; this module supplies its `dmdrvi` implementation.

```ini
[codec]
driver_name=dmwm8994
friends_group=audio_i2c
address=26
```

## API

Open the codec control node and use `Dmod_Ioctl`:

```c
#include "dmod.h"
#include "dmwm8994.h"
#include "dmsai_ioctl.h"

void *codec = Dmod_FileOpen("/dev/codec", "r+");
void *sai = Dmod_FileOpen("/dev/dmsai1", "r+");
dmwm8994_info_t info;
if (codec && sai &&
    Dmod_Ioctl(codec, DMWM8994_IOCTL_GET_INFO, &info) == 0 &&
    info.chip_id == DMWM8994_CHIP_ID &&
    Dmod_Ioctl(sai, DMSAI_IOCTL_START, NULL) == 0) {
    dmwm8994_config_t config = {
        .sample_rate_hz = 48000,
        .output = dmwm8994_output_headphone,
        .volume = 32,
        .muted = true,
    };
    if (Dmod_Ioctl(codec, DMWM8994_IOCTL_CONFIGURE, &config) == 0) {
        bool muted = false;
        if (Dmod_Ioctl(codec, DMWM8994_IOCTL_SET_MUTE, &muted) == 0) {
            /* Write PCM frames to sai, then drain the SAI stream. */
            Dmod_Ioctl(sai, DMSAI_IOCTL_DRAIN, NULL);
        }
        muted = true;
        Dmod_Ioctl(codec, DMWM8994_IOCTL_SET_MUTE, &muted);
    }
    Dmod_Ioctl(sai, DMSAI_IOCTL_STOP, NULL);
}
if (sai) Dmod_FileClose(sai);
if (codec) Dmod_FileClose(codec);
```

Start the corresponding `dmsai` stream first so it supplies MCLK, then configure the codec while SAI sends silence. After configuration, unmute and write PCM. Stop the stream after muting. The first start of MCLK after codec configuration can clear the codec's audio registers, so `GET_INFO` marks the codec unconfigured if it sees that the rate register changed. `DMWM8994_IOCTL_SET_VOLUME` takes `uint8_t *`; `DMWM8994_IOCTL_SET_MUTE` takes `bool *`. `DMWM8994_IOCTL_GET_INFO` probes the chip independently of audio setup. Failed I²C transactions return a negative error code.

For a board check, run `wm8994ctl info` to read the ID. `wm8994ctl setup` initializes the headphone path in a muted state; supply MCLK first if you intend to play audio. `wm8994ctl unmute` and `wm8994ctl mute` control the digital mute around playback. The optional `wm8994playtest` starts SAI, configures WM8994, transmits a short PCM tone through DMA, then mutes and stops. See [API reference](docs/api-reference.md) for all arguments and errors.

## Build and test

```sh
cmake -S . -B build -DDMOD_DIR=/path/to/dmod
cmake --build build
ctest --test-dir build --output-on-failure
```

For STM32F746G-DISCO, select `-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7 -DDMOD_CPU_FAMILY=stm32f7`. The module, its command, and test application are separate DMOD files. The native [Makefile](Makefile) is also available through `make DMOD_DIR=/path/to/dmod`.

To build the hardware integration test with a local `dmsai` checkout, add `-DDMWM8994_BUILD_BOARD_TEST=ON -DDMWM8994_BOARD_TEST_DMSAI_INCLUDE=/path/to/dmsai/include`. Load its `wm8994playtest.dmf` together with the codec and SAI modules; it expects `/dev/codec` and `/dev/dmsai1`.

## Sources

The AIF1 routing, clock and headphone startup register values follow [ST's WM8994 component driver](https://github.com/STMicroelectronics/stm32-wm8994/blob/main/wm8994.c); register bit definitions follow its [register header](https://github.com/STMicroelectronics/stm32-wm8994/blob/main/wm8994_reg.h). Board connections follow [ST's STM32F746G Discovery audio BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_audio.c).

## License

MIT. See [LICENSE](LICENSE).
