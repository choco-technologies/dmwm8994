#include "private.h"
#include "dmdrvi_ioctl.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

/* One AIF1 frame is four 16-bit time slots: HP-L, DMIC2-L, HP-R, DMIC2-R. */
#define CODEC_PCM_FRAME_BYTES 4U
#define CODEC_SAI_FRAME_BYTES 8U
/* Frames converted per write to the SAI: a write is a path through dmvfs and
 * dmdevfs, so a period of the mixer (480 frames) is two, not fifteen */
#define CODEC_PCM_BATCH_FRAMES 256U
#define CODEC_PCM_BATCH_BYTES (CODEC_PCM_BATCH_FRAMES * CODEC_SAI_FRAME_BYTES)
#define CODEC_HEADPHONE_SLOTS 0x05U
#define CODEC_ALL_SLOTS 0x0FU

/** @brief Check that the SAI friend transports the codec's AIF1 format. */
static int check_stream(dmdrvi_context_t c, codec_handle_t *handle,
                        const dmdrvi_audio_stream_t *sai,
                        const dmdrvi_audio_config_t *config)
{
    bool quad = sai->active_slots == CODEC_ALL_SLOTS;
    if (sai->framing != DMDRVI_AUDIO_FRAMING_TDM || sai->format != DMDRVI_AUDIO_FORMAT_S16_LE ||
        sai->slot_bits != 16 || sai->slot_count != 4 || sai->frame_bits != 64 ||
        sai->sample_rate_hz != config->sample_rate_hz || !sai->transmit ||
        (c->digital_mic2 && (!quad || !sai->receive)) ||
        (!quad && sai->active_slots != CODEC_HEADPHONE_SLOTS))
    {
        DMOD_LOG_ERROR("dmwm8994: incompatible SAI friend configuration\n");
        return -ENOTSUP;
    }
    handle->quad_slots = quad;
    return 0;
}

/** @brief Open the SAI friend and start its DMA stream before codec reset. */
int codec_stream_start(dmdrvi_context_t c, codec_handle_t *handle,
                       const dmdrvi_audio_config_t *config)
{
    if (handle->started)
    {
        dmdrvi_audio_stream_t active;
        int ret = Dmod_Ioctl(handle->sai, DMDRVI_IOCTL_AUDIO_GET_STREAM, &active);
        return ret ? ret : check_stream(c, handle, &active, config);
    }
    if (!c->sai_path)
    {
        DMOD_LOG_ERROR("dmwm8994: audio stream friend is not ready\n");
        return -ENODEV;
    }
    handle->sai = Dmod_FileOpen(c->sai_path, "r+");
    if (!handle->sai)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot open SAI friend %s\n", c->sai_path);
        return -ENODEV;
    }
    dmdrvi_audio_stream_t sai;
    int ret = Dmod_Ioctl(handle->sai, DMDRVI_IOCTL_AUDIO_GET_STREAM, &sai);
    if (!ret) ret = check_stream(c, handle, &sai, config);
    if (!ret) ret = Dmod_Ioctl(handle->sai, DMDRVI_IOCTL_AUDIO_START, NULL);
    if (!ret) ret = Dmod_Ioctl(handle->sai, DMDRVI_IOCTL_AUDIO_SET_TIMEOUT,
                               &handle->timeout_ms);
    if (ret)
    {
        DMOD_LOG_ERROR("dmwm8994: SAI start failed (%d)\n", ret);
        codec_stream_stop(handle);
        return ret;
    }
    handle->started = true;
    return 0;
}

/** @brief Stop DMA and release the exclusive SAI friend handle. */
void codec_stream_stop(codec_handle_t *handle)
{
    if (!handle) return;
    if (handle->sai)
    {
        if (handle->started) Dmod_Ioctl(handle->sai, DMDRVI_IOCTL_AUDIO_STOP, NULL);
        Dmod_FileClose(handle->sai);
    }
    handle->sai = NULL;
    handle->started = false;
    if (handle->pcm_rx_buffer) Dmod_Free(handle->pcm_rx_buffer);
    if (handle->pcm_tx_buffer) Dmod_Free(handle->pcm_tx_buffer);
    handle->pcm_rx_buffer = NULL;
    handle->pcm_tx_buffer = NULL;
}

/** @brief Allocate one DMA frame conversion buffer on first use. */
static uint8_t *pcm_buffer(uint8_t **buffer)
{
    if (!*buffer) *buffer = Dmod_Malloc(CODEC_PCM_BATCH_BYTES);
    if (!*buffer) DMOD_LOG_ERROR("dmwm8994: cannot allocate PCM conversion buffer\n");
    return *buffer;
}

