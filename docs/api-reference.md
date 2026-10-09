# dmwm8994 API

`dmwm8994` implements the `dmdrvi` DIF and is mounted by `dmdevfs`. It controls codec registers over a shared `dmi2c` bus. It does not own the I²C controller, SAI controller, or PCM buffers.

## Configuration

| INI key | Meaning | Default |
| --- | --- | --- |
| `driver_name` | Must be `dmwm8994` | Required |
| `address` | Unshifted 7-bit I²C address, decimal 8..119 | 26 |
| `friends_group` | Group containing one `dmi2c` node with `friend_role=i2c_bus` | Required for bus discovery |

The section name becomes the device node name. Bus discovery is asynchronous; an ioctl before the bus is ready returns `-ENODEV`.

## Controls

| Command | Argument | Effect |
| --- | --- | --- |
| `DMWM8994_IOCTL_GET_INFO` | `dmwm8994_info_t *` output | Reads chip ID (`0x0000`), AIF1 rate (`0x0210`) and DAC1 filter (`0x0420`); returns these with last configuration and configured flag. |
| `DMWM8994_IOCTL_CONFIGURE` | `const dmwm8994_config_t *` input | Probes ID, resets the chip, configures AIF1 and headphone path. |
| `DMWM8994_IOCTL_SET_VOLUME` | `const uint8_t *` input | Updates both headphone gains, code 0..63. Requires prior configuration. |
| `DMWM8994_IOCTL_SET_MUTE` | `const bool *` input | Controls the AIF1 DAC1 digital mute. Requires prior configuration. |

`dmwm8994_config_t` accepts 8000, 11025, 16000, 22050, 32000, 44100, 48000 and 96000 Hz at the AIF1 256fs ratio. The output route is currently `dmwm8994_output_headphone`. Unsupported rates or routes return `-ENOTSUP`. A bad volume returns `-EINVAL`. A chip ID other than `0x8994` returns `-ENODEV`. Unknown ioctls return `-ENOTTY`.

`GET_INFO` works before `CONFIGURE`. Only the control node is defined here; read and write of PCM are handled by `dmsai`. Start SAI first to supply MCLK and silence, then configure the codec; mute before stopping SAI. A first MCLK start after configuration can reset codec audio registers. `GET_INFO` detects a changed rate register and clears `configured`; volume and mute controls then return `-EPIPE` until reconfiguration. `SET_MUTE` reads the mute bit back and returns `-EIO` if the codec did not apply the change. No codec setup is run from the dmdevfs friend notification; the first ioctl opens the bus from a normal caller thread.

## Board check

```sh
wm8994ctl info
wm8994ctl setup
wm8994ctl unmute
wm8994ctl mute
```

`setup` leaves the output digitally muted. A PCM application can subsequently unmute with `DMWM8994_IOCTL_SET_MUTE` after configuring with MCLK present. The command's `unmute` and `mute` subcommands exercise the same ioctl.
