# dmwm8994 API reference

`dmwm8994` implements the `dmdrvi` DIF and is mounted by `dmdevfs`. It controls WM8994 registers through a shared `dmi2c` friend. PCM read and write belong to the paired `dmsai` node.

## INI configuration

| Key | Meaning |
| --- | --- |
| `driver_name` | `dmwm8994` |
| `address` | Required unshifted 7-bit I2C address, decimal 8..119 |
| `friends_group` | Group containing one `dmi2c` node with `friend_role=i2c_bus` |
| `driver_order` | Place after pins and I2C/SAI controllers when using one board file |

The section name is the codec device node name and must be 1..32 characters. An ioctl before the bus is available returns `-ENODEV`. The STM32F746G-DISCO board file is [codec.ini](../configs/board/stm32f746g-disco/codec.ini); it configures all pins, I2C3, SAI2 and WM8994 in one friend group.

## Standard audio controls

These types and commands are defined by `dmdrvi_ioctl.h` in [dmdrvi PR #26](https://github.com/choco-technologies/dmdrvi/pull/26).

| Command | Argument | Behavior |
| --- | --- | --- |
| `DMDRVI_IOCTL_AUDIO_GET_INFO` | `dmdrvi_audio_info_t *` output | Reads chip ID, AIF1 rate and mute register; returns hardware ID, last settings and `configured`. |
| `DMDRVI_IOCTL_AUDIO_CONFIGURE` | `const dmdrvi_audio_config_t *` input | Probes ID, resets WM8994, configures AIF1 and headphone path. |
| `DMDRVI_IOCTL_AUDIO_SET_VOLUME` | `const uint8_t *` input | Updates both headphone gains using a 0..100 percentage. Requires prior configuration. |
| `DMDRVI_IOCTL_AUDIO_SET_MUTE` | `const bool *` input | Controls AIF1 DAC1 digital mute. Requires prior configuration. |

`dmdrvi_audio_config_t` has `sample_rate_hz`, `channels`, `sample_bits`, `output`, `volume_percent` and `muted`. This driver accepts two channels, 16 bits, `DMDRVI_AUDIO_OUTPUT_HEADPHONE`, and 8000, 11025, 16000, 22050, 32000, 44100, 48000 or 96000 Hz. Other formats or outputs return `-ENOTSUP`; volume over 100 returns `-EINVAL`. A chip ID other than `0x8994` returns `-ENODEV`. Unknown commands return `-ENOTTY`. `GET_INFO` works before configuration.

Start the SAI stream before `AUDIO_CONFIGURE` so MCLK is already running. The first MCLK start after configuration may clear audio registers. `GET_INFO` detects a changed rate register and clears `configured`; volume and mute then return `-EPIPE` until reconfiguration. `SET_MUTE` reads the mute bit back and returns `-EIO` if the codec did not apply the change. Mute before stopping SAI.

## Tests

`tests/dmwm8994_test.c` exercises configuration validation and missing-bus behavior through the driver DIF on a host. The optional `tests/board-test` executable `wm8994playtest CODEC_DEVICE SAI_DEVICE` exercises the mounted codec and SAI device nodes on hardware. Both node paths are supplied by the caller.