/** @brief Validate a stereo byte-stream request on the codec node. */
static int check_pcm(dmdrvi_context_t c, codec_handle_t *handle,
                     const void *buffer, size_t size, dmdrvi_offset_t offset)
{
    if (!c || c->magic != CODEC_MAGIC || !handle ||
        handle->magic != CODEC_HANDLE_MAGIC || c->open_handle != handle)
        return -EBADF;
    if (offset < 0 || (!buffer && size) || size % CODEC_PCM_FRAME_BYTES)
        return -EINVAL;
    if (size > (size_t)INT64_MAX) return -EOVERFLOW;
    if (!size) return 0;
    if (!handle->started || !c->info.configured) return -EPIPE;
    return 0;
}

/** @brief Copy microphone slots 1 and 3 into interleaved stereo PCM. */
dmdrvi_ssize_t codec_pcm_read(dmdrvi_context_t c, codec_handle_t *handle,
                              void *buffer, size_t size, dmdrvi_offset_t offset)
{
    int ret = check_pcm(c, handle, buffer, size, offset);
    if (ret || !size) return ret;
    if (!c->digital_mic2 || !handle->quad_slots) return -ENOTSUP;
    uint8_t *out = buffer;
    size_t done = 0;
    uint8_t *frames = pcm_buffer(&handle->pcm_rx_buffer);
    if (!frames) return -ENOMEM;
    while (done < size)
    {
        size_t count = (size - done) / CODEC_PCM_FRAME_BYTES;
        if (count > CODEC_PCM_BATCH_FRAMES) count = CODEC_PCM_BATCH_FRAMES;
        size_t got = Dmod_FileRead(frames, 1, count * CODEC_SAI_FRAME_BYTES,
                                   handle->sai);
        if (!got || got > count * CODEC_SAI_FRAME_BYTES ||
            got % CODEC_SAI_FRAME_BYTES)
            return done ? (dmdrvi_ssize_t)done : -EIO;
        for (size_t i = 0; i < got / CODEC_SAI_FRAME_BYTES; ++i)
        {
            memcpy(out + done, frames + i * CODEC_SAI_FRAME_BYTES + 2, 2);
            memcpy(out + done + 2, frames + i * CODEC_SAI_FRAME_BYTES + 6, 2);
            done += CODEC_PCM_FRAME_BYTES;
        }
        if (got < count * CODEC_SAI_FRAME_BYTES) break;
    }
    return (dmdrvi_ssize_t)done;
}

/** @brief Put stereo PCM in headphone slots 0 and 2 of SAI DMA frames. */
dmdrvi_ssize_t codec_pcm_write(dmdrvi_context_t c, codec_handle_t *handle,
                               const void *buffer, size_t size, dmdrvi_offset_t offset)
{
    int ret = check_pcm(c, handle, buffer, size, offset);
    if (ret || !size) return ret;
    if (!handle->quad_slots)
    {
        size_t wrote = Dmod_FileWrite(buffer, 1, size, handle->sai);
        return wrote && wrote <= size && wrote % CODEC_PCM_FRAME_BYTES == 0
                   ? (dmdrvi_ssize_t)wrote : -EIO;
    }
    const uint8_t *in = buffer;
    size_t done = 0;
    uint8_t *frames = pcm_buffer(&handle->pcm_tx_buffer);
    if (!frames) return -ENOMEM;
    while (done < size)
    {
        size_t count = (size - done) / CODEC_PCM_FRAME_BYTES;
        if (count > CODEC_PCM_BATCH_FRAMES) count = CODEC_PCM_BATCH_FRAMES;
        const uint8_t *pcm = in + done;
        uint8_t *slot = frames;
        for (size_t i = 0; i < count; ++i)
        {
            /* HP-L, DMIC2-L (silent), HP-R, DMIC2-R (silent) */
            slot[0] = pcm[0];
            slot[1] = pcm[1];
            slot[2] = 0;
            slot[3] = 0;
            slot[4] = pcm[2];
            slot[5] = pcm[3];
            slot[6] = 0;
            slot[7] = 0;
            pcm += CODEC_PCM_FRAME_BYTES;
            slot += CODEC_SAI_FRAME_BYTES;
        }
        size_t wrote = Dmod_FileWrite(frames, 1,
                                      count * CODEC_SAI_FRAME_BYTES, handle->sai);
        if (!wrote || wrote > count * CODEC_SAI_FRAME_BYTES ||
            wrote % CODEC_SAI_FRAME_BYTES)
            return done ? (dmdrvi_ssize_t)done : -EIO;
        done += wrote / CODEC_SAI_FRAME_BYTES * CODEC_PCM_FRAME_BYTES;
        if (wrote < count * CODEC_SAI_FRAME_BYTES) break;
    }
    return (dmdrvi_ssize_t)done;
}
