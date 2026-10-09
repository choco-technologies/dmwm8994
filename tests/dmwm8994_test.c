#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmwm8994.h"
#include <errno.h>
#include <string.h>

typedef struct {
    dmod_dmdrvi_create_t create;
    dmod_dmdrvi_free_t free;
    dmod_dmdrvi_ioctl_t ioctl;
    dmod_dmdrvi_friend_changed_t friend_changed;
} driver_t;

/** @brief Resolve the same DIF entries that dmdevfs uses. */
static bool get_driver(driver_t *drv)
{
    if (Dmod_LoadModuleByName("dmwm8994") == NULL ||
        !Dmod_EnableModule("dmwm8994", false, NULL)) return false;
    Dmod_Context_t *module = Dmod_GetModuleContext("dmwm8994");
    if (!module) return false;
    drv->create = Dmod_GetDifFunction(module, dmod_dmdrvi_create_sig);
    drv->free = Dmod_GetDifFunction(module, dmod_dmdrvi_free_sig);
    drv->ioctl = Dmod_GetDifFunction(module, dmod_dmdrvi_ioctl_sig);
    drv->friend_changed = Dmod_GetDifFunction(module, dmod_dmdrvi_friend_changed_sig);
    return drv->create && drv->free && drv->ioctl && drv->friend_changed;
}

/** @brief Build a valid INI context for the codec. */
static dmini_context_t make_config(const char *address)
{
    dmini_context_t ini = dmini_create();
    if (!ini) return NULL;
    dmini_parse_string(ini, "[codec]\ndriver_name=dmwm8994\n");
    if (address) dmini_set_string(ini, "codec", "address", address);
    dmini_set_active_section(ini, "codec", 0);
    return ini;
}

void dmod_test_setup(void) { }
void dmod_test_teardown(void) { }

DMOD_TEST_STEP(rejects_invalid_address)
{
    driver_t drv;
    if (!get_driver(&drv)) { DMOD_TEST_EXPECT_TRUE(false); return; }
    dmini_context_t ini = make_config("120");
    if (!ini) { DMOD_TEST_EXPECT_NOT_NULL(ini); return; }
    dmdrvi_dev_num_t num;
    DMOD_TEST_EXPECT_TRUE(drv.create(ini, &num) == NULL);
    dmini_destroy(ini);
}

DMOD_TEST_STEP(reports_missing_bus)
{
    driver_t drv;
    if (!get_driver(&drv)) { DMOD_TEST_EXPECT_TRUE(false); return; }
    dmini_context_t ini = make_config("26");
    if (!ini) { DMOD_TEST_EXPECT_NOT_NULL(ini); return; }
    dmdrvi_dev_num_t num;
    dmdrvi_context_t ctx = drv.create(ini, &num);
    if (!ctx) { DMOD_TEST_EXPECT_NOT_NULL(ctx); dmini_destroy(ini); return; }
    DMOD_TEST_EXPECT_EQ(num.flags, DMDRVI_NUM_ALT_NAME);
    DMOD_TEST_EXPECT_EQ(strcmp(num.alt_name, "codec"), 0);
    dmwm8994_info_t info;
    DMOD_TEST_EXPECT_EQ(drv.ioctl(ctx, ctx, DMWM8994_IOCTL_GET_INFO, &info), -ENODEV);
    dmwm8994_config_t config = { 48000, dmwm8994_output_headphone, 32, false };
    DMOD_TEST_EXPECT_EQ(drv.ioctl(ctx, ctx, DMWM8994_IOCTL_CONFIGURE, &config), -ENODEV);
    drv.free(ctx);
    dmini_destroy(ini);
}

DMOD_TEST_STEP(rejects_unsupported_format_before_bus_access)
{
    driver_t drv;
    if (!get_driver(&drv)) { DMOD_TEST_EXPECT_TRUE(false); return; }
    dmini_context_t ini = make_config(NULL);
    if (!ini) { DMOD_TEST_EXPECT_NOT_NULL(ini); return; }
    dmdrvi_dev_num_t num;
    dmdrvi_context_t ctx = drv.create(ini, &num);
    if (!ctx) { DMOD_TEST_EXPECT_NOT_NULL(ctx); dmini_destroy(ini); return; }
    dmwm8994_config_t config = { 12345, dmwm8994_output_headphone, 32, false };
    DMOD_TEST_EXPECT_EQ(drv.ioctl(ctx, ctx, DMWM8994_IOCTL_CONFIGURE, &config), -ENOTSUP);
    drv.free(ctx);
    dmini_destroy(ini);
}
