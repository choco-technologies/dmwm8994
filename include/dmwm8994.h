#ifndef DMWM8994_H
#define DMWM8994_H

#include <stdbool.h>
#include <stdint.h>
#include "dmdrvi_ioctl.h"

/** @brief Expected value of the WM8994 ID register. */
#define DMWM8994_CHIP_ID 0x8994U

/** @brief Available analog output route. */
typedef enum { dmwm8994_output_headphone = 0 } dmwm8994_output_t;

/** @brief AIF1 and analog output settings. */
typedef struct {
    uint32_t sample_rate_hz; /**< PCM frame rate: 8, 11.025, 16, 22.05, 32, 44.1, 48 or 96 kHz. */
    dmwm8994_output_t output; /**< Analog output route. */
    uint8_t volume; /**< Headphone gain code, 0..63. */
    bool muted; /**< True to mute the AIF1 DAC. */
} dmwm8994_config_t;

/** @brief Identification and current settings. */
typedef struct {
    uint16_t chip_id; /**< Read from register 0x0000. */
    uint16_t aif1_rate_register; /**< Current AIF1 rate register (0x0210). */
    uint16_t dac1_filter_register; /**< Current AIF1 DAC1 filter register (0x0420). */
    dmwm8994_config_t config; /**< Last successfully applied settings. */
    bool configured; /**< True after successful CONFIGURE. */
} dmwm8994_info_t;

/**
 * @brief Read codec ID and driver state.
 * @param arg Output pointer to dmwm8994_info_t.
 * @return 0 or a negative errno code.
 */
#define DMWM8994_IOCTL_GET_INFO (DMDRVI_IOCTL_CUSTOM_BASE + 0)

/**
 * @brief Reset and configure AIF1 and the headphone path.
 * @param arg Input pointer to dmwm8994_config_t.
 * @return 0, -ENOTSUP for an unsupported format, or another negative errno.
 * @note The paired interface must already supply MCLK before this call.
 * Start the paired SAI stream with silence, configure the codec, then send
 * audible PCM. A later first start of MCLK may reset codec audio registers.
 */
#define DMWM8994_IOCTL_CONFIGURE (DMDRVI_IOCTL_CUSTOM_BASE + 1)

/**
 * @brief Change headphone gain without restarting the codec.
 * @param arg Input pointer to uint8_t in the range 0..63.
 * @return 0 or a negative errno code.
 */
#define DMWM8994_IOCTL_SET_VOLUME (DMDRVI_IOCTL_CUSTOM_BASE + 2)

/**
 * @brief Mute or unmute the AIF1 DAC path.
 * @param arg Input pointer to bool.
 * @return 0 or a negative errno code.
 */
#define DMWM8994_IOCTL_SET_MUTE (DMDRVI_IOCTL_CUSTOM_BASE + 3)

#endif
