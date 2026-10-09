# WM8994 audio codec driver

`dmwm8994` is the control-plane module for the WM8994 codec. `dmdevfs` creates its device node from an INI section; the driver finds a `dmi2c` friend with `friend_role=i2c_bus` and performs 16-bit register transfers over that shared bus. Audio samples are written to a separate `dmsai` stream node. No architecture-specific WM8994 port is needed.

## Bring-up on STM32F746G-DISCO

Load `configs/board/stm32f746g-disco/codec.ini` with the GPIO, `dmi2c`, `dmsai` and `dmwm8994` modules. It configures the I2C3 and SAI2 pins, their controllers and the codec in one `friends_group=wm8994`. The codec address is the board's unshifted 7-bit address `0x1A`. The codec section is named `wm8994`, giving `/dev/wm8994`. Discover the SAI stream node with `ls /dev` and supply both node paths to the tools.

Do not load another configuration for I2C3 or SAI2 at the same time. A board using the touch controller must share its single I2C3 node with the codec and assign matching friends groups.

## Controls

Use the generic `DMDRVI_IOCTL_AUDIO_*` commands from `dmdrvi_ioctl.h`:

| Command | Argument | Effect |
| --- | --- | --- |
| `AUDIO_GET_INFO` | `dmdrvi_audio_info_t *` | Read chip ID and current audio state |
| `AUDIO_CONFIGURE` | `const dmdrvi_audio_config_t *` | Configure 16-bit stereo AIF1 and headphone output |
| `AUDIO_SET_VOLUME` | `const uint8_t *` | Set volume from 0 to 100 percent |
| `AUDIO_SET_MUTE` | `const bool *` | Set playback mute |

The full command names start with `DMDRVI_IOCTL_`. Start SAI before configuring WM8994: SAI supplies MCLK, and the first MCLK start can clear codec audio registers. Configure while the stream sends silence, unmute, send PCM, drain, mute, then stop SAI. A rate-register mismatch makes `GET_INFO` report `configured=false` and prevents volume or mute changes until reconfiguration.

Supported rates are 8, 11.025, 16, 22.05, 32, 44.1, 48 and 96 kHz at AIF1 256fs. The implemented analog route is stereo headphones. Only 48 kHz has been exercised on STM32F746G-DISCO so far.

## Commands

```sh
wm8994ctl /dev/wm8994 info
wm8994ctl /dev/wm8994 setup
wm8994playtest /dev/wm8994 SAI_DEVICE
```

`wm8994ctl setup` leaves playback muted. `wm8994playtest` starts the supplied SAI node, configures the codec, sends a short PCM tone through DMA, checks stream status, then mutes and stops.

See [API reference](api-reference.md) for exact types, errors and the portable board configuration contract. See the repository [README](../README.md) for a C example and build instructions.
