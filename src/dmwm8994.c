#define DMOD_ENABLE_REGISTRATION ON
#include "private.h"
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
                              const char **section)
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
    if (!parse_node_config(config, &address, &section)) return NULL;
    dmdrvi_context_t c = Dmod_Malloc(sizeof(*c));
    if (!c)
    {
        DMOD_LOG_ERROR("dmwm8994: cannot allocate node context\n");
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->magic = CODEC_MAGIC;
    c->address = (uint16_t)address;
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

/** @brief Release the codec context and its shared bus handle. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _free,
    ( dmdrvi_context_t c ))
{
    if (!valid(c))
    {
        DMOD_LOG_ERROR("dmwm8994: invalid context on free\n");
        return;
    }
    codec_disconnect(c);
    Dmod_Free(c->bus_path);
    dmosi_mutex_destroy(c->lock);
    c->magic = 0;
    Dmod_Free(c);
    dmosi_mutex_lock(module_lock);
    active_contexts--;
    dmosi_mutex_unlock(module_lock);
}

/** @brief Track the dmi2c bus reported by dmdevfs. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _friend_changed,
    ( dmdrvi_context_t c, const dmdrvi_friend_info_t *info ))
{
    if (!valid(c) || !info || !info->friend_role ||
        strcmp(info->friend_role, "i2c_bus"))
    {
        return;
    }
    dmosi_mutex_lock(c->lock);
    codec_disconnect(c);
    Dmod_Free(c->bus_path);
    c->bus_path = NULL;
    if (info->state == dmdrvi_dev_state_ready && info->node_path)
    {
        c->bus_path = Dmod_StrDup(info->node_path);
        if (!c->bus_path)
        {
            DMOD_LOG_ERROR("dmwm8994: cannot copy I2C friend path\n");
        }
    }
    dmosi_mutex_unlock(c->lock);
}

/** @brief Return a control handle for the codec node. */
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
    return c;
}

/** @brief Close a control handle; state is owned by the node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _close,
    ( dmdrvi_context_t c, void *handle ))
{
    (void)c;
    (void)handle;
}

/** @brief Reject byte reads; PCM comes from the paired dmsai node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_ssize_t, _read,
    ( dmdrvi_context_t c, void *handle, void *buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)handle;
    (void)buffer;
    (void)offset;
    return valid(c) ? (size ? -ENOTSUP : 0) : -EINVAL;
}

/** @brief Reject byte writes; PCM goes to the paired dmsai node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t c, void *handle, const void *buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)handle;
    (void)buffer;
    (void)offset;
    return valid(c) ? (size ? -ENOTSUP : 0) : -EINVAL;
}

/** @brief Read live identity, rate and mute state into a generic audio info. */
static int read_info(dmdrvi_context_t c, dmdrvi_audio_info_t *out)
{
    uint16_t chip_id = 0;
    uint16_t rate = 0;
    uint16_t filter = 0;
    int ret = codec_read_reg(c, 0x0000, &chip_id);
    if (!ret) ret = codec_read_reg(c, 0x0210, &rate);
    if (!ret) ret = codec_read_reg(c, 0x0420, &filter);
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
        info.config.muted = (filter & 0x0200U) != 0U;
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
    int ret = codec_read_reg(c, 0x0210, &actual_rate);
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

/** @brief Dispatch standard audio identity, setup, gain and mute controls. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _ioctl,
    ( dmdrvi_context_t c, void *handle, int command, void *arg ))
{
    (void)handle;
    if (!valid(c))
    {
        DMOD_LOG_ERROR("dmwm8994: invalid context on ioctl\n");
        return -EINVAL;
    }
    if (command < DMDRVI_IOCTL_AUDIO_GET_INFO ||
        command > DMDRVI_IOCTL_AUDIO_SET_MUTE) return -ENOTTY;
    if (!arg) return -EINVAL;
    dmosi_mutex_lock(c->lock);
    int ret = 0;
    if (command == DMDRVI_IOCTL_AUDIO_GET_INFO)
    {
        ret = read_info(c, arg);
    }
    else if (command == DMDRVI_IOCTL_AUDIO_CONFIGURE)
    {
        ret = codec_configure(c, (const dmdrvi_audio_config_t *)arg);
    }
    else
    {
        ret = set_control(c, command, arg);
    }
    dmosi_mutex_unlock(c->lock);
    if (ret && ret != -ENOTSUP && ret != -ENOTTY && ret != -EINVAL)
    {
        DMOD_LOG_ERROR("dmwm8994: ioctl %d failed (%d)\n", command, ret);
    }
    return ret;
}

/** @brief Flush an already completed control operation. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _flush,
    ( dmdrvi_context_t c, void *handle ))
{
    (void)handle;
    return valid(c) ? 0 : -EINVAL;
}

/** @brief Identify the node as a control device. */
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
