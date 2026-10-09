# dmwm8994

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmwm8994/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmwm8994/actions/workflows/ci.yml)

WM8994 audio codec control driver for DMOD. `dmdevfs` mounts the codec node, `dmi2c` carries register transactions, and a separate `dmsai` node carries PCM. The control interface uses the standard `DMDRVI_IOCTL_AUDIO_*` commands proposed in [dmdrvi PR #26](https://github.com/choco-technologies/dmdrvi/pull/26). No CPU family specific code is required in this module.

## Board configuration

[codec.ini](configs/board/stm32f746g-disco/codec.ini) contains the I2C3 pins and bus, SAI2 pins and stream, and the WM8994 node for STM32F746G-DISCO. All sections use `friends_group=wm8994`; the bus has `friend_role=i2c_bus` so the codec discovers it without a fixed path. The codec's unshifted 7-bit address is `0x1A` (decimal 26), as used by ST's Discovery audio BSP. `driver_order=10` configures pins, `11` creates controllers, and `12` creates the codec. The resulting codec node is `/dev/wm8994`; use the actual SAI node shown by `ls /dev`.

This configuration owns I2C3 and SAI2. Do not also load `dmft5336`'s `touch.ini`, `dmi2c`'s `i2c3.ini`, or `dmsai`'s `sai2.ini`: each of those would configure one of the same controllers again. To use touch and audio together, put their devices on one shared I2C3 node and assign the same friends group in the board configuration.

On another board, provide an INI section with `driver_name=dmwm8994`, `address=<unshifted 7-bit address>`, and a `friends_group` shared with a `dmi2c` node whose `friend_role=i2c_bus`. The section name becomes the codec node name. The driver supports headphone output with two 16-bit channels at 8, 11.025, 16, 22.05, 32, 44.1, 48 or 96 kHz; 48 kHz has been checked on STM32F746G-DISCO.

## Control API

Include `dmdrvi_ioctl.h` for the generic types and commands. `DMDRVI_IOCTL_AUDIO_GET_INFO` reads the WM8994 ID and current settings. `DMDRVI_IOCTL_AUDIO_CONFIGURE` accepts `dmdrvi_audio_config_t`. `DMDRVI_IOCTL_AUDIO_SET_VOLUME` takes a `uint8_t` percentage from 0 to 100, mapped to the WM8994 gain code. `DMDRVI_IOCTL_AUDIO_SET_MUTE` takes a `bool`. See [API reference](docs/api-reference.md) for arguments, errors and sequencing.

```c
#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include "dmsai_ioctl.h"

void *codec = Dmod_FileOpen(codec_path, "r+");
void *sai = Dmod_FileOpen(sai_path, "r+");
if (codec && sai && Dmod_Ioctl(sai, DMSAI_IOCTL_START, NULL) == 0) {
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
            /* Write PCM frames to sai and call DMSAI_IOCTL_DRAIN. */
        }
        muted = true;
        Dmod_Ioctl(codec, DMDRVI_IOCTL_AUDIO_SET_MUTE, &muted);
    }
    Dmod_Ioctl(sai, DMSAI_IOCTL_STOP, NULL);
}
if (sai) Dmod_FileClose(sai);
if (codec) Dmod_FileClose(codec);
```

Start SAI first to supply MCLK and silence, configure the codec, then unmute and send PCM. A first start of MCLK after codec setup can clear its audio registers. `GET_INFO` detects this by reading back the AIF1 rate and reports `configured=false`; configure again while MCLK is running.

## Tools and tests

`wm8994ctl DEVICE info|setup|mute|unmute` works on a caller-supplied codec node. For the board configuration above, run `wm8994ctl /dev/wm8994 info`.

The optional board test `wm8994playtest CODEC_DEVICE SAI_DEVICE` in `tests/board-test` exercises both device nodes: it starts SAI, configures WM8994, sends 256 stereo frames through SAI/DMA, drains, mutes and stops. Build it with `-DDMWM8994_BUILD_BOARD_TEST=ON -DDMWM8994_BOARD_TEST_DMSAI_INCLUDE=/path/to/dmsai/include`. The host unit suite checks configuration validation and unavailable-bus errors through the driver DIF; the board test checks the mounted device nodes.

```sh
cmake -S . -B build -DDMOD_DIR=/path/to/dmod
cmake --build build
ctest --test-dir build --output-on-failure
```

For STM32F746G-DISCO also use `-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7 -DDMOD_CPU_FAMILY=stm32f7`. Until the generic audio API is released by `dmdrvi`, build against the companion checkout with `-DDMDRVI_API_INCLUDE_DIR=/path/to/dmdrvi/include`.

## Sources

The AIF1 routing, clock and headphone startup register values follow [ST's WM8994 component driver](https://github.com/STMicroelectronics/stm32-wm8994/blob/main/wm8994.c); register bit definitions follow its [register header](https://github.com/STMicroelectronics/stm32-wm8994/blob/main/wm8994_reg.h). Board connections follow [ST's STM32F746G Discovery audio BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_audio.c).

## License

MIT. See [LICENSE](LICENSE).
