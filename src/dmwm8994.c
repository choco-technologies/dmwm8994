#define DMOD_ENABLE_REGISTRATION ON
#include "private.h"
#include "registers.h"
#include <errno.h>
#include <string.h>

static uint32_t active_contexts;
static dmosi_mutex_t module_lock;

/* Reserved I2C addresses are outside the usable 7-bit target range. */
#define CODEC_I2C_FIRST_ADDRESS 0x08
#define CODEC_I2C_LAST_ADDRESS  0x77

/** @brief Check the opaque context's type marker. */
static bool valid(dmdrvi_context_t c)
{
    return c && c->magic == CODEC_MAGIC;
}

/** @brief Initialize the architecture-independent codec module. */
int dmod_init(const Dmod_Config_t *config)
{
    (void)config;
    active_contexts = 0;
    module_lock = dmosi_mutex_create(false);
    if (!module_lock)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot create module mutex\n");
        return -ENOMEM;
    }
    return 0;
}

/** @brief Refuse unload while dmdevfs still owns a codec context. */
int dmod_deinit(void)
{
    dmosi_mutex_lock(module_lock);
    uint32_t count = active_contexts;
    dmosi_mutex_unlock(module_lock);
    if (count != 0)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot unload with %lu active nodes\n",
                       (unsigned long)count);
        return -EBUSY;
    }
    dmosi_mutex_destroy(module_lock);
    module_lock = NULL;
    return 0;
}

/** @brief Validate the bus address and node name in one INI section. */
static bool parse_node_config(dmini_context_t config, int *address,
                              const char **section, bool *digital_mic2)
{
    *address = dmini_get_int(config, NULL, "address", 0);
    if (*address < CODEC_I2C_FIRST_ADDRESS ||
        *address > CODEC_I2C_LAST_ADDRESS)
    {
        DMOD_LOG_ERROR("dmwm8994: invalid or missing 7-bit I2C address %d\n", *address);
        return false;
    }
    *section = dmini_section_name(config, 0);
    if (!*section || !(*section)[0] ||
        strlen(*section) > DMDRVI_ALT_NAME_MAX_LEN)
    {
        DMOD_LOG_ERROR("dmwm8994: invalid device section name\n");
        return false;
    }
    const char *input = dmini_get_string(config, NULL, "input", "none");
    if (strcmp(input, "none") && strcmp(input, "dmic2"))
    {
        DMOD_LOG_ERROR("dmwm8994: unsupported input route %s\n", input);
        return false;
    }
    *digital_mic2 = strcmp(input, "dmic2") == 0;
    return true;
}

