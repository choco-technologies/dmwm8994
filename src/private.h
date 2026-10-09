#ifndef DMWM8994_PRIVATE_H
#define DMWM8994_PRIVATE_H
#include "dmod.h"
#include "dmdrvi.h"
#include "dmosi.h"
#include "dmwm8994.h"
#define CODEC_MAGIC 0x574D3934U
#define CODEC_HANDLE_MAGIC 0x574D4844U
typedef struct codec_handle {
    uint32_t magic;
    void *sai;
    bool started;
    bool quad_slots;
} codec_handle_t;
struct dmdrvi_context {
    uint32_t magic;
    char *bus_path;
    char *sai_path;
    void *bus;
    dmosi_mutex_t lock;
    codec_handle_t *open_handle;
    bool digital_mic2;
    uint16_t address;
    uint16_t expected_rate_register;
    dmdrvi_audio_info_t info;
};
int codec_connect(dmdrvi_context_t c);
void codec_disconnect(dmdrvi_context_t c);
int codec_read_reg(dmdrvi_context_t c, uint16_t reg, uint16_t *value);
int codec_validate_config(const dmdrvi_audio_config_t *config);
int codec_configure(dmdrvi_context_t c, const dmdrvi_audio_config_t *config);
int codec_set_volume(dmdrvi_context_t c, uint8_t volume);
int codec_set_mute(dmdrvi_context_t c, bool mute);
int codec_stream_start(dmdrvi_context_t c, codec_handle_t *handle,
                       const dmdrvi_audio_config_t *config);
void codec_stream_stop(codec_handle_t *handle);
dmdrvi_ssize_t codec_pcm_read(dmdrvi_context_t c, codec_handle_t *handle,
                              void *buffer, size_t size, dmdrvi_offset_t offset);
dmdrvi_ssize_t codec_pcm_write(dmdrvi_context_t c, codec_handle_t *handle,
                               const void *buffer, size_t size, dmdrvi_offset_t offset);
#endif
