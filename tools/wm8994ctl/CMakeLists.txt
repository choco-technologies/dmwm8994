set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/wm8994ctl.dmr)
dmod_add_executable(wm8994ctl ${DMOD_MODULE_VERSION} main.c)
target_link_libraries(wm8994ctl dmwm8994_if)
dmod_link_modules(wm8994ctl dmdrvi@>=2.6)