/** @brief Create the codec node from one dmdevfs INI section. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_context_t, _create,
    ( dmini_context_t config, dmdrvi_dev_num_t *dev_num ))
{
    if (!config || !dev_num)
    {
        DMOD_LOG_ERROR("dmwm8994: missing node configuration\n");
        return NULL;
    }
    int address = 0;
    const char *section = NULL;
    bool digital_mic2 = false;
    if (!parse_node_config(config, &address, &section, &digital_mic2)) return NULL;
    dmdrvi_context_t c = Dmod_Malloc(sizeof(*c));
    if (!c)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot allocate node context\n");
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->magic = CODEC_MAGIC;
    c->address = (uint16_t)address;
    c->digital_mic2 = digital_mic2;
    c->lock = dmosi_mutex_create(false);
    if (!c->lock)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot create node mutex\n");
        Dmod_Free(c);
        return NULL;
    }
    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->flags = DMDRVI_NUM_ALT_NAME;
    memcpy(dev_num->alt_name, section, strlen(section) + 1);
    dmosi_mutex_lock(module_lock);
    active_contexts++;
    dmosi_mutex_unlock(module_lock);
    return c;
}

/** @brief Release the codec context and its friend handles. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _free,
    ( dmdrvi_context_t c ))
{
    if (!valid(c))
    {
        DMOD_LOG_ERROR("dmwm8994: invalid context on free\n");
        return;
    }
    if (c->open_handle)
    {
        codec_stream_stop(c->open_handle);
        c->open_handle->magic = 0;
        Dmod_Free(c->open_handle);
    }
    codec_disconnect(c);
    Dmod_Free(c->bus_path);
    Dmod_Free(c->sai_path);
    dmosi_mutex_destroy(c->lock);
    c->magic = 0;
    Dmod_Free(c);
    dmosi_mutex_lock(module_lock);
    active_contexts--;
    dmosi_mutex_unlock(module_lock);
}

/** @brief Track I2C and SAI friends reported by dmdevfs. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _friend_changed,
    ( dmdrvi_context_t c, const dmdrvi_friend_info_t *info ))
{
    if (!valid(c) || !info || !info->friend_role)
    {
        return;
    }
    bool bus = strcmp(info->friend_role, "i2c_bus") == 0;
    bool sai = strcmp(info->friend_role, "audio_stream") == 0;
    if (!bus && !sai) return;
    dmosi_mutex_lock(c->lock);
    char **path = bus ? &c->bus_path : &c->sai_path;
    if (bus) codec_disconnect(c);
    if (sai && c->open_handle)
    {
        codec_stream_stop(c->open_handle);
        c->info.configured = false;
    }
    Dmod_Free(*path);
    *path = NULL;
    if (info->state == dmdrvi_dev_state_ready && info->node_path)
    {
        *path = Dmod_StrDup(info->node_path);
        if (!*path)
        {
            DMOD_LOG_ERROR("dmwm8994: cannot copy friend path\n");
        }
    }
    dmosi_mutex_unlock(c->lock);
}

/** @brief Allocate one exclusive control and PCM handle for the codec node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void *, _open,
    ( dmdrvi_context_t c, int flags, const dmdrvi_dev_num_t *dev_num ))
{
    (void)flags;
    (void)dev_num;
    if (!valid(c))
    {
        DMOD_LOG_ERROR("dmwm8994: invalid context on open\n");
        return NULL;
    }
    codec_handle_t *open = Dmod_Malloc(sizeof(*open));
    if (!open) return NULL;
    memset(open, 0, sizeof(*open));
    open->magic = CODEC_HANDLE_MAGIC;
    dmosi_mutex_lock(c->lock);
    if (c->open_handle)
    {
        dmosi_mutex_unlock(c->lock);
        Dmod_Free(open);
        return NULL;
    }
    c->open_handle = open;
    dmosi_mutex_unlock(c->lock);
    return open;
}

/** @brief Stop the stream and release the codec's exclusive open handle. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _close,
    ( dmdrvi_context_t c, void *handle ))
{
    if (!valid(c) || !handle) return;
    dmosi_mutex_lock(c->lock);
    codec_handle_t *open = handle;
    if (c->open_handle == open && open->magic == CODEC_HANDLE_MAGIC)
    {
        codec_stream_stop(open);
        c->info.configured = false;
        c->open_handle = NULL;
        open->magic = 0;
        Dmod_Free(open);
    }
    dmosi_mutex_unlock(c->lock);
}

/** @brief Read stereo PCM captured from the board's selected microphone. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_ssize_t, _read,
    ( dmdrvi_context_t c, void *handle, void *buffer, size_t size, dmdrvi_offset_t offset ))
{
    return codec_pcm_read(c, handle, buffer, size, offset);
}

/** @brief Write stereo PCM to the headphone slots of the SAI friend. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t c, void *handle, const void *buffer, size_t size, dmdrvi_offset_t offset ))
{
    return codec_pcm_write(c, handle, buffer, size, offset);
}

/** @brief Read live identity, rate and mute state into a generic audio info. */
static int read_info(dmdrvi_context_t c, dmdrvi_audio_info_t *out)
{
    uint16_t chip_id = 0;
    uint16_t rate = 0;
    uint16_t filter = 0;
    int ret = codec_read_reg(c, WM_REG_RESET, &chip_id);
    if (!ret) ret = codec_read_reg(c, WM_REG_AIF1_RATE, &rate);
    if (!ret) ret = codec_read_reg(c, WM_REG_DAC1_FILTER, &filter);
    if (ret) return ret;
    dmdrvi_audio_info_t info = c->info;
    info.hardware_id = chip_id;
    if (info.configured &&
        (rate != c->expected_rate_register || chip_id != DMWM8994_CHIP_ID))
    {
        info.configured = false;
    }
    if (info.configured)
    {
        info.config.muted = (filter & WM_DAC1_MUTE) != 0U;
    }
    c->info = info;
    *out = info;
    return 0;
}

/** @brief Apply volume or mute after verifying the codec retained its rate. */
static int set_control(dmdrvi_context_t c, int command, const void *arg)
{
    if (!c->info.configured) return -EPIPE;
    uint16_t actual_rate = 0;
    int ret = codec_read_reg(c, WM_REG_AIF1_RATE, &actual_rate);
    if (ret) return ret;
    if (actual_rate != c->expected_rate_register)
    {
        c->info.configured = false;
        return -EPIPE;
    }
    if (command == DMDRVI_IOCTL_AUDIO_SET_VOLUME)
        return codec_set_volume(c, *(const uint8_t *)arg);
    return codec_set_mute(c, *(const bool *)arg);
}

