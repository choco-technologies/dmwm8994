#ifndef DMWM8994_PRIVATE_H
#define DMWM8994_PRIVATE_H
#include "dmod.h"
#include "dmdrvi.h"
#include "dmosi.h"
#include "dmwm8994.h"
#define CODEC_MAGIC 0x574D3934U
struct dmdrvi_context {
    uint32_t magic;
    char *bus_path;
    void *bus;
    dmosi_mutex_t lock;
    uint16_t address;
    uint16_t expected_rate_register;
    dmdrvi_audio_info_t info;
};
int codec_connect(dmdrvi_context_t c);
void codec_disconnect(dmdrvi_context_t c);
int codec_read_reg(dmdrvi_context_t c, uint16_t reg, uint16_t *value);
int codec_configure(dmdrvi_context_t c, const dmdrvi_audio_config_t *config);
int codec_set_volume(dmdrvi_context_t c, uint8_t volume);
int codec_set_mute(dmdrvi_context_t c, bool mute);
#endif
