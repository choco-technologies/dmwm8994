# dmwm8994 API reference

`dmwm8994` implements the `dmdrvi` DIF and is mounted by `dmdevfs`. It controls WM8994 registers through a shared `dmi2c` friend and PCM through a `dmsai` friend. Applications use only the codec node; the driver owns the SAI handle while configured.

## INI configuration

| Key | Meaning |
| --- | --- |
| `driver_name` | `dmwm8994` |
| `address` | Required unshifted 7-bit I2C address, decimal 8..119 |
| `friends_group` | Group containing a `dmi2c` node with `friend_role=i2c_bus` and a `dmsai` node with `friend_role=audio_stream` |
| `input` | `none` (default) for playback or `dmic2` for digital microphone 2 capture |
| `driver_order` | Place after pins and I2C/SAI controllers when using one board file |

The section name is the codec device node name and must be 1..32 characters. An ioctl before the I2C friend is available returns `-ENODEV`; `AUDIO_CONFIGURE` also needs a ready SAI friend. The STM32F746G-DISCO board file is [codec.ini](../configs/board/stm32f746g-disco/codec.ini); it configures all pins, I2C3, SAI2 and WM8994 in one friend group. Its address 26 is the 7-bit form of ST's shifted `AUDIO_I2C_ADDRESS=0x34` in the [board header](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery.h).

## Standard audio controls

These types and commands are defined by `dmdrvi_ioctl.h` in [dmdrvi v2.6](https://github.com/choco-technologies/dmdrvi/releases/tag/v2.6) and later.

| Command | Argument | Behavior |
| --- | --- | --- |
| `DMDRVI_IOCTL_AUDIO_GET_INFO` | `dmdrvi_audio_info_t *` output | Reads chip ID, AIF1 rate and mute register; returns hardware ID, last settings and `configured`. |
| `DMDRVI_IOCTL_AUDIO_CONFIGURE` | `const dmdrvi_audio_config_t *` input | Starts the SAI DMA stream, probes ID, resets WM8994, and configures AIF1, headphone and selected microphone paths. |
| `DMDRVI_IOCTL_AUDIO_SET_VOLUME` | `const uint8_t *` input | Updates both headphone gains using a 0..100 percentage. Requires prior configuration. |
| `DMDRVI_IOCTL_AUDIO_SET_MUTE` | `const bool *` input | Controls AIF1 DAC1 digital mute. Requires prior configuration. |

`dmdrvi_audio_config_t` has `sample_rate_hz`, `channels`, `sample_bits`, `output`, `volume_percent` and `muted`. This driver accepts two channels, 16 bits, `DMDRVI_AUDIO_OUTPUT_HEADPHONE`, and 8000, 11025, 16000, 22050, 32000, 44100, 48000 or 96000 Hz. Other formats or outputs return `-ENOTSUP`; volume over 100 returns `-EINVAL`. A chip ID other than `0x8994` returns `-ENODEV`. Unknown commands return `-ENOTTY`. `GET_INFO` works before configuration.

`AUDIO_CONFIGURE` opens and starts the SAI friend before resetting the codec, so MCLK is running during setup. It requires SAI TDM, signed 16-bit little-endian samples, four 16-bit slots per 64-bit frame, and a matching sample rate. Playback-only configurations may activate slots 0 and 2; `input=dmic2` requires transmit and receive with all four slots active. `GET_INFO` detects a changed rate register and clears `configured`; volume and mute then return `-EPIPE` until reconfiguration. `SET_MUTE` reads the mute bit back and returns `-EIO` if the codec did not apply the change. Close stops the SAI stream.

## PCM stream

Open the codec node with `Dmod_FileOpen(path, "r+")` and call `AUDIO_CONFIGURE` before `Dmod_FileWrite` or `Dmod_FileRead`. Both use interleaved stereo signed 16-bit little-endian samples: four bytes per frame. Write maps left/right samples to SAI slots 0/2. With `input=dmic2`, read returns microphone samples from slots 1/3; with `input=none`, the driver read callback returns `-ENOTSUP`. A read or write before configuration returns `-EPIPE`; the byte count must be a multiple of four. `DMSAI_IOCTL_GET_CONFIG`, `GET_STATUS`, `SET_IO_TIMEOUT`, `GET_IO_TIMEOUT` and `DRAIN` are forwarded to the active SAI friend through the codec handle. The codec node permits one open handle at a time.

## Tests

`tests/dmwm8994_test.c` mounts `dmdevfs` with valid and invalid INI fixtures and exercises the caller-supplied codec node without manually activating the codec driver. The optional `tests/board-test` executable `wm8994playtest CODEC_DEVICE` exercises PCM write, microphone read and controls through the mounted codec node on hardware. The caller supplies the codec node path.