/** @brief Start the SAI friend and configure the codec; undo both on failure. */
static int apply_config(dmdrvi_context_t c, codec_handle_t *open,
                        const dmdrvi_audio_config_t *config)
{
    int ret = codec_validate_config(config);
    if (!ret) ret = codec_stream_start(c, open, config);
    if (!ret) ret = codec_configure(c, config);
    if (ret)
    {
        codec_stream_stop(open);
        c->info.configured = false;
        return ret;
    }
    open->last_config = *config;
    open->has_config = true;
    return 0;
}

/** @brief Describe the stereo buffers and, while running, the SAI link. */
static int get_stream(dmdrvi_context_t c, codec_handle_t *open,
                      dmdrvi_audio_stream_t *out)
{
    memset(out, 0, sizeof(*out));
    if (open->started)
    {
        int ret = Dmod_Ioctl(open->sai, DMDRVI_IOCTL_AUDIO_GET_STREAM, out);
        if (ret) return ret;
    }
    else if (open->has_config)
    {
        out->sample_rate_hz = open->last_config.sample_rate_hz;
    }
    out->format = DMDRVI_AUDIO_FORMAT_S16_LE;
    out->channels = 2;
    out->transmit = true;
    out->receive = c->digital_mic2;
    return 0;
}

/**
 * @brief Stream commands: the codec's own START/STOP/layout/timeout, and
 * status and drain of the SAI friend while running.
 */
static int stream_ioctl(dmdrvi_context_t c, codec_handle_t *open,
                        int command, void *arg)
{
    switch (command)
    {
    case DMDRVI_IOCTL_AUDIO_GET_STREAM:
        return arg ? get_stream(c, open, arg) : -EINVAL;
    case DMDRVI_IOCTL_AUDIO_START:
        if (open->started && c->info.configured) return 0;
        return open->has_config ? apply_config(c, open, &open->last_config)
                                : -EPIPE;
    case DMDRVI_IOCTL_AUDIO_STOP:
        codec_stream_stop(open);
        c->info.configured = false;
        return 0;
    case DMDRVI_IOCTL_AUDIO_GET_TIMEOUT:
        if (!arg) return -EINVAL;
        *(uint32_t *)arg = open->timeout_ms;
        return 0;
    case DMDRVI_IOCTL_AUDIO_SET_TIMEOUT:
        if (!arg) return -EINVAL;
        open->timeout_ms = *(const uint32_t *)arg;
        return open->started ? Dmod_Ioctl(open->sai, command, arg) : 0;
    default: /* GET_STATUS, DRAIN */
        return open->started ? Dmod_Ioctl(open->sai, command, arg) : -EPIPE;
    }
}

/** @brief Dispatch the standard audio controls and stream commands. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _ioctl,
    ( dmdrvi_context_t c, void *handle, int command, void *arg ))
{
    codec_handle_t *open = handle;
    if (!valid(c) || !open || open->magic != CODEC_HANDLE_MAGIC ||
        c->open_handle != open)
    {
        DMOD_LOG_ERROR("dmwm8994: invalid context on ioctl\n");
        return -EINVAL;
    }
    if (command < DMDRVI_IOCTL_AUDIO_GET_INFO ||
        command > DMDRVI_IOCTL_AUDIO_DRAIN) return -ENOTTY;
    bool control = command <= DMDRVI_IOCTL_AUDIO_SET_MUTE;
    if (control && !arg) return -EINVAL;
    dmosi_mutex_lock(c->lock);
    int ret;
    if (!control)
        ret = stream_ioctl(c, open, command, arg);
    else if (command == DMDRVI_IOCTL_AUDIO_GET_INFO)
        ret = read_info(c, arg);
    else if (command == DMDRVI_IOCTL_AUDIO_CONFIGURE)
        ret = apply_config(c, open, (const dmdrvi_audio_config_t *)arg);
    else
        ret = set_control(c, command, arg);
    dmosi_mutex_unlock(c->lock);
    if (ret && ret != -ENOTSUP && ret != -ENOTTY && ret != -EINVAL &&
        ret != -EPIPE)
    {
        DMOD_LOG_ERROR("dmwm8994: ioctl %d failed (%d)\n", command, ret);
    }
    return ret;
}

/** @brief Drain PCM already accepted by the SAI DMA stream. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _flush,
    ( dmdrvi_context_t c, void *handle ))
{
    codec_handle_t *open = handle;
    if (!valid(c) || !open || open->magic != CODEC_HANDLE_MAGIC ||
        c->open_handle != open) return -EBADF;
    if (!open->started) return 0;
    return Dmod_Ioctl(open->sai, DMDRVI_IOCTL_AUDIO_DRAIN, NULL);
}

/** @brief Report the codec node's stream device metadata. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _stat,
    ( dmdrvi_context_t c, const char *path, dmdrvi_stat_t *stat ))
{
    (void)path;
    if (!valid(c) || !stat)
    {
        return -EINVAL;
    }
    stat->size = 0;
    stat->mode = 0666;
    return 0;
}
