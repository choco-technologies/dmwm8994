#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod.h"
#include "dmfsi.h"
#include "dmwm8994.h"
#include <errno.h>
#include <string.h>

typedef struct {
    dmod_dmfsi_init_t init;
    dmod_dmfsi_deinit_t deinit;
    dmod_dmfsi_mounted_t mounted;
    dmod_dmfsi_fopen_t fopen;
    dmod_dmfsi_fclose_t fclose;
    dmod_dmfsi_ioctl_t ioctl;
} test_fs_t;

/** @brief Load dmdevfs, which discovers and starts the codec driver itself. */
static bool load_devfs(test_fs_t *fs)
{
    if (!Dmod_LoadModuleByName("dmdevfs") ||
        !Dmod_EnableModule("dmdevfs", false, NULL)) return false;
    Dmod_Context_t *module = Dmod_GetModuleContext("dmdevfs");
    if (!module) return false;
    fs->init = Dmod_GetDifFunction(module, dmod_dmfsi_init_sig);
    fs->deinit = Dmod_GetDifFunction(module, dmod_dmfsi_deinit_sig);
    fs->mounted = Dmod_GetDifFunction(module, dmod_dmfsi_mounted_sig);
    fs->fopen = Dmod_GetDifFunction(module, dmod_dmfsi_fopen_sig);
    fs->fclose = Dmod_GetDifFunction(module, dmod_dmfsi_fclose_sig);
    fs->ioctl = Dmod_GetDifFunction(module, dmod_dmfsi_ioctl_sig);
    return fs->init && fs->deinit && fs->mounted && fs->fopen &&
           fs->fclose && fs->ioctl;
}

/** @brief Exercise the supplied mounted node while no I2C friend exists. */
static int test_valid_node(test_fs_t *fs, const char *node, const char *config_dir)
{
    dmfsi_context_t mount = fs->init(config_dir);
    if (!mount) return 1;
    fs->mounted(mount, "/dev");
    void *file = NULL;
    int failed = fs->fopen(mount, &file, node, DMFSI_O_RDWR, 0) != DMFSI_OK;
    if (!failed)
    {
        dmdrvi_audio_info_t info;
        if (fs->ioctl(mount, file, DMDRVI_IOCTL_AUDIO_GET_INFO, &info) != -ENODEV)
            failed = 1;
        dmdrvi_audio_config_t config = {
            .sample_rate_hz = 12345, .channels = 2, .sample_bits = 16,
            .output = DMDRVI_AUDIO_OUTPUT_HEADPHONE,
            .volume_percent = 50, .muted = false,
        };
        if (fs->ioctl(mount, file, DMDRVI_IOCTL_AUDIO_CONFIGURE, &config) != -ENOTSUP)
            failed = 1;
        fs->fclose(mount, file);
    }
    fs->deinit(mount);
    return failed;
}

/** @brief Check that dmdevfs does not mount a node with invalid I2C address. */
static int test_invalid_address(test_fs_t *fs, const char *node,
                                const char *config_dir)
{
    dmfsi_context_t mount = fs->init(config_dir);
    if (!mount) return 1;
    fs->mounted(mount, "/dev");
    void *file = NULL;
    int opened = fs->fopen(mount, &file, node, DMFSI_O_RDWR, 0) == DMFSI_OK;
    if (opened) fs->fclose(mount, file);
    fs->deinit(mount);
    return opened;
}

/** @brief Test codec behavior through a caller-selected dmdevfs device node. */
int main(int argc, char **argv)
{
    if (argc != 4 || strncmp(argv[1], "/dev/", 5) != 0)
    {
        Dmod_Printf("Usage: test_dmwm8994 /dev/CODEC VALID_CONFIG_DIR INVALID_CONFIG_DIR\n");
        return 1;
    }
    test_fs_t fs = {0};
    if (!load_devfs(&fs))
    {
        Dmod_Printf("WM8994 TEST: cannot load dmdevfs\n");
        return 1;
    }
    const char *node = argv[1] + 4;
    int failures = test_valid_node(&fs, node, argv[2]);
    failures += test_invalid_address(&fs, node, argv[3]);
    Dmod_Printf("WM8994 TEST: %s (%d failures)\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
