#define DMOD_ENABLE_REGISTRATION ON
#include "private.h"
#include <errno.h>
#include <string.h>

static uint32_t active_contexts;
static dmosi_mutex_t module_lock;

/** @brief Check the opaque context's type marker. */
static bool valid(dmdrvi_context_t c) { return c && c->magic == CODEC_MAGIC; }

/** @brief Initialize the architecture-independent codec module. */
int dmod_init(const Dmod_Config_t *config)
{
    (void)config;
    active_contexts = 0;
    module_lock = dmosi_mutex_create(false);
    return module_lock ? 0 : -ENOMEM;
}

/** @brief Refuse unload while dmdevfs still owns a codec context. */
int dmod_deinit(void)
{
    dmosi_mutex_lock(module_lock);
    bool busy = active_contexts != 0;
    dmosi_mutex_unlock(module_lock);
    if (busy) return -EBUSY;
    dmosi_mutex_destroy(module_lock);
    module_lock = NULL;
    return 0;
}

/** @brief Create the codec node from one dmdevfs INI section. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_context_t, _create,
    ( dmini_context_t config, dmdrvi_dev_num_t *dev_num ))
{
    if (!config || !dev_num) return NULL;
    int address = dmini_get_int(config, NULL, "address", 26);
    if (address < 0x08 || address > 0x77) return NULL;
    dmdrvi_context_t c = Dmod_Malloc(sizeof(*c));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->magic = CODEC_MAGIC;
    c->address = (uint16_t)address;
    c->lock = dmosi_mutex_create(false);
    if (!c->lock) { Dmod_Free(c); return NULL; }
    memset(dev_num, 0, sizeof(*dev_num));
    const char *section = dmini_section_name(config, 0);
    if (section && section[0] && strlen(section) <= DMDRVI_ALT_NAME_MAX_LEN) {
        dev_num->flags = DMDRVI_NUM_ALT_NAME;
        memcpy(dev_num->alt_name, section, strlen(section) + 1);
    } else {
        dev_num->flags = DMDRVI_NUM_MAJOR;
    }
    dmosi_mutex_lock(module_lock);
    active_contexts++;
    dmosi_mutex_unlock(module_lock);
    return c;
}

/** @brief Release the codec context and its shared bus handle. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _free,
    ( dmdrvi_context_t c ))
{
    if (!valid(c)) return;
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
        strcmp(info->friend_role, "i2c_bus")) return;
    dmosi_mutex_lock(c->lock);
    codec_disconnect(c);
    Dmod_Free(c->bus_path);
    c->bus_path = NULL;
    if (info->state == dmdrvi_dev_state_ready && info->node_path)
        c->bus_path = Dmod_StrDup(info->node_path);
    dmosi_mutex_unlock(c->lock);
}

/** @brief Return a control handle for the codec node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void *, _open,
    ( dmdrvi_context_t c, int flags, const dmdrvi_dev_num_t *dev_num ))
{
    (void)flags; (void)dev_num;
    return valid(c) ? c : NULL;
}

/** @brief Close a control handle; state is owned by the node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, void, _close,
    ( dmdrvi_context_t c, void *handle ))
{ (void)c; (void)handle; }

/** @brief Reject byte reads; PCM comes from the paired dmsai node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_ssize_t, _read,
    ( dmdrvi_context_t c, void *handle, void *buffer, size_t size, dmdrvi_offset_t offset ))
{ (void)handle; (void)buffer; (void)offset; return valid(c) ? (size ? -ENOTSUP : 0) : -EINVAL; }

/** @brief Reject byte writes; PCM goes to the paired dmsai node. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t c, void *handle, const void *buffer, size_t size, dmdrvi_offset_t offset ))
{ (void)handle; (void)buffer; (void)offset; return valid(c) ? (size ? -ENOTSUP : 0) : -EINVAL; }

/** @brief Dispatch identification, setup, gain and mute controls. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _ioctl,
    ( dmdrvi_context_t c, void *handle, int command, void *arg ))
{
    (void)handle;
    if (!valid(c)) return -EINVAL;
    if (command < DMWM8994_IOCTL_GET_INFO || command > DMWM8994_IOCTL_SET_MUTE)
        return -ENOTTY;
    if (!arg) return -EINVAL;
    dmosi_mutex_lock(c->lock);
    int ret = 0;
    if (command == DMWM8994_IOCTL_GET_INFO) {
        dmwm8994_info_t info = c->info;
        ret = codec_read_reg(c, 0x0000, &info.chip_id);
        if (!ret) ret = codec_read_reg(c, 0x0210, &info.aif1_rate_register);
        if (!ret) ret = codec_read_reg(c, 0x0420, &info.dac1_filter_register);
        if (!ret && info.configured &&
            info.aif1_rate_register != c->info.aif1_rate_register)
            info.configured = false;
        if (!ret) c->info = info;
        if (!ret) memcpy(arg, &c->info, sizeof(c->info));
    } else if (command == DMWM8994_IOCTL_CONFIGURE) {
        ret = codec_configure(c, (const dmwm8994_config_t *)arg);
    } else if (!c->info.configured) {
        ret = -EPIPE;
    } else {
        uint16_t actual_rate = 0;
        ret = codec_read_reg(c, 0x0210, &actual_rate);
        if (!ret && actual_rate != c->info.aif1_rate_register) {
            c->info.configured = false;
            ret = -EPIPE;
        }
        if (!ret && command == DMWM8994_IOCTL_SET_VOLUME)
            ret = codec_set_volume(c, *(const uint8_t *)arg);
        else if (!ret)
            ret = codec_set_mute(c, *(const bool *)arg);
    }
    dmosi_mutex_unlock(c->lock);
    return ret;
}

/** @brief Flush an already completed control operation. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _flush,
    ( dmdrvi_context_t c, void *handle ))
{ (void)handle; return valid(c) ? 0 : -EINVAL; }

/** @brief Identify the node as a control device. */
dmod_dmdrvi_dif_api_declaration(2.0, dmwm8994, int, _stat,
    ( dmdrvi_context_t c, const char *path, dmdrvi_stat_t *stat ))
{
    (void)path;
    if (!valid(c) || !stat) return -EINVAL;
    stat->size = 0;
    stat->mode = 0666;
    return 0;
}
